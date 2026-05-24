#include "browserd/mcp/tools/cookie_tools.h"

#include "base/functional/bind.h"
#include "base/strings/string_number_conversions.h"
#include "base/time/time.h"
#include "browserd/mcp/mcp_server.h"
#include "browserd/mcp/mcp_tool.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/web_contents.h"
#include "net/cookies/canonical_cookie.h"
#include "net/cookies/cookie_options.h"
#include "services/network/public/mojom/cookie_manager.mojom.h"

namespace browserd {

namespace {

network::mojom::CookieManager* GetCookieManager(
    content::WebContents* web_contents) {
  return web_contents->GetBrowserContext()
      ->GetDefaultStoragePartition()
      ->GetCookieManagerForBrowserProcess();
}

void HandleCookieList(content::WebContents* web_contents,
                      base::DictValue args,
                      ToolResultCallback callback) {
  auto* cm = GetCookieManager(web_contents);
  if (!cm) {
    std::move(callback).Run(TextContent("No cookie manager"), true);
    return;
  }

  cm->GetAllCookies(base::BindOnce(
      [](ToolResultCallback cb,
         const std::vector<net::CanonicalCookie>& cookies) {
        if (cookies.empty()) {
          std::move(cb).Run(TextContent("No cookies found"), false);
          return;
        }

        std::string output;
        for (const auto& cookie : cookies) {
          output += cookie.Name() + "=" + cookie.Value();
          output += " (domain=" + cookie.Domain();
          output += ", path=" + cookie.Path() + ")";
          output += "\n";
        }

        std::move(cb).Run(TextContent(output), false);
      },
      std::move(callback)));
}

void HandleCookieGet(content::WebContents* web_contents,
                     base::DictValue args,
                     ToolResultCallback callback) {
  const std::string* name = args.FindString("name");
  if (!name) {
    std::move(callback).Run(TextContent("Missing required parameter: name"),
                            true);
    return;
  }

  auto* cm = GetCookieManager(web_contents);
  if (!cm) {
    std::move(callback).Run(TextContent("No cookie manager"), true);
    return;
  }

  std::string name_copy = *name;

  cm->GetAllCookies(base::BindOnce(
      [](std::string cookie_name, ToolResultCallback cb,
         const std::vector<net::CanonicalCookie>& cookies) {
        for (const auto& cookie : cookies) {
          if (cookie.Name() == cookie_name) {
            std::string info =
                cookie.Name() + "=" + cookie.Value() +
                "\ndomain: " + cookie.Domain() +
                "\npath: " + cookie.Path() +
                "\nsecure: " + (cookie.SecureAttribute() ? "true" : "false") +
                "\nhttpOnly: " + (cookie.IsHttpOnly() ? "true" : "false");
            std::move(cb).Run(TextContent(info), false);
            return;
          }
        }
        std::move(cb).Run(
            TextContent("Cookie '" + cookie_name + "' not found"), false);
      },
      std::move(name_copy), std::move(callback)));
}

void HandleCookieSet(content::WebContents* web_contents,
                     base::DictValue args,
                     ToolResultCallback callback) {
  const std::string* name = args.FindString("name");
  const std::string* value = args.FindString("value");
  if (!name || !value) {
    std::move(callback).Run(
        TextContent("Missing required parameters: name, value"), true);
    return;
  }

  auto* cm = GetCookieManager(web_contents);
  if (!cm) {
    std::move(callback).Run(TextContent("No cookie manager"), true);
    return;
  }

  const std::string* url_str = args.FindString("url");
  GURL url = url_str ? GURL(*url_str)
                     : web_contents->GetLastCommittedURL();

  const std::string* domain = args.FindString("domain");
  const std::string* path = args.FindString("path");
  bool secure = args.FindBool("secure").value_or(false);
  bool http_only = args.FindBool("httpOnly").value_or(false);

  auto cookie = net::CanonicalCookie::CreateSanitizedCookie(
      url, *name, *value,
      domain ? *domain : "",
      path ? *path : "/",
      base::Time::Now(),
      base::Time::Now() + base::Days(365),
      base::Time::Now(),
      secure, http_only,
      net::CookieSameSite::LAX_MODE,
      net::COOKIE_PRIORITY_DEFAULT,
      std::nullopt, nullptr);

  if (!cookie) {
    std::move(callback).Run(TextContent("Failed to create cookie"), true);
    return;
  }

  std::string name_copy = *name;

  cm->SetCanonicalCookie(
      *cookie, url,
      net::CookieOptions::MakeAllInclusive(),
      base::BindOnce(
          [](std::string cookie_name, ToolResultCallback cb,
             net::CookieAccessResult result) {
            if (result.status.IsInclude()) {
              std::move(cb).Run(
                  TextContent("Cookie '" + cookie_name + "' set"), false);
            } else {
              std::move(cb).Run(
                  TextContent("Failed to set cookie '" + cookie_name + "'"),
                  true);
            }
          },
          std::move(name_copy), std::move(callback)));
}

void HandleCookieDelete(content::WebContents* web_contents,
                        base::DictValue args,
                        ToolResultCallback callback) {
  const std::string* name = args.FindString("name");
  if (!name) {
    std::move(callback).Run(TextContent("Missing required parameter: name"),
                            true);
    return;
  }

  auto* cm = GetCookieManager(web_contents);
  if (!cm) {
    std::move(callback).Run(TextContent("No cookie manager"), true);
    return;
  }

  auto filter = network::mojom::CookieDeletionFilter::New();
  filter->cookie_name = *name;

  const std::string* domain = args.FindString("domain");
  if (domain) {
    filter->host_name = *domain;
  }

  std::string name_copy = *name;

  cm->DeleteCookies(
      std::move(filter),
      base::BindOnce(
          [](std::string cookie_name, ToolResultCallback cb,
             uint32_t num_deleted) {
            std::move(cb).Run(
                TextContent("Cookie '" + cookie_name + "' deleted (" +
                            base::NumberToString(num_deleted) + ")"),
                false);
          },
          std::move(name_copy), std::move(callback)));
}

void HandleCookieClear(content::WebContents* web_contents,
                       base::DictValue args,
                       ToolResultCallback callback) {
  auto* cm = GetCookieManager(web_contents);
  if (!cm) {
    std::move(callback).Run(TextContent("No cookie manager"), true);
    return;
  }

  cm->DeleteCookies(
      network::mojom::CookieDeletionFilter::New(),
      base::BindOnce(
          [](ToolResultCallback cb, uint32_t num_deleted) {
            std::move(cb).Run(
                TextContent("All cookies cleared (" +
                            base::NumberToString(num_deleted) + ")"),
                false);
          },
          std::move(callback)));
}

}  // namespace

void RegisterCookieTools(MCPServer& server) {
  server.RegisterTool({
      "browser_cookie_list",
      "List cookies, optionally filtered by URL",
      SchemaObject(base::DictValue().Set("url", SchemaString("URL to filter cookies by")), {}),
      base::BindRepeating(&HandleCookieList),
  });

  server.RegisterTool({
      "browser_cookie_get",
      "Get a specific cookie by name",
      SchemaObject(base::DictValue().Set("name", SchemaString("Cookie name")), {"name"}),
      base::BindRepeating(&HandleCookieGet),
  });

  server.RegisterTool({
      "browser_cookie_set",
      "Set a cookie",
      SchemaObject(
          base::DictValue().Set("name", SchemaString("Cookie name")).Set("value", SchemaString("Cookie value")).Set("domain", SchemaString("Cookie domain")).Set("path", SchemaString("Cookie path")).Set("url", SchemaString("URL to associate cookie with")).Set("secure", SchemaBool("Secure flag")).Set("httpOnly", SchemaBool("HttpOnly flag")),
          {"name", "value"}),
      base::BindRepeating(&HandleCookieSet),
  });

  server.RegisterTool({
      "browser_cookie_delete",
      "Delete a cookie by name",
      SchemaObject(
          base::DictValue().Set("name", SchemaString("Cookie name to delete")).Set("url", SchemaString("URL the cookie belongs to")).Set("domain", SchemaString("Cookie domain")),
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
