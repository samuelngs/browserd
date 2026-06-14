#include "browserd/mcp/mcp_http_transport.h"

#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/message_loop/message_pump_type.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/synchronization/waitable_event.h"
#include "base/task/bind_post_task.h"
#include "net/base/ip_endpoint.h"
#include "net/base/net_errors.h"
#include "net/log/net_log_source.h"
#include "net/server/http_server_request_info.h"
#include "net/server/http_server_response_info.h"
#include "net/socket/tcp_server_socket.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "url/gurl.h"

namespace browserd {
namespace {

constexpr int kBacklog = 10;

constexpr net::NetworkTrafficAnnotationTag kMcpHttpTrafficAnnotation =
    net::DefineNetworkTrafficAnnotation("browserd_mcp_http_transport", R"(
      semantics {
        sender: "browserd MCP HTTP Transport"
        description:
          "Local Model Context Protocol HTTP server exposed by browserd when "
          "the --mcp-http-port switch is provided. It receives MCP JSON-RPC "
          "requests and returns MCP JSON-RPC responses for browser automation."
        trigger:
          "Started only when browserd is launched with --mcp-http-port."
        data:
          "MCP JSON-RPC messages, including browser automation commands and "
          "tool results from pages controlled by browserd."
        destination: LOCAL
      }
      policy {
        cookies_allowed: NO
        setting:
          "This local server is disabled unless browserd is launched with "
          "--mcp-http-port."
        policy_exception_justification:
          "browserd is a developer/agent tool configured by command-line "
          "switches outside enterprise policy."
      })");

bool IsAllowedListenHost(const std::string& host) {
  return host == "127.0.0.1" || host == "0.0.0.0" || host == "::1" ||
         host == "::" ||
         base::EqualsCaseInsensitiveASCII(host, "localhost");
}

std::string NormalizeListenHost(const std::string& host) {
  if (base::EqualsCaseInsensitiveASCII(host, "localhost")) {
    return "127.0.0.1";
  }
  return host;
}

bool IsLoopbackOriginHost(std::string_view host) {
  return host == "127.0.0.1" || host == "::1" ||
         base::EqualsCaseInsensitiveASCII(host, "localhost");
}

bool HeaderContainsMimeType(std::string_view header,
                            std::string_view expected_type) {
  for (std::string_view item : base::SplitStringPiece(
           header, ",", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY)) {
    std::string_view media_type = item;
    size_t parameter_pos = media_type.find(';');
    if (parameter_pos != std::string_view::npos) {
      media_type = media_type.substr(0, parameter_pos);
      media_type = base::TrimString(media_type, " \t", base::TRIM_ALL);
    }

    if (base::EqualsCaseInsensitiveASCII(media_type, expected_type) ||
        base::EqualsCaseInsensitiveASCII(media_type, "*/*") ||
        base::EqualsCaseInsensitiveASCII(media_type, "application/*")) {
      return true;
    }
  }
  return false;
}

}  // namespace

MCPHttpTransport::MCPHttpTransport() : http_thread_("MCPHttpServer") {}

MCPHttpTransport::~MCPHttpTransport() {
  if (http_thread_.IsRunning()) {
    http_task_runner_->PostTask(
        FROM_HERE, base::BindOnce(&MCPHttpTransport::StopOnHttpThread,
                                  base::Unretained(this)));
    http_thread_.Stop();
  }
}

bool MCPHttpTransport::Start(scoped_refptr<base::SequencedTaskRunner> main_task_runner,
                             const std::string& host,
                             uint16_t port,
                             const std::string& token,
                             MCPMessageCallback message_callback) {
  if (!IsAllowedListenHost(host)) {
    LOG(ERROR) << "Invalid MCP HTTP host: " << host;
    return false;
  }

  main_task_runner_ = std::move(main_task_runner);
  message_callback_ = std::move(message_callback);

  base::Thread::Options options;
  options.message_pump_type = base::MessagePumpType::IO;
  if (!http_thread_.StartWithOptions(std::move(options))) {
    LOG(ERROR) << "Failed to start MCP HTTP server thread";
    return false;
  }
  http_task_runner_ = http_thread_.task_runner();

  bool success = false;
  base::WaitableEvent started;
  http_task_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(&MCPHttpTransport::StartOnHttpThread,
                     base::Unretained(this), NormalizeListenHost(host), port,
                     token, &success, &started));
  started.Wait();
  if (!success) {
    http_thread_.Stop();
    http_task_runner_ = nullptr;
  }
  return success;
}

void MCPHttpTransport::StartOnHttpThread(const std::string& listen_host,
                                         uint16_t port,
                                         const std::string& token,
                                         bool* success,
                                         base::WaitableEvent* started) {
  auto server_socket =
      std::make_unique<net::TCPServerSocket>(nullptr, net::NetLogSource());
  int rv = server_socket->ListenWithAddressAndPort(listen_host, port, kBacklog);
  if (rv != net::OK) {
    LOG(ERROR) << "Failed to bind MCP HTTP server to " << listen_host << ":"
               << port << ": " << net::ErrorToString(rv);
    *success = false;
    started->Signal();
    return;
  }

  token_ = token;
  http_server_ = std::make_unique<net::HttpServer>(std::move(server_socket),
                                                   this);

  net::IPEndPoint local_address;
  rv = http_server_->GetLocalAddress(&local_address);
  if (rv == net::OK) {
    std::cerr << "browserd MCP HTTP listening on http://" << listen_host << ":"
              << local_address.port() << "/mcp" << std::endl;
  }

  *success = true;
  started->Signal();
}

void MCPHttpTransport::StopOnHttpThread() {
  open_connections_.clear();
  http_server_.reset();
}

