#ifndef BROWSERD_CHROME_CONTENT_BROWSER_CLIENT_H_
#define BROWSERD_CHROME_CONTENT_BROWSER_CLIENT_H_

#include <memory>

#include "browserd/chrome/browserd_chrome_main.h"
#include "chrome/browser/chrome_content_browser_client.h"

namespace base {
class CommandLine;
}  // namespace base

namespace content {
class BrowserMainParts;
}

namespace browserd::chrome {

class ContentBrowserClient : public ChromeContentBrowserClient {
 public:
  explicit ContentBrowserClient(RuntimeReadyCallback ready_callback);
  ~ContentBrowserClient() override;

  ContentBrowserClient(const ContentBrowserClient&) = delete;
  ContentBrowserClient& operator=(const ContentBrowserClient&) = delete;

 private:
  std::unique_ptr<content::BrowserMainParts> CreateBrowserMainParts(
      bool is_integration_test) override;
  void AppendExtraCommandLineSwitches(base::CommandLine* command_line,
                                      int child_process_id) override;

  RuntimeReadyCallback ready_callback_;
};

}  // namespace browserd::chrome

#endif  // BROWSERD_CHROME_CONTENT_BROWSER_CLIENT_H_
