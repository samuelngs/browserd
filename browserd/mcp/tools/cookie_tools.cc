#include "browserd/mcp/tools/cookie_tools.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "browserd/core/browser_controller.h"
#include "browserd/mcp/mcp_server.h"
#include "browserd/mcp/mcp_tool.h"
#include "url/gurl.h"

namespace browserd {
namespace {

void FormatCookieList(ToolResultCallback callback,
                      BrowserResult<std::vector<CookieInfo>> result) {
  if (!result.ok()) {
    std::move(callback).Run(TextContent(result.status.message), true);
    return;
  }

  const auto& cookies = result.value.value();
  if (cookies.empty()) {
    std::move(callback).Run(TextContent("No cookies found"), false);
    return;
  }

  std::string output;
  for (const auto& cookie : cookies) {
    output += cookie.name + "=" + cookie.value;
    output += " (domain=" + cookie.domain;
    output += ", path=" + cookie.path + ")";
    output += "\n";
  }

  std::move(callback).Run(TextContent(output), false);
}

void FormatCookieGet(std::string cookie_name,
                     ToolResultCallback callback,
                     BrowserResult<CookieInfo> result) {
  if (!result.ok()) {
    bool is_not_found = result.status.code == BrowserStatusCode::kNotFound;
    std::move(callback).Run(TextContent(result.status.message),
                            !is_not_found);
    return;
  }

  const CookieInfo& cookie = result.value.value();
  std::string info = cookie.name + "=" + cookie.value +
                     "\ndomain: " + cookie.domain +
                     "\npath: " + cookie.path +
                     "\nsecure: " + (cookie.secure ? "true" : "false") +
                     "\nhttpOnly: " + (cookie.http_only ? "true" : "false");
  std::move(callback).Run(TextContent(info), false);
}

void CompleteStatus(std::string success_message,
                    ToolResultCallback callback,
                    BrowserStatus status) {
  if (!status.ok()) {
    std::move(callback).Run(TextContent(status.message), true);
    return;
  }
  std::move(callback).Run(TextContent(success_message), false);
}

void CompleteDelete(std::string cookie_name,
                    ToolResultCallback callback,
                    BrowserStatus status) {
  if (!status.ok() && status.code != BrowserStatusCode::kNotFound) {
    std::move(callback).Run(TextContent(status.message), true);
    return;
  }
  std::move(callback).Run(
      TextContent("Cookie '" + cookie_name + "' deleted"), false);
}

void HandleCookieList(BrowserController* controller,
                      base::DictValue args,
                      ToolResultCallback callback) {
  CookieListOptions options;
  if (const std::string* url_str = args.FindString("url")) {
    GURL url(*url_str);
    if (!url.is_valid()) {
      std::move(callback).Run(TextContent("Invalid URL"), true);
      return;
    }
    options.url = *url_str;
  }

  controller->ListCookies(
      std::nullopt, std::move(options),
      base::BindOnce(&FormatCookieList, std::move(callback)));
}

void HandleCookieGet(BrowserController* controller,
                     base::DictValue args,
                     ToolResultCallback callback) {
  const std::string* name = args.FindString("name");
  if (!name) {
    std::move(callback).Run(TextContent("Missing required parameter: name"),
                            true);
    return;
  }

  CookieGetOptions options;
  options.name = *name;
  controller->GetCookie(
      std::nullopt, std::move(options),
      base::BindOnce(&FormatCookieGet, *name, std::move(callback)));
}

void HandleCookieSet(BrowserController* controller,
                     base::DictValue args,
                     ToolResultCallback callback) {
  const std::string* name = args.FindString("name");
  const std::string* value = args.FindString("value");
  if (!name || !value) {
    std::move(callback).Run(
        TextContent("Missing required parameters: name, value"), true);
    return;
  }

  CookieSetOptions options;
  options.name = *name;
  options.value = *value;
  if (const std::string* domain = args.FindString("domain")) {
    options.domain = *domain;
  }
  if (const std::string* path = args.FindString("path")) {
    options.path = *path;
  }
  if (const std::string* url = args.FindString("url")) {
    options.url = *url;
  }
  options.secure = args.FindBool("secure").value_or(false);
  options.http_only = args.FindBool("httpOnly").value_or(false);

  controller->SetCookie(
      std::nullopt, std::move(options),
      base::BindOnce(&CompleteStatus, "Cookie '" + *name + "' set",
                     std::move(callback)));
}

void HandleCookieDelete(BrowserController* controller,
                        base::DictValue args,
                        ToolResultCallback callback) {
  const std::string* name = args.FindString("name");
  if (!name) {
    std::move(callback).Run(TextContent("Missing required parameter: name"),
                            true);
    return;
  }

  CookieDeleteOptions options;
  options.name = *name;
  if (const std::string* url = args.FindString("url")) {
    options.url = *url;
  }
  if (const std::string* domain = args.FindString("domain")) {
    options.domain = *domain;
  }

  controller->DeleteCookie(
      std::nullopt, std::move(options),
      base::BindOnce(&CompleteDelete, *name, std::move(callback)));
}

void HandleCookieClear(BrowserController* controller,
                       base::DictValue args,
                       ToolResultCallback callback) {
  controller->ClearCookies(
      std::nullopt,
      base::BindOnce(&CompleteStatus, "All cookies cleared",
                     std::move(callback)));
}

}  // namespace

void RegisterCookieTools(MCPServer& server) {
  server.RegisterTool({
      "browser_cookie_list",
      "List cookies, optionally filtered by URL",
      SchemaObject(
          base::DictValue().Set("url",
                                SchemaString("URL to filter cookies by")),
          {}),
      base::BindRepeating(&HandleCookieList),
  });

  server.RegisterTool({
      "browser_cookie_get",
      "Get a specific cookie by name",
      SchemaObject(
          base::DictValue().Set("name", SchemaString("Cookie name")),
          {"name"}),
      base::BindRepeating(&HandleCookieGet),
  });

  server.RegisterTool({
      "browser_cookie_set",
      "Set a cookie",
      SchemaObject(base::DictValue()
                       .Set("name", SchemaString("Cookie name"))
                       .Set("value", SchemaString("Cookie value"))
                       .Set("domain", SchemaString("Cookie domain"))
                       .Set("path", SchemaString("Cookie path"))
                       .Set("url",
                            SchemaString("URL to associate cookie with"))
                       .Set("secure", SchemaBool("Secure flag"))
                       .Set("httpOnly", SchemaBool("HttpOnly flag")),
                   {"name", "value"}),
      base::BindRepeating(&HandleCookieSet),
  });

  server.RegisterTool({
      "browser_cookie_delete",
      "Delete a cookie by name",
      SchemaObject(base::DictValue()
                       .Set("name",
                            SchemaString("Cookie name to delete"))
                       .Set("url",
                            SchemaString("URL the cookie belongs to"))
                       .Set("domain", SchemaString("Cookie domain")),
                   {"name"}),
      base::BindRepeating(&HandleCookieDelete),
  });

  server.RegisterTool({
      "browser_cookie_clear",
      "Clear all cookies",
      SchemaObject(base::DictValue()),
      base::BindRepeating(&HandleCookieClear),
  });
}

}  // namespace browserd
