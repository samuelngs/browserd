#include "browserd/startup/browserd_main.h"

#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "base/base_switches.h"
#include "base/check.h"
#include "base/command_line.h"
#include "base/environment.h"
#include "browserd/gui/main_delegate.h"
#include "browserd/switches.h"
#include "build/build_config.h"
#include "content/public/app/content_main.h"
#include "content/public/common/content_switches.h"
#include "headless/lib/browser/headless_browser_impl.h"
#include "headless/lib/headless_content_main_delegate.h"
#include "headless/public/switches.h"
#include "ui/gl/gl_switches.h"

#if BUILDFLAG(IS_MAC)
#include "components/os_crypt/common/os_crypt_switches.h"
#include "sandbox/mac/seatbelt_exec.h"
#endif

namespace browserd::startup {
namespace {

constexpr char kGuiEnvironment[] = "BROWSERD_INTERNAL_GUI";
constexpr char kBrowserdForceGles2Context[] =
    "browserd-force-gles2-context";
constexpr char kBrowserdDisableDawnInfo[] =
    "browserd-disable-dawn-info";

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

#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)
void AppendCommaSeparatedSwitchValue(base::CommandLine* command_line,
                                     const char* switch_name,
                                     const char* value) {
  std::string existing = command_line->GetSwitchValueASCII(switch_name);
  if (existing.find(value) != std::string::npos) {
    return;
  }
  if (!existing.empty()) {
    existing += ",";
  }
  existing += value;
  command_line->RemoveSwitch(switch_name);
  command_line->AppendSwitchASCII(switch_name, existing);
}
#endif

void ConfigureGuiWaylandGpu(base::CommandLine* command_line,
                            bool requested_gui) {
#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)
  if (!requested_gui || command_line->HasSwitch(::switches::kDisableGpu)) {
    return;
  }

  if (!command_line->HasSwitch("ozone-platform")) {
    command_line->AppendSwitchASCII("ozone-platform", "wayland");
  }
  AppendCommaSeparatedSwitchValue(command_line, ::switches::kEnableFeatures,
                                  "UseOzonePlatform");
  AppendCommaSeparatedSwitchValue(command_line, ::switches::kDisableFeatures,
                                  "FallbackToSWIfGLES3NotSupported");
  command_line->AppendSwitch(::switches::kDisableGpuCompositing);
  command_line->AppendSwitch(::switches::kDisableAcceleratedVideoDecode);
  command_line->AppendSwitch(::switches::kDisableGpuMemoryBufferVideoFrames);
  command_line->AppendSwitch(kBrowserdForceGles2Context);
  command_line->AppendSwitch(kBrowserdDisableDawnInfo);
  if (!command_line->HasSwitch(::switches::kUseGL)) {
    command_line->AppendSwitchASCII(::switches::kUseGL,
                                    gl::kGLImplementationEGLName);
  }
  if (!command_line->HasSwitch(::switches::kUseCmdDecoder)) {
    command_line->AppendSwitchASCII(::switches::kUseCmdDecoder,
                                    gl::kCmdDecoderValidatingName);
  }
#else
  (void)command_line;
  (void)requested_gui;
#endif
}

void ConfigureGpu(base::CommandLine* command_line,
                  bool requested_gui,
                  base::Environment* environment) {
  if (command_line->HasSwitch(headless::switches::kEnableGPU) ||
      command_line->HasSwitch(::switches::kDisableGpu)) {
    return;
  }

  if (ShouldAutoEnableGpu(requested_gui, environment)) {
    command_line->AppendSwitch(headless::switches::kEnableGPU);
  } else {
    command_line->AppendSwitch(::switches::kDisableGpu);
  }
}

void MaybeInitializeMacSandbox(int argc, const char** argv) {
#if BUILDFLAG(IS_MAC)
  base::CommandLine* command_line = base::CommandLine::ForCurrentProcess();
  sandbox::SeatbeltExecServer::CreateFromArgumentsResult seatbelt =
      sandbox::SeatbeltExecServer::CreateFromArguments(
          command_line->GetProgram().value().c_str(), argc,
          const_cast<char**>(argv));
  if (seatbelt.sandbox_required) {
    CHECK(seatbelt.server->InitializeSandbox());
  }
#else
  (void)argc;
  (void)argv;
#endif
}

void SetContentMainArgv(content::ContentMainParams* params,
                        int argc,
                        const char** argv,
                        base::CommandLine::StringVector* argv_storage,
                        std::vector<const char*>* argv_ptrs) {
  if (argc > 0 && argv) {
    params->argc = argc;
    params->argv = argv;
    return;
  }

  *argv_storage = base::CommandLine::ForCurrentProcess()->argv();
  argv_ptrs->reserve(argv_storage->size());
  for (const auto& arg : *argv_storage) {
    argv_ptrs->push_back(arg.c_str());
  }
  params->argc = static_cast<int>(argv_ptrs->size());
  params->argv = argv_ptrs->data();
}

}  // namespace

bool EnsureCommandLineInitialized(int argc, const char** argv) {
  if (base::CommandLine::InitializedForCurrentProcess()) {
    return true;
  }
  return base::CommandLine::Init(argc, argv);
}

bool HasHelpSwitch(const base::CommandLine& command_line) {
  return command_line.HasSwitch("help") || command_line.HasSwitch("h");
}

void PrintHelp() {
  std::cout
      << "Usage: browserd [options]\n"
      << "\n"
      << "Options:\n"
      << "  --help, -h                 Show this help and exit.\n"
      << "  --gui                      Run with the native Chromium browser UI.\n"
      << "  --mcp-http-port=<port>     Enable Streamable HTTP MCP on /mcp.\n"
      << "  --mcp-http-host=<host>     HTTP bind host/address. Use 0.0.0.0 or ::\n"
      << "                             to listen on all interfaces.\n"
      << "                             Defaults to 127.0.0.1.\n"
      << "  --mcp-ipc-path=<path>      Enable local IPC MCP. On macOS/Linux this is\n"
      << "                             an absolute Unix socket path. On Windows this\n"
      << "                             is a named pipe name or \\\\.\\pipe\\ name.\n"
      << "  --user-data-dir=<path>     Browser profile directory.\n"
      << "\n"
      << "Environment:\n"
      << "  BROWSERD_MCP_HTTP_TOKEN    Required when --mcp-http-port is used.\n"
      << "\n"
      << "GPU:\n"
      << "  Headless mode enables GPU automatically on desktop sessions. On Linux,\n"
      << "  GPU is disabled unless DISPLAY or WAYLAND_DISPLAY is set.\n";
}

bool IsGuiRequested(const base::CommandLine& command_line,
                    base::Environment* environment,
                    bool propagate_to_child_processes) {
  const bool requested_gui = command_line.HasSwitch(browserd::switches::kGui) ||
                             environment->HasVar(kGuiEnvironment);
  if (propagate_to_child_processes &&
      command_line.HasSwitch(browserd::switches::kGui)) {
    environment->SetVar(kGuiEnvironment, "1");
  }
  return requested_gui;
}

void ApplyDefaultCommandLineSwitches(int argc,
                                     const char** argv,
                                     bool requested_gui,
                                     bool include_headless_defaults,
                                     base::Environment* environment) {
  base::CommandLine* command_line = base::CommandLine::ForCurrentProcess();

  if (argc > 0 && argv) {
    MaybeInitializeMacSandbox(argc, argv);
  }
  ConfigureGuiWaylandGpu(command_line, requested_gui);
  ConfigureGpu(command_line, requested_gui, environment);
  command_line->AppendSwitch(::switches::kEnableUnsafeSwiftShader);
  command_line->AppendSwitch("use-fake-device-for-media-stream");
#if BUILDFLAG(IS_MAC)
  command_line->AppendSwitch(os_crypt::switches::kUseMockKeychain);
#endif

  if (!command_line->HasSwitch(headless::switches::kAcceptLang)) {
    command_line->AppendSwitchASCII(headless::switches::kAcceptLang,
                                    "en-US,en");
  }

  if (include_headless_defaults &&
      !command_line->HasSwitch(headless::switches::kScreenInfo)) {
    command_line->AppendSwitchASCII(
        headless::switches::kScreenInfo,
        "{0,0 3840x2160 devicePixelRatio=2 workAreaTop=50}");
  }
}

int RunGuiContentMain(
    int argc,
    const char** argv,
    browserd::gui::RuntimeOptions options,
    browserd::gui::BrowserMainParts::RuntimeReadyCallback ready_callback) {
  browserd::gui::MainDelegate delegate(options, std::move(ready_callback));
  content::ContentMainParams params(&delegate);
  base::CommandLine::StringVector argv_storage;
  std::vector<const char*> argv_ptrs;
  SetContentMainArgv(&params, argc, argv, &argv_storage, &argv_ptrs);
  return content::ContentMain(std::move(params));
}

int RunHeadlessContentMain(
    int argc,
    const char** argv,
    base::OnceCallback<void(headless::HeadlessBrowser*)> on_start_callback) {
  auto browser = std::make_unique<headless::HeadlessBrowserImpl>(
      std::move(on_start_callback));
  headless::HeadlessContentMainDelegate delegate(std::move(browser));
  content::ContentMainParams params(&delegate);
  base::CommandLine::StringVector argv_storage;
  std::vector<const char*> argv_ptrs;
  SetContentMainArgv(&params, argc, argv, &argv_storage, &argv_ptrs);
  return content::ContentMain(std::move(params));
}

}  // namespace browserd::startup
