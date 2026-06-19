#ifndef BROWSERD_STARTUP_BROWSERD_MAIN_H_
#define BROWSERD_STARTUP_BROWSERD_MAIN_H_

#include <memory>

#include "base/functional/callback.h"
#include "browserd/gui/browser_main_parts.h"
#include "browserd/gui/runtime_options.h"

namespace base {
class CommandLine;
class Environment;
}  // namespace base

namespace headless {
class HeadlessBrowser;
}  // namespace headless

namespace browserd::startup {

bool EnsureCommandLineInitialized(int argc, const char** argv);
bool HasHelpSwitch(const base::CommandLine& command_line);
void PrintHelp();

bool IsGuiRequested(const base::CommandLine& command_line,
                    base::Environment* environment,
                    bool propagate_to_child_processes);

void ApplyDefaultCommandLineSwitches(int argc,
                                     const char** argv,
                                     bool requested_gui,
                                     bool include_headless_defaults,
                                     base::Environment* environment);

int RunGuiContentMain(
    int argc,
    const char** argv,
    browserd::gui::RuntimeOptions options,
    browserd::gui::BrowserMainParts::RuntimeReadyCallback ready_callback);
int RunHeadlessContentMain(
    int argc,
    const char** argv,
    base::OnceCallback<void(headless::HeadlessBrowser*)> on_start_callback);

}  // namespace browserd::startup

#endif  // BROWSERD_STARTUP_BROWSERD_MAIN_H_
