#ifndef BROWSERD_CHROME_NATIVE_CHROME_RUNTIME_H_
#define BROWSERD_CHROME_NATIVE_CHROME_RUNTIME_H_

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "browserd/browser_runtime.h"

class BrowserWindowInterface;
class ScopedKeepAlive;
class ScopedProfileKeepAlive;

namespace browserd::chrome {

class NativeChromeRuntime : public BrowserRuntime {
 public:
  NativeChromeRuntime();
  ~NativeChromeRuntime() override;

  NativeChromeRuntime(const NativeChromeRuntime&) = delete;
  NativeChromeRuntime& operator=(const NativeChromeRuntime&) = delete;

  content::BrowserContext* browser_context() const override;
  content::WebContents* active_web_contents() const override;
  content::WebContents* GetWebContentsByTargetId(
      const std::string& target_id) const override;
  std::vector<content::WebContents*> AllWebContents() const override;
  std::vector<BrowserTabInfo> ListTabs() const override;
  std::optional<BrowserTabInfo> CreateTab(const GURL& url) override;
  bool CloseTab(const std::optional<std::string>& target_id) override;
  void ResizeActive(const gfx::Size& size) override;
  void Shutdown() override;

 private:
  BrowserWindowInterface* ActiveBrowserWindow() const;
  void EnsureKeepAlive();

  std::unique_ptr<ScopedKeepAlive> keep_alive_;
  std::unique_ptr<ScopedProfileKeepAlive> profile_keep_alive_;
};

}  // namespace browserd::chrome

#endif  // BROWSERD_CHROME_NATIVE_CHROME_RUNTIME_H_
