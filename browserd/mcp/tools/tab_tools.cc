#include "browserd/mcp/tools/tab_tools.h"

#include "base/functional/bind.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "browserd/mcp/mcp_server.h"
#include "browserd/mcp/mcp_tool.h"
#include "content/public/browser/web_contents.h"
#include "ui/gfx/geometry/size.h"
#include "url/gurl.h"

namespace browserd {

namespace {

void HandleTabs(MCPServer* server,
                content::WebContents* web_contents,
                base::DictValue args,
                ToolResultCallback callback) {
  auto* runtime = server->runtime();
  if (!runtime) {
    std::move(callback).Run(TextContent("No browser runtime"), true);
    return;
  }

  std::string output;
  int index = 0;
  for (const auto& tab : runtime->ListTabs()) {
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

void HandleNewTab(MCPServer* server,
                  content::WebContents* web_contents,
                  base::DictValue args,
                  ToolResultCallback callback) {
  auto* runtime = server->runtime();
  if (!runtime) {
    std::move(callback).Run(TextContent("No browser runtime"), true);
    return;
  }

  const std::string* url = args.FindString("url");
  std::string target_url = url ? *url : "about:blank";
  GURL gurl(target_url);
  if (!gurl.is_valid()) {
    std::move(callback).Run(TextContent("Invalid URL"), true);
    return;
  }

  std::optional<BrowserTabInfo> new_tab = runtime->CreateTab(gurl);
  server->RefreshActiveWebContents();

  if (new_tab.has_value()) {
    std::move(callback).Run(
        TextContent("Created new tab: " + new_tab->target_id), false);
  } else {
    std::move(callback).Run(TextContent("Failed to create tab"), true);
  }
}

void HandleClose(MCPServer* server,
                 content::WebContents* web_contents,
                 base::DictValue args,
                 ToolResultCallback callback) {
  auto* runtime = server->runtime();
  if (!runtime) {
    std::move(callback).Run(TextContent("No browser runtime"), true);
    return;
  }

  const std::string* target_id = args.FindString("targetId");
  std::optional<std::string> target;
  if (target_id) {
    target = *target_id;
  }

  if (!runtime->CloseTab(target)) {
    std::move(callback).Run(
        TextContent(target_id ? "Tab not found: " + *target_id
                              : "No active tab"),
        true);
    return;
  }

  server->RefreshActiveWebContents();
  std::move(callback).Run(
      TextContent(target_id ? "Tab closed" : "Current tab closed"), false);
}

void HandleResize(MCPServer* server,
                  content::WebContents* web_contents,
                  base::DictValue args,
                  ToolResultCallback callback) {
  auto* runtime = server->runtime();
  if (!runtime) {
    std::move(callback).Run(TextContent("No browser runtime"), true);
    return;
  }

  auto width = args.FindInt("width").value_or(1280);
  auto height = args.FindInt("height").value_or(720);

  runtime->ResizeActive(gfx::Size(width, height));

  std::move(callback).Run(
      TextContent("Resized to " + base::NumberToString(width) + "x" +
                  base::NumberToString(height)),
      false);
}

}  // namespace

void RegisterTabTools(MCPServer& server) {
  server.RegisterTool({
      "browser_tab_list",
      "List all open browser tabs",
      SchemaObject(base::DictValue()),
      base::BindRepeating(&HandleTabs, base::Unretained(&server)),
  });

  server.RegisterTool({
      "browser_tab_new",
      "Open a new browser tab",
      SchemaObject(base::DictValue().Set("url", SchemaString("URL to open in the new tab")), {}),
      base::BindRepeating(&HandleNewTab, base::Unretained(&server)),
  });

  server.RegisterTool({
      "browser_close",
      "Close a browser tab",
      SchemaObject(
          base::DictValue().Set("targetId", SchemaString("Target ID of the tab to close (closes current if omitted)")),
          {}),
      base::BindRepeating(&HandleClose, base::Unretained(&server)),
  });

  server.RegisterTool({
      "browser_resize",
      "Resize the browser viewport",
      SchemaObject(base::DictValue().Set("width", SchemaInt("Viewport width in pixels")).Set("height", SchemaInt("Viewport height in pixels")),
                   {}),
      base::BindRepeating(&HandleResize, base::Unretained(&server)),
  });
}

}  // namespace browserd
