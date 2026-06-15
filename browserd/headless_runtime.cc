#include "browserd/headless_runtime.h"

#include <utility>

#include "base/files/file_path.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "content/public/browser/web_contents.h"
#include "headless/lib/browser/headless_browser_context_impl.h"
#include "headless/lib/browser/headless_web_contents_impl.h"
#include "headless/public/headless_browser.h"
#include "headless/public/headless_browser_context.h"
#include "headless/public/headless_web_contents.h"
#include "browserd/user_data_dir.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/size.h"
#include "url/gurl.h"

namespace browserd {

namespace {

std::string MakeTargetId(headless::HeadlessWebContents* contents) {
  return base::StringPrintf("%p", static_cast<void*>(contents));
}

headless::HeadlessBrowserContextImpl* ToImpl(
    headless::HeadlessBrowserContext* context) {
  return headless::HeadlessBrowserContextImpl::From(context);
}

}  // namespace

HeadlessRuntime::HeadlessRuntime(headless::HeadlessBrowser* browser)
    : browser_(browser) {}

HeadlessRuntime::~HeadlessRuntime() = default;

bool HeadlessRuntime::Initialize() {
  headless::HeadlessBrowserContext::Builder context_builder =
      browser_->CreateBrowserContextBuilder();
  context_builder.SetIncognitoMode(false);
  base::FilePath user_data_dir;
  if (!GetHeadlessUserDataDir(&user_data_dir)) {
    return false;
  }
  context_builder.SetUserDataDir(user_data_dir);

  browser_context_ = context_builder.Build();
  browser_->SetDefaultBrowserContext(browser_context_);

  headless::HeadlessWebContents::Builder builder(
      browser_context_->CreateWebContentsBuilder());
  headless::HeadlessWebContents* headless_contents =
      builder.SetInitialURL(GURL("about:blank"))
          .SetWindowBounds(gfx::Rect(72, 50, 1920, 1030))
          .Build();

  if (!headless_contents) {
    return false;
  }

  active_web_contents_ =
      headless::HeadlessWebContentsImpl::From(headless_contents)
          ->web_contents();
  return active_web_contents_ != nullptr;
}

content::BrowserContext* HeadlessRuntime::browser_context() const {
  return ToImpl(browser_context_);
}

content::WebContents* HeadlessRuntime::active_web_contents() const {
  return active_web_contents_;
}

content::WebContents* HeadlessRuntime::GetWebContentsByTargetId(
    const std::string& target_id) const {
  headless::HeadlessWebContents* contents = FindByTargetId(target_id);
  if (!contents) {
    return nullptr;
  }
  auto* impl = headless::HeadlessWebContentsImpl::From(contents);
  return impl ? impl->web_contents() : nullptr;
}

std::vector<content::WebContents*> HeadlessRuntime::AllWebContents() const {
  std::vector<content::WebContents*> web_contents_list;
  auto* context = ToImpl(browser_context_);
  if (!context) {
    return web_contents_list;
  }

  for (auto* contents : context->GetAllWebContents()) {
    auto* impl = headless::HeadlessWebContentsImpl::From(contents);
    if (impl && impl->web_contents()) {
      web_contents_list.push_back(impl->web_contents());
    }
  }
  return web_contents_list;
}

std::vector<BrowserTabInfo> HeadlessRuntime::ListTabs() const {
  std::vector<BrowserTabInfo> tabs;
  auto* context = ToImpl(browser_context_);
  if (!context) {
    return tabs;
  }

  for (auto* contents : context->GetAllWebContents()) {
    tabs.push_back(MakeTabInfo(contents));
  }
  return tabs;
}

std::optional<BrowserTabInfo> HeadlessRuntime::CreateTab(const GURL& url) {
  if (!browser_context_) {
    return std::nullopt;
  }

  headless::HeadlessWebContents::Builder builder(
      browser_context_->CreateWebContentsBuilder());
  auto* new_contents = builder.SetInitialURL(url).Build();
  if (!new_contents) {
    return std::nullopt;
  }

  active_web_contents_ =
      headless::HeadlessWebContentsImpl::From(new_contents)->web_contents();
  return MakeTabInfo(new_contents);
}

bool HeadlessRuntime::CloseTab(const std::optional<std::string>& target_id) {
  headless::HeadlessWebContents* contents = nullptr;
  if (target_id.has_value()) {
    contents = FindByTargetId(target_id.value());
  } else {
    contents = ActiveHeadlessWebContents();
  }

  if (!contents) {
    return false;
  }

  ChooseFallbackActiveTab(contents);
  contents->Close();

  if (!active_web_contents_) {
    Shutdown();
  }
  return true;
}

void HeadlessRuntime::ResizeActive(const gfx::Size& size) {
  if (active_web_contents_) {
    active_web_contents_->Resize(gfx::Rect(size));
  }
}

void HeadlessRuntime::Shutdown() {
  if (browser_) {
    browser_->Shutdown();
  }
}

headless::HeadlessWebContents*
HeadlessRuntime::ActiveHeadlessWebContents() const {
  if (!active_web_contents_) {
    return nullptr;
  }
  auto* impl = headless::HeadlessWebContentsImpl::From(active_web_contents_);
  return impl;
}

headless::HeadlessWebContents* HeadlessRuntime::FindByTargetId(
    const std::string& target_id) const {
  auto* context = ToImpl(browser_context_);
  if (!context) {
    return nullptr;
  }

  for (auto* contents : context->GetAllWebContents()) {
    if (MakeTargetId(contents) == target_id) {
      return contents;
    }
  }
  return nullptr;
}

BrowserTabInfo HeadlessRuntime::MakeTabInfo(
    headless::HeadlessWebContents* contents) const {
  BrowserTabInfo info;
  info.target_id = MakeTargetId(contents);

  auto* impl = headless::HeadlessWebContentsImpl::From(contents);
  auto* web_contents = impl ? impl->web_contents() : nullptr;
  if (web_contents) {
    info.title = base::UTF16ToUTF8(web_contents->GetTitle());
    info.url = web_contents->GetLastCommittedURL().spec();
  }
  return info;
}

void HeadlessRuntime::ChooseFallbackActiveTab(
    headless::HeadlessWebContents* closing) {
  active_web_contents_ = nullptr;
  auto* context = ToImpl(browser_context_);
  if (!context) {
    return;
  }

  for (auto* contents : context->GetAllWebContents()) {
    if (contents == closing) {
      continue;
    }
    auto* impl = headless::HeadlessWebContentsImpl::From(contents);
    active_web_contents_ = impl ? impl->web_contents() : nullptr;
    if (active_web_contents_) {
      return;
    }
  }
}

}  // namespace browserd