void MCPHttpTransport::OnConnect(int connection_id) {
  open_connections_.insert(connection_id);
}

void MCPHttpTransport::OnHttpRequest(
    int connection_id,
    const net::HttpServerRequestInfo& info) {
  if (info.path != "/mcp") {
    SendTextResponse(connection_id, net::HTTP_NOT_FOUND, "Not Found",
                     "text/plain; charset=utf-8");
    return;
  }

  if (!IsAllowedOrigin(info)) {
    SendTextResponse(connection_id, net::HTTP_FORBIDDEN, "Forbidden",
                     "text/plain; charset=utf-8");
    return;
  }

  if (!IsAuthorized(info)) {
    if (!open_connections_.contains(connection_id)) {
      return;
    }
    net::HttpServerResponseInfo response(net::HTTP_UNAUTHORIZED);
    response.AddHeader("WWW-Authenticate", "Bearer");
    response.AddHeader("Cache-Control", "no-store");
    response.SetBody("Unauthorized", "text/plain; charset=utf-8");
    http_server_->SendResponse(connection_id, response,
                               kMcpHttpTrafficAnnotation);
    return;
  }

  if (info.method != "POST") {
    if (!open_connections_.contains(connection_id)) {
      return;
    }
    net::HttpServerResponseInfo response(net::HTTP_METHOD_NOT_ALLOWED);
    response.AddHeader("Allow", "POST");
    response.AddHeader("Cache-Control", "no-store");
    response.SetBody("Method Not Allowed", "text/plain; charset=utf-8");
    http_server_->SendResponse(connection_id, response,
                               kMcpHttpTrafficAnnotation);
    return;
  }

  if (!AcceptsJson(info)) {
    SendTextResponse(connection_id, net::HTTP_NOT_ACCEPTABLE,
                     "Not Acceptable", "text/plain; charset=utf-8");
    return;
  }

  auto message = base::JSONReader::ReadDict(info.data, base::JSON_PARSE_RFC);
  if (!message.has_value()) {
    SendTextResponse(connection_id, net::HTTP_BAD_REQUEST, "Invalid JSON",
                     "text/plain; charset=utf-8");
    return;
  }

  main_task_runner_->PostTask(
      FROM_HERE, base::BindOnce(
                     message_callback_, std::move(message.value()),
                     base::BindPostTask(
                         http_task_runner_,
                         base::BindOnce(&MCPHttpTransport::SendMCPResponse,
                                        base::Unretained(this),
                                        connection_id))));
}

void MCPHttpTransport::OnWebSocketRequest(
    int connection_id,
    const net::HttpServerRequestInfo& info) {
  SendTextResponse(connection_id, net::HTTP_METHOD_NOT_ALLOWED,
                   "Method Not Allowed", "text/plain; charset=utf-8");
}

void MCPHttpTransport::OnWebSocketMessage(int connection_id, std::string data) {
  SendTextResponse(connection_id, net::HTTP_METHOD_NOT_ALLOWED,
                   "Method Not Allowed", "text/plain; charset=utf-8");
}

void MCPHttpTransport::OnClose(int connection_id) {
  open_connections_.erase(connection_id);
}

void MCPHttpTransport::SendMCPResponse(
    int connection_id,
    std::optional<base::DictValue> response_value) {
  if (!open_connections_.contains(connection_id)) {
    return;
  }

  if (!response_value.has_value()) {
    net::HttpServerResponseInfo response(net::HTTP_ACCEPTED);
    response.AddHeader("Cache-Control", "no-store");
    response.SetBody("", "text/plain; charset=utf-8");
    http_server_->SendResponse(connection_id, response,
                               kMcpHttpTrafficAnnotation);
    return;
  }

  std::string json;
  if (!base::JSONWriter::Write(response_value.value(), &json)) {
    SendTextResponse(connection_id, net::HTTP_INTERNAL_SERVER_ERROR,
                     "Failed to serialize MCP response",
                     "text/plain; charset=utf-8");
    return;
  }

  net::HttpServerResponseInfo response(net::HTTP_OK);
  response.AddHeader("Cache-Control", "no-store");
  response.SetBody(json, "application/json");
  http_server_->SendResponse(connection_id, response, kMcpHttpTrafficAnnotation);
}

void MCPHttpTransport::SendTextResponse(int connection_id,
                                        net::HttpStatusCode status_code,
                                        const std::string& body,
                                        const std::string& content_type) {
  if (!open_connections_.contains(connection_id)) {
    return;
  }

  net::HttpServerResponseInfo response(status_code);
  response.AddHeader("Cache-Control", "no-store");
  response.SetBody(body, content_type);
  http_server_->SendResponse(connection_id, response, kMcpHttpTrafficAnnotation);
}

bool MCPHttpTransport::IsAuthorized(
    const net::HttpServerRequestInfo& info) const {
  std::string authorization = info.GetHeaderValue("authorization");
  constexpr std::string_view kBearerPrefix = "Bearer ";
  if (!base::StartsWith(authorization, kBearerPrefix,
                        base::CompareCase::INSENSITIVE_ASCII)) {
    return false;
  }

  return authorization.substr(kBearerPrefix.size()) == token_;
}

bool MCPHttpTransport::IsAllowedOrigin(
    const net::HttpServerRequestInfo& info) const {
  std::string origin = info.GetHeaderValue("origin");
  if (origin.empty()) {
    return true;
  }

  GURL origin_url(origin);
  return origin_url.is_valid() && IsLoopbackOriginHost(origin_url.host());
}

bool MCPHttpTransport::AcceptsJson(
    const net::HttpServerRequestInfo& info) const {
  std::string accept = info.GetHeaderValue("accept");
  return HeaderContainsMimeType(accept, "application/json");
}

}  // namespace browserd
