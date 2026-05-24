#include "browserd/mcp/tools/wait_tools.h"

#include "base/functional/bind.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "browserd/mcp/mcp_server.h"
#include "browserd/mcp/mcp_tool.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/isolated_world_ids.h"

namespace browserd {

namespace {

void HandleWaitFor(content::WebContents* web_contents,
                   base::DictValue args,
                   ToolResultCallback callback) {
  const std::string* selector = args.FindString("selector");
  const std::string* text = args.FindString("text");
  auto timeout = args.FindInt("timeout").value_or(30000);

  std::string expression;
  std::string wait_desc;

  if (selector) {
    wait_desc = "selector: " + *selector;
    expression =
        "new Promise((resolve, reject) => {"
        "  const timeout = " +
        base::NumberToString(timeout) +
        ";"
        "  const start = Date.now();"
        "  const check = () => {"
        "    const el = document.querySelector('" +
        *selector +
        "');"
        "    if (el) { resolve('Element found'); return; }"
        "    if (Date.now() - start > timeout) {"
        "      reject(new Error('Timeout waiting for element'));"
        "      return;"
        "    }"
        "    setTimeout(check, 100);"
        "  };"
        "  check();"
        "})";
  } else if (text) {
    wait_desc = "text: " + *text;
    expression =
        "new Promise((resolve, reject) => {"
        "  const timeout = " +
        base::NumberToString(timeout) +
        ";"
        "  const start = Date.now();"
        "  const check = () => {"
        "    if (document.body && document.body.innerText.includes('" +
        *text +
        "')) {"
        "      resolve('Text found'); return;"
        "    }"
        "    if (Date.now() - start > timeout) {"
        "      reject(new Error('Timeout waiting for text'));"
        "      return;"
        "    }"
        "    setTimeout(check, 100);"
        "  };"
        "  check();"
        "})";
  } else {
    std::move(callback).Run(
        TextContent("Must provide either selector or text to wait for"), true);
    return;
  }

  auto* rfh = web_contents->GetPrimaryMainFrame();
  if (!rfh) {
    std::move(callback).Run(TextContent("No active frame"), true);
    return;
  }

  rfh->ExecuteJavaScriptForTests(
      base::UTF8ToUTF16(expression),
      base::BindOnce(
          [](std::string desc, ToolResultCallback cb, base::Value result) {
            if (result.is_string() && result.GetString() == "Element found") {
              std::move(cb).Run(
                  TextContent("Wait satisfied for " + desc), false);
            } else if (result.is_string() &&
                       result.GetString() == "Text found") {
              std::move(cb).Run(
                  TextContent("Wait satisfied for " + desc), false);
            } else {
              std::move(cb).Run(TextContent("Wait timeout"), true);
            }
          },
          std::move(wait_desc), std::move(callback)),
      content::ISOLATED_WORLD_ID_GLOBAL);
}

}  // namespace

void RegisterWaitTools(MCPServer& server) {
  server.RegisterTool({
      "browser_wait_for",
      "Wait for a selector to appear or text to be present on the page",
      SchemaObject(
          base::DictValue().Set("selector", SchemaString("CSS selector to wait for")).Set("text", SchemaString("Text to wait for on the page")).Set("timeout", SchemaInt("Timeout in milliseconds (default 30000)")),
          {}),
      base::BindRepeating(&HandleWaitFor),
  });
}

}  // namespace browserd
