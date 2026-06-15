#include "browserd/mcp/mcp_server.h"

#include <optional>
#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/values.h"
#include "browserd/core/browser_controller.h"

namespace browserd {

MCPServer::PendingToolCall::PendingToolCall(std::string name,
                                            base::DictValue args,
                                            base::Value id,
                                            MCPResponseCallback response_callback)
    : name(std::move(name)),
      args(std::move(args)),
      id(std::move(id)),
      response_callback(std::move(response_callback)) {}
MCPServer::PendingToolCall::~PendingToolCall() = default;
MCPServer::PendingToolCall::PendingToolCall(PendingToolCall&&) = default;

MCPServer::MCPServer() = default;
MCPServer::~MCPServer() = default;

void MCPServer::Start(scoped_refptr<base::SequencedTaskRunner> task_runner,
                      bool shutdown_on_stdio_close) {
  task_runner_ = task_runner;
  shutdown_on_stdio_close_ = shutdown_on_stdio_close;
  stdio_transport_.Start(
      std::move(task_runner), base::BindRepeating(&MCPServer::OnMessage,
                                                  base::Unretained(this)),
      base::BindOnce(&MCPServer::OnTransportClosed, base::Unretained(this)));
}

bool MCPServer::StartHttpTransport(const std::string& host,
                                   uint16_t port,
                                   const std::string& token) {
  return http_transport_.Start(
      task_runner_, host, port, token,
      base::BindRepeating(&MCPServer::OnMessage, base::Unretained(this)));
}

void MCPServer::SetController(BrowserController* controller) {
  controller_ = controller;
}

void MCPServer::RegisterTool(MCPToolDef tool) {
  tools_.push_back(std::move(tool));
}

void MCPServer::OnMessage(base::DictValue message,
                          MCPResponseCallback response_callback) {
  const std::string* method = message.FindString("method");
  base::Value null_id;
  const base::Value* id_ptr = message.Find("id");
  const base::Value& id = id_ptr ? *id_ptr : null_id;

  if (!method) {
    LOG(WARNING) << "MCP message missing method";
    if (id_ptr) {
      SendError(id, -32600, "Invalid Request", std::move(response_callback));
    } else {
      SendNoResponse(std::move(response_callback));
    }
    return;
  }

  const base::DictValue* params = message.FindDict("params");
  base::DictValue empty_params;

  if (*method == "initialize") {
    if (id_ptr) {
      HandleInitialize(params ? *params : empty_params, id,
                       std::move(response_callback));
    } else {
      SendNoResponse(std::move(response_callback));
    }
  } else if (*method == "notifications/initialized") {
    initialized_ = true;
    SendNoResponse(std::move(response_callback));
  } else if (*method == "ping") {
    if (id_ptr) {
      SendResult(id, base::DictValue(), std::move(response_callback));
    } else {
      SendNoResponse(std::move(response_callback));
    }
  } else if (!initialized_) {
    if (id_ptr) {
      SendError(id, -32002, "Server not initialized",
                std::move(response_callback));
    } else {
      SendNoResponse(std::move(response_callback));
    }
  } else if (*method == "tools/list") {
    if (id_ptr) {
      HandleToolsList(id, std::move(response_callback));
    } else {
      SendNoResponse(std::move(response_callback));
    }
  } else if (*method == "tools/call") {
    if (id_ptr) {
      HandleToolsCall(params ? *params : empty_params, id,
                      std::move(response_callback));
    } else {
      SendNoResponse(std::move(response_callback));
    }
  } else {
    if (id_ptr) {
      SendError(id, -32601, "Method not found: " + *method,
                std::move(response_callback));
    } else {
      SendNoResponse(std::move(response_callback));
    }
  }
}

void MCPServer::HandleInitialize(const base::DictValue& params,
                                 const base::Value& id,
                                 MCPResponseCallback response_callback) {
  initialized_ = true;

  base::DictValue result;

  base::DictValue server_info;
  server_info.Set("name", "browserd");
  server_info.Set("version", "1.0.0");
  result.Set("serverInfo", std::move(server_info));

  result.Set("protocolVersion", "2024-11-05");

  base::DictValue capabilities;
  base::DictValue tools_cap;
  tools_cap.Set("listChanged", false);
  capabilities.Set("tools", std::move(tools_cap));
  result.Set("capabilities", std::move(capabilities));

  SendResult(id, std::move(result), std::move(response_callback));
}

void MCPServer::HandleToolsList(const base::Value& id,
                                MCPResponseCallback response_callback) {
  base::DictValue result;
  base::ListValue tool_list;

  for (const auto& tool : tools_) {
    base::DictValue tool_info;
    tool_info.Set("name", tool.name);
    tool_info.Set("description", tool.description);
    tool_info.Set("inputSchema", tool.input_schema.Clone());
    tool_list.Append(std::move(tool_info));
  }

  result.Set("tools", std::move(tool_list));
  SendResult(id, std::move(result), std::move(response_callback));
}

void MCPServer::HandleToolsCall(const base::DictValue& params,
                                const base::Value& id,
                                MCPResponseCallback response_callback) {
  const std::string* tool_name = params.FindString("name");
  if (!tool_name) {
    SendError(id, -32602, "Missing tool name", std::move(response_callback));
    return;
  }

  const base::DictValue* arguments = params.FindDict("arguments");
  base::DictValue args = arguments ? arguments->Clone() : base::DictValue();

  tool_call_queue_.emplace(*tool_name, std::move(args), id.Clone(),
                           std::move(response_callback));

  if (!tool_call_in_progress_) {
    ExecuteNextToolCall();
  }
}

void MCPServer::ExecuteNextToolCall() {
  if (tool_call_queue_.empty()) {
    tool_call_in_progress_ = false;
    return;
  }

  tool_call_in_progress_ = true;
  PendingToolCall call = std::move(tool_call_queue_.front());
  tool_call_queue_.pop();

  if (!controller_) {
    OnToolCallComplete(std::move(call.id), std::move(call.response_callback),
                       TextContent("No browser controller"), true);
    return;
  }

  for (auto& tool : tools_) {
    if (tool.name == call.name) {
      tool.handler.Run(
          controller_, std::move(call.args),
          base::BindOnce(&MCPServer::OnToolCallComplete,
                         base::Unretained(this), std::move(call.id),
                         std::move(call.response_callback)));
      return;
    }
  }

  SendError(call.id, -32602, "Unknown tool: " + call.name,
            std::move(call.response_callback));
  ExecuteNextToolCall();
}

void MCPServer::OnToolCallComplete(base::Value id,
                                   MCPResponseCallback response_callback,
                                   base::ListValue content,
                                   bool is_error) {
  base::DictValue result;
  result.Set("content", std::move(content));
  if (is_error) {
    result.Set("isError", true);
  }
  SendResult(id, std::move(result), std::move(response_callback));

  ExecuteNextToolCall();
}

void MCPServer::SendNoResponse(MCPResponseCallback response_callback) {
  std::move(response_callback).Run(std::nullopt);
}

void MCPServer::SendResult(const base::Value& id,
                           base::DictValue result,
                           MCPResponseCallback response_callback) {
  base::DictValue response;
  response.Set("jsonrpc", "2.0");
  response.Set("id", id.Clone());
  response.Set("result", std::move(result));
  std::move(response_callback).Run(
      std::optional<base::DictValue>(std::move(response)));
}

void MCPServer::SendError(const base::Value& id,
                          int code,
                          const std::string& message,
                          MCPResponseCallback response_callback) {
  base::DictValue response;
  response.Set("jsonrpc", "2.0");
  response.Set("id", id.Clone());

  base::DictValue error;
  error.Set("code", code);
  error.Set("message", message);
  response.Set("error", std::move(error));

  std::move(response_callback).Run(
      std::optional<base::DictValue>(std::move(response)));
}

void MCPServer::OnTransportClosed() {
  if (shutdown_on_stdio_close_) {
    if (controller_) {
      controller_->Shutdown();
    }
  }
}

}  // namespace browserd
