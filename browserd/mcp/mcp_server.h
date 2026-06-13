#ifndef BROWSERD_MCP_MCP_SERVER_H_
#define BROWSERD_MCP_MCP_SERVER_H_

#include <cstdint>
#include <queue>
#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "base/task/sequenced_task_runner.h"
#include "base/timer/timer.h"
#include "base/values.h"
#include "browserd/browser_runtime.h"
#include "browserd/mcp/mcp_http_transport.h"
#include "browserd/mcp/mcp_tool.h"
#include "browserd/mcp/mcp_transport.h"
#include "content/public/browser/web_contents_observer.h"

namespace browserd {

class MCPServer : public content::WebContentsObserver {
 public:
  MCPServer();
  ~MCPServer() override;

  MCPServer(const MCPServer&) = delete;
  MCPServer& operator=(const MCPServer&) = delete;

  void Start(scoped_refptr<base::SequencedTaskRunner> task_runner,
             bool shutdown_on_stdio_close);
  bool StartHttpTransport(const std::string& host,
                          uint16_t port,
                          const std::string& token);

  void SetRuntime(BrowserRuntime* runtime);
  void RefreshActiveWebContents();

  content::WebContents* web_contents() const { return web_contents_; }
  BrowserRuntime* runtime() const { return runtime_; }

  void InjectScriptOnNewDocument(const std::string& source);

  void RegisterTool(MCPToolDef tool);

  // content::WebContentsObserver:
  void DidFinishNavigation(content::NavigationHandle* navigation_handle) override;

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
  void StartBehaviorSimulation();
  void DispatchMouseMove();
  void SetWebContents(content::WebContents* web_contents);

  MCPStdioTransport stdio_transport_;
  MCPHttpTransport http_transport_;
  raw_ptr<BrowserRuntime> runtime_ = nullptr;
  raw_ptr<content::WebContents> web_contents_ = nullptr;
  std::vector<MCPToolDef> tools_;
  std::vector<std::string> injected_scripts_;
  bool initialized_ = false;
  bool shutdown_on_stdio_close_ = true;

  bool tool_call_in_progress_ = false;
  std::queue<PendingToolCall> tool_call_queue_;

  base::RepeatingTimer behavior_timer_;
  scoped_refptr<base::SequencedTaskRunner> task_runner_;
  double mouse_x_ = 400.0;
  double mouse_y_ = 300.0;
  double mouse_vx_ = 0.0;
  double mouse_vy_ = 0.0;
  uint64_t behavior_tick_ = 0;
};

}  // namespace browserd

#endif  // BROWSERD_MCP_MCP_SERVER_H_
