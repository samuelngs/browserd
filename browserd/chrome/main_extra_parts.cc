#include "browserd/chrome/main_extra_parts.h"

#include <memory>
#include <utility>

#include "base/check.h"
#include "browserd/chrome/native_chrome_runtime.h"
#include "content/public/browser/browser_thread.h"

namespace browserd::chrome {

MainExtraParts::MainExtraParts(RuntimeReadyCallback ready_callback)
    : ready_callback_(std::move(ready_callback)) {}

MainExtraParts::~MainExtraParts() = default;

void MainExtraParts::PostBrowserStart() {
  if (!ready_callback_) {
    return;
  }

  scoped_refptr<base::SequencedTaskRunner> task_runner =
      content::GetUIThreadTaskRunner({});
  CHECK(task_runner);
  std::move(ready_callback_)
      .Run(std::make_unique<NativeChromeRuntime>(), std::move(task_runner));
}

}  // namespace browserd::chrome
