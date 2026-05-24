#include "browserd/mcp/tools/evaluate_tools.h"

#include "base/functional/bind.h"
#include "base/json/json_writer.h"
#include "base/strings/utf_string_conversions.h"
#include "browserd/mcp/mcp_server.h"
#include "browserd/mcp/mcp_tool.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/isolated_world_ids.h"

namespace browserd {

namespace {

void HandleEvaluate(content::WebContents* web_contents,
                    base::DictValue args,
                    ToolResultCallback callback) {
  const std::string* expression = args.FindString("expression");
  if (!expression) {
    std::move(callback).Run(
        TextContent("Missing required parameter: expression"), true);
    return;
  }

  auto* rfh = web_contents->GetPrimaryMainFrame();
  if (!rfh) {
    std::move(callback).Run(TextContent("No active frame"), true);
    return;
  }

  rfh->ExecuteJavaScriptForTests(
      base::UTF8ToUTF16(*expression),
      base::BindOnce(
          [](ToolResultCallback cb, base::Value result) {
            if (result.is_none()) {
              std::move(cb).Run(TextContent("undefined"), false);
              return;
            }
            std::string json;
            base::JSONWriter::Write(result, &json);
            std::move(cb).Run(TextContent(json), false);
          },
          std::move(callback)),
      content::ISOLATED_WORLD_ID_GLOBAL);
}

void HandleConsoleMessages(content::WebContents* web_contents,
                           base::DictValue args,
                           ToolResultCallback callback) {
  auto* rfh = web_contents->GetPrimaryMainFrame();
  if (!rfh) {
    std::move(callback).Run(TextContent("No active frame"), true);
    return;
  }

  rfh->ExecuteJavaScriptForTests(
      u"(() => {"
      u"  if (!window.__browserdLogs) return '[]';"
      u"  return JSON.stringify(window.__browserdLogs);"
      u"})()",
      base::BindOnce(
          [](ToolResultCallback cb, base::Value result) {
            if (result.is_string()) {
              std::move(cb).Run(TextContent(result.GetString()), false);
            } else {
              std::move(cb).Run(
                  TextContent("No console messages captured"), false);
            }
          },
          std::move(callback)),
      content::ISOLATED_WORLD_ID_GLOBAL);
}

}  // namespace

void RegisterEvaluateTools(MCPServer& server) {
  server.RegisterTool({
      "browser_evaluate",
      "Execute JavaScript in the browser console",
      SchemaObject(
          base::DictValue().Set("expression", SchemaString("JavaScript expression to evaluate")),
          {"expression"}),
      base::BindRepeating(&HandleEvaluate),
  });

  server.RegisterTool({
      "browser_console_messages",
      "Get console messages from the page",
      SchemaObject(base::DictValue()),
      base::BindRepeating(&HandleConsoleMessages),
  });
}

}  // namespace browserd
