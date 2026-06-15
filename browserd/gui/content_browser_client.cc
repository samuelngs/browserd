#include "browserd/gui/content_browser_client.h"

#include <utility>

#include "base/command_line.h"
#include "browserd/embedder_switches.h"
#include "browserd/gui/browser_main_parts.h"
#include "browserd/switches.h"
#include "components/embedder_support/user_agent_utils.h"
#include "headless/public/headless_browser.h"
#include "headless/public/switches.h"
#include "third_party/blink/public/common/user_agent/user_agent_metadata.h"

namespace browserd::gui {

ContentBrowserClient::ContentBrowserClient() = default;
ContentBrowserClient::ContentBrowserClient(
    BrowserMainParts::RuntimeReadyCallback runtime_ready_callback)
    : runtime_ready_callback_(std::move(runtime_ready_callback)) {}

ContentBrowserClient::~ContentBrowserClient() = default;

std::unique_ptr<content::BrowserMainParts>
ContentBrowserClient::CreateBrowserMainParts(bool is_integration_test) {
  return std::make_unique<BrowserMainParts>(std::move(runtime_ready_callback_));
}

void ContentBrowserClient::AppendExtraCommandLineSwitches(
    base::CommandLine* command_line,
    int child_process_id) {
  command_line->AppendSwitch(browserd::switches::kGui);
  browserd::AppendEmbedderSwitchesForChild(command_line);
}

std::string ContentBrowserClient::GetAcceptLangs(
    content::BrowserContext* context) {
  std::string accept_lang =
      base::CommandLine::ForCurrentProcess()->GetSwitchValueASCII(
          headless::switches::kAcceptLang);
  return accept_lang.empty() ? "en-US,en" : accept_lang;
}

std::string ContentBrowserClient::GetProduct() {
  return headless::HeadlessBrowser::GetProductNameAndVersion();
}

std::string ContentBrowserClient::GetUserAgent() {
  std::string user_agent = embedder_support::GetUserAgent();
  const size_t product_start = user_agent.find("Chrome/");
  if (product_start == std::string::npos) {
    return user_agent;
  }

  size_t product_end = user_agent.find(' ', product_start);
  if (product_end == std::string::npos) {
    product_end = user_agent.size();
  }
  return user_agent.replace(product_start, product_end - product_start,
                            GetProduct());
}

blink::UserAgentMetadata ContentBrowserClient::GetUserAgentMetadata() {
  return headless::HeadlessBrowser::GetUserAgentMetadata();
}

}  // namespace browserd::gui
