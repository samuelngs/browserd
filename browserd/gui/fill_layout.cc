#include "browserd/gui/fill_layout.h"

#include "ui/aura/window.h"

namespace browserd::gui {

FillLayout::FillLayout(aura::Window* root) : root_(root) {}

FillLayout::~FillLayout() = default;

void FillLayout::OnWindowResized() {
  if (root_->bounds().IsEmpty()) {
    return;
  }
  for (aura::Window* child : root_->children()) {
    SetChildBoundsDirect(child, gfx::Rect(root_->bounds().size()));
  }
}

void FillLayout::OnWindowAddedToLayout(aura::Window* child) {
  child->SetBounds(root_->bounds());
}

void FillLayout::OnWillRemoveWindowFromLayout(aura::Window* child) {}

void FillLayout::OnWindowRemovedFromLayout(aura::Window* child) {}

void FillLayout::OnChildWindowVisibilityChanged(aura::Window* child,
                                                bool visible) {}

void FillLayout::SetChildBounds(aura::Window* child,
                                const gfx::Rect& requested_bounds) {
  SetChildBoundsDirect(child, requested_bounds);
}

}  // namespace browserd::gui
