#ifndef BROWSERD_GUI_CONTENT_BROWSER_CLIENT_H_
#define BROWSERD_GUI_CONTENT_BROWSER_CLIENT_H_

#include <memory>
#include <string>

#include "content/public/browser/content_browser_client.h"

namespace base {
class CommandLine;
}  // namespace base

namespace blink {
struct UserAgentMetadata;
}  // namespace blink

namespace content {
class BrowserContext;
}  // namespace content

namespace browserd::gui {

class ContentBrowserClient : public content::ContentBrowserClient {
 public:
  ContentBrowserClient();
  ~ContentBrowserClient() override;

  ContentBrowserClient(const ContentBrowserClient&) = delete;
  ContentBrowserClient& operator=(const ContentBrowserClient&) = delete;

 private:
  std::unique_ptr<content::BrowserMainParts> CreateBrowserMainParts(
      bool is_integration_test) override;
  void AppendExtraCommandLineSwitches(base::CommandLine* command_line,
                                      int child_process_id) override;
  std::string GetAcceptLangs(content::BrowserContext* context) override;
  std::string GetProduct() override;
  std::string GetUserAgent() override;
  blink::UserAgentMetadata GetUserAgentMetadata() override;
};

}  // namespace browserd::gui

#endif  // BROWSERD_GUI_CONTENT_BROWSER_CLIENT_H_
