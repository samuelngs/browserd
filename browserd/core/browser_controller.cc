#include "browserd/core/browser_controller.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/base64.h"
#include "base/functional/bind.h"
#include "base/json/json_writer.h"
#include "base/json/string_escape.h"
#include "base/location.h"
#include "base/memory/raw_ptr.h"
#include "base/rand_util.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "components/viz/common/frame_sinks/copy_output_result.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_widget_host.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/common/isolated_world_ids.h"
#include "net/base/net_errors.h"
#include "net/cookies/canonical_cookie.h"
#include "net/cookies/cookie_access_result.h"
#include "net/cookies/cookie_options.h"
#include "net/cookies/cookie_partition_key_collection.h"
#include "services/network/public/mojom/cookie_manager.mojom.h"
#include "third_party/blink/public/common/input/web_mouse_event.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/accessibility/ax_enum_util.h"
#include "ui/accessibility/ax_mode.h"
#include "ui/accessibility/ax_node_data.h"
#include "ui/accessibility/ax_tree_update.h"
#include "ui/base/page_transition_types.h"
#include "ui/events/base_event_utils.h"
#include "ui/gfx/codec/png_codec.h"
#include "url/gurl.h"

namespace browserd {
namespace {

constexpr char kConsoleCaptureScript[] =
    "window.__browserdLogs = [];"
    "(function() {"
    "  var orig = {"
    "    log: console.log,"
    "    warn: console.warn,"
    "    error: console.error,"
    "    info: console.info,"
    "    debug: console.debug"
    "  };"
    "  function capture(level, args) {"
    "    try {"
    "      window.__browserdLogs.push({"
    "        level: level,"
    "        text: Array.prototype.map.call(args, function(a) {"
    "          try { return typeof a === 'object' ? JSON.stringify(a) : String(a); }"
    "          catch(e) { return String(a); }"
    "        }).join(' '),"
    "        timestamp: Date.now()"
    "      });"
    "      if (window.__browserdLogs.length > 1000)"
    "        window.__browserdLogs.shift();"
    "    } catch(e) {}"
    "  }"
    "  console.log = function() { capture('log', arguments); orig.log.apply(console, arguments); };"
    "  console.warn = function() { capture('warn', arguments); orig.warn.apply(console, arguments); };"
    "  console.error = function() { capture('error', arguments); orig.error.apply(console, arguments); };"
    "  console.info = function() { capture('info', arguments); orig.info.apply(console, arguments); };"
    "  console.debug = function() { capture('debug', arguments); orig.debug.apply(console, arguments); };"
    "})();";

BrowserStatus TabNotFound(const std::optional<std::string>& target_id) {
  return BrowserStatus::Error(
      BrowserStatusCode::kTabNotFound,
      target_id.has_value() ? "Tab not found: " + target_id.value()
                            : "No active tab");
}

BrowserStatus NoActiveFrame() {
  return BrowserStatus::Error(BrowserStatusCode::kNoActiveFrame,
                              "No active frame");
}

network::mojom::CookieManager* GetCookieManager(
    content::WebContents* web_contents) {
  if (!web_contents || !web_contents->GetBrowserContext()) {
    return nullptr;
  }
  return web_contents->GetBrowserContext()
      ->GetDefaultStoragePartition()
      ->GetCookieManagerForBrowserProcess();
}

CookieInfo ToCookieInfo(const net::CanonicalCookie& cookie) {
  CookieInfo info;
  info.name = cookie.Name();
  info.value = cookie.Value();
  info.domain = cookie.Domain();
  info.path = cookie.Path();
  info.secure = cookie.IsSecure();
  info.http_only = cookie.IsHttpOnly();
  info.expires_unix_seconds =
      cookie.ExpiryDate().is_null() ? 0.0 : cookie.ExpiryDate().InSecondsFSinceUnixEpoch();
  return info;
}

}  // namespace

class BrowserController::PageInstrumentation
    : public content::WebContentsObserver {
 public:
  explicit PageInstrumentation(content::WebContents* web_contents)
      : content::WebContentsObserver(web_contents), web_contents_(web_contents) {}

  content::WebContents* observed_web_contents() const { return web_contents_; }

  void DidFinishNavigation(content::NavigationHandle* navigation_handle) override {
    if (!navigation_handle->IsInPrimaryMainFrame() ||
        !navigation_handle->HasCommitted()) {
      return;
    }

    auto* rfh = navigation_handle->GetRenderFrameHost();
    if (!rfh) {
      return;
    }

    rfh->ExecuteJavaScriptWithUserGestureForTests(
        base::UTF8ToUTF16(std::string_view(kConsoleCaptureScript)),
        base::NullCallback(),
        content::ISOLATED_WORLD_ID_GLOBAL);
  }

  void WebContentsDestroyed() override {
    web_contents_ = nullptr;
    Observe(nullptr);
  }

 private:
  raw_ptr<content::WebContents> web_contents_ = nullptr;
};

class BrowserController::BehaviorSimulator {
 public:
  BehaviorSimulator() = default;
  ~BehaviorSimulator() = default;

