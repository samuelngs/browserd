#include "browserd/gui/gui_runtime.h"

#include <algorithm>
#include <utility>

#include "base/functional/bind.h"
#include "base/location.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "browserd/gui/browser_context.h"
#include "browserd/gui/gui_window.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/web_contents.h"
#include "url/gurl.h"

namespace browserd::gui {

GuiRuntime::Tab::Tab(std::string target_id_in,
                     std::unique_ptr<GuiWindow> window_in)
    : target_id(std::move(target_id_in)), window(std::move(window_in)) {}

GuiRuntime::Tab::~Tab() = default;
GuiRuntime::Tab::Tab(Tab&&) = default;
GuiRuntime::Tab& GuiRuntime::Tab::operator=(Tab&&) = default;

GuiRuntime::GuiRuntime(std::unique_ptr<BrowserContext> browser_context,
                       base::RepeatingClosure quit_callback)
    : browser_context_(std::move(browser_context)),
      quit_callback_(std::move(quit_callback)) {}

GuiRuntime::~GuiRuntime() = default;

bool GuiRuntime::Initialize() {
  return CreateTab(GURL("about:blank")).has_value();
}

content::BrowserContext* GuiRuntime::browser_context() const {
  return browser_context_.get();
}

content::WebContents* GuiRuntime::active_web_contents() const {
  const Tab* tab = ActiveTab();
  return tab ? tab->window->web_contents() : nullptr;
}

content::WebContents* GuiRuntime::GetWebContentsByTargetId(
    const std::string& target_id) const {
  const Tab* tab = FindTab(target_id);
  return tab ? tab->window->web_contents() : nullptr;
}

std::vector<content::WebContents*> GuiRuntime::AllWebContents() const {
  std::vector<content::WebContents*> web_contents_list;
  for (const auto& tab : tabs_) {
    if (auto* web_contents = tab.window->web_contents()) {
      web_contents_list.push_back(web_contents);
    }
  }
  return web_contents_list;
}

std::vector<BrowserTabInfo> GuiRuntime::ListTabs() const {
  std::vector<BrowserTabInfo> tabs;
  for (const auto& tab : tabs_) {
    tabs.push_back(MakeTabInfo(tab));
  }
  return tabs;
}

std::optional<BrowserTabInfo> GuiRuntime::CreateTab(const GURL& url) {
  content::WebContents::CreateParams params(browser_context_.get());
  std::unique_ptr<content::WebContents> web_contents =
      content::WebContents::Create(params);
  if (!web_contents) {
    return std::nullopt;
  }

  std::string target_id = "gui-" + base::NumberToString(next_tab_id_++);
  content::WebContents* web_contents_ptr = web_contents.get();
  auto window = CreateGuiWindow(std::move(web_contents), default_window_size_);
  if (!window) {
    return std::nullopt;
  }

  window->SetCloseCallback(base::BindOnce(&GuiRuntime::OnWindowCloseRequested,
                                          weak_factory_.GetWeakPtr(),
                                          target_id));
  tabs_.emplace_back(target_id, std::move(window));
  active_target_id_ = target_id;

  content::NavigationController::LoadURLParams load_params(url);
  web_contents_ptr->GetController().LoadURLWithParams(load_params);
  tabs_.back().window->Show();

  return MakeTabInfo(tabs_.back());
}

bool GuiRuntime::CloseTab(const std::optional<std::string>& target_id) {
  std::string id = target_id.value_or(active_target_id_);
  if (id.empty()) {
    return false;
  }

  auto it = std::ranges::find_if(
      tabs_, [&](const Tab& tab) { return tab.target_id == id; });
  if (it == tabs_.end()) {
    return false;
  }

  bool was_active = active_target_id_ == id;
  it->window->Close();
  tabs_.erase(it);

  if (was_active) {
    ChooseFallbackActiveTab();
  }
  if (tabs_.empty()) {
    Shutdown();
  }
  return true;
}

void GuiRuntime::ResizeActive(const gfx::Size& size) {
  Tab* tab = ActiveTab();
  if (tab) {
    tab->window->Resize(size);
  }
}

void GuiRuntime::Shutdown() {
  if (shutdown_started_) {
    return;
  }
  shutdown_started_ = true;

  for (auto& tab : tabs_) {
    tab.window->Close();
  }
  tabs_.clear();
  active_target_id_.clear();

  if (quit_callback_) {
    quit_callback_.Run();
  }
}

BrowserTabInfo GuiRuntime::MakeTabInfo(const Tab& tab) const {
  BrowserTabInfo info;
  info.target_id = tab.target_id;

  content::WebContents* web_contents = tab.window->web_contents();
  if (web_contents) {
    info.title = base::UTF16ToUTF8(web_contents->GetTitle());
    info.url = web_contents->GetLastCommittedURL().spec();
  }
  return info;
}

GuiRuntime::Tab* GuiRuntime::FindTab(const std::string& target_id) {
  auto it = std::ranges::find_if(
      tabs_, [&](const Tab& tab) { return tab.target_id == target_id; });
  return it == tabs_.end() ? nullptr : &*it;
}

const GuiRuntime::Tab* GuiRuntime::FindTab(
    const std::string& target_id) const {
  auto it = std::ranges::find_if(
      tabs_, [&](const Tab& tab) { return tab.target_id == target_id; });
  return it == tabs_.end() ? nullptr : &*it;
}

GuiRuntime::Tab* GuiRuntime::ActiveTab() {
  return FindTab(active_target_id_);
}

const GuiRuntime::Tab* GuiRuntime::ActiveTab() const {
  return FindTab(active_target_id_);
}

void GuiRuntime::OnWindowCloseRequested(std::string target_id) {
  content::GetUIThreadTaskRunner({})->PostTask(
      FROM_HERE,
      base::BindOnce(&GuiRuntime::RemoveClosedWindow,
                     weak_factory_.GetWeakPtr(), std::move(target_id)));
}

void GuiRuntime::RemoveClosedWindow(std::string target_id) {
  auto it = std::ranges::find_if(
      tabs_, [&](const Tab& tab) { return tab.target_id == target_id; });
  if (it == tabs_.end()) {
    return;
  }

  bool was_active = active_target_id_ == target_id;
  tabs_.erase(it);
  if (was_active) {
    ChooseFallbackActiveTab();
  }
  if (tabs_.empty()) {
    Shutdown();
  }
}

void GuiRuntime::ChooseFallbackActiveTab() {
  active_target_id_ = tabs_.empty() ? std::string() : tabs_.front().target_id;
}

}  // namespace browserd::gui
