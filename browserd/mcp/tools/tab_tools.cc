#include "browserd/mcp/tools/tab_tools.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/strings/string_number_conversions.h"
#include "browserd/core/browser_controller.h"
#include "browserd/mcp/mcp_server.h"
#include "browserd/mcp/mcp_tool.h"
#include "ui/gfx/geometry/size.h"
#include "url/gurl.h"

namespace browserd {
namespace {

void CompleteTabs(ToolResultCallback callback,
                  BrowserResult<std::vector<BrowserTabInfo>> result) {
  if (!result.ok()) {
    std::move(callback).Run(TextContent(result.status.message), true);
    return;
  }

  std::string output;
  int index = 0;
  for (const auto& tab : result.value.value()) {
    output += "[" + base::NumberToString(index) + "] ";
    output += tab.title;
    output += " (" + tab.url + ")";
    output += " [id=" + tab.target_id + "]";
    output += "\n";
    index++;
  }

  std::move(callback).Run(
      TextContent(output.empty() ? "No tabs open" : output), false);
}

void CompleteCreateTab(ToolResultCallback callback,
                       BrowserResult<BrowserTabInfo> result) {
  if (!result.ok()) {
    std::move(callback).Run(TextContent(result.status.message), true);
    return;
  }
  std::move(callback).Run(
      TextContent("Created new tab: " + result.value->target_id), false);
}

void CompleteStatus(std::string success_message,
                    ToolResultCallback callback,
                    BrowserStatus status) {
  if (!status.ok()) {
    std::move(callback).Run(TextContent(status.message), true);
    return;
  }
  std::move(callback).Run(TextContent(success_message), false);
}

void HandleTabs(BrowserController* controller,
                base::DictValue args,
                ToolResultCallback callback) {
  controller->ListTabs(base::BindOnce(&CompleteTabs, std::move(callback)));
}

void HandleNewTab(BrowserController* controller,
                  base::DictValue args,
                  ToolResultCallback callback) {
  const std::string* url = args.FindString("url");
  std::string target_url = url ? *url : "about:blank";
  controller->CreateTab(GURL(target_url),
                        base::BindOnce(&CompleteCreateTab,
                                       std::move(callback)));
}

void HandleClose(BrowserController* controller,
                 base::DictValue args,
                 ToolResultCallback callback) {
  const std::string* target_id = args.FindString("targetId");
  std::optional<std::string> target;
  if (target_id) {
    target = *target_id;
  }

  controller->CloseTab(
      target,
      base::BindOnce(&CompleteStatus,
                     target_id ? "Tab closed" : "Current tab closed",
                     std::move(callback)));
}

void HandleResize(BrowserController* controller,
                  base::DictValue args,
                  ToolResultCallback callback) {
  auto width = args.FindInt("width").value_or(1280);
  auto height = args.FindInt("height").value_or(720);

  controller->ResizeActive(
      gfx::Size(width, height),
      base::BindOnce(&CompleteStatus,
                     "Resized to " + base::NumberToString(width) + "x" +
                         base::NumberToString(height),
                     std::move(callback)));
}

}  // namespace

void RegisterTabTools(MCPServer& server) {
  server.RegisterTool({
      "browser_tab_list",
      "List all open browser tabs",
      SchemaObject(base::DictValue()),
      base::BindRepeating(&HandleTabs),
  });

  server.RegisterTool({
      "browser_tab_new",
      "Open a new browser tab",
      SchemaObject(
          base::DictValue().Set(
              "url", SchemaString("URL to open in the new tab")),
          {}),
      base::BindRepeating(&HandleNewTab),
  });

  server.RegisterTool({
      "browser_close",
      "Close a browser tab",
      SchemaObject(base::DictValue().Set(
                       "targetId",
                       SchemaString(
                           "Target ID of the tab to close (closes current if omitted)")),
                   {}),
      base::BindRepeating(&HandleClose),
  });

  server.RegisterTool({
      "browser_resize",
      "Resize the browser viewport",
      SchemaObject(base::DictValue()
                       .Set("width",
                            SchemaInt("Viewport width in pixels"))
                       .Set("height",
                            SchemaInt("Viewport height in pixels")),
                   {}),
      base::BindRepeating(&HandleResize),
  });
}

}  // namespace browserd