  void Start() {
    mouse_x_ = 400.0 + base::RandDouble() * 200.0;
    mouse_y_ = 300.0 + base::RandDouble() * 200.0;
    ScheduleNext();
  }

  void UpdateWebContents(content::WebContents* web_contents) {
    web_contents_ = web_contents;
  }

 private:
  void ScheduleNext() {
    int next_ms = 70 + base::RandInt(0, 80);
    behavior_timer_.Start(
        FROM_HERE, base::Milliseconds(next_ms),
        base::BindRepeating(&BehaviorSimulator::DispatchMouseMove,
                            base::Unretained(this)));
  }

  void DispatchMouseMove() {
    behavior_tick_++;

    double jitter_x = (base::RandDouble() - 0.5) * 3.0;
    double jitter_y = (base::RandDouble() - 0.5) * 3.0;
    double drift_x = std::sin(behavior_tick_ * 0.02) * 0.8;
    double drift_y = std::cos(behavior_tick_ * 0.017) * 0.6;

    mouse_vx_ = mouse_vx_ * 0.85 + (drift_x + jitter_x) * 0.15;
    mouse_vy_ = mouse_vy_ * 0.85 + (drift_y + jitter_y) * 0.15;
    mouse_x_ += mouse_vx_;
    mouse_y_ += mouse_vy_;

    mouse_x_ = std::clamp(mouse_x_, 10.0, 1910.0);
    mouse_y_ = std::clamp(mouse_y_, 10.0, 1070.0);

    if (web_contents_) {
      auto* rwhv = web_contents_->GetRenderWidgetHostView();
      auto* rwh = rwhv ? rwhv->GetRenderWidgetHost() : nullptr;
      if (rwh) {
        blink::WebMouseEvent event(
            blink::WebInputEvent::Type::kMouseMove,
            blink::WebInputEvent::kNoModifiers, ui::EventTimeForNow());
        event.SetPositionInWidget(static_cast<float>(mouse_x_),
                                  static_cast<float>(mouse_y_));
        event.SetPositionInScreen(static_cast<float>(mouse_x_),
                                  static_cast<float>(mouse_y_));
        rwh->ForwardMouseEvent(event);
      }
    }

    ScheduleNext();
  }

  raw_ptr<content::WebContents> web_contents_ = nullptr;
  base::RepeatingTimer behavior_timer_;
  double mouse_x_ = 0.0;
  double mouse_y_ = 0.0;
  double mouse_vx_ = 0.0;
  double mouse_vy_ = 0.0;
  int behavior_tick_ = 0;
};

namespace {

class NavigationWaiter : public content::WebContentsObserver {
 public:
  NavigationWaiter(content::WebContents* web_contents,
                   BrowserController::StatusCallback callback)
      : content::WebContentsObserver(web_contents),
        callback_(std::move(callback)) {}

  void DidStopLoading() override {
    if (callback_) {
      std::move(callback_).Run(BrowserStatus::Ok());
    }
    delete this;
  }

  void DidFailLoad(content::RenderFrameHost* rfh,
                   const GURL& validated_url,
                   int error_code) override {
    if (callback_) {
      std::move(callback_).Run(BrowserStatus::Error(
          BrowserStatusCode::kNavigationFailed,
          "Navigation failed: " + net::ErrorToString(error_code)));
    }
    delete this;
  }

 private:
  BrowserController::StatusCallback callback_;
};

class ReloadWaiter : public content::WebContentsObserver {
 public:
  ReloadWaiter(content::WebContents* web_contents,
               BrowserController::StatusCallback callback)
      : content::WebContentsObserver(web_contents),
        callback_(std::move(callback)) {}

  void DidStopLoading() override {
    if (callback_) {
      std::move(callback_).Run(BrowserStatus::Ok());
    }
    delete this;
  }

