#ifndef BROWSERD_MCP_MCP_SERVER_H_
#define BROWSERD_MCP_MCP_SERVER_H_

#include <cstdint>
#include <queue>
#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "base/task/sequenced_task_runner.h"
#include "base/values.h"
#include "browserd/mcp/mcp_http_transport.h"
#include "browserd/mcp/mcp_tool.h"
#include "browserd/mcp/mcp_transport.h"

namespace browserd {

class BrowserController;

class MCPServer {
 public:
  MCPServer();
  ~MCPServer();

  MCPServer(const MCPServer&) = delete;
  MCPServer& operator=(const MCPServer&) = delete;

  void Start(scoped_refptr<base::SequencedTaskRunner> task_runner,
             bool shutdown_on_stdio_close);
  bool StartHttpTransport(const std::string& host,
                          uint16_t port,
                          const std::string& token);

  void SetController(BrowserController* controller);
  BrowserController* controller() const { return controller_; }

  void RegisterTool(MCPToolDef tool);

 private:
  struct PendingToolCall {
    PendingToolCall(std::string name,
                    base::DictValue args,
                    base::Value id,
                    MCPResponseCallback response_callback);
    ~PendingToolCall();
    PendingToolCall(PendingToolCall&&);

    std::string name;
    base::DictValue args;
    base::Value id;
    MCPResponseCallback response_callback;
  };

  void OnMessage(base::DictValue message,
                 MCPResponseCallback response_callback);

  void HandleInitialize(const base::DictValue& params,
                        const base::Value& id,
                        MCPResponseCallback response_callback);
  void HandleToolsList(const base::Value& id,
                       MCPResponseCallback response_callback);
  void HandleToolsCall(const base::DictValue& params,
                       const base::Value& id,
                       MCPResponseCallback response_callback);
  void ExecuteNextToolCall();
  void OnToolCallComplete(base::Value id,
                          MCPResponseCallback response_callback,
                          base::ListValue content,
                          bool is_error);

  void SendNoResponse(MCPResponseCallback response_callback);
  void SendResult(const base::Value& id,
                  base::DictValue result,
                  MCPResponseCallback response_callback);
  void SendError(const base::Value& id,
                 int code,
                 const std::string& message,
                 MCPResponseCallback response_callback);

  void OnTransportClosed();

  MCPStdioTransport stdio_transport_;
  MCPHttpTransport http_transport_;
  raw_ptr<BrowserController> controller_ = nullptr;
  std::vector<MCPToolDef> tools_;
  bool initialized_ = false;
  bool shutdown_on_stdio_close_ = true;

  bool tool_call_in_progress_ = false;
  std::queue<PendingToolCall> tool_call_queue_;

  scoped_refptr<base::SequencedTaskRunner> task_runner_;
};

}  // namespace browserd

#endif  // BROWSERD_MCP_MCP_SERVER_H_
