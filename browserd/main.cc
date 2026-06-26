#include "base/command_line.h"
#include "base/environment.h"
#include "base/functional/bind.h"
#include "browserd/app.h"
#include "browserd/chrome/browserd_chrome_main.h"
#include "browserd/startup/browserd_main.h"
#include "headless/public/headless_browser.h"

int main(int argc, const char** argv) {
  browserd::startup::EnsureCommandLineInitialized(argc, argv);

  base::CommandLine* command_line = base::CommandLine::ForCurrentProcess();
  if (browserd::startup::HasHelpSwitch(*command_line)) {
    browserd::startup::PrintHelp();
    return 0;
  }

  auto environment = base::Environment::Create();
  const bool requested_gui = browserd::startup::IsGuiRequested(
      *command_line, environment.get(), true);

  browserd::startup::ApplyDefaultCommandLineSwitches(
      argc, argv, requested_gui, !requested_gui, environment.get());

  if (requested_gui) {
    browserd::App app;
    return browserd::chrome::RunChromeBrowserMain(
        argc, argv,
        base::BindOnce(&browserd::App::Start, base::Unretained(&app)));
  }

  browserd::App app;
  return browserd::startup::RunHeadlessContentMain(
      argc, argv,
      base::BindOnce(&browserd::App::OnBrowserStart,
                     base::Unretained(&app)));
}
