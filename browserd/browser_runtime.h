#ifndef BROWSERD_BROWSER_RUNTIME_H_
#define BROWSERD_BROWSER_RUNTIME_H_

#include <optional>
#include <string>
#include <vector>

class GURL;

namespace content {
class BrowserContext;
class WebContents;
}  // namespace content

namespace gfx {
class Size;
}

namespace browserd {

struct BrowserTabInfo {
  std::string target_id;
  std::string title;
  std::string url;
};

class BrowserRuntime {
 public:
  virtual ~BrowserRuntime() = default;

  virtual content::BrowserContext* browser_context() const = 0;
  virtual content::WebContents* active_web_contents() const = 0;
  virtual std::vector<BrowserTabInfo> ListTabs() const = 0;
  virtual std::optional<BrowserTabInfo> CreateTab(const GURL& url) = 0;
  virtual bool CloseTab(const std::optional<std::string>& target_id) = 0;
  virtual void ResizeActive(const gfx::Size& size) = 0;
  virtual void Shutdown() = 0;
};

}  // namespace browserd

#endif  // BROWSERD_BROWSER_RUNTIME_H_
