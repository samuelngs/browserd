#ifndef BROWSERD_CHROME_MAIN_EXTRA_PARTS_H_
#define BROWSERD_CHROME_MAIN_EXTRA_PARTS_H_

#include "browserd/chrome/browserd_chrome_main.h"
#include "chrome/browser/chrome_browser_main_extra_parts.h"

namespace browserd::chrome {

class MainExtraParts : public ChromeBrowserMainExtraParts {
 public:
  explicit MainExtraParts(RuntimeReadyCallback ready_callback);
  ~MainExtraParts() override;

  MainExtraParts(const MainExtraParts&) = delete;
  MainExtraParts& operator=(const MainExtraParts&) = delete;

 private:
  void PostBrowserStart() override;

  RuntimeReadyCallback ready_callback_;
};

}  // namespace browserd::chrome

#endif  // BROWSERD_CHROME_MAIN_EXTRA_PARTS_H_
