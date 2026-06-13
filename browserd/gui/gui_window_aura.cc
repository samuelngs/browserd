#include "browserd/gui/gui_window.h"

#include "base/memory/raw_ptr.h"
#include "base/no_destructor.h"
#include "browserd/gui/aura_context.h"
#include "content/public/browser/web_contents.h"
#include "ui/aura/window.h"
#include "ui/aura/window_tree_host.h"
#include "ui/aura/window_tree_host_observer.h"
#include "ui/gfx/geometry/rect.h"

namespace browserd::gui {

namespace {

class AuraGuiWindow : public GuiWindow {
 public:
  AuraGuiWindow(std::unique_ptr<content::WebContents> web_contents,
                const gfx::Size& initial_size)
      : web_contents_(std::move(web_contents)) {
    host_ = GetAuraContext().CreateHost(initial_size);
    aura::Window* web_contents_window = web_contents_->GetNativeView();
    host_->window_tree_host()->window()->AddChild(web_contents_window);
  }

  ~AuraGuiWindow() override {
    if (close_observer_) {
      host_->window_tree_host()->RemoveObserver(close_observer_.get());
    }
  }

  content::WebContents* web_contents() const override {
    return web_contents_.get();
  }

  void Show() override {
    web_contents_->GetNativeView()->Show();
    host_->window_tree_host()->window()->Show();
    host_->window_tree_host()->Show();
  }

  void Resize(const gfx::Size& size) override {
    host_->window_tree_host()->SetBoundsInPixels(gfx::Rect(size));
  }

  void Close() override {
    close_callback_.Reset();
    host_->window_tree_host()->Hide();
  }

  void SetCloseCallback(base::OnceClosure callback) override {
    close_callback_ = std::move(callback);
    close_observer_ = std::make_unique<CloseObserver>(this);
    host_->window_tree_host()->AddObserver(close_observer_.get());
  }

  void OnCloseRequested() {
    if (close_callback_) {
      std::move(close_callback_).Run();
    }
  }

 private:
  class CloseObserver : public aura::WindowTreeHostObserver {
   public:
    explicit CloseObserver(AuraGuiWindow* window) : window_(window) {}
    ~CloseObserver() override = default;

    void OnHostCloseRequested(aura::WindowTreeHost* host) override {
      window_->OnCloseRequested();
    }

   private:
    raw_ptr<AuraGuiWindow> window_;
  };

  static AuraContext& GetAuraContext() {
    static base::NoDestructor<AuraContext> context;
    return *context;
  }

  std::unique_ptr<AuraContext::Host> host_;
  std::unique_ptr<content::WebContents> web_contents_;
  std::unique_ptr<CloseObserver> close_observer_;
  base::OnceClosure close_callback_;
};

}  // namespace

std::unique_ptr<GuiWindow> CreateGuiWindow(
    std::unique_ptr<content::WebContents> web_contents,
    const gfx::Size& initial_size) {
  return std::make_unique<AuraGuiWindow>(std::move(web_contents), initial_size);
}

}  // namespace browserd::gui
