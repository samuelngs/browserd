#include "browserd/mcp/tools/navigation_tools.h"

#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "browserd/mcp/mcp_server.h"
#include "browserd/mcp/mcp_tool.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"
#include "net/base/net_errors.h"
#include "ui/base/page_transition_types.h"

namespace browserd {

namespace {

class NavigationWaiter : public content::WebContentsObserver {
 public:
  NavigationWaiter(content::WebContents* wc, ToolResultCallback cb)
      : content::WebContentsObserver(wc), callback_(std::move(cb)) {}

  void DidStopLoading() override {
    if (callback_) {
      std::move(callback_).Run(TextContent("Navigated to page"), false);
    }
    delete this;
  }

  void DidFailLoad(content::RenderFrameHost* rfh,
                   const GURL& validated_url,
                   int error_code) override {
    if (callback_) {
      std::move(callback_).Run(
          TextContent("Navigation failed: " + net::ErrorToString(error_code)),
          true);
    }
    delete this;
  }

 private:
  ToolResultCallback callback_;
};

void HandleNavigate(content::WebContents* web_contents,
                    base::DictValue args,
                    ToolResultCallback callback) {
  const std::string* url = args.FindString("url");
  if (!url) {
    std::move(callback).Run(TextContent("Missing required parameter: url"),
                            true);
    return;
  }

  new NavigationWaiter(web_contents, std::move(callback));

  content::NavigationController::LoadURLParams params((GURL(*url)));
  params.transition_type = ui::PAGE_TRANSITION_TYPED;
  web_contents->GetController().LoadURLWithParams(params);
}

void HandleNavigateBack(content::WebContents* web_contents,
                        base::DictValue args,
                        ToolResultCallback callback) {
  if (web_contents->GetController().CanGoBack()) {
    web_contents->GetController().GoBack();
    std::move(callback).Run(TextContent("Navigated back"), false);
  } else {
    std::move(callback).Run(TextContent("Cannot go back"), true);
  }
}

void HandleNavigateForward(content::WebContents* web_contents,
                           base::DictValue args,
                           ToolResultCallback callback) {
  if (web_contents->GetController().CanGoForward()) {
    web_contents->GetController().GoForward();
    std::move(callback).Run(TextContent("Navigated forward"), false);
  } else {
    std::move(callback).Run(TextContent("Cannot go forward"), true);
  }
}

void HandleReload(content::WebContents* web_contents,
                  base::DictValue args,
                  ToolResultCallback callback) {
  web_contents->GetController().Reload(content::ReloadType::NORMAL, false);
  std::move(callback).Run(TextContent("Page reloaded"), false);
}

}  // namespace

void RegisterNavigationTools(MCPServer& server) {
  server.RegisterTool({
      "browser_navigate",
      "Navigate to a URL",
      SchemaObject(base::DictValue().Set("url", SchemaString("URL to navigate to")), {"url"}),
      base::BindRepeating(&HandleNavigate),
  });

  server.RegisterTool({
      "browser_navigate_back",
      "Go back in browser history",
      SchemaObject(base::DictValue()),
      base::BindRepeating(&HandleNavigateBack),
  });

  server.RegisterTool({
      "browser_navigate_forward",
      "Go forward in browser history",
      SchemaObject(base::DictValue()),
      base::BindRepeating(&HandleNavigateForward),
  });

  server.RegisterTool({
      "browser_reload",
      "Reload the current page",
      SchemaObject(base::DictValue()),
      base::BindRepeating(&HandleReload),
  });
}

}  // namespace browserd
