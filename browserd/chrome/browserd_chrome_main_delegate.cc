#include "browserd/chrome/browserd_chrome_main_delegate.h"

#include <memory>
#include <utility>

#include "base/time/time.h"
#include "browserd/chrome/content_browser_client.h"
#include "build/build_config.h"
#include "chrome/app/startup_timestamps.h"
#include "chrome/common/profiler/main_thread_stack_sampling_profiler.h"
#include "content/public/browser/content_browser_client.h"

namespace browserd::chrome {

BrowserdChromeMainDelegate::BrowserdChromeMainDelegate(
    RuntimeReadyCallback ready_callback)
    : ChromeMainDelegate(
          StartupTimestamps{.exe_entry_point_ticks = base::TimeTicks::Now()}),
      ready_callback_(std::move(ready_callback)) {}

BrowserdChromeMainDelegate::~BrowserdChromeMainDelegate() = default;

content::ContentBrowserClient*
BrowserdChromeMainDelegate::CreateContentBrowserClient() {
  chrome_content_browser_client_ =
      std::make_unique<ContentBrowserClient>(std::move(ready_callback_));
#if !BUILDFLAG(IS_ANDROID)
  if (sampling_profiler_) {
    chrome_content_browser_client_->SetSamplingProfiler(
        std::move(sampling_profiler_));
  }
#endif
  return chrome_content_browser_client_.get();
}

}  // namespace browserd::chrome
