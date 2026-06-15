#ifndef BROWSERD_HEADLESS_RUNTIME_H_
#define BROWSERD_HEADLESS_RUNTIME_H_

#include <optional>
#include <string>

#include "base/memory/raw_ptr.h"
#include "browserd/browser_runtime.h"

namespace headless {
class HeadlessBrowser;
class HeadlessBrowserContext;
class HeadlessWebContents;
}  // namespace headless

namespace browserd {

class HeadlessRuntime : public BrowserRuntime {
 public:
  explicit HeadlessRuntime(headless::HeadlessBrowser* browser);
  ~HeadlessRuntime() override;

  HeadlessRuntime(const HeadlessRuntime&) = delete;
  HeadlessRuntime& operator=(const HeadlessRuntime&) = delete;

  bool Initialize();

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
  headless::HeadlessWebContents* ActiveHeadlessWebContents() const;
  headless::HeadlessWebContents* FindByTargetId(
      const std::string& target_id) const;
  BrowserTabInfo MakeTabInfo(headless::HeadlessWebContents* contents) const;
  void ChooseFallbackActiveTab(headless::HeadlessWebContents* closing);

  raw_ptr<headless::HeadlessBrowser> browser_ = nullptr;
  raw_ptr<headless::HeadlessBrowserContext> browser_context_ = nullptr;
  raw_ptr<content::WebContents> active_web_contents_ = nullptr;
};

}  // namespace browserd

#endif  // BROWSERD_HEADLESS_RUNTIME_H_
