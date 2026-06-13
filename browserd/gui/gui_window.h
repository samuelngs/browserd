#ifndef BROWSERD_GUI_GUI_WINDOW_H_
#define BROWSERD_GUI_GUI_WINDOW_H_

#include <memory>

#include "base/functional/callback.h"
#include "ui/gfx/geometry/size.h"

namespace content {
class WebContents;
}  // namespace content

namespace browserd::gui {

class GuiWindow {
 public:
  virtual ~GuiWindow() = default;

  virtual content::WebContents* web_contents() const = 0;
  virtual void Show() = 0;
  virtual void Resize(const gfx::Size& size) = 0;
  virtual void Close() = 0;
  virtual void SetCloseCallback(base::OnceClosure callback) = 0;
};

std::unique_ptr<GuiWindow> CreateGuiWindow(
    std::unique_ptr<content::WebContents> web_contents,
    const gfx::Size& initial_size);

}  // namespace browserd::gui

#endif  // BROWSERD_GUI_GUI_WINDOW_H_
