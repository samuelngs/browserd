#include "browserd/chrome/content_browser_client.h"

#include <memory>
#include <utility>

#include "base/command_line.h"
#include "base/logging.h"
#include "browserd/chrome/main_extra_parts.h"
#include "browserd/embedder_switches.h"
#include "browserd/switches.h"
#include "chrome/browser/chrome_browser_main.h"
#include "content/public/browser/browser_main_parts.h"

namespace browserd::chrome {

ContentBrowserClient::ContentBrowserClient(RuntimeReadyCallback ready_callback)
    : ready_callback_(std::move(ready_callback)) {}

ContentBrowserClient::~ContentBrowserClient() = default;

std::unique_ptr<content::BrowserMainParts>
ContentBrowserClient::CreateBrowserMainParts(bool is_integration_test) {
  std::unique_ptr<content::BrowserMainParts> parts =
      ChromeContentBrowserClient::CreateBrowserMainParts(is_integration_test);
  if (ready_callback_) {
    static_cast<ChromeBrowserMainParts*>(parts.get())
        ->AddParts(std::make_unique<MainExtraParts>(
            std::move(ready_callback_)));
  }
  return parts;
}

void ContentBrowserClient::AppendExtraCommandLineSwitches(
    base::CommandLine* command_line,
    int child_process_id) {
  ChromeContentBrowserClient::AppendExtraCommandLineSwitches(command_line,
                                                             child_process_id);
  command_line->AppendSwitch(browserd::switches::kGui);
  browserd::AppendEmbedderSwitchesForChild(command_line);
  LOG(INFO) << "browserd chrome child extra switches id=" << child_process_id
            << " " << command_line->GetCommandLineString();
}

}  // namespace browserd::chrome
