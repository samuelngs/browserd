#include "browserd/app.h"

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
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
  mcp_server_.Start(browser_->BrowserMainThread());
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
