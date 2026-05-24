#ifndef BROWSERD_APP_H_
#define BROWSERD_APP_H_

#include <memory>

#include "base/memory/raw_ptr.h"
#include "browserd/mcp/mcp_server.h"
#include "headless/public/headless_browser.h"

namespace browserd {

class App {
 public:
  App();
  ~App();

  App(const App&) = delete;
  App& operator=(const App&) = delete;

  void OnBrowserStart(headless::HeadlessBrowser* browser);

 private:
  void RegisterTools();

  raw_ptr<headless::HeadlessBrowser> browser_ = nullptr;
  MCPServer mcp_server_;
};

}  // namespace browserd

#endif  // BROWSERD_APP_H_
