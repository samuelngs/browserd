#include "browserd/app.h"

#include <cstdint>
#include <optional>
#include <string>

#include "base/command_line.h"
#include "base/environment.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/process/process.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "browserd/browser_runtime.h"
#include "browserd/core/browser_controller.h"
#include "browserd/headless_runtime.h"
#include "browserd/mcp/tools/cookie_tools.h"
#include "browserd/mcp/tools/evaluate_tools.h"
#include "browserd/mcp/tools/interaction_tools.h"
#include "browserd/mcp/tools/navigation_tools.h"
#include "browserd/mcp/tools/snapshot_tools.h"
#include "browserd/mcp/tools/tab_tools.h"
#include "browserd/mcp/tools/wait_tools.h"
#include "headless/public/headless_browser.h"

namespace browserd {
namespace {

constexpr char kMcpHttpHostSwitch[] = "mcp-http-host";
constexpr char kMcpHttpPortSwitch[] = "mcp-http-port";
constexpr char kMcpIpcPathSwitch[] = "mcp-ipc-path";
constexpr char kMcpHttpTokenEnv[] = "BROWSERD_MCP_HTTP_TOKEN";

struct MCPHttpConfig {
  std::string host;
  uint16_t port = 0;
  std::string token;
};

struct MCPIPCConfig {
  std::string path;
};

bool IsAllowedHttpHost(const std::string& host) {
  return host == "127.0.0.1" || host == "0.0.0.0" || host == "::1" ||
         host == "::" ||
         base::EqualsCaseInsensitiveASCII(host, "localhost");
}

std::optional<MCPHttpConfig> GetMCPHttpConfig() {
  base::CommandLine* command_line = base::CommandLine::ForCurrentProcess();
  if (!command_line->HasSwitch(kMcpHttpPortSwitch)) {
    return std::nullopt;
  }

  std::string host = command_line->GetSwitchValueASCII(kMcpHttpHostSwitch);
  if (host.empty()) {
    host = "127.0.0.1";
  }

  if (!IsAllowedHttpHost(host)) {
    LOG(ERROR) << "Invalid --" << kMcpHttpHostSwitch
               << "; expected localhost, 127.0.0.1, 0.0.0.0, ::1, or ::";
    return MCPHttpConfig();
  }

  unsigned port_value = 0;
  if (!base::StringToUint(
          command_line->GetSwitchValueASCII(kMcpHttpPortSwitch),
          &port_value) ||
      port_value > 65535) {
    LOG(ERROR) << "Invalid --" << kMcpHttpPortSwitch << " value";
    return MCPHttpConfig();
  }

  auto environment = base::Environment::Create();
  std::optional<std::string> token = environment->GetVar(kMcpHttpTokenEnv);
  if (!token.has_value() || token->empty()) {
    LOG(ERROR) << kMcpHttpTokenEnv
               << " must be set when --" << kMcpHttpPortSwitch
               << " is used";
    return MCPHttpConfig();
  }

  MCPHttpConfig config;
  config.host = host;
  config.port = static_cast<uint16_t>(port_value);
  config.token = std::move(token.value());
  return config;
}

std::optional<MCPIPCConfig> GetMCPIPCConfig() {
  base::CommandLine* command_line = base::CommandLine::ForCurrentProcess();
  if (!command_line->HasSwitch(kMcpIpcPathSwitch)) {
    return std::nullopt;
  }

  std::string path = command_line->GetSwitchValueASCII(kMcpIpcPathSwitch);
  if (path.empty()) {
    LOG(ERROR) << "Invalid --" << kMcpIpcPathSwitch << " value";
    return MCPIPCConfig();
  }

  MCPIPCConfig config;
  config.path = std::move(path);
  return config;
}

bool IsInvalidHttpConfig(const MCPHttpConfig& config) {
  return config.host.empty() || config.token.empty();
}

bool IsInvalidIPCConfig(const MCPIPCConfig& config) {
  return config.path.empty();
}

}  // namespace

App::App() = default;
App::~App() = default;

void App::OnBrowserStart(headless::HeadlessBrowser* browser) {
  auto runtime = std::make_unique<HeadlessRuntime>(browser);
  if (!runtime->Initialize()) {
    LOG(ERROR) << "Failed to create initial web contents.";
    browser->Shutdown();
    return;
  }

  Start(std::move(runtime), browser->BrowserMainThread());
}

void App::Start(std::unique_ptr<BrowserRuntime> runtime,
                scoped_refptr<base::SequencedTaskRunner> task_runner) {
  controller_ =
      std::make_unique<BrowserController>(std::move(runtime), task_runner);

  RegisterTools();

  mcp_server_.SetController(controller_.get());
  std::optional<MCPHttpConfig> http_config = GetMCPHttpConfig();
  if (http_config.has_value() && IsInvalidHttpConfig(http_config.value())) {
    base::Process::TerminateCurrentProcessImmediately(1);
  }
  std::optional<MCPIPCConfig> ipc_config = GetMCPIPCConfig();
  if (ipc_config.has_value() && IsInvalidIPCConfig(ipc_config.value())) {
    base::Process::TerminateCurrentProcessImmediately(1);
  }

  const bool has_non_stdio_transport =
      http_config.has_value() || ipc_config.has_value();
  mcp_server_.Start(controller_->task_runner(), !has_non_stdio_transport);
  if (http_config.has_value() &&
      !mcp_server_.StartHttpTransport(http_config->host, http_config->port,
                                      http_config->token)) {
    base::Process::TerminateCurrentProcessImmediately(1);
  }
  if (ipc_config.has_value() &&
      !mcp_server_.StartIPCTransport(ipc_config->path)) {
    base::Process::TerminateCurrentProcessImmediately(1);
  }
}

void App::RegisterTools() {
  RegisterNavigationTools(mcp_server_);
  RegisterSnapshotTools(mcp_server_);
  RegisterInteractionTools(mcp_server_);
  RegisterEvaluateTools(mcp_server_);
  RegisterWaitTools(mcp_server_);
  RegisterTabTools(mcp_server_);
  RegisterCookieTools(mcp_server_);
}

}  // namespace browserd
