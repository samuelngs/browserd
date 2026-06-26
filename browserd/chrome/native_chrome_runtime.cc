#include "browserd/chrome/native_chrome_runtime.h"

#include <cstring>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "browserd/chrome/platform_setup.h"
#include "chrome/browser/lifetime/application_lifetime_desktop.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/sessions/content/session_tab_helper.h"
#include "components/sessions/core/session_id.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/base_window.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/size.h"
#include "url/gurl.h"

namespace browserd::chrome {
namespace {

constexpr char kTargetPrefix[] = "chrome-";

std::string MakeTargetId(content::WebContents* web_contents) {
  SessionID id = sessions::SessionTabHelper::IdForTab(web_contents);
  if (!id.is_valid()) {
    return std::string();
  }
  return std::string(kTargetPrefix) + base::NumberToString(id.id());
}

std::optional<SessionID> ParseTargetId(const std::string& target_id) {
  if (!target_id.starts_with(kTargetPrefix)) {
    return std::nullopt;
  }

  int value = 0;
  if (!base::StringToInt(target_id.substr(strlen(kTargetPrefix)), &value)) {
    return std::nullopt;
  }
  SessionID id = SessionID::FromSerializedValue(value);
  if (!id.is_valid()) {
    return std::nullopt;
  }
  return id;
}

BrowserTabInfo MakeTabInfo(content::WebContents* web_contents) {
  BrowserTabInfo info;
  info.target_id = MakeTargetId(web_contents);
  info.title = base::UTF16ToUTF8(web_contents->GetTitle());
  content::NavigationEntry* entry =
      web_contents->GetController().GetLastCommittedEntry();
  info.url = entry ? entry->GetURL().spec() : web_contents->GetVisibleURL().spec();
  return info;
}

struct LocatedTab {
  raw_ptr<BrowserWindowInterface> browser = nullptr;
  raw_ptr<content::WebContents> web_contents = nullptr;
  int index = TabStripModel::kNoTab;
};

std::optional<LocatedTab> FindTabByTargetId(const std::string& target_id) {
  std::optional<SessionID> target = ParseTargetId(target_id);
  if (!target.has_value()) {
    return std::nullopt;
  }

  std::optional<LocatedTab> located;
  ForEachCurrentBrowserWindowInterfaceOrderedByActivation(
      [&](BrowserWindowInterface* browser) {
        TabStripModel* tab_strip = browser->GetTabStripModel();
        for (int index = 0; index < tab_strip->count(); ++index) {
          content::WebContents* web_contents =
              tab_strip->GetWebContentsAt(index);
          if (sessions::SessionTabHelper::IdForTab(web_contents) ==
              target.value()) {
            located = LocatedTab{browser, web_contents, index};
            return false;
          }
        }
        return true;
      });
  return located;
}

BrowserWindowInterface* FirstBrowserWindow() {
  BrowserWindowInterface* first = nullptr;
  ForEachCurrentBrowserWindowInterfaceOrderedByActivation(
      [&](BrowserWindowInterface* browser) {
        first = browser;
        return false;
      });
  return first;
}

void ShowAndActivate(BrowserWindowInterface* browser) {
  if (browser && browser->GetWindow()) {
    browser->GetWindow()->Show();
    browser->GetWindow()->Activate();
  }
  ActivateChromeApplication();
}

}  // namespace

NativeChromeRuntime::NativeChromeRuntime() = default;

NativeChromeRuntime::~NativeChromeRuntime() = default;

content::BrowserContext* NativeChromeRuntime::browser_context() const {
  BrowserWindowInterface* browser = ActiveBrowserWindow();
  if (browser) {
    return browser->GetProfile();
  }
  return ProfileManager::GetLastUsedProfileIfLoaded();
}

content::WebContents* NativeChromeRuntime::active_web_contents() const {
  BrowserWindowInterface* browser = ActiveBrowserWindow();
  return browser ? browser->GetTabStripModel()->GetActiveWebContents()
                 : nullptr;
}

content::WebContents* NativeChromeRuntime::GetWebContentsByTargetId(
    const std::string& target_id) const {
  std::optional<LocatedTab> located = FindTabByTargetId(target_id);
  return located.has_value() ? located->web_contents.get() : nullptr;
}

std::vector<content::WebContents*> NativeChromeRuntime::AllWebContents() const {
  std::vector<content::WebContents*> web_contents_list;
  ForEachCurrentBrowserWindowInterfaceOrderedByActivation(
      [&](BrowserWindowInterface* browser) {
        TabStripModel* tab_strip = browser->GetTabStripModel();
        for (int index = 0; index < tab_strip->count(); ++index) {
          if (content::WebContents* web_contents =
                  tab_strip->GetWebContentsAt(index)) {
            web_contents_list.push_back(web_contents);
          }
        }
        return true;
      });
  return web_contents_list;
}

std::vector<BrowserTabInfo> NativeChromeRuntime::ListTabs() const {
  std::vector<BrowserTabInfo> tabs;
  ForEachCurrentBrowserWindowInterfaceOrderedByActivation(
      [&](BrowserWindowInterface* browser) {
        TabStripModel* tab_strip = browser->GetTabStripModel();
        for (int index = 0; index < tab_strip->count(); ++index) {
          if (content::WebContents* web_contents =
                  tab_strip->GetWebContentsAt(index)) {
            tabs.push_back(MakeTabInfo(web_contents));
          }
        }
        return true;
      });
  return tabs;
}

std::optional<BrowserTabInfo> NativeChromeRuntime::CreateTab(const GURL& url) {
  BrowserWindowInterface* browser = ActiveBrowserWindow();
  if (!browser) {
    Profile* profile = ProfileManager::GetLastUsedProfileAllowedByPolicy();
    if (!profile) {
      return std::nullopt;
    }
    browser = ::chrome::OpenEmptyWindow(
        profile, /*should_trigger_session_restore=*/false);
  }
  if (!browser) {
    return std::nullopt;
  }

  content::WebContents* web_contents =
      ::chrome::AddAndReturnTabAt(browser, url, -1, true);
  if (!web_contents) {
    return std::nullopt;
  }
  ShowAndActivate(browser);
  return MakeTabInfo(web_contents);
}

bool NativeChromeRuntime::CloseTab(
    const std::optional<std::string>& target_id) {
  BrowserWindowInterface* browser = nullptr;
  int index = TabStripModel::kNoTab;
  if (target_id.has_value()) {
    std::optional<LocatedTab> located = FindTabByTargetId(target_id.value());
    if (!located.has_value()) {
      return false;
    }
    browser = located->browser.get();
    index = located->index;
  } else {
    browser = ActiveBrowserWindow();
    if (!browser) {
      return false;
    }
    index = browser->GetTabStripModel()->active_index();
  }

  if (!browser || !browser->GetTabStripModel()->ContainsIndex(index)) {
    return false;
  }
  browser->GetTabStripModel()->CloseWebContentsAt(
      index, CLOSE_USER_GESTURE | CLOSE_CREATE_HISTORICAL_TAB);
  return true;
}

void NativeChromeRuntime::ResizeActive(const gfx::Size& size) {
  BrowserWindowInterface* browser = ActiveBrowserWindow();
  if (!browser || !browser->GetWindow()) {
    return;
  }
  gfx::Rect bounds = browser->GetWindow()->GetBounds();
  bounds.set_size(size);
  browser->GetWindow()->SetBounds(bounds);
}

void NativeChromeRuntime::Shutdown() {
  ::chrome::CloseAllBrowsersAndQuit();
}

BrowserWindowInterface* NativeChromeRuntime::ActiveBrowserWindow() const {
  BrowserWindowInterface* active =
      GetLastActiveBrowserWindowInterfaceWithAnyProfile();
  return active ? active : FirstBrowserWindow();
}

}  // namespace browserd::chrome
