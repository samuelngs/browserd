#include "browserd/mcp/tools/interaction_tools.h"

#include <optional>
#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "browserd/core/browser_controller.h"
#include "browserd/mcp/mcp_server.h"
#include "browserd/mcp/mcp_tool.h"

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

void HandleClick(BrowserController* controller,
                 base::DictValue args,
                 ToolResultCallback callback) {
  const std::string* ref = args.FindString("ref");
  if (!ref) {
    std::move(callback).Run(TextContent("Missing required parameter: ref"),
                            true);
    return;
  }

  RefOptions options;
  options.ref = *ref;
  controller->Click(std::nullopt, std::move(options),
                    base::BindOnce(&CompleteStatus, "Clicked element",
                                   std::move(callback)));
}

void HandleHover(BrowserController* controller,
                 base::DictValue args,
                 ToolResultCallback callback) {
  const std::string* ref = args.FindString("ref");
  if (!ref) {
    std::move(callback).Run(TextContent("Missing required parameter: ref"),
                            true);
    return;
  }

  RefOptions options;
  options.ref = *ref;
  controller->Hover(std::nullopt, std::move(options),
                    base::BindOnce(&CompleteStatus, "Hovered over element",
                                   std::move(callback)));
}

void HandleType(BrowserController* controller,
                base::DictValue args,
                ToolResultCallback callback) {
  const std::string* ref = args.FindString("ref");
  const std::string* text = args.FindString("text");
  if (!ref || !text) {
    std::move(callback).Run(
        TextContent("Missing required parameters: ref, text"), true);
    return;
  }

  TypeOptions options;
  options.ref = *ref;
  options.text = *text;
  options.clear = args.FindBool("clear").value_or(false);
  controller->Type(
      std::nullopt, std::move(options),
      base::BindOnce(&CompleteStatus, "Typed text", std::move(callback)));
}

void HandlePressKey(BrowserController* controller,
                    base::DictValue args,
                    ToolResultCallback callback) {
  const std::string* key = args.FindString("key");
  if (!key) {
    std::move(callback).Run(TextContent("Missing required parameter: key"),
                            true);
    return;
  }

  KeyOptions options;
  options.key = *key;
  controller->PressKey(std::nullopt, std::move(options),
                       base::BindOnce(&CompleteStatus, "Pressed key: " + *key,
                                      std::move(callback)));
}

void HandleSelectOption(BrowserController* controller,
                        base::DictValue args,
                        ToolResultCallback callback) {
  const std::string* ref = args.FindString("ref");
  const base::ListValue* values = args.FindList("values");
  if (!ref || !values) {
    std::move(callback).Run(
        TextContent("Missing required parameters: ref, values"), true);
    return;
  }

  SelectOptions options;
  options.ref = *ref;
  for (const auto& value : *values) {
    if (const std::string* string_value = value.GetIfString()) {
      options.values.push_back(*string_value);
    }
  }

  controller->SelectOption(
      std::nullopt, std::move(options),
      base::BindOnce(&CompleteStatus, "Options selected",
                     std::move(callback)));
}

void HandleDragAndDrop(BrowserController* controller,
                       base::DictValue args,
                       ToolResultCallback callback) {
  const std::string* start_ref = args.FindString("startRef");
  const std::string* end_ref = args.FindString("endRef");
  if (!start_ref || !end_ref) {
    std::move(callback).Run(
        TextContent("Missing required parameters: startRef, endRef"), true);
    return;
  }

  DragOptions options;
  options.start_ref = *start_ref;
  options.end_ref = *end_ref;
  controller->Drag(
      std::nullopt, std::move(options),
      base::BindOnce(&CompleteStatus, "Drag and drop completed",
                     std::move(callback)));
}

void HandleScroll(BrowserController* controller,
                  base::DictValue args,
                  ToolResultCallback callback) {
  ScrollOptions options;
  options.delta_x = args.FindInt("deltaX").value_or(0);
  options.delta_y = args.FindInt("deltaY").value_or(0);
  controller->Scroll(
      std::nullopt, options,
      base::BindOnce(&CompleteStatus, "Scrolled page", std::move(callback)));
}

}  // namespace

void RegisterInteractionTools(MCPServer& server) {
  server.RegisterTool({
      "browser_click",
      "Click an element on the page using its accessibility ref",
      SchemaObject(
          base::DictValue().Set(
              "ref", SchemaString("Element ref from accessibility snapshot")),
          {"ref"}),
      base::BindRepeating(&HandleClick),
  });

  server.RegisterTool({
      "browser_hover",
      "Hover over an element on the page",
      SchemaObject(
          base::DictValue().Set(
              "ref", SchemaString("Element ref from accessibility snapshot")),
          {"ref"}),
      base::BindRepeating(&HandleHover),
  });

  server.RegisterTool({
      "browser_type",
      "Type text into a focused element",
      SchemaObject(base::DictValue()
                       .Set("ref",
                            SchemaString(
                                "Element ref from accessibility snapshot"))
                       .Set("text", SchemaString("Text to type"))
                       .Set("clear",
                            SchemaBool(
                                "Clear existing text before typing")),
                   {"ref", "text"}),
      base::BindRepeating(&HandleType),
  });

  server.RegisterTool({
      "browser_press_key",
      "Press a keyboard key or key combination",
      SchemaObject(base::DictValue().Set(
                       "key",
                       SchemaString("Key to press (e.g. Enter, Tab, ArrowDown)")),
                   {"key"}),
      base::BindRepeating(&HandlePressKey),
  });

  server.RegisterTool({
      "browser_select_option",
      "Select option(s) in a select element",
      SchemaObject(
          base::DictValue()
              .Set("ref",
                   SchemaString("Element ref from accessibility snapshot"))
              .Set("values",
                   base::DictValue()
                       .Set("type", "array")
                       .Set("description", "Option values to select")
                       .Set("items",
                            base::DictValue().Set("type", "string"))),
          {"ref", "values"}),
      base::BindRepeating(&HandleSelectOption),
  });

  server.RegisterTool({
      "browser_drag",
      "Drag and drop from one element to another",
      SchemaObject(base::DictValue()
                       .Set("startRef", SchemaString("Source element ref"))
                       .Set("endRef", SchemaString("Target element ref")),
                   {"startRef", "endRef"}),
      base::BindRepeating(&HandleDragAndDrop),
  });

  server.RegisterTool({
      "browser_scroll",
      "Scroll the page",
      SchemaObject(base::DictValue()
                       .Set("deltaX",
                            SchemaInt("Horizontal scroll amount in pixels"))
                       .Set("deltaY",
                            SchemaInt("Vertical scroll amount in pixels")),
                   {}),
      base::BindRepeating(&HandleScroll),
  });
}

}  // namespace browserd
