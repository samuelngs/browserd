#ifndef BROWSERD_GUI_BROWSER_MAIN_PARTS_H_
#define BROWSERD_GUI_BROWSER_MAIN_PARTS_H_

#include <memory>

#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"
#include "base/task/sequenced_task_runner.h"
#include "browserd/browser_runtime.h"
#include "build/build_config.h"
#include "content/public/browser/browser_main_parts.h"

#if BUILDFLAG(IS_MAC)
namespace display {
class ScopedNativeScreen;
}  // namespace display
#endif

namespace browserd::gui {

class BrowserMainParts : public content::BrowserMainParts {
 public:
  using RuntimeReadyCallback =
      base::OnceCallback<void(std::unique_ptr<BrowserRuntime>,
                              scoped_refptr<base::SequencedTaskRunner>)>;

  explicit BrowserMainParts(RuntimeReadyCallback runtime_ready_callback);
  ~BrowserMainParts() override;

  BrowserMainParts(const BrowserMainParts&) = delete;
  BrowserMainParts& operator=(const BrowserMainParts&) = delete;

 private:
  int PreMainMessageLoopRun() override;
  void WillRunMainMessageLoop(std::unique_ptr<base::RunLoop>& run_loop)
      override;
  void PostMainMessageLoopRun() override;

  void RequestQuit();

  RuntimeReadyCallback runtime_ready_callback_;
#if BUILDFLAG(IS_MAC)
  std::unique_ptr<display::ScopedNativeScreen> screen_;
#endif
  base::OnceClosure quit_closure_;
  bool quit_requested_ = false;
  base::WeakPtrFactory<BrowserMainParts> weak_factory_{this};
};

}  // namespace browserd::gui

#endif  // BROWSERD_GUI_BROWSER_MAIN_PARTS_H_
