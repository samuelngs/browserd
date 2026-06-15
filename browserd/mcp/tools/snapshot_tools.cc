#include "browserd/mcp/tools/snapshot_tools.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/base64.h"
#include "base/functional/bind.h"
#include "browserd/core/browser_controller.h"
#include "browserd/mcp/mcp_server.h"
#include "browserd/mcp/mcp_tool.h"

namespace browserd {
namespace {

void CompleteSnapshot(ToolResultCallback callback,
                      BrowserResult<std::string> result) {
  if (!result.ok()) {
    std::move(callback).Run(TextContent(result.status.message), true);
    return;
  }
  std::move(callback).Run(TextContent(result.value.value()), false);
}

void CompleteScreenshot(ToolResultCallback callback,
                        BrowserResult<std::vector<uint8_t>> result) {
  if (!result.ok()) {
    std::move(callback).Run(TextContent(result.status.message), true);
    return;
  }
  std::string base64 = base::Base64Encode(result.value.value());
  std::move(callback).Run(ImageContent(base64, "image/png"), false);
}

void HandleSnapshot(BrowserController* controller,
                    base::DictValue args,
                    ToolResultCallback callback) {
  controller->Snapshot(std::nullopt,
                       base::BindOnce(&CompleteSnapshot,
                                      std::move(callback)));
}

void HandleScreenshot(BrowserController* controller,
                      base::DictValue args,
                      ToolResultCallback callback) {
  controller->Screenshot(
      std::nullopt,
      base::BindOnce(&CompleteScreenshot, std::move(callback)));
}

}  // namespace

void RegisterSnapshotTools(MCPServer& server) {
  server.RegisterTool({
      "browser_snapshot",
      "Capture an accessibility snapshot of the current page",
      SchemaObject(base::DictValue()),
      base::BindRepeating(&HandleSnapshot),
  });

  server.RegisterTool({
      "browser_take_screenshot",
      "Take a screenshot of the current page",
      SchemaObject(base::DictValue()),
      base::BindRepeating(&HandleScreenshot),
  });
}

}  // namespace browserd
