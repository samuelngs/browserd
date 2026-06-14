#include <iostream>

#include "base/check.h"
#include "base/command_line.h"
#include "base/environment.h"
#include "browserd/app.h"
#include "browserd/gui/main_delegate.h"
#include "browserd/switches.h"
#include "build/build_config.h"
#include "content/public/app/content_main.h"
#include "content/public/common/content_switches.h"
#include "headless/lib/browser/headless_browser_impl.h"
#include "headless/lib/headless_content_main_delegate.h"
#include "headless/public/switches.h"
#include "sandbox/policy/switches.h"
#include "ui/gl/gl_switches.h"

#if BUILDFLAG(IS_MAC)
#include "components/os_crypt/common/os_crypt_switches.h"
#include "sandbox/mac/seatbelt_exec.h"
#endif

namespace {

constexpr char kGuiEnvironment[] = "BROWSERD_INTERNAL_GUI";

bool HasHelpSwitch(const base::CommandLine& command_line) {
  return command_line.HasSwitch("help") || command_line.HasSwitch("h");
}

void PrintHelp() {
  std::cout
      << "Usage: browserd [options]\n"
      << "\n"
      << "Options:\n"
      << "  --help, -h                 Show this help and exit.\n"
      << "  --gui                      Run with visible content-only windows.\n"
      << "  --mcp-http-port=<port>     Enable Streamable HTTP MCP on /mcp.\n"
      << "  --mcp-http-host=<host>     HTTP bind host/address. Use 0.0.0.0 or ::\n"
      << "                             to listen on all interfaces.\n"
      << "                             Defaults to 127.0.0.1.\n"
      << "  --user-data-dir=<path>     Browser profile directory.\n"
      << "\n"
      << "Environment:\n"
      << "  BROWSERD_MCP_HTTP_TOKEN    Required when --mcp-http-port is used.\n"
      << "\n"
      << "GPU:\n"
      << "  Headless mode enables GPU automatically on desktop sessions. On Linux,\n"
      << "  GPU is auto-enabled only when DISPLAY or WAYLAND_DISPLAY is set.\n";
}

bool ShouldAutoEnableGpu(bool requested_gui, base::Environment* environment) {
  if (requested_gui) {
    return true;
  }

#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)
  return environment->HasVar("DISPLAY") ||
         environment->HasVar("WAYLAND_DISPLAY");
#else
  return true;
#endif
}

void MaybeEnableGpu(base::CommandLine* command_line,
                    bool requested_gui,
                    base::Environment* environment) {
  if (command_line->HasSwitch(headless::switches::kEnableGPU) ||
      command_line->HasSwitch(switches::kDisableGpu)) {
    return;
  }

  if (ShouldAutoEnableGpu(requested_gui, environment)) {
    command_line->AppendSwitch(headless::switches::kEnableGPU);
  }
}

}  // namespace

int main(int argc, const char** argv) {
  base::CommandLine::Init(argc, argv);

  base::CommandLine* command_line = base::CommandLine::ForCurrentProcess();
  if (HasHelpSwitch(*command_line)) {
    PrintHelp();
    return 0;
  }

  auto environment = base::Environment::Create();
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
  MaybeEnableGpu(command_line, requested_gui, environment.get());
  command_line->AppendSwitch(switches::kEnableUnsafeSwiftShader);
  command_line->AppendSwitch("use-fake-device-for-media-stream");
#if BUILDFLAG(IS_MAC)
  command_line->AppendSwitch(os_crypt::switches::kUseMockKeychain);
#endif

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
