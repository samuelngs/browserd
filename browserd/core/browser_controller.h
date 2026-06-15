#ifndef BROWSERD_CORE_BROWSER_CONTROLLER_H_
#define BROWSERD_CORE_BROWSER_CONTROLLER_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/task/sequenced_task_runner.h"
#include "browserd/browser_runtime.h"
#include "browserd/core/browser_result.h"
#include "ui/gfx/geometry/size.h"

class GURL;

namespace content {
class WebContents;
}

namespace browserd {

class BrowserController {
 public:
  using StatusCallback = base::OnceCallback<void(BrowserStatus)>;
  using TabCallback = base::OnceCallback<void(BrowserResult<BrowserTabInfo>)>;
  using TabsCallback =
      base::OnceCallback<void(BrowserResult<std::vector<BrowserTabInfo>>)>;
  using EvaluateCallback =
      base::OnceCallback<void(BrowserResult<EvaluateResult>)>;
  using SnapshotCallback =
      base::OnceCallback<void(BrowserResult<std::string>)>;
  using ScreenshotCallback =
      base::OnceCallback<void(BrowserResult<std::vector<uint8_t>>)>;
  using CookieCallback =
      base::OnceCallback<void(BrowserResult<CookieInfo>)>;
  using CookiesCallback =
      base::OnceCallback<void(BrowserResult<std::vector<CookieInfo>>)>;

  BrowserController(std::unique_ptr<BrowserRuntime> runtime,
                    scoped_refptr<base::SequencedTaskRunner> task_runner);
  ~BrowserController();

  BrowserController(const BrowserController&) = delete;
  BrowserController& operator=(const BrowserController&) = delete;

  BrowserRuntime* runtime() const { return runtime_.get(); }
  scoped_refptr<base::SequencedTaskRunner> task_runner() const {
    return task_runner_;
  }

  void ListTabs(TabsCallback callback);
  void CreateTab(GURL url, TabCallback callback);
  void CloseTab(std::optional<std::string> target_id,
                StatusCallback callback);
  void ResizeActive(gfx::Size size, StatusCallback callback);

  void Navigate(std::optional<std::string> target_id,
                GURL url,
                StatusCallback callback);
  void NavigateBack(std::optional<std::string> target_id,
                    StatusCallback callback);
  void NavigateForward(std::optional<std::string> target_id,
                       StatusCallback callback);
  void Reload(std::optional<std::string> target_id, StatusCallback callback);

  void Evaluate(std::optional<std::string> target_id,
                std::string expression,
                EvaluateCallback callback);
  void ConsoleMessages(std::optional<std::string> target_id,
                       SnapshotCallback callback);
  void Snapshot(std::optional<std::string> target_id,
                SnapshotCallback callback);
  void Screenshot(std::optional<std::string> target_id,
                  ScreenshotCallback callback);

  void Click(std::optional<std::string> target_id,
             RefOptions options,
             StatusCallback callback);
  void Hover(std::optional<std::string> target_id,
             RefOptions options,
             StatusCallback callback);
  void Type(std::optional<std::string> target_id,
            TypeOptions options,
            StatusCallback callback);
  void PressKey(std::optional<std::string> target_id,
                KeyOptions options,
                StatusCallback callback);
  void SelectOption(std::optional<std::string> target_id,
                    SelectOptions options,
                    StatusCallback callback);
  void Drag(std::optional<std::string> target_id,
            DragOptions options,
            StatusCallback callback);
  void Scroll(std::optional<std::string> target_id,
              ScrollOptions options,
              StatusCallback callback);

  void WaitFor(std::optional<std::string> target_id,
               WaitOptions options,
               StatusCallback callback);

  void ListCookies(std::optional<std::string> target_id,
                   CookieListOptions options,
                   CookiesCallback callback);
  void GetCookie(std::optional<std::string> target_id,
                 CookieGetOptions options,
                 CookieCallback callback);
  void SetCookie(std::optional<std::string> target_id,
                 CookieSetOptions options,
                 StatusCallback callback);
  void DeleteCookie(std::optional<std::string> target_id,
                    CookieDeleteOptions options,
                    StatusCallback callback);
  void ClearCookies(std::optional<std::string> target_id,
                    StatusCallback callback);

  void Shutdown();

 private:
  class BehaviorSimulator;
  class PageInstrumentation;

  content::WebContents* ResolveWebContents(
      const std::optional<std::string>& target_id) const;
  void RefreshSharedRuntimeBehavior();
  void RunOrPost(base::OnceClosure task);

  std::unique_ptr<BrowserRuntime> runtime_;
  scoped_refptr<base::SequencedTaskRunner> task_runner_;
  std::vector<std::unique_ptr<PageInstrumentation>> instrumentation_;
  std::unique_ptr<BehaviorSimulator> behavior_simulator_;
  base::WeakPtrFactory<BrowserController> weak_factory_{this};
};

}  // namespace browserd

#endif  // BROWSERD_CORE_BROWSER_CONTROLLER_H_
