#ifndef BROWSERD_CHROME_BROWSERD_CHROME_MAIN_DELEGATE_H_
#define BROWSERD_CHROME_BROWSERD_CHROME_MAIN_DELEGATE_H_

#include "browserd/chrome/browserd_chrome_main.h"
#include "chrome/app/chrome_main_delegate.h"

namespace browserd::chrome {

class BrowserdChromeMainDelegate : public ChromeMainDelegate {
 public:
  explicit BrowserdChromeMainDelegate(RuntimeReadyCallback ready_callback);
  ~BrowserdChromeMainDelegate() override;

  BrowserdChromeMainDelegate(const BrowserdChromeMainDelegate&) = delete;
  BrowserdChromeMainDelegate& operator=(const BrowserdChromeMainDelegate&) =
      delete;

 private:
  content::ContentBrowserClient* CreateContentBrowserClient() override;

  RuntimeReadyCallback ready_callback_;
};

}  // namespace browserd::chrome

#endif  // BROWSERD_CHROME_BROWSERD_CHROME_MAIN_DELEGATE_H_
