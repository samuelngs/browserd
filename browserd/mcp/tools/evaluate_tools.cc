#include "browserd/mcp/tools/evaluate_tools.h"

#include <optional>
#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "browserd/core/browser_controller.h"
#include "browserd/mcp/mcp_server.h"
#include "browserd/mcp/mcp_tool.h"

namespace browserd {
namespace {

void CompleteEvaluate(ToolResultCallback callback,
                      BrowserResult<EvaluateResult> result) {
  if (!result.ok()) {
    std::move(callback).Run(TextContent(result.status.message), true);
    return;
  }

  const EvaluateResult& value = result.value.value();
  if (value.kind == EvaluateResult::Kind::kUndefined) {
    std::move(callback).Run(TextContent("undefined"), false);
    return;
  }
  std::move(callback).Run(TextContent(value.json), false);
}

void CompleteConsoleMessages(ToolResultCallback callback,
                             BrowserResult<std::string> result) {
  if (!result.ok()) {
    std::move(callback).Run(TextContent(result.status.message), true);
    return;
  }
  std::move(callback).Run(TextContent(result.value.value()), false);
}

void HandleEvaluate(BrowserController* controller,
                    base::DictValue args,
                    ToolResultCallback callback) {
  const std::string* expression = args.FindString("expression");
  if (!expression) {
    std::move(callback).Run(
        TextContent("Missing required parameter: expression"), true);
    return;
  }

  controller->Evaluate(std::nullopt, *expression,
                       base::BindOnce(&CompleteEvaluate,
                                      std::move(callback)));
}

void HandleConsoleMessages(BrowserController* controller,
                           base::DictValue args,
                           ToolResultCallback callback) {
  controller->ConsoleMessages(
      std::nullopt,
      base::BindOnce(&CompleteConsoleMessages, std::move(callback)));
}

}  // namespace

void RegisterEvaluateTools(MCPServer& server) {
  server.RegisterTool({
      "browser_evaluate",
      "Execute JavaScript in the browser console",
      SchemaObject(base::DictValue().Set(
                       "expression",
                       SchemaString("JavaScript expression to evaluate")),
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
