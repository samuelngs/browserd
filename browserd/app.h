#ifndef BROWSERD_APP_H_
#define BROWSERD_APP_H_

#include <memory>

#include "base/memory/scoped_refptr.h"
#include "browserd/mcp/mcp_server.h"

namespace base {
class SequencedTaskRunner;
}  // namespace base

namespace headless {
class HeadlessBrowser;
}  // namespace headless

namespace browserd {

class BrowserController;
class BrowserRuntime;

class App {
 public:
  App();
  ~App();

  App(const App&) = delete;
  App& operator=(const App&) = delete;

  void OnBrowserStart(headless::HeadlessBrowser* browser);
  void Start(std::unique_ptr<BrowserRuntime> runtime,
             scoped_refptr<base::SequencedTaskRunner> task_runner);

 private:
  void RegisterTools();

  std::unique_ptr<BrowserController> controller_;
  MCPServer mcp_server_;
};

}  // namespace browserd

#endif  // BROWSERD_APP_H_
