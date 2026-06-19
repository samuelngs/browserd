#ifndef BROWSERD_GUI_GUI_RUNTIME_H_
#define BROWSERD_GUI_GUI_RUNTIME_H_

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"
#include "browserd/browser_runtime.h"
#include "browserd/gui/runtime_options.h"
#include "ui/gfx/geometry/size.h"

namespace browserd::gui {

class BrowserContext;
class GuiWindow;

class GuiRuntime : public BrowserRuntime {
 public:
  GuiRuntime(std::unique_ptr<BrowserContext> browser_context,
             RuntimeOptions options,
             base::RepeatingClosure quit_callback);
  ~GuiRuntime() override;

  GuiRuntime(const GuiRuntime&) = delete;
  GuiRuntime& operator=(const GuiRuntime&) = delete;

  bool Initialize();

  content::BrowserContext* browser_context() const override;
  content::WebContents* active_web_contents() const override;
  content::WebContents* GetWebContentsByTargetId(
      const std::string& target_id) const override;
  std::vector<content::WebContents*> AllWebContents() const override;
  std::vector<BrowserTabInfo> ListTabs() const override;
  std::optional<BrowserTabInfo> CreateTab(const GURL& url) override;
  bool CloseTab(const std::optional<std::string>& target_id) override;
  void ResizeActive(const gfx::Size& size) override;
  void Shutdown() override;

 private:
  struct Tab {
    Tab(std::string target_id, std::unique_ptr<GuiWindow> window);
    ~Tab();
    Tab(Tab&&);
    Tab& operator=(Tab&&);

    std::string target_id;
    std::unique_ptr<GuiWindow> window;
  };

  BrowserTabInfo MakeTabInfo(const Tab& tab) const;
  Tab* FindTab(const std::string& target_id);
  const Tab* FindTab(const std::string& target_id) const;
  Tab* ActiveTab();
  const Tab* ActiveTab() const;
  void OnWindowCloseRequested(std::string target_id);
  void RemoveClosedWindow(std::string target_id);
  void ChooseFallbackActiveTab();

  std::unique_ptr<BrowserContext> browser_context_;
  RuntimeOptions options_;
  std::vector<Tab> tabs_;
  std::string active_target_id_;
  uint64_t next_tab_id_ = 1;
  gfx::Size default_window_size_{1280, 720};
  base::RepeatingClosure quit_callback_;
  bool shutdown_started_ = false;
  base::WeakPtrFactory<GuiRuntime> weak_factory_{this};
};

}  // namespace browserd::gui

#endif  // BROWSERD_GUI_GUI_RUNTIME_H_
