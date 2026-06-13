#ifndef BROWSERD_GUI_FILL_LAYOUT_H_
#define BROWSERD_GUI_FILL_LAYOUT_H_

#include "base/memory/raw_ptr.h"
#include "ui/aura/layout_manager.h"

namespace aura {
class Window;
}

namespace browserd::gui {

class FillLayout : public aura::LayoutManager {
 public:
  explicit FillLayout(aura::Window* root);
  ~FillLayout() override;

  FillLayout(const FillLayout&) = delete;
  FillLayout& operator=(const FillLayout&) = delete;

 private:
  void OnWindowResized() override;
  void OnWindowAddedToLayout(aura::Window* child) override;
  void OnWillRemoveWindowFromLayout(aura::Window* child) override;
  void OnWindowRemovedFromLayout(aura::Window* child) override;
  void OnChildWindowVisibilityChanged(aura::Window* child,
                                      bool visible) override;
  void SetChildBounds(aura::Window* child,
                      const gfx::Rect& requested_bounds) override;

  raw_ptr<aura::Window> const root_;
};

}  // namespace browserd::gui

#endif  // BROWSERD_GUI_FILL_LAYOUT_H_
