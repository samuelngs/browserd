#include "browserd/gui/main_delegate.h"

#include "base/check.h"
#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/logging.h"
#include "base/logging/logging_settings.h"
#include "base/path_service.h"
#include "browserd/gui/content_browser_client.h"
#include "browserd/gui/content_client.h"
#include "browserd/gui/content_renderer_client.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/base/ui_base_switches.h"

#if BUILDFLAG(IS_MAC)
#include "browserd/gui/mac_init.h"
#endif

namespace browserd::gui {
namespace {

void InitializeResourceBundle(const base::CommandLine& command_line) {
  base::FilePath resource_dir;
  CHECK(base::PathService::Get(base::DIR_ASSETS, &resource_dir));

  base::FilePath string_pack =
      resource_dir.Append(FILE_PATH_LITERAL("headless_lib_strings.pak"));
  base::FilePath data_pack =
      resource_dir.Append(FILE_PATH_LITERAL("headless_lib_data.pak"));
  if (base::PathExists(string_pack) && base::PathExists(data_pack)) {
    ui::ResourceBundle::InitSharedInstanceWithPakPath(string_pack);
    ui::ResourceBundle::GetSharedInstance().AddDataPackFromPath(
        data_pack, ui::k100Percent);
    return;
  }

  std::string locale = command_line.GetSwitchValueASCII(::switches::kLang);
  if (locale.empty()) {
    locale = "en-US";
  }
  ui::ResourceBundle::InitSharedInstanceWithLocale(
      locale, nullptr, ui::ResourceBundle::DO_NOT_LOAD_COMMON_RESOURCES);
}

}  // namespace

MainDelegate::MainDelegate() = default;
MainDelegate::~MainDelegate() = default;

std::optional<int> MainDelegate::BasicStartupComplete() {
  logging::LoggingSettings settings;
  settings.logging_dest =
      logging::LOG_TO_SYSTEM_DEBUG_LOG | logging::LOG_TO_STDERR;
  CHECK(logging::InitLogging(settings));

  content_client_ = std::make_unique<ContentClient>();
  content::SetContentClient(content_client_.get());
  return std::nullopt;
}

void MainDelegate::PreSandboxStartup() {
  InitializeResourceBundle(*base::CommandLine::ForCurrentProcess());
}

std::optional<int> MainDelegate::PreBrowserMain() {
#if BUILDFLAG(IS_MAC)
  MacPreBrowserMain();
#endif
  return content::ContentMainDelegate::PreBrowserMain();
}

content::ContentBrowserClient* MainDelegate::CreateContentBrowserClient() {
  content_browser_client_ = std::make_unique<ContentBrowserClient>();
  return content_browser_client_.get();
}

content::ContentRendererClient* MainDelegate::CreateContentRendererClient() {
  content_renderer_client_ = std::make_unique<ContentRendererClient>();
  return content_renderer_client_.get();
}

}  // namespace browserd::gui
