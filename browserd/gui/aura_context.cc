#include "browserd/gui/aura_context.h"

#include "base/containers/flat_set.h"
#include "base/memory/raw_ptr.h"
#include "base/notimplemented.h"
#include "browserd/gui/fill_layout.h"
#include "ui/aura/client/cursor_client.h"
#include "ui/aura/client/focus_client.h"
#include "ui/aura/test/test_screen.h"
#include "ui/aura/window.h"
#include "ui/aura/window_event_dispatcher.h"
#include "ui/aura/window_tree_host.h"
#include "ui/base/cursor/cursor.h"
#include "ui/display/screen.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/size.h"
#include "ui/platform_window/platform_window_init_properties.h"
#include "ui/wm/core/base_focus_rules.h"
#include "ui/wm/core/cursor_loader.h"
#include "ui/wm/core/cursor_manager.h"
#include "ui/wm/core/focus_controller.h"
#include "ui/wm/core/native_cursor_manager.h"
#include "ui/wm/public/activation_client.h"

namespace browserd::gui {

namespace {

class FocusRules : public wm::BaseFocusRules {
 public:
  FocusRules() = default;
  ~FocusRules() override = default;

 private:
  bool SupportsChildActivation(const aura::Window* window) const override {
    return true;
  }
};

}  // namespace

class AuraContext::NativeCursorManager : public wm::NativeCursorManager {
 public:
  NativeCursorManager() { aura::client::SetCursorShapeClient(&cursor_loader_); }
  ~NativeCursorManager() override = default;

  void AddHost(aura::WindowTreeHost* host) { hosts_.insert(host); }
  void RemoveHost(aura::WindowTreeHost* host) { hosts_.erase(host); }

 private:
  void SetDisplay(const display::Display& display,
                  wm::NativeCursorManagerDelegate* delegate) override {
    if (cursor_loader_.SetDisplay(display)) {
      SetCursor(delegate->GetCursor(), delegate);
    }
  }

  void SetCursor(gfx::NativeCursor cursor,
                 wm::NativeCursorManagerDelegate* delegate) override {
    gfx::NativeCursor new_cursor = cursor;
    cursor_loader_.SetPlatformCursor(&new_cursor);
    delegate->CommitCursor(new_cursor);
    if (delegate->IsCursorVisible()) {
      for (aura::WindowTreeHost* host : hosts_) {
        host->SetCursor(new_cursor);
      }
    }
  }

  void SetVisibility(bool visible,
                     wm::NativeCursorManagerDelegate* delegate) override {
    delegate->CommitVisibility(visible);
    if (visible) {
      SetCursor(delegate->GetCursor(), delegate);
    } else {
      gfx::NativeCursor invisible_cursor(ui::mojom::CursorType::kNone);
      cursor_loader_.SetPlatformCursor(&invisible_cursor);
      for (aura::WindowTreeHost* host : hosts_) {
        host->SetCursor(invisible_cursor);
      }
    }

    for (aura::WindowTreeHost* host : hosts_) {
      host->OnCursorVisibilityChanged(visible);
    }
  }

  void SetCursorSize(ui::CursorSize cursor_size,
                     wm::NativeCursorManagerDelegate* delegate) override {
    NOTIMPLEMENTED();
  }

  void SetLargeCursorSizeInDip(
      int large_cursor_size_in_dip,
      wm::NativeCursorManagerDelegate* delegate) override {
    NOTIMPLEMENTED();
  }

  void SetMouseEventsEnabled(
      bool enabled,
      wm::NativeCursorManagerDelegate* delegate) override {
    delegate->CommitMouseEventsEnabled(enabled);
    SetVisibility(delegate->IsCursorVisible(), delegate);
    for (aura::WindowTreeHost* host : hosts_) {
      host->dispatcher()->OnMouseEventsEnableStateChanged(enabled);
    }
  }

  void SetCursorColor(SkColor color,
                      wm::NativeCursorManagerDelegate* delegate) override {
    NOTIMPLEMENTED();
  }

  base::flat_set<raw_ptr<aura::WindowTreeHost, CtnExperimental>> hosts_;
  wm::CursorLoader cursor_loader_;
};

AuraContext::Host::Host(base::PassKey<AuraContext>,
                        AuraContext* context,
                        std::unique_ptr<aura::WindowTreeHost> host)
    : context_(context), window_tree_host_(std::move(host)) {
  context_->InitializeHost(window_tree_host_.get());
}

AuraContext::Host::~Host() {
  context_->UninitializeHost(window_tree_host_.get());
}

AuraContext::AuraContext()
    : screen_(aura::TestScreen::Create(gfx::Size(1024, 768))) {
  if (!display::Screen::Get()) {
    display::Screen::SetScreenInstance(screen_.get());
  }
  focus_controller_ = std::make_unique<wm::FocusController>(new FocusRules());
  auto native_cursor_manager = std::make_unique<NativeCursorManager>();
  native_cursor_manager_ = native_cursor_manager.get();
  cursor_manager_ =
      std::make_unique<wm::CursorManager>(std::move(native_cursor_manager));
}

AuraContext::~AuraContext() = default;

std::unique_ptr<AuraContext::Host> AuraContext::CreateHost(
    const gfx::Size& size) {
  ui::PlatformWindowInitProperties properties;
  properties.bounds = gfx::Rect(size);
  auto host = aura::WindowTreeHost::Create(std::move(properties));
  return std::make_unique<Host>(base::PassKey<AuraContext>(), this,
                                std::move(host));
}

void AuraContext::InitializeHost(aura::WindowTreeHost* host) {
  host->InitHost();
  aura::client::SetFocusClient(host->window(), focus_controller_.get());
  wm::SetActivationClient(host->window(), focus_controller_.get());
  host->window()->AddPreTargetHandler(focus_controller_.get());
  host->window()->SetLayoutManager(std::make_unique<FillLayout>(host->window()));

  native_cursor_manager_->AddHost(host);
  aura::client::SetCursorClient(host->window(), cursor_manager_.get());
}

void AuraContext::UninitializeHost(aura::WindowTreeHost* host) {
  native_cursor_manager_->RemoveHost(host);
  host->window()->RemovePreTargetHandler(focus_controller_.get());
}

}  // namespace browserd::gui