 private:
  BrowserController::StatusCallback callback_;
};

}  // namespace

BrowserController::BrowserController(
    std::unique_ptr<BrowserRuntime> runtime,
    scoped_refptr<base::SequencedTaskRunner> task_runner)
    : runtime_(std::move(runtime)), task_runner_(std::move(task_runner)) {
  behavior_simulator_ = std::make_unique<BehaviorSimulator>();
  RefreshSharedRuntimeBehavior();
  behavior_simulator_->Start();
}

BrowserController::~BrowserController() = default;

void BrowserController::RunOrPost(base::OnceClosure task) {
  if (!task_runner_ || task_runner_->RunsTasksInCurrentSequence()) {
    std::move(task).Run();
    return;
  }
  task_runner_->PostTask(FROM_HERE, std::move(task));
}

content::WebContents* BrowserController::ResolveWebContents(
    const std::optional<std::string>& target_id) const {
  if (!runtime_) {
    return nullptr;
  }
  if (target_id.has_value() && !target_id->empty()) {
    return runtime_->GetWebContentsByTargetId(target_id.value());
  }
  return runtime_->active_web_contents();
}

void BrowserController::RefreshSharedRuntimeBehavior() {
  if (!runtime_) {
    instrumentation_.clear();
    if (behavior_simulator_) {
      behavior_simulator_->UpdateWebContents(nullptr);
    }
    return;
  }

  std::vector<content::WebContents*> all_web_contents =
      runtime_->AllWebContents();
  instrumentation_.erase(
      std::remove_if(instrumentation_.begin(), instrumentation_.end(),
                     [&](const std::unique_ptr<PageInstrumentation>& observer) {
                       content::WebContents* observed =
                           observer->observed_web_contents();
                       return !observed ||
                              std::find(all_web_contents.begin(),
                                        all_web_contents.end(),
                                        observed) == all_web_contents.end();
                     }),
      instrumentation_.end());

  for (auto* web_contents : all_web_contents) {
    bool already_observed = std::any_of(
        instrumentation_.begin(), instrumentation_.end(),
        [&](const std::unique_ptr<PageInstrumentation>& observer) {
          return observer->observed_web_contents() == web_contents;
        });
    if (!already_observed) {
      instrumentation_.push_back(
          std::make_unique<PageInstrumentation>(web_contents));
    }
  }

  if (behavior_simulator_) {
    behavior_simulator_->UpdateWebContents(runtime_->active_web_contents());
  }
}

void BrowserController::ListTabs(TabsCallback callback) {
  RunOrPost(base::BindOnce(
      [](base::WeakPtr<BrowserController> self, TabsCallback cb) {
        if (!self || !self->runtime_) {
          std::move(cb).Run(BrowserResult<std::vector<BrowserTabInfo>>::Error(
              BrowserStatusCode::kSessionNotReady, "Browser session not ready"));
          return;
        }
        self->RefreshSharedRuntimeBehavior();
        std::move(cb).Run(
            BrowserResult<std::vector<BrowserTabInfo>>::Ok(
                self->runtime_->ListTabs()));
      },
      weak_factory_.GetWeakPtr(), std::move(callback)));
}

void BrowserController::CreateTab(GURL url, TabCallback callback) {
  RunOrPost(base::BindOnce(
      [](base::WeakPtr<BrowserController> self, GURL gurl, TabCallback cb) {
        if (!self || !self->runtime_) {
          std::move(cb).Run(BrowserResult<BrowserTabInfo>::Error(
              BrowserStatusCode::kSessionNotReady, "Browser session not ready"));
          return;
        }
        if (!gurl.is_valid()) {
          std::move(cb).Run(BrowserResult<BrowserTabInfo>::Error(
              BrowserStatusCode::kInvalidArgument, "Invalid URL"));
          return;
        }
        std::optional<BrowserTabInfo> tab = self->runtime_->CreateTab(gurl);
        self->RefreshSharedRuntimeBehavior();
        if (!tab.has_value()) {
          std::move(cb).Run(BrowserResult<BrowserTabInfo>::Error(
              BrowserStatusCode::kInternalError, "Failed to create tab"));
          return;
        }
        std::move(cb).Run(BrowserResult<BrowserTabInfo>::Ok(std::move(*tab)));
      },
      weak_factory_.GetWeakPtr(), std::move(url), std::move(callback)));
}

void BrowserController::CloseTab(std::optional<std::string> target_id,
                                 StatusCallback callback) {
  RunOrPost(base::BindOnce(
      [](base::WeakPtr<BrowserController> self,
         std::optional<std::string> id, StatusCallback cb) {
        if (!self || !self->runtime_) {
          std::move(cb).Run(BrowserStatus::Error(
              BrowserStatusCode::kSessionNotReady, "Browser session not ready"));
          return;
        }
        if (!self->runtime_->CloseTab(id)) {
          std::move(cb).Run(TabNotFound(id));
          return;
        }
        self->RefreshSharedRuntimeBehavior();
        std::move(cb).Run(BrowserStatus::Ok());
      },
      weak_factory_.GetWeakPtr(), std::move(target_id), std::move(callback)));
}

void BrowserController::ResizeActive(gfx::Size size,
                                     StatusCallback callback) {
  RunOrPost(base::BindOnce(
      [](base::WeakPtr<BrowserController> self, gfx::Size target_size,
         StatusCallback cb) {
        if (!self || !self->runtime_) {
          std::move(cb).Run(BrowserStatus::Error(
              BrowserStatusCode::kSessionNotReady, "Browser session not ready"));
          return;
        }
        self->runtime_->ResizeActive(target_size);
        std::move(cb).Run(BrowserStatus::Ok());
      },
      weak_factory_.GetWeakPtr(), size, std::move(callback)));
}

void BrowserController::Navigate(std::optional<std::string> target_id,
                                 GURL url,
                                 StatusCallback callback) {
  RunOrPost(base::BindOnce(
      [](base::WeakPtr<BrowserController> self,
         std::optional<std::string> id, GURL target_url, StatusCallback cb) {
        if (!self || !self->runtime_) {
          std::move(cb).Run(BrowserStatus::Error(
              BrowserStatusCode::kSessionNotReady, "Browser session not ready"));
          return;
        }
        if (!target_url.is_valid()) {
          std::move(cb).Run(BrowserStatus::Error(
              BrowserStatusCode::kInvalidArgument, "Invalid URL"));
          return;
        }
        content::WebContents* web_contents = self->ResolveWebContents(id);
        if (!web_contents) {
          std::move(cb).Run(TabNotFound(id));
          return;
        }
        new NavigationWaiter(web_contents, std::move(cb));
        content::NavigationController::LoadURLParams params(target_url);
        params.transition_type = ui::PAGE_TRANSITION_TYPED;
        web_contents->GetController().LoadURLWithParams(params);
      },
      weak_factory_.GetWeakPtr(), std::move(target_id), std::move(url),
      std::move(callback)));
}

void BrowserController::NavigateBack(std::optional<std::string> target_id,
                                     StatusCallback callback) {
  RunOrPost(base::BindOnce(
      [](base::WeakPtr<BrowserController> self,
         std::optional<std::string> id, StatusCallback cb) {
        content::WebContents* web_contents =
            self ? self->ResolveWebContents(id) : nullptr;
        if (!web_contents) {
          std::move(cb).Run(self ? TabNotFound(id) : BrowserStatus::Error(
                                      BrowserStatusCode::kSessionNotReady,
                                      "Browser session not ready"));
          return;
        }
        if (!web_contents->GetController().CanGoBack()) {
          std::move(cb).Run(BrowserStatus::Error(
              BrowserStatusCode::kNotFound, "Cannot go back"));
          return;
        }
        web_contents->GetController().GoBack();
        std::move(cb).Run(BrowserStatus::Ok());
      },
      weak_factory_.GetWeakPtr(), std::move(target_id), std::move(callback)));
}

void BrowserController::NavigateForward(std::optional<std::string> target_id,
                                        StatusCallback callback) {
  RunOrPost(base::BindOnce(
      [](base::WeakPtr<BrowserController> self,
         std::optional<std::string> id, StatusCallback cb) {
        content::WebContents* web_contents =
            self ? self->ResolveWebContents(id) : nullptr;
        if (!web_contents) {
          std::move(cb).Run(self ? TabNotFound(id) : BrowserStatus::Error(
                                      BrowserStatusCode::kSessionNotReady,
                                      "Browser session not ready"));
          return;
        }
        if (!web_contents->GetController().CanGoForward()) {
          std::move(cb).Run(BrowserStatus::Error(
              BrowserStatusCode::kNotFound, "Cannot go forward"));
          return;
        }
        web_contents->GetController().GoForward();
        std::move(cb).Run(BrowserStatus::Ok());
      },
      weak_factory_.GetWeakPtr(), std::move(target_id), std::move(callback)));
}

void BrowserController::Reload(std::optional<std::string> target_id,
                               StatusCallback callback) {
  RunOrPost(base::BindOnce(
      [](base::WeakPtr<BrowserController> self,
         std::optional<std::string> id, StatusCallback cb) {
        content::WebContents* web_contents =
            self ? self->ResolveWebContents(id) : nullptr;
        if (!web_contents) {
          std::move(cb).Run(self ? TabNotFound(id) : BrowserStatus::Error(
                                      BrowserStatusCode::kSessionNotReady,
                                      "Browser session not ready"));
          return;
        }
        new ReloadWaiter(web_contents, std::move(cb));
        web_contents->GetController().Reload(content::ReloadType::NORMAL,
                                             false);
      },
      weak_factory_.GetWeakPtr(), std::move(target_id), std::move(callback)));
}

void BrowserController::Evaluate(std::optional<std::string> target_id,
                                 std::string expression,
                                 EvaluateCallback callback) {
  RunOrPost(base::BindOnce(
      [](base::WeakPtr<BrowserController> self,
         std::optional<std::string> id, std::string js,
         EvaluateCallback cb) {
        content::WebContents* web_contents =
            self ? self->ResolveWebContents(id) : nullptr;
        if (!web_contents) {
          std::move(cb).Run(BrowserResult<EvaluateResult>::Error(
              self ? BrowserStatusCode::kTabNotFound
                   : BrowserStatusCode::kSessionNotReady,
              self ? TabNotFound(id).message : "Browser session not ready"));
          return;
        }
        auto* rfh = web_contents->GetPrimaryMainFrame();
        if (!rfh) {
          std::move(cb).Run(BrowserResult<EvaluateResult>::Error(
              BrowserStatusCode::kNoActiveFrame, "No active frame"));
          return;
        }
        rfh->ExecuteJavaScriptForTests(
            base::UTF8ToUTF16(js),
            base::BindOnce(
                [](EvaluateCallback callback, base::Value result) {
                  EvaluateResult eval;
                  if (result.is_none()) {
                    eval.kind = EvaluateResult::Kind::kUndefined;
                    std::move(callback).Run(
                        BrowserResult<EvaluateResult>::Ok(std::move(eval)));
                    return;
                  }
                  eval.kind = EvaluateResult::Kind::kJson;
                  base::JSONWriter::Write(result, &eval.json);
                  std::move(callback).Run(
                      BrowserResult<EvaluateResult>::Ok(std::move(eval)));
                },
                std::move(cb)),
            content::ISOLATED_WORLD_ID_GLOBAL);
      },
      weak_factory_.GetWeakPtr(), std::move(target_id), std::move(expression),
      std::move(callback)));
}

void BrowserController::ConsoleMessages(std::optional<std::string> target_id,
                                        SnapshotCallback callback) {
  RunOrPost(base::BindOnce(
      [](base::WeakPtr<BrowserController> self,
         std::optional<std::string> id, SnapshotCallback cb) {
        content::WebContents* web_contents =
            self ? self->ResolveWebContents(id) : nullptr;
        if (!web_contents) {
          std::move(cb).Run(BrowserResult<std::string>::Error(
              self ? BrowserStatusCode::kTabNotFound
                   : BrowserStatusCode::kSessionNotReady,
              self ? TabNotFound(id).message : "Browser session not ready"));
          return;
        }
        auto* rfh = web_contents->GetPrimaryMainFrame();
        if (!rfh) {
          std::move(cb).Run(BrowserResult<std::string>::Error(
              BrowserStatusCode::kNoActiveFrame, "No active frame"));
          return;
        }
        rfh->ExecuteJavaScriptForTests(
            u"(() => {"
            u"  if (!window.__browserdLogs) return '[]';"
            u"  return JSON.stringify(window.__browserdLogs);"
            u"})()",
            base::BindOnce(
                [](SnapshotCallback callback, base::Value result) {
                  if (result.is_string()) {
                    std::move(callback).Run(
                        BrowserResult<std::string>::Ok(result.GetString()));
                    return;
                  }
                  std::move(callback).Run(
                      BrowserResult<std::string>::Ok("[]"));
                },
                std::move(cb)),
            content::ISOLATED_WORLD_ID_GLOBAL);
      },
      weak_factory_.GetWeakPtr(), std::move(target_id), std::move(callback)));
}

void BrowserController::Snapshot(std::optional<std::string> target_id,
                                 SnapshotCallback callback) {
  RunOrPost(base::BindOnce(
      [](base::WeakPtr<BrowserController> self,
         std::optional<std::string> id, SnapshotCallback cb) {
        content::WebContents* web_contents =
            self ? self->ResolveWebContents(id) : nullptr;
        if (!web_contents) {
          std::move(cb).Run(BrowserResult<std::string>::Error(
              self ? BrowserStatusCode::kTabNotFound
                   : BrowserStatusCode::kSessionNotReady,
              self ? TabNotFound(id).message : "Browser session not ready"));
          return;
        }
        web_contents->RequestAXTreeSnapshot(
            base::BindOnce(
                [](SnapshotCallback callback, ui::AXTreeUpdate& tree) {
                  std::string tree_text;
                  for (const auto& node : tree.nodes) {
                    const char* role_str = ui::ToString(node.role);
                    if (!role_str) {
                      continue;
                    }
                    std::string role(role_str);
                    if (role == "none" || role == "generic" ||
                        role == "genericContainer" ||
                        role == "inlineTextBox") {
                      continue;
                    }
                    tree_text += role;
                    std::string name;
                    if (node.HasStringAttribute(
                            ax::mojom::StringAttribute::kName)) {
                      name = node.GetStringAttribute(
                          ax::mojom::StringAttribute::kName);
                    }
                    if (!name.empty()) {
                      tree_text += " \"" + name + "\"";
                    }
                    tree_text +=
                        " [ref=" + base::NumberToString(node.id) + "]";
                    tree_text += "\n";
                  }
                  std::move(callback).Run(
                      BrowserResult<std::string>::Ok(std::move(tree_text)));
                },
                std::move(cb)),
            ui::AXMode::kWebContents, /*max_nodes=*/0,
            /*timeout=*/base::Seconds(5),
            content::WebContents::AXTreeSnapshotPolicy::kAll);
      },
      weak_factory_.GetWeakPtr(), std::move(target_id), std::move(callback)));
}

void BrowserController::Screenshot(std::optional<std::string> target_id,
                                   ScreenshotCallback callback) {
  RunOrPost(base::BindOnce(
      [](base::WeakPtr<BrowserController> self,
         std::optional<std::string> id, ScreenshotCallback cb) {
        content::WebContents* web_contents =
            self ? self->ResolveWebContents(id) : nullptr;
        if (!web_contents) {
          std::move(cb).Run(BrowserResult<std::vector<uint8_t>>::Error(
              self ? BrowserStatusCode::kTabNotFound
                   : BrowserStatusCode::kSessionNotReady,
              self ? TabNotFound(id).message : "Browser session not ready"));
          return;
        }
        auto* rwhv = web_contents->GetRenderWidgetHostView();
        if (!rwhv) {
          std::move(cb).Run(BrowserResult<std::vector<uint8_t>>::Error(
              BrowserStatusCode::kScreenshotFailed,
              "No render view available"));
          return;
        }
        rwhv->CopyFromSurface(
            gfx::Rect(), gfx::Size(), base::Seconds(5),
            base::BindOnce(
                [](ScreenshotCallback callback,
                   const content::CopyFromSurfaceResult& result) {
                  if (!result.has_value()) {
                    std::move(callback).Run(
                        BrowserResult<std::vector<uint8_t>>::Error(
                            BrowserStatusCode::kScreenshotFailed,
                            "Screenshot failed"));
                    return;
                  }
                  const SkBitmap& bitmap = result.value().bitmap;
                  if (bitmap.drawsNothing()) {
                    std::move(callback).Run(
                        BrowserResult<std::vector<uint8_t>>::Error(
                            BrowserStatusCode::kScreenshotFailed,
                            "Screenshot is empty"));
                    return;
                  }
                  auto png_data =
                      gfx::PNGCodec::EncodeBGRASkBitmap(bitmap, false);
                  if (!png_data) {
                    std::move(callback).Run(
                        BrowserResult<std::vector<uint8_t>>::Error(
                            BrowserStatusCode::kScreenshotFailed,
                            "PNG encoding failed"));
                    return;
                  }
                  std::move(callback).Run(
                      BrowserResult<std::vector<uint8_t>>::Ok(
                          std::move(*png_data)));
                },
                std::move(cb)));
      },
      weak_factory_.GetWeakPtr(), std::move(target_id), std::move(callback)));
}

void BrowserController::WaitFor(std::optional<std::string> target_id,
                                WaitOptions options,
                                StatusCallback callback) {
  RunOrPost(base::BindOnce(
      [](base::WeakPtr<BrowserController> self,
         std::optional<std::string> id, WaitOptions wait_options,
         StatusCallback cb) {
        content::WebContents* web_contents =
            self ? self->ResolveWebContents(id) : nullptr;
        if (!web_contents) {
          std::move(cb).Run(self ? TabNotFound(id) : BrowserStatus::Error(
                                      BrowserStatusCode::kSessionNotReady,
                                      "Browser session not ready"));
          return;
        }
        if (!wait_options.selector.has_value() &&
            !wait_options.text.has_value()) {
          std::move(cb).Run(BrowserStatus::Error(
              BrowserStatusCode::kInvalidArgument,
              "Must provide either selector or text to wait for"));
          return;
        }
        int timeout = wait_options.timeout_ms > 0 ? wait_options.timeout_ms
                                                  : 30000;
        std::string expression;
        if (wait_options.selector.has_value()) {
          expression =
              "new Promise((resolve, reject) => {"
              "  const timeout = " +
              base::NumberToString(timeout) +
              ";"
              "  const selector = " +
              base::GetQuotedJSONString(wait_options.selector.value()) +
              ";"
              "  const start = Date.now();"
              "  const check = () => {"
              "    const el = document.querySelector(selector);"
              "    if (el) { resolve('found'); return; }"
              "    if (Date.now() - start > timeout) { reject(new Error('timeout')); return; }"
              "    setTimeout(check, 100);"
              "  };"
              "  check();"
              "})";
        } else {
          expression =
              "new Promise((resolve, reject) => {"
              "  const timeout = " +
              base::NumberToString(timeout) +
              ";"
              "  const text = " +
              base::GetQuotedJSONString(wait_options.text.value()) +
              ";"
              "  const start = Date.now();"
              "  const check = () => {"
              "    if (document.body && document.body.innerText.includes(text)) { resolve('found'); return; }"
              "    if (Date.now() - start > timeout) { reject(new Error('timeout')); return; }"
              "    setTimeout(check, 100);"
              "  };"
              "  check();"
              "})";
        }

        auto* rfh = web_contents->GetPrimaryMainFrame();
        if (!rfh) {
          std::move(cb).Run(NoActiveFrame());
          return;
        }
        rfh->ExecuteJavaScriptForTests(
            base::UTF8ToUTF16(expression),
            base::BindOnce(
                [](StatusCallback callback, base::Value result) {
                  if (result.is_string() && result.GetString() == "found") {
                    std::move(callback).Run(BrowserStatus::Ok());
                  } else {
                    std::move(callback).Run(BrowserStatus::Error(
                        BrowserStatusCode::kTimeout, "Wait timeout"));
                  }
                },
                std::move(cb)),
            content::ISOLATED_WORLD_ID_GLOBAL);
      },
      weak_factory_.GetWeakPtr(), std::move(target_id), std::move(options),
      std::move(callback)));
}

void BrowserController::ListCookies(std::optional<std::string> target_id,
                                    CookieListOptions options,
                                    CookiesCallback callback) {
  RunOrPost(base::BindOnce(
      [](base::WeakPtr<BrowserController> self,
         std::optional<std::string> id, CookieListOptions list_options,
         CookiesCallback cb) {
        content::WebContents* web_contents =
            self ? self->ResolveWebContents(id) : nullptr;
        if (!web_contents) {
          std::move(cb).Run(BrowserResult<std::vector<CookieInfo>>::Error(
              self ? BrowserStatusCode::kTabNotFound
                   : BrowserStatusCode::kSessionNotReady,
              self ? TabNotFound(id).message : "Browser session not ready"));
          return;
        }
        auto* cm = GetCookieManager(web_contents);
        if (!cm) {
          std::move(cb).Run(BrowserResult<std::vector<CookieInfo>>::Error(
              BrowserStatusCode::kInternalError, "No cookie manager"));
          return;
        }
        if (list_options.url.has_value()) {
          GURL url(list_options.url.value());
          cm->GetCookieList(
              url, net::CookieOptions::MakeAllInclusive(),
              net::CookiePartitionKeyCollection(),
              base::BindOnce(
                  [](CookiesCallback callback,
                     const net::CookieAccessResultList& included,
                     const net::CookieAccessResultList&) {
                    std::vector<CookieInfo> cookies;
                    for (const auto& cookie : included) {
                      cookies.push_back(ToCookieInfo(cookie.cookie));
                    }
                    std::move(callback).Run(
                        BrowserResult<std::vector<CookieInfo>>::Ok(
                            std::move(cookies)));
                  },
                  std::move(cb)));
          return;
        }
        cm->GetAllCookies(base::BindOnce(
            [](CookiesCallback callback,
               const std::vector<net::CanonicalCookie>& cookie_list) {
              std::vector<CookieInfo> cookies;
              for (const auto& cookie : cookie_list) {
                cookies.push_back(ToCookieInfo(cookie));
              }
              std::move(callback).Run(
                  BrowserResult<std::vector<CookieInfo>>::Ok(
                      std::move(cookies)));
            },
            std::move(cb)));
      },
      weak_factory_.GetWeakPtr(), std::move(target_id), std::move(options),
      std::move(callback)));
}

void BrowserController::GetCookie(std::optional<std::string> target_id,
                                  CookieGetOptions options,
                                  CookieCallback callback) {
  ListCookies(std::move(target_id), CookieListOptions(),
              base::BindOnce(
                  [](std::string name, CookieCallback cb,
                     BrowserResult<std::vector<CookieInfo>> result) {
                    if (!result.ok()) {
                      std::move(cb).Run(BrowserResult<CookieInfo>::Error(
                          result.status.code, result.status.message));
                      return;
                    }
                    for (auto& cookie : result.value.value()) {
                      if (cookie.name == name) {
                        std::move(cb).Run(
                            BrowserResult<CookieInfo>::Ok(std::move(cookie)));
                        return;
                      }
                    }
                    std::move(cb).Run(BrowserResult<CookieInfo>::Error(
                        BrowserStatusCode::kNotFound,
                        "Cookie '" + name + "' not found"));
                  },
                  std::move(options.name), std::move(callback)));
}

void BrowserController::SetCookie(std::optional<std::string> target_id,
                                  CookieSetOptions options,
                                  StatusCallback callback) {
  RunOrPost(base::BindOnce(
      [](base::WeakPtr<BrowserController> self,
         std::optional<std::string> id, CookieSetOptions cookie_options,
         StatusCallback cb) {
        content::WebContents* web_contents =
            self ? self->ResolveWebContents(id) : nullptr;
        if (!web_contents) {
          std::move(cb).Run(self ? TabNotFound(id) : BrowserStatus::Error(
                                      BrowserStatusCode::kSessionNotReady,
                                      "Browser session not ready"));
          return;
        }
        auto* cm = GetCookieManager(web_contents);
        if (!cm) {
          std::move(cb).Run(BrowserStatus::Error(
              BrowserStatusCode::kInternalError, "No cookie manager"));
          return;
        }

        GURL source_url(cookie_options.url.value_or(
            web_contents->GetLastCommittedURL().spec()));
        std::string domain = cookie_options.domain.value_or("");
        std::string path = cookie_options.path.value_or("/");
        auto cookie = net::CanonicalCookie::CreateSanitizedCookie(
            source_url, cookie_options.name, cookie_options.value, domain, path,
            base::Time::Now(), base::Time::Now() + base::Days(365),
            base::Time::Now(), cookie_options.secure, cookie_options.http_only,
            net::CookieSameSite::LAX_MODE, net::COOKIE_PRIORITY_DEFAULT,
            std::nullopt, nullptr);
        if (!cookie) {
          std::move(cb).Run(BrowserStatus::Error(
              BrowserStatusCode::kInvalidArgument, "Invalid cookie"));
          return;
        }
        cm->SetCanonicalCookie(
            *cookie, source_url, net::CookieOptions::MakeAllInclusive(),
            base::BindOnce(
                [](StatusCallback callback, net::CookieAccessResult result) {
                  if (result.status.IsInclude()) {
                    std::move(callback).Run(BrowserStatus::Ok());
                  } else {
                    std::move(callback).Run(BrowserStatus::Error(
                        BrowserStatusCode::kInternalError,
                        "Failed to set cookie"));
                  }
                },
                std::move(cb)));
      },
      weak_factory_.GetWeakPtr(), std::move(target_id), std::move(options),
      std::move(callback)));
}

void BrowserController::DeleteCookie(std::optional<std::string> target_id,
                                     CookieDeleteOptions options,
                                     StatusCallback callback) {
  RunOrPost(base::BindOnce(
      [](base::WeakPtr<BrowserController> self,
         std::optional<std::string> id, CookieDeleteOptions cookie_options,
         StatusCallback cb) {
        content::WebContents* web_contents =
            self ? self->ResolveWebContents(id) : nullptr;
        if (!web_contents) {
          std::move(cb).Run(self ? TabNotFound(id) : BrowserStatus::Error(
                                      BrowserStatusCode::kSessionNotReady,
                                      "Browser session not ready"));
          return;
        }
        auto* cm = GetCookieManager(web_contents);
        if (!cm) {
          std::move(cb).Run(BrowserStatus::Error(
              BrowserStatusCode::kInternalError, "No cookie manager"));
          return;
        }
        auto filter = network::mojom::CookieDeletionFilter::New();
        filter->cookie_name = cookie_options.name;
        if (cookie_options.url.has_value()) {
          filter->url = GURL(cookie_options.url.value());
        }
        if (cookie_options.domain.has_value()) {
          filter->host_name = cookie_options.domain.value();
        }
        cm->DeleteCookies(
            std::move(filter),
            base::BindOnce(
                [](StatusCallback callback, uint32_t deleted_count) {
                  if (deleted_count > 0) {
                    std::move(callback).Run(BrowserStatus::Ok());
                  } else {
                    std::move(callback).Run(BrowserStatus::Error(
                        BrowserStatusCode::kNotFound,
                        "Cookie not found"));
                  }
                },
                std::move(cb)));
      },
      weak_factory_.GetWeakPtr(), std::move(target_id), std::move(options),
      std::move(callback)));
}

void BrowserController::ClearCookies(std::optional<std::string> target_id,
                                     StatusCallback callback) {
  RunOrPost(base::BindOnce(
      [](base::WeakPtr<BrowserController> self,
         std::optional<std::string> id, StatusCallback cb) {
        content::WebContents* web_contents =
            self ? self->ResolveWebContents(id) : nullptr;
        if (!web_contents) {
          std::move(cb).Run(self ? TabNotFound(id) : BrowserStatus::Error(
                                      BrowserStatusCode::kSessionNotReady,
                                      "Browser session not ready"));
          return;
        }
        auto* cm = GetCookieManager(web_contents);
        if (!cm) {
          std::move(cb).Run(BrowserStatus::Error(
              BrowserStatusCode::kInternalError, "No cookie manager"));
          return;
        }
        cm->DeleteCookies(
            network::mojom::CookieDeletionFilter::New(),
            base::BindOnce(
                [](StatusCallback callback, uint32_t) {
                  std::move(callback).Run(BrowserStatus::Ok());
                },
                std::move(cb)));
      },
      weak_factory_.GetWeakPtr(), std::move(target_id), std::move(callback)));
}

void BrowserController::Shutdown() {
  RunOrPost(base::BindOnce(
      [](base::WeakPtr<BrowserController> self) {
        if (!self || !self->runtime_) {
          return;
        }
        self->runtime_->Shutdown();
      },
      weak_factory_.GetWeakPtr()));
}

}  // namespace browserd
