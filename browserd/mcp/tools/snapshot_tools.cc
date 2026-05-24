#include "browserd/mcp/tools/snapshot_tools.h"

#include "base/base64.h"
#include "base/functional/bind.h"
#include "base/strings/string_number_conversions.h"
#include "browserd/mcp/mcp_server.h"
#include "browserd/mcp/mcp_tool.h"
#include "content/public/browser/render_widget_host.h"
#include "content/public/browser/render_widget_host_view.h"
#include "components/viz/common/frame_sinks/copy_output_result.h"
#include "content/public/browser/web_contents.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/accessibility/ax_enum_util.h"
#include "ui/accessibility/ax_mode.h"
#include "ui/accessibility/ax_node_data.h"
#include "ui/accessibility/ax_tree_update.h"
#include "ui/gfx/codec/png_codec.h"

namespace browserd {

namespace {

void HandleSnapshot(content::WebContents* web_contents,
                    base::DictValue args,
                    ToolResultCallback callback) {
  web_contents->RequestAXTreeSnapshot(
      base::BindOnce(
          [](ToolResultCallback cb, ui::AXTreeUpdate& tree) {
            std::string tree_text;
            for (const auto& node : tree.nodes) {
              const char* role_str = ui::ToString(node.role);
              if (!role_str)
                continue;

              std::string role(role_str);
              if (role == "none" || role == "generic" ||
                  role == "genericContainer" || role == "inlineTextBox")
                continue;

              tree_text += role;

              std::string name;
              if (node.HasStringAttribute(
                      ax::mojom::StringAttribute::kName)) {
                name = node.GetStringAttribute(
                    ax::mojom::StringAttribute::kName);
              }
              if (!name.empty()) {
                tree_text += " \"" + name + "\"";
              }

              tree_text += " [ref=" +
                           base::NumberToString(node.id) + "]";
              tree_text += "\n";
            }

            std::move(cb).Run(TextContent(tree_text), false);
          },
          std::move(callback)),
      ui::AXMode::kWebContents,
      /*max_nodes=*/0,
      /*timeout=*/base::Seconds(5),
      content::WebContents::AXTreeSnapshotPolicy::kAll);
}

void HandleScreenshot(content::WebContents* web_contents,
                      base::DictValue args,
                      ToolResultCallback callback) {
  auto* rwhv = web_contents->GetRenderWidgetHostView();
  if (!rwhv) {
    std::move(callback).Run(TextContent("No render view available"), true);
    return;
  }

  rwhv->CopyFromSurface(
      gfx::Rect(), gfx::Size(), base::Seconds(5),
      base::BindOnce(
          [](ToolResultCallback cb,
             const content::CopyFromSurfaceResult& result) {
            if (!result.has_value()) {
              std::move(cb).Run(TextContent("Screenshot failed"), true);
              return;
            }

            const SkBitmap& bitmap = result.value().bitmap;
            if (bitmap.drawsNothing()) {
              std::move(cb).Run(TextContent("Screenshot is empty"), true);
              return;
            }

            auto png_data =
                gfx::PNGCodec::EncodeBGRASkBitmap(bitmap, false);
            if (!png_data) {
              std::move(cb).Run(TextContent("PNG encoding failed"), true);
              return;
            }

            std::string base64 = base::Base64Encode(*png_data);
            std::move(cb).Run(ImageContent(base64, "image/png"), false);
          },
          std::move(callback)));
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
      SchemaObject(base::DictValue().Set("format", SchemaString("Image format: png or jpeg")),
                   {}),
      base::BindRepeating(&HandleScreenshot),
  });
}

}  // namespace browserd
