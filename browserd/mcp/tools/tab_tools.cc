#include "browserd/mcp/tools/tab_tools.h"

#include "base/functional/bind.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "browserd/mcp/mcp_server.h"
#include "browserd/mcp/mcp_tool.h"
#include "content/public/browser/web_contents.h"
#include "headless/lib/browser/headless_browser_context_impl.h"
#include "headless/lib/browser/headless_web_contents_impl.h"
#include "headless/public/headless_browser.h"
#include "headless/public/headless_browser_context.h"
#include "headless/public/headless_web_contents.h"

namespace browserd {

namespace {

std::string MakeTargetId(headless::HeadlessWebContents* hwc) {
  return base::StringPrintf("%p", static_cast<void*>(hwc));
}

headless::HeadlessBrowserContextImpl* GetBrowserContext(
    content::WebContents* web_contents) {
  auto* hwc_impl =
      headless::HeadlessWebContentsImpl::From(web_contents);
  return hwc_impl ? hwc_impl->browser_context() : nullptr;
}

headless::HeadlessWebContents* FindByTargetId(
    headless::HeadlessBrowserContextImpl* ctx,
    const std::string& target_id) {
  for (auto* hwc : ctx->GetAllWebContents()) {
    if (MakeTargetId(hwc) == target_id) {
      return hwc;
    }
  }
  return nullptr;
}

void HandleTabs(content::WebContents* web_contents,
                base::DictValue args,
                ToolResultCallback callback) {
  auto* ctx = GetBrowserContext(web_contents);
  if (!ctx) {
    std::move(callback).Run(TextContent("No browser context"), true);
    return;
  }

  std::string output;
  int index = 0;
  for (auto* hwc : ctx->GetAllWebContents()) {
    auto* impl = headless::HeadlessWebContentsImpl::From(hwc);
    if (!impl)
      continue;

    auto* wc = impl->web_contents();
    if (!wc)
      continue;

    output += "[" + base::NumberToString(index) + "] ";
    output += base::UTF16ToUTF8(wc->GetTitle());
    output += " (" + wc->GetLastCommittedURL().spec() + ")";
    output += " [id=" + MakeTargetId(hwc) + "]";
    output += "\n";
    index++;
  }

  std::move(callback).Run(
      TextContent(output.empty() ? "No tabs open" : output), false);
}

void HandleNewTab(content::WebContents* web_contents,
                  base::DictValue args,
                  ToolResultCallback callback) {
  auto* ctx = GetBrowserContext(web_contents);
  if (!ctx) {
    std::move(callback).Run(TextContent("No browser context"), true);
    return;
  }

  const std::string* url = args.FindString("url");
  std::string target_url = url ? *url : "about:blank";

  headless::HeadlessWebContents::Builder builder(
      ctx->CreateWebContentsBuilder());
  auto* new_contents = builder.SetInitialURL(GURL(target_url)).Build();

  if (new_contents) {
    std::move(callback).Run(
        TextContent("Created new tab: " + MakeTargetId(new_contents)), false);
  } else {
    std::move(callback).Run(TextContent("Failed to create tab"), true);
  }
}

void HandleClose(content::WebContents* web_contents,
                 base::DictValue args,
                 ToolResultCallback callback) {
  const std::string* target_id = args.FindString("targetId");

  if (target_id) {
    auto* ctx = GetBrowserContext(web_contents);
    if (!ctx) {
      std::move(callback).Run(TextContent("No browser context"), true);
      return;
    }

    auto* hwc = FindByTargetId(ctx, *target_id);
    if (hwc) {
      hwc->Close();
      std::move(callback).Run(TextContent("Tab closed"), false);
    } else {
      std::move(callback).Run(TextContent("Tab not found: " + *target_id),
                              true);
    }
  } else {
    web_contents->ClosePage();
    std::move(callback).Run(TextContent("Current tab closed"), false);
  }
}

void HandleResize(content::WebContents* web_contents,
                  base::DictValue args,
                  ToolResultCallback callback) {
  auto width = args.FindInt("width").value_or(1280);
  auto height = args.FindInt("height").value_or(720);

  web_contents->Resize(gfx::Rect(width, height));

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
      base::BindRepeating(&HandleTabs),
  });

  server.RegisterTool({
      "browser_tab_new",
      "Open a new browser tab",
      SchemaObject(base::DictValue().Set("url", SchemaString("URL to open in the new tab")), {}),
      base::BindRepeating(&HandleNewTab),
  });

  server.RegisterTool({
      "browser_close",
      "Close a browser tab",
      SchemaObject(
          base::DictValue().Set("targetId", SchemaString("Target ID of the tab to close (closes current if omitted)")),
          {}),
      base::BindRepeating(&HandleClose),
  });

  server.RegisterTool({
      "browser_resize",
      "Resize the browser viewport",
      SchemaObject(base::DictValue().Set("width", SchemaInt("Viewport width in pixels")).Set("height", SchemaInt("Viewport height in pixels")),
                   {}),
      base::BindRepeating(&HandleResize),
  });
}

}  // namespace browserd
