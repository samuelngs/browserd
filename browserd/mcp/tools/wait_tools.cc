#include "browserd/mcp/tools/wait_tools.h"

#include <optional>
#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "browserd/core/browser_controller.h"
#include "browserd/mcp/mcp_server.h"
#include "browserd/mcp/mcp_tool.h"

namespace browserd {
namespace {

void CompleteWait(std::string wait_desc,
                  ToolResultCallback callback,
                  BrowserStatus status) {
  if (!status.ok()) {
    std::move(callback).Run(TextContent(status.message), true);
    return;
  }
  std::move(callback).Run(TextContent("Wait satisfied for " + wait_desc),
                          false);
}

void HandleWaitFor(BrowserController* controller,
                   base::DictValue args,
                   ToolResultCallback callback) {
  WaitOptions options;
  if (const std::string* selector = args.FindString("selector")) {
    options.selector = *selector;
  }
  if (const std::string* text = args.FindString("text")) {
    options.text = *text;
  }
  options.timeout_ms = args.FindInt("timeout").value_or(30000);

  std::string wait_desc;
  if (options.selector.has_value()) {
    wait_desc = "selector: " + options.selector.value();
  } else if (options.text.has_value()) {
    wait_desc = "text: " + options.text.value();
  } else {
    std::move(callback).Run(
        TextContent("Must provide either selector or text to wait for"), true);
    return;
  }

  controller->WaitFor(
      std::nullopt, std::move(options),
      base::BindOnce(&CompleteWait, std::move(wait_desc),
                     std::move(callback)));
}

}  // namespace

void RegisterWaitTools(MCPServer& server) {
  server.RegisterTool({
      "browser_wait_for",
      "Wait for a selector to appear or text to be present on the page",
      SchemaObject(
          base::DictValue()
              .Set("selector", SchemaString("CSS selector to wait for"))
              .Set("text", SchemaString("Text to wait for on the page"))
              .Set("timeout",
                   SchemaInt("Timeout in milliseconds (default 30000)")),
          {}),
      base::BindRepeating(&HandleWaitFor),
  });
}

}  // namespace browserd
