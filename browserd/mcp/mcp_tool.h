#ifndef BROWSERD_MCP_MCP_TOOL_H_
#define BROWSERD_MCP_MCP_TOOL_H_

#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/values.h"

namespace browserd {

class BrowserController;

using ToolResultCallback =
    base::OnceCallback<void(base::ListValue content, bool is_error)>;

using ToolHandler =
    base::RepeatingCallback<void(BrowserController* controller,
                                 base::DictValue arguments,
                                 ToolResultCallback callback)>;

struct MCPToolDef {
  MCPToolDef(std::string name,
             std::string description,
             base::DictValue input_schema,
             ToolHandler handler);
  ~MCPToolDef();
  MCPToolDef(MCPToolDef&&);
  MCPToolDef& operator=(MCPToolDef&&);

  std::string name;
  std::string description;
  base::DictValue input_schema;
  ToolHandler handler;
};

base::ListValue TextContent(const std::string& text);
base::ListValue ImageContent(const std::string& base64_data,
                             const std::string& mime_type);

base::DictValue SchemaObject(
    base::DictValue properties,
    std::initializer_list<std::string> required = {});
base::DictValue SchemaString(const std::string& desc = "");
base::DictValue SchemaInt(const std::string& desc = "");
base::DictValue SchemaBool(const std::string& desc = "");

}  // namespace browserd

#endif  // BROWSERD_MCP_MCP_TOOL_H_
