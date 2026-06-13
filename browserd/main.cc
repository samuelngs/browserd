#include "base/check.h"
#include "base/command_line.h"
#include "base/environment.h"
#include "browserd/app.h"
#include "browserd/gui/main_delegate.h"
#include "browserd/switches.h"
#include "build/build_config.h"
#include "components/os_crypt/common/os_crypt_switches.h"
#include "content/public/app/content_main.h"
#include "headless/lib/browser/headless_browser_impl.h"
#include "headless/lib/headless_content_main_delegate.h"
#include "headless/public/switches.h"
#include "sandbox/policy/switches.h"
#include "ui/gl/gl_switches.h"

#if BUILDFLAG(IS_MAC)
#include "sandbox/mac/seatbelt_exec.h"
#endif

int main(int argc, const char** argv) {
  base::CommandLine::Init(argc, argv);

  base::CommandLine* command_line = base::CommandLine::ForCurrentProcess();
  auto environment = base::Environment::Create();
  constexpr char kGuiEnvironment[] = "BROWSERD_INTERNAL_GUI";
  const bool requested_gui =
      command_line->HasSwitch(browserd::switches::kGui) ||
      environment->HasVar(kGuiEnvironment);
  if (command_line->HasSwitch(browserd::switches::kGui)) {
    environment->SetVar(kGuiEnvironment, "1");
  }

#if BUILDFLAG(IS_MAC)
  sandbox::SeatbeltExecServer::CreateFromArgumentsResult seatbelt =
      sandbox::SeatbeltExecServer::CreateFromArguments(
          command_line->GetProgram().value().c_str(), argc,
          const_cast<char**>(argv));
  if (seatbelt.sandbox_required) {
    CHECK(seatbelt.server->InitializeSandbox());
  }
#endif

  command_line->AppendSwitch(sandbox::policy::switches::kNoSandbox);
  command_line->AppendSwitch(headless::switches::kEnableGPU);
  command_line->AppendSwitch(switches::kEnableUnsafeSwiftShader);
  command_line->AppendSwitch("use-fake-device-for-media-stream");
  command_line->AppendSwitch(os_crypt::switches::kUseMockKeychain);

  if (!command_line->HasSwitch(headless::switches::kAcceptLang)) {
    command_line->AppendSwitchASCII(headless::switches::kAcceptLang,
                                    "en-US,en");
  }

  if (requested_gui) {
    browserd::gui::MainDelegate delegate;
    content::ContentMainParams params(&delegate);
    params.argc = argc;
    params.argv = argv;
    return content::ContentMain(std::move(params));
  }

  if (!command_line->HasSwitch(headless::switches::kScreenInfo)) {
    command_line->AppendSwitchASCII(
        headless::switches::kScreenInfo,
        "{0,0 3840x2160 devicePixelRatio=2 workAreaTop=50}");
  }

  browserd::App app;
  auto browser = std::make_unique<headless::HeadlessBrowserImpl>(
      base::BindOnce(&browserd::App::OnBrowserStart,
                     base::Unretained(&app)));
  headless::HeadlessContentMainDelegate delegate(std::move(browser));
  content::ContentMainParams params(&delegate);
  params.argc = argc;
  params.argv = argv;
  return content::ContentMain(std::move(params));
}
