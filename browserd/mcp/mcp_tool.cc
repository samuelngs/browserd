#include "browserd/mcp/mcp_tool.h"

namespace browserd {

MCPToolDef::MCPToolDef(std::string name_in,
                       std::string description_in,
                       base::DictValue input_schema_in,
                       ToolHandler handler_in)
    : name(std::move(name_in)),
      description(std::move(description_in)),
      input_schema(std::move(input_schema_in)),
      handler(std::move(handler_in)) {}

MCPToolDef::~MCPToolDef() = default;
MCPToolDef::MCPToolDef(MCPToolDef&&) = default;
MCPToolDef& MCPToolDef::operator=(MCPToolDef&&) = default;

base::ListValue TextContent(const std::string& text) {
  base::ListValue content;
  content.Append(base::DictValue().Set("type", "text").Set("text", text));
  return content;
}

base::ListValue ImageContent(const std::string& base64_data,
                             const std::string& mime_type) {
  base::ListValue content;
  content.Append(base::DictValue()
                     .Set("type", "image")
                     .Set("data", base64_data)
                     .Set("mimeType", mime_type));
  return content;
}

base::DictValue SchemaObject(
    base::DictValue properties,
    std::initializer_list<std::string> required) {
  base::DictValue schema;
  schema.Set("type", "object");
  schema.Set("properties", std::move(properties));

  if (required.size() > 0) {
    base::ListValue req_list;
    for (const auto& r : required) {
      req_list.Append(r);
    }
    schema.Set("required", std::move(req_list));
  }

  return schema;
}

base::DictValue SchemaString(const std::string& desc) {
  base::DictValue prop;
  prop.Set("type", "string");
  if (!desc.empty()) {
    prop.Set("description", desc);
  }
  return prop;
}

base::DictValue SchemaInt(const std::string& desc) {
  base::DictValue prop;
  prop.Set("type", "integer");
  if (!desc.empty()) {
    prop.Set("description", desc);
  }
  return prop;
}

base::DictValue SchemaBool(const std::string& desc) {
  base::DictValue prop;
  prop.Set("type", "boolean");
  if (!desc.empty()) {
    prop.Set("description", desc);
  }
  return prop;
}

}  // namespace browserd
