#include "browserd/gui/gui_window.h"

#import <Cocoa/Cocoa.h>

#include "base/memory/raw_ptr.h"
#include "content/public/browser/web_contents.h"

@interface BrowserdWindowCloseObserver : NSObject
- (instancetype)initWithWindow:(NSWindow*)window
                     guiWindow:(browserd::gui::GuiWindow*)gui_window;
- (void)onWillClose;
@end

namespace browserd::gui {

class MacGuiWindow : public GuiWindow {
 public:
  MacGuiWindow(std::unique_ptr<content::WebContents> web_contents,
               const gfx::Size& initial_size)
      : web_contents_(std::move(web_contents)) {
    window_ = [[NSWindow alloc]
        initWithContentRect:NSMakeRect(0, 0, initial_size.width(),
                                       initial_size.height())
                  styleMask:NSWindowStyleMaskTitled |
                            NSWindowStyleMaskClosable |
                            NSWindowStyleMaskMiniaturizable |
                            NSWindowStyleMaskResizable
                    backing:NSBackingStoreBuffered
                      defer:NO];
    window_.releasedWhenClosed = NO;
    close_observer_ = [[BrowserdWindowCloseObserver alloc]
        initWithWindow:window_
             guiWindow:this];
    [window_ setContentView:web_contents_->GetNativeView().GetNativeNSView()];
  }

  ~MacGuiWindow() override = default;

  content::WebContents* web_contents() const override {
    return web_contents_.get();
  }

  void Show() override { [window_ makeKeyAndOrderFront:nil]; }

  void Resize(const gfx::Size& size) override {
    NSRect frame = [window_ frame];
    frame.size = NSMakeSize(size.width(), size.height());
    [window_ setFrame:frame display:YES animate:NO];
  }

  void Close() override {
    close_callback_.Reset();
    [window_ close];
  }

  void SetCloseCallback(base::OnceClosure callback) override {
    close_callback_ = std::move(callback);
  }

  void OnWindowWillClose() {
    if (close_callback_) {
      std::move(close_callback_).Run();
    }
  }

 private:
  std::unique_ptr<content::WebContents> web_contents_;
  NSWindow* __strong window_;
  BrowserdWindowCloseObserver* __strong close_observer_;
  base::OnceClosure close_callback_;
};

}  // namespace browserd::gui

@implementation BrowserdWindowCloseObserver {
  NSWindow* __strong _window;
  raw_ptr<browserd::gui::GuiWindow> _guiWindow;
}

- (instancetype)initWithWindow:(NSWindow*)window
                     guiWindow:(browserd::gui::GuiWindow*)gui_window {
  self = [super init];
  if (self) {
    _window = window;
    _guiWindow = gui_window;
    [[NSNotificationCenter defaultCenter]
        addObserver:self
           selector:@selector(onWillClose)
               name:NSWindowWillCloseNotification
             object:_window];
  }
  return self;
}

- (void)onWillClose {
  [[NSNotificationCenter defaultCenter]
      removeObserver:self
                  name:NSWindowWillCloseNotification
                object:_window];
  static_cast<browserd::gui::MacGuiWindow*>(_guiWindow.get())
      ->OnWindowWillClose();
}

@end

namespace browserd::gui {

std::unique_ptr<GuiWindow> CreateGuiWindow(
    std::unique_ptr<content::WebContents> web_contents,
    const gfx::Size& initial_size) {
  return std::make_unique<MacGuiWindow>(std::move(web_contents), initial_size);
}

}  // namespace browserd::gui
