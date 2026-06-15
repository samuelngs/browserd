#ifndef BROWSERD_GUI_MAIN_DELEGATE_H_
#define BROWSERD_GUI_MAIN_DELEGATE_H_

#include <memory>
#include <optional>

#include "browserd/gui/browser_main_parts.h"
#include "content/public/app/content_main_delegate.h"

namespace content {
class ContentBrowserClient;
class ContentClient;
class ContentRendererClient;
}  // namespace content

namespace browserd::gui {

class MainDelegate : public content::ContentMainDelegate {
 public:
  MainDelegate();
  explicit MainDelegate(
      BrowserMainParts::RuntimeReadyCallback runtime_ready_callback);
  ~MainDelegate() override;

  MainDelegate(const MainDelegate&) = delete;
  MainDelegate& operator=(const MainDelegate&) = delete;

 private:
  std::optional<int> BasicStartupComplete() override;
  void PreSandboxStartup() override;
  std::optional<int> PreBrowserMain() override;
  content::ContentBrowserClient* CreateContentBrowserClient() override;
  content::ContentRendererClient* CreateContentRendererClient() override;

  std::unique_ptr<content::ContentClient> content_client_;
  std::unique_ptr<content::ContentBrowserClient> content_browser_client_;
  std::unique_ptr<content::ContentRendererClient> content_renderer_client_;
  BrowserMainParts::RuntimeReadyCallback runtime_ready_callback_;
};

}  // namespace browserd::gui

#endif  // BROWSERD_GUI_MAIN_DELEGATE_H_
