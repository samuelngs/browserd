#include "browserd/mcp/tools/navigation_tools.h"

#include <optional>
#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "browserd/core/browser_controller.h"
#include "browserd/mcp/mcp_server.h"
#include "browserd/mcp/mcp_tool.h"
#include "url/gurl.h"

namespace browserd {
namespace {

void CompleteStatus(std::string success_message,
                    ToolResultCallback callback,
                    BrowserStatus status) {
  if (!status.ok()) {
    std::move(callback).Run(TextContent(status.message), true);
    return;
  }
  std::move(callback).Run(TextContent(success_message), false);
}

void HandleNavigate(BrowserController* controller,
                    base::DictValue args,
                    ToolResultCallback callback) {
  const std::string* url = args.FindString("url");
  if (!url) {
    std::move(callback).Run(TextContent("Missing required parameter: url"),
                            true);
    return;
  }

  controller->Navigate(
      std::nullopt, GURL(*url),
      base::BindOnce(&CompleteStatus, "Navigated to page",
                     std::move(callback)));
}

void HandleNavigateBack(BrowserController* controller,
                        base::DictValue args,
                        ToolResultCallback callback) {
  controller->NavigateBack(
      std::nullopt,
      base::BindOnce(&CompleteStatus, "Navigated back", std::move(callback)));
}

void HandleNavigateForward(BrowserController* controller,
                           base::DictValue args,
                           ToolResultCallback callback) {
  controller->NavigateForward(
      std::nullopt,
      base::BindOnce(&CompleteStatus, "Navigated forward",
                     std::move(callback)));
}

void HandleReload(BrowserController* controller,
                  base::DictValue args,
                  ToolResultCallback callback) {
  controller->Reload(
      std::nullopt,
      base::BindOnce(&CompleteStatus, "Page reloaded", std::move(callback)));
}

}  // namespace

void RegisterNavigationTools(MCPServer& server) {
  server.RegisterTool({
      "browser_navigate",
      "Navigate to a URL",
      SchemaObject(
          base::DictValue().Set("url", SchemaString("URL to navigate to")),
          {"url"}),
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
