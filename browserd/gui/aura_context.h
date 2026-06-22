#ifndef BROWSERD_GUI_AURA_CONTEXT_H_
#define BROWSERD_GUI_AURA_CONTEXT_H_

#include <memory>

#include "base/memory/raw_ptr.h"
#include "base/types/pass_key.h"

namespace aura {
class WindowTreeHost;
}

namespace display {
class Screen;
}

namespace gfx {
class Size;
}

namespace wm {
class CursorManager;
class FocusController;
}  // namespace wm

namespace browserd::gui {

class AuraContext {
 public:
  class Host {
   public:
    Host(base::PassKey<AuraContext>,
         AuraContext* context,
         std::unique_ptr<aura::WindowTreeHost> host);
    ~Host();

    aura::WindowTreeHost* window_tree_host() const {
      return window_tree_host_.get();
    }

   private:
    raw_ptr<AuraContext> const context_;
    std::unique_ptr<aura::WindowTreeHost> window_tree_host_;
  };

  AuraContext();
  ~AuraContext();

  AuraContext(const AuraContext&) = delete;
  AuraContext& operator=(const AuraContext&) = delete;

  std::unique_ptr<Host> CreateHost(const gfx::Size& size);

 private:
  class NativeCursorManager;

  void InitializeHost(aura::WindowTreeHost* host);
  void UninitializeHost(aura::WindowTreeHost* host);

  std::unique_ptr<display::Screen> screen_;
  std::unique_ptr<wm::FocusController> focus_controller_;
  std::unique_ptr<wm::CursorManager> cursor_manager_;
  raw_ptr<NativeCursorManager> native_cursor_manager_ = nullptr;
};

}  // namespace browserd::gui

#endif  // BROWSERD_GUI_AURA_CONTEXT_H_
