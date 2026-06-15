#include "browserd/gui/browser_main_parts.h"

#include <memory>

#include "base/check.h"
#include "base/files/file_path.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/run_loop.h"
#include "browserd/gui/browser_context.h"
#include "browserd/gui/gui_runtime.h"
#include "browserd/user_data_dir.h"
#include "content/public/browser/browser_thread.h"

#if BUILDFLAG(IS_MAC)
#include "ui/display/screen.h"
#endif

namespace browserd::gui {

BrowserMainParts::BrowserMainParts(RuntimeReadyCallback runtime_ready_callback)
    : runtime_ready_callback_(std::move(runtime_ready_callback)) {}

BrowserMainParts::~BrowserMainParts() = default;

int BrowserMainParts::PreMainMessageLoopRun() {
#if BUILDFLAG(IS_MAC)
  screen_ = std::make_unique<display::ScopedNativeScreen>();
#endif

  base::FilePath user_data_dir;
  if (!GetGuiUserDataDir(&user_data_dir)) {
    LOG(ERROR) << "Failed to create GUI user data directory.";
    return 1;
  }

  auto browser_context = std::make_unique<BrowserContext>(user_data_dir);
  auto runtime = std::make_unique<GuiRuntime>(
      std::move(browser_context),
      base::BindRepeating(&BrowserMainParts::RequestQuit,
                          weak_factory_.GetWeakPtr()));
  if (!runtime->Initialize()) {
    LOG(ERROR) << "Failed to create initial GUI web contents.";
    return 1;
  }

  scoped_refptr<base::SequencedTaskRunner> task_runner =
      content::GetUIThreadTaskRunner({});
  CHECK(runtime_ready_callback_);
  std::move(runtime_ready_callback_).Run(std::move(runtime), task_runner);
  return 0;
}

void BrowserMainParts::WillRunMainMessageLoop(
    std::unique_ptr<base::RunLoop>& run_loop) {
  quit_closure_ = run_loop->QuitClosure();
  if (quit_requested_) {
    RequestQuit();
  }
}

void BrowserMainParts::PostMainMessageLoopRun() {}

void BrowserMainParts::RequestQuit() {
  if (quit_requested_) {
    return;
  }
  quit_requested_ = true;
  if (quit_closure_) {
    std::move(quit_closure_).Run();
  }
}

}  // namespace browserd::gui
