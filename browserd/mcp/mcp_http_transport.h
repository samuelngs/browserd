#ifndef BROWSERD_MCP_MCP_HTTP_TRANSPORT_H_
#define BROWSERD_MCP_MCP_HTTP_TRANSPORT_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <set>
#include <string>

#include "base/memory/scoped_refptr.h"
#include "base/synchronization/waitable_event.h"
#include "base/task/sequenced_task_runner.h"
#include "base/threading/thread.h"
#include "browserd/mcp/mcp_transport.h"
#include "net/http/http_status_code.h"
#include "net/server/http_server.h"

namespace net {
class HttpServerRequestInfo;
}

namespace browserd {

class MCPHttpTransport : public net::HttpServer::Delegate {
 public:
  MCPHttpTransport();
  ~MCPHttpTransport() override;

  MCPHttpTransport(const MCPHttpTransport&) = delete;
  MCPHttpTransport& operator=(const MCPHttpTransport&) = delete;

  bool Start(scoped_refptr<base::SequencedTaskRunner> main_task_runner,
             const std::string& host,
             uint16_t port,
             const std::string& token,
             MCPMessageCallback message_callback);

 private:
  // net::HttpServer::Delegate:
  void OnConnect(int connection_id) override;
  void OnHttpRequest(int connection_id,
                     const net::HttpServerRequestInfo& info) override;
  void OnWebSocketRequest(int connection_id,
                          const net::HttpServerRequestInfo& info) override;
  void OnWebSocketMessage(int connection_id, std::string data) override;
  void OnClose(int connection_id) override;

  void StartOnHttpThread(const std::string& host,
                         uint16_t port,
                         const std::string& token,
                         bool* success,
                         base::WaitableEvent* started);
  void StopOnHttpThread();
  void SendMCPResponse(int connection_id,
                       std::optional<base::DictValue> response);
  void SendTextResponse(int connection_id,
                        net::HttpStatusCode status_code,
                        const std::string& body,
                        const std::string& content_type);
  bool IsAuthorized(const net::HttpServerRequestInfo& info) const;
  bool IsAllowedOrigin(const net::HttpServerRequestInfo& info) const;
  bool AcceptsJson(const net::HttpServerRequestInfo& info) const;

  base::Thread http_thread_;
  scoped_refptr<base::SequencedTaskRunner> main_task_runner_;
  scoped_refptr<base::SequencedTaskRunner> http_task_runner_;
  std::unique_ptr<net::HttpServer> http_server_;
  std::string token_;
  MCPMessageCallback message_callback_;
  std::set<int> open_connections_;
};

}  // namespace browserd

#endif  // BROWSERD_MCP_MCP_HTTP_TRANSPORT_H_
