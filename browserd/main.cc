#include "base/command_line.h"
#include "browserd/app.h"
#include "components/os_crypt/common/os_crypt_switches.h"
#include "content/public/app/content_main.h"
#include "headless/lib/browser/headless_browser_impl.h"
#include "headless/lib/headless_content_main_delegate.h"
#include "headless/public/switches.h"
#include "sandbox/policy/switches.h"
#include "ui/gl/gl_switches.h"

int main(int argc, const char** argv) {
  base::CommandLine::Init(argc, argv);

  base::CommandLine* command_line = base::CommandLine::ForCurrentProcess();
  command_line->AppendSwitch(sandbox::policy::switches::kNoSandbox);
  command_line->AppendSwitch(headless::switches::kEnableGPU);
  command_line->AppendSwitch(switches::kEnableUnsafeSwiftShader);
  command_line->AppendSwitch("use-fake-device-for-media-stream");
  command_line->AppendSwitch(os_crypt::switches::kUseMockKeychain);

  if (!command_line->HasSwitch(headless::switches::kAcceptLang)) {
    command_line->AppendSwitchASCII(headless::switches::kAcceptLang,
                                    "en-US,en");
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
