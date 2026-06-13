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
#include "browserd/mcp/tools/cookie_tools.h"
#include "browserd/mcp/tools/evaluate_tools.h"
#include "browserd/mcp/tools/interaction_tools.h"
#include "browserd/mcp/tools/navigation_tools.h"
#include "browserd/mcp/tools/snapshot_tools.h"
#include "browserd/mcp/tools/tab_tools.h"
#include "browserd/mcp/tools/wait_tools.h"
#include "headless/lib/browser/headless_browser_impl.h"
#include "headless/lib/browser/headless_web_contents_impl.h"
#include "headless/public/headless_browser_context.h"
#include "headless/public/headless_web_contents.h"
#include "ui/gfx/geometry/rect.h"

namespace browserd {
namespace {

constexpr char kMcpHttpHostSwitch[] = "mcp-http-host";
constexpr char kMcpHttpPortSwitch[] = "mcp-http-port";
constexpr char kMcpHttpTokenEnv[] = "BROWSERD_MCP_HTTP_TOKEN";

struct MCPHttpConfig {
  std::string host;
  uint16_t port = 0;
  std::string token;
};

bool IsAllowedHttpHost(const std::string& host) {
  return host == "127.0.0.1" || host == "::1" ||
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
               << "; only localhost, 127.0.0.1, and ::1 are supported";
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

bool IsInvalidHttpConfig(const MCPHttpConfig& config) {
  return config.host.empty() || config.token.empty();
}

}  // namespace

App::App() = default;
App::~App() = default;

void App::OnBrowserStart(headless::HeadlessBrowser* browser) {
  browser_ = browser;

  headless::HeadlessBrowserContext::Builder context_builder =
      browser_->CreateBrowserContextBuilder();
  context_builder.SetIncognitoMode(false);
  base::FilePath temp_dir;
  if (base::CreateNewTempDirectory(base::FilePath::StringType(), &temp_dir)) {
    context_builder.SetUserDataDir(temp_dir);
  }
  headless::HeadlessBrowserContext* browser_context = context_builder.Build();
  browser_->SetDefaultBrowserContext(browser_context);

  headless::HeadlessWebContents::Builder builder(
      browser_context->CreateWebContentsBuilder());
  headless::HeadlessWebContents* headless_contents =
      builder.SetInitialURL(GURL("about:blank"))
          .SetWindowBounds(gfx::Rect(72, 50, 1920, 1030))
          .Build();

  if (!headless_contents) {
    LOG(ERROR) << "Failed to create initial web contents.";
    browser_->Shutdown();
    return;
  }

  content::WebContents* web_contents =
      headless::HeadlessWebContentsImpl::From(headless_contents)
          ->web_contents();

  RegisterTools();

  mcp_server_.InjectScriptOnNewDocument(
      "window.__browserdLogs = [];"
      "(function() {"
      "  var orig = {"
      "    log: console.log,"
      "    warn: console.warn,"
      "    error: console.error,"
      "    info: console.info,"
      "    debug: console.debug"
      "  };"
      "  function capture(level, args) {"
      "    try {"
      "      window.__browserdLogs.push({"
      "        level: level,"
      "        text: Array.prototype.map.call(args, function(a) {"
      "          try { return typeof a === 'object' ? JSON.stringify(a) : String(a); }"
      "          catch(e) { return String(a); }"
      "        }).join(' '),"
      "        timestamp: Date.now()"
      "      });"
      "      if (window.__browserdLogs.length > 1000)"
      "        window.__browserdLogs.shift();"
      "    } catch(e) {}"
      "  }"
      "  console.log = function() { capture('log', arguments); orig.log.apply(console, arguments); };"
      "  console.warn = function() { capture('warn', arguments); orig.warn.apply(console, arguments); };"
      "  console.error = function() { capture('error', arguments); orig.error.apply(console, arguments); };"
      "  console.info = function() { capture('info', arguments); orig.info.apply(console, arguments); };"
      "  console.debug = function() { capture('debug', arguments); orig.debug.apply(console, arguments); };"
      "})();");

  mcp_server_.SetBrowser(browser, browser_context);
  mcp_server_.SetWebContents(web_contents);
  std::optional<MCPHttpConfig> http_config = GetMCPHttpConfig();
  if (http_config.has_value() && IsInvalidHttpConfig(http_config.value())) {
    base::Process::TerminateCurrentProcessImmediately(1);
  }

  mcp_server_.Start(browser_->BrowserMainThread(),
                    !http_config.has_value());
  if (http_config.has_value() &&
      !mcp_server_.StartHttpTransport(http_config->host, http_config->port,
                                      http_config->token)) {
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
