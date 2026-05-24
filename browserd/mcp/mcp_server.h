#ifndef BROWSERD_MCP_MCP_SERVER_H_
#define BROWSERD_MCP_MCP_SERVER_H_

#include <queue>
#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "base/task/sequenced_task_runner.h"
#include "base/timer/timer.h"
#include "base/values.h"
#include "browserd/mcp/mcp_tool.h"
#include "browserd/mcp/mcp_transport.h"
#include "content/public/browser/web_contents_observer.h"

namespace headless {
class HeadlessBrowser;
class HeadlessBrowserContext;
}

namespace browserd {

class MCPServer : public content::WebContentsObserver {
 public:
  MCPServer();
  ~MCPServer() override;

  MCPServer(const MCPServer&) = delete;
  MCPServer& operator=(const MCPServer&) = delete;

  void Start(scoped_refptr<base::SequencedTaskRunner> task_runner);

  void SetBrowser(headless::HeadlessBrowser* browser,
                  headless::HeadlessBrowserContext* context);
  void SetWebContents(content::WebContents* web_contents);

  content::WebContents* web_contents() const { return web_contents_; }
  headless::HeadlessBrowser* browser() const { return browser_; }
  headless::HeadlessBrowserContext* browser_context() const {
    return browser_context_;
  }

  void InjectScriptOnNewDocument(const std::string& source);

  void RegisterTool(MCPToolDef tool);

  // content::WebContentsObserver:
  void ReadyToCommitNavigation(
      content::NavigationHandle* navigation_handle) override;

 private:
  struct PendingToolCall {
    PendingToolCall(std::string name,
                    base::DictValue args,
                    base::Value id);
    ~PendingToolCall();
    PendingToolCall(PendingToolCall&&);

    std::string name;
    base::DictValue args;
    base::Value id;
  };

  void OnMessage(base::DictValue message);

  void HandleInitialize(const base::DictValue& params,
                        const base::Value& id);
  void HandleToolsList(const base::Value& id);
  void HandleToolsCall(const base::DictValue& params,
                       const base::Value& id);
  void ExecuteNextToolCall();
  void OnToolCallComplete(base::Value id,
                          base::ListValue content,
                          bool is_error);

  void SendResult(const base::Value& id, base::DictValue result);
  void SendError(const base::Value& id, int code, const std::string& message);

  void StartBehaviorSimulation();
  void DispatchMouseMove();

  MCPTransport transport_;
  raw_ptr<content::WebContents> web_contents_ = nullptr;
  raw_ptr<headless::HeadlessBrowser> browser_ = nullptr;
  raw_ptr<headless::HeadlessBrowserContext> browser_context_ = nullptr;
  std::vector<MCPToolDef> tools_;
  std::vector<std::string> injected_scripts_;
  bool initialized_ = false;

  bool tool_call_in_progress_ = false;
  std::queue<PendingToolCall> tool_call_queue_;

  base::RepeatingTimer behavior_timer_;
  double mouse_x_ = 400.0;
  double mouse_y_ = 300.0;
  double mouse_vx_ = 0.0;
  double mouse_vy_ = 0.0;
  uint64_t behavior_tick_ = 0;
};

}  // namespace browserd

#endif  // BROWSERD_MCP_MCP_SERVER_H_
