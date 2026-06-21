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
#include "base/containers/span.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/json/string_escape.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "base/rand_util.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "browserd/core/devtools_evaluator.h"
#include "components/viz/common/frame_sinks/copy_output_result.h"
#include "content/browser/renderer_host/render_frame_host_impl.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/devtools_agent_host.h"
#include "content/public/browser/devtools_agent_host_client.h"
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
#include "third_party/blink/public/mojom/frame/frame.mojom.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/accessibility/ax_enum_util.h"
#include "ui/accessibility/ax_mode.h"
#include "ui/accessibility/ax_node_data.h"
#include "ui/accessibility/ax_tree_update.h"
#include "ui/base/page_transition_types.h"
#include "ui/events/base_event_utils.h"
#include "ui/gfx/image/image.h"
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

constexpr char kDomSnapshotScript[] = R"JS(
(() => {
  const refs = [];
  const lines = [];

  function clean(text) {
    return String(text || '')
      .replace(/\s+/g, ' ')
      .trim()
      .slice(0, 160);
  }

  function quoted(text) {
    return clean(text).replace(/[\\"]/g, '\\$&');
  }

  function isVisible(el) {
    if (!el || el.nodeType !== Node.ELEMENT_NODE) {
      return false;
    }
    const style = getComputedStyle(el);
    if (style.display === 'none' || style.visibility === 'hidden') {
      return false;
    }
    const rect = el.getBoundingClientRect();
    return rect.width > 0 && rect.height > 0;
  }

  function roleFor(el) {
    const explicit = el.getAttribute('role');
    if (explicit) {
      return explicit;
    }
    const tag = el.tagName.toLowerCase();
    if (tag === 'a') return 'link';
    if (tag === 'button') return 'button';
    if (tag === 'select') return 'combobox';
    if (tag === 'textarea') return 'textbox';
    if (tag === 'input') {
      const type = (el.getAttribute('type') || 'text').toLowerCase();
      if (type === 'checkbox') return 'checkbox';
      if (type === 'radio') return 'radio';
      if (type === 'button' || type === 'submit' || type === 'reset') {
        return 'button';
      }
      return 'textbox';
    }
    if (/^h[1-6]$/.test(tag)) return 'heading';
    if (tag === 'img') return 'img';
    if (tag === 'summary') return 'button';
    if (el.isContentEditable) return 'textbox';
    return 'generic';
  }

  function nameFor(el) {
    return clean(
      el.getAttribute('aria-label') ||
      el.getAttribute('alt') ||
      el.getAttribute('title') ||
      el.getAttribute('placeholder') ||
      el.value ||
      el.innerText ||
      el.textContent ||
      el.href ||
      el.id ||
      el.name ||
      ''
    );
  }

  function add(el, roleOverride) {
    if (!el || refs.includes(el)) {
      return;
    }
    const ref = refs.length;
    refs.push(el);
    const role = roleOverride || roleFor(el);
    const name = nameFor(el);
    lines.push(role + (name ? ' "' + quoted(name) + '"' : '') + ' [ref=dom:' + ref + ']');
  }

  add(document.body || document.documentElement, 'document');
  const selector = [
    'a[href]',
    'button',
    'input',
    'textarea',
    'select',
    '[role]',
    'summary',
    '[contenteditable=""]',
    '[contenteditable="true"]',
    '[tabindex]:not([tabindex="-1"])',
    'h1',
    'h2',
    'h3',
    'h4',
    'h5',
    'h6',
    'img'
  ].join(',');

  for (const el of Array.from(document.querySelectorAll(selector))) {
    if (refs.length >= 500) {
      break;
    }
    if (!isVisible(el)) {
      continue;
    }
    add(el);
  }

  window.__browserdDomRefs = refs;
  return lines.join('\n') + '\n';
})()
)JS";

constexpr char kCanvasScreenshotScript[] = R"JS(
(() => new Promise((resolve) => {
  let finished = false;
  const finish = (value) => {
    if (finished) return;
    finished = true;
    resolve(value || '');
  };

  try {
    const width = Math.max(1, Math.ceil(window.innerWidth || document.documentElement.clientWidth || 1));
    const height = Math.max(1, Math.ceil(window.innerHeight || document.documentElement.clientHeight || 1));
    const clone = document.documentElement.cloneNode(true);
    const props = [
      'display', 'position', 'box-sizing', 'width', 'height', 'margin',
      'padding', 'border', 'background', 'background-color', 'color',
      'font', 'font-family', 'font-size', 'font-weight', 'line-height',
      'text-align', 'vertical-align', 'white-space', 'overflow',
      'border-radius', 'box-shadow', 'opacity'
    ];

    function inlineStyles(source, target) {
      if (!source || !target || source.nodeType !== Node.ELEMENT_NODE) return;
      const computed = getComputedStyle(source);
      let style = target.getAttribute('style') || '';
      for (const prop of props) {
        style += prop + ':' + computed.getPropertyValue(prop) + ';';
      }
      target.setAttribute('style', style);
      const sourceChildren = source.children || [];
      const targetChildren = target.children || [];
      for (let i = 0; i < sourceChildren.length && i < targetChildren.length; i++) {
        inlineStyles(sourceChildren[i], targetChildren[i]);
      }
    }

    inlineStyles(document.documentElement, clone);
    clone.setAttribute('xmlns', 'http://www.w3.org/1999/xhtml');
    const html = new XMLSerializer().serializeToString(clone);
    const svg =
      '<svg xmlns="http://www.w3.org/2000/svg" width="' + width +
      '" height="' + height + '">' +
      '<foreignObject width="100%" height="100%">' + html + '</foreignObject>' +
      '</svg>';
    const image = new Image();
    image.onload = () => {
      try {
        const canvas = document.createElement('canvas');
        canvas.width = width;
        canvas.height = height;
        const ctx = canvas.getContext('2d');
        ctx.drawImage(image, 0, 0);
        finish(canvas.toDataURL('image/png'));
      } catch (e) {
        finish('');
      }
    };
    image.onerror = () => finish('');
    image.src = 'data:image/svg+xml;charset=utf-8,' + encodeURIComponent(svg);
    setTimeout(() => finish(''), 5000);
  } catch (e) {
    finish('');
  }
}))()
)JS";

constexpr int kDevToolsAttachCommandId = 1;
constexpr int kDevToolsEvaluateCommandId = 2;

class DevToolsEvaluationClient final
    : public content::DevToolsAgentHostClient {
 public:
  DevToolsEvaluationClient(content::WebContents* web_contents,
                           std::string expression,
                           bool await_promise,
                           base::TimeDelta timeout,
                           DevToolsEvaluationCallback callback);
  ~DevToolsEvaluationClient() override;

  DevToolsEvaluationClient(const DevToolsEvaluationClient&) = delete;
  DevToolsEvaluationClient& operator=(const DevToolsEvaluationClient&) =
      delete;

  void Start();

  void DispatchProtocolMessage(
      content::DevToolsAgentHost* agent_host,
      base::span<const uint8_t> protocol_message) override;
  void AgentHostClosed(content::DevToolsAgentHost* agent_host) override;
  bool MayAttachToURL(const GURL& url, bool is_webui) override;
  bool MayAttachToRenderFrameHost(
      content::RenderFrameHost* render_frame_host) override;
  bool IsTrusted() override;
  bool MayAccessAllCookies() override;
  bool MayReadLocalFiles() override;
  bool MayWriteLocalFiles() override;
  bool AllowUnsafeOperations() override;

 private:
  void SendAttachToFrameTarget();
  void SendEvaluate();
  void CompleteProtocolError(const base::DictValue& error,
                             std::string_view fallback_message);
  void OnTimeout();
  void Complete(BrowserResult<DevToolsEvaluationResult> result);

  raw_ptr<content::WebContents> web_contents_ = nullptr;
  scoped_refptr<content::DevToolsAgentHost> agent_host_;
  scoped_refptr<content::DevToolsAgentHost> frame_agent_host_;
  std::string frame_target_id_;
  std::string session_id_;
  std::string expression_;
  bool await_promise_ = false;
  base::TimeDelta timeout_;
  DevToolsEvaluationCallback callback_;
  base::WeakPtrFactory<DevToolsEvaluationClient> weak_factory_{this};
};

class JavaScriptEvaluationRequest final {
 public:
  JavaScriptEvaluationRequest(content::WebContents* web_contents,
                              std::string expression,
                              bool await_promise,
                              base::TimeDelta timeout,
                              DevToolsEvaluationCallback callback);
  ~JavaScriptEvaluationRequest();

  JavaScriptEvaluationRequest(const JavaScriptEvaluationRequest&) = delete;
  JavaScriptEvaluationRequest& operator=(const JavaScriptEvaluationRequest&) =
      delete;

  void Start();

 private:
  void OnResult(blink::mojom::JavaScriptExecutionResultType result_type,
                base::Value value);
  void OnTimeout();
  void Complete(BrowserResult<DevToolsEvaluationResult> result);

  raw_ptr<content::WebContents> web_contents_ = nullptr;
  std::string expression_;
  bool await_promise_ = false;
  base::TimeDelta timeout_;
  DevToolsEvaluationCallback callback_;
  base::OneShotTimer timeout_timer_;
  base::WeakPtrFactory<JavaScriptEvaluationRequest> weak_factory_{this};
};

class ScreenshotCaptureRequest final {
 public:
  ScreenshotCaptureRequest(content::WebContents* web_contents,
                           content::RenderWidgetHostView* view,
                           BrowserController::ScreenshotCallback callback);
  ~ScreenshotCaptureRequest();

  ScreenshotCaptureRequest(const ScreenshotCaptureRequest&) = delete;
  ScreenshotCaptureRequest& operator=(const ScreenshotCaptureRequest&) =
      delete;

  void Start();

 private:
  void OnSnapshotFromBrowser(const gfx::Image& image);
  void OnCopyFromSurfaceResult(const content::CopyFromSurfaceResult& result);
  void OnTimeout();
  void StartDomFallback();
  void OnDomFallback(BrowserResult<DevToolsEvaluationResult> result);
  void Complete(BrowserResult<std::vector<uint8_t>> result);

  raw_ptr<content::WebContents> web_contents_ = nullptr;
  raw_ptr<content::RenderWidgetHostView> view_ = nullptr;
  BrowserController::ScreenshotCallback callback_;
  base::OneShotTimer timeout_timer_;
  base::WeakPtrFactory<ScreenshotCaptureRequest> weak_factory_{this};
};

BrowserStatus TabNotFound(const std::optional<std::string>& target_id) {
  return BrowserStatus::Error(
      BrowserStatusCode::kTabNotFound,
      target_id.has_value() ? "Tab not found: " + target_id.value()
                            : "No active tab");
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

EvaluateResult ToEvaluateResult(const DevToolsEvaluationResult& result) {
  EvaluateResult eval;
  if (result.is_undefined || result.value.is_none()) {
    eval.kind = EvaluateResult::Kind::kUndefined;
    return eval;
  }

  eval.kind = EvaluateResult::Kind::kJson;
  base::JSONWriter::Write(result.value, &eval.json);
  return eval;
}

DevToolsEvaluationClient::DevToolsEvaluationClient(
    content::WebContents* web_contents,
    std::string expression,
    bool await_promise,
    base::TimeDelta timeout,
    DevToolsEvaluationCallback callback)
    : web_contents_(web_contents),
      expression_(std::move(expression)),
      await_promise_(await_promise),
      timeout_(timeout),
      callback_(std::move(callback)) {}

DevToolsEvaluationClient::~DevToolsEvaluationClient() = default;

void DevToolsEvaluationClient::Start() {
  if (!web_contents_) {
    Complete(BrowserResult<DevToolsEvaluationResult>::Error(
        BrowserStatusCode::kTabNotFound, "Tab not found"));
    return;
  }

  LOG(INFO) << "browserd DevTools evaluate start url="
            << web_contents_->GetLastCommittedURL()
            << " visible=" << static_cast<int>(web_contents_->GetVisibility())
            << " frame_live="
            << (web_contents_->GetPrimaryMainFrame() &&
                web_contents_->GetPrimaryMainFrame()->IsRenderFrameLive());
  frame_agent_host_ = content::DevToolsAgentHost::GetOrCreateFor(web_contents_);
  if (!frame_agent_host_) {
    Complete(BrowserResult<DevToolsEvaluationResult>::Error(
        BrowserStatusCode::kInternalError,
        "Failed to create DevTools frame target"));
    return;
  }
  frame_target_id_ = frame_agent_host_->GetId();
  LOG(INFO) << "browserd DevTools frame target id=" << frame_target_id_
            << " type=" << frame_agent_host_->GetType()
            << " url=" << frame_agent_host_->GetURL();

  agent_host_ = content::DevToolsAgentHost::GetOrCreateForTab(web_contents_);
  if (!agent_host_) {
    Complete(BrowserResult<DevToolsEvaluationResult>::Error(
        BrowserStatusCode::kInternalError,
        "Failed to create DevTools agent host"));
    return;
  }

  LOG(INFO) << "browserd DevTools agent created id="
            << agent_host_->GetId() << " type=" << agent_host_->GetType()
            << " url=" << agent_host_->GetURL();
  if (!agent_host_->AttachClient(this)) {
    agent_host_ = nullptr;
    Complete(BrowserResult<DevToolsEvaluationResult>::Error(
        BrowserStatusCode::kInternalError,
        "Failed to attach DevTools client"));
    return;
  }
  LOG(INFO) << "browserd DevTools client attached";

  if (timeout_.is_positive()) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(&DevToolsEvaluationClient::OnTimeout,
                       weak_factory_.GetWeakPtr()),
        timeout_);
  }

  SendAttachToFrameTarget();
}

void DevToolsEvaluationClient::DispatchProtocolMessage(
    content::DevToolsAgentHost*,
    base::span<const uint8_t> protocol_message) {
  LOG(INFO) << "browserd DevTools protocol message bytes="
            << protocol_message.size();
  std::string_view message_str(
      reinterpret_cast<const char*>(protocol_message.data()),
      protocol_message.size());
  std::optional<base::Value> parsed = base::JSONReader::Read(
      message_str, base::JSON_PARSE_CHROMIUM_EXTENSIONS);
  if (!parsed.has_value() || !parsed->is_dict()) {
    return;
  }

  base::DictValue& dict = parsed->GetDict();
  std::optional<int> id = dict.FindInt("id");
  if (!id.has_value()) {
    return;
  }

  if (id.value() == kDevToolsAttachCommandId) {
    if (const base::DictValue* error = dict.FindDict("error")) {
      CompleteProtocolError(*error, "DevTools attach failed");
      return;
    }
    const base::DictValue* result = dict.FindDict("result");
    const std::string* session_id =
        result ? result->FindString("sessionId") : nullptr;
    if (!session_id || session_id->empty()) {
      Complete(BrowserResult<DevToolsEvaluationResult>::Error(
          BrowserStatusCode::kInternalError,
          "DevTools attach returned no session id"));
      return;
    }
    session_id_ = *session_id;
    LOG(INFO) << "browserd DevTools attached to frame session=" << session_id_;
    SendEvaluate();
    return;
  }

  if (id.value() != kDevToolsEvaluateCommandId) {
    return;
  }

  if (const base::DictValue* error = dict.FindDict("error")) {
    CompleteProtocolError(*error, "DevTools evaluation failed");
    return;
  }

  const base::DictValue* result = dict.FindDict("result");
  if (!result) {
    Complete(BrowserResult<DevToolsEvaluationResult>::Error(
        BrowserStatusCode::kInternalError,
        "DevTools evaluation returned no result"));
    return;
  }

  if (const base::DictValue* exception =
          result->FindDict("exceptionDetails")) {
    std::string exception_message = "DevTools evaluation exception";
    if (const base::DictValue* details = exception->FindDict("exception")) {
      if (const std::string* description =
              details->FindString("description")) {
        exception_message = *description;
      } else if (const std::string* value = details->FindString("value")) {
        exception_message = *value;
      }
    } else if (const std::string* text = exception->FindString("text")) {
      exception_message = *text;
    }
    Complete(BrowserResult<DevToolsEvaluationResult>::Error(
        BrowserStatusCode::kInternalError, std::move(exception_message)));
    return;
  }

  const base::DictValue* remote_object = result->FindDict("result");
  if (!remote_object) {
    Complete(BrowserResult<DevToolsEvaluationResult>::Error(
        BrowserStatusCode::kInternalError,
        "DevTools evaluation returned no remote object"));
    return;
  }

  DevToolsEvaluationResult evaluation;
  const std::string* type = remote_object->FindString("type");
  if (type && *type == "undefined") {
    evaluation.is_undefined = true;
    Complete(
        BrowserResult<DevToolsEvaluationResult>::Ok(std::move(evaluation)));
    return;
  }

  if (const base::Value* value = remote_object->Find("value")) {
    evaluation.value = value->Clone();
  } else if (const std::string* unserializable =
                 remote_object->FindString("unserializableValue")) {
    evaluation.value = base::Value(*unserializable);
  } else if (const std::string* description =
                 remote_object->FindString("description")) {
    evaluation.value = base::Value(*description);
  } else {
    evaluation.is_undefined = true;
  }

  Complete(BrowserResult<DevToolsEvaluationResult>::Ok(
      std::move(evaluation)));
}

void DevToolsEvaluationClient::SendAttachToFrameTarget() {
  base::DictValue params;
  params.Set("targetId", frame_target_id_);
  params.Set("flatten", true);

  base::DictValue command;
  command.Set("id", kDevToolsAttachCommandId);
  command.Set("method", "Target.attachToTarget");
  command.Set("params", std::move(params));

  std::string json_command = base::WriteJson(command).value_or("{}");
  LOG(INFO) << "browserd DevTools dispatch Target.attachToTarget bytes="
            << json_command.size();
  agent_host_->DispatchProtocolMessage(this, base::as_byte_span(json_command));
}

void DevToolsEvaluationClient::SendEvaluate() {
  base::DictValue params;
  params.Set("expression", std::move(expression_));
  params.Set("awaitPromise", await_promise_);
  params.Set("returnByValue", true);
  params.Set("userGesture", true);

  base::DictValue command;
  command.Set("id", kDevToolsEvaluateCommandId);
  command.Set("sessionId", session_id_);
  command.Set("method", "Runtime.evaluate");
  command.Set("params", std::move(params));

  std::string json_command = base::WriteJson(command).value_or("{}");
  LOG(INFO) << "browserd DevTools dispatch Runtime.evaluate bytes="
            << json_command.size();
  agent_host_->DispatchProtocolMessage(this, base::as_byte_span(json_command));
}

void DevToolsEvaluationClient::CompleteProtocolError(
    const base::DictValue& error,
    std::string_view fallback_message) {
  const std::string* error_message = error.FindString("message");
  Complete(BrowserResult<DevToolsEvaluationResult>::Error(
      BrowserStatusCode::kInternalError,
      error_message ? *error_message : std::string(fallback_message)));
}

void DevToolsEvaluationClient::AgentHostClosed(content::DevToolsAgentHost*) {
  LOG(INFO) << "browserd DevTools agent host closed";
  agent_host_ = nullptr;
  Complete(BrowserResult<DevToolsEvaluationResult>::Error(
      BrowserStatusCode::kInternalError, "DevTools agent host closed"));
}

bool DevToolsEvaluationClient::MayAttachToURL(const GURL&, bool) {
  return true;
}

bool DevToolsEvaluationClient::MayAttachToRenderFrameHost(
    content::RenderFrameHost*) {
  return true;
}

bool DevToolsEvaluationClient::IsTrusted() {
  return true;
}

bool DevToolsEvaluationClient::MayAccessAllCookies() {
  return true;
}

bool DevToolsEvaluationClient::MayReadLocalFiles() {
  return true;
}

bool DevToolsEvaluationClient::MayWriteLocalFiles() {
  return true;
}

bool DevToolsEvaluationClient::AllowUnsafeOperations() {
  return true;
}

void DevToolsEvaluationClient::OnTimeout() {
  LOG(ERROR) << "browserd DevTools evaluation timed out";
  Complete(BrowserResult<DevToolsEvaluationResult>::Error(
      BrowserStatusCode::kTimeout, "DevTools evaluation timeout"));
}

void DevToolsEvaluationClient::Complete(
    BrowserResult<DevToolsEvaluationResult> result) {
  if (!callback_) {
    return;
  }

  DevToolsEvaluationCallback callback = std::move(callback_);
  if (agent_host_) {
    agent_host_->DetachClient(this);
    agent_host_ = nullptr;
  }
  frame_agent_host_ = nullptr;
  std::move(callback).Run(std::move(result));
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce([](DevToolsEvaluationClient* client) { delete client; },
                     this));
}

JavaScriptEvaluationRequest::JavaScriptEvaluationRequest(
    content::WebContents* web_contents,
    std::string expression,
    bool await_promise,
    base::TimeDelta timeout,
    DevToolsEvaluationCallback callback)
    : web_contents_(web_contents),
      expression_(std::move(expression)),
      await_promise_(await_promise),
      timeout_(timeout),
      callback_(std::move(callback)) {}

JavaScriptEvaluationRequest::~JavaScriptEvaluationRequest() = default;

void JavaScriptEvaluationRequest::Start() {
  if (!web_contents_) {
    Complete(BrowserResult<DevToolsEvaluationResult>::Error(
        BrowserStatusCode::kTabNotFound, "Tab not found"));
    return;
  }

  content::RenderFrameHost* frame = web_contents_->GetPrimaryMainFrame();
  if (!frame || !frame->IsRenderFrameLive()) {
    Complete(BrowserResult<DevToolsEvaluationResult>::Error(
        BrowserStatusCode::kNoActiveFrame, "No active frame"));
    return;
  }

  LOG(INFO) << "browserd direct JS evaluate start url="
            << web_contents_->GetLastCommittedURL()
            << " visible=" << static_cast<int>(web_contents_->GetVisibility())
            << " frame_live=" << frame->IsRenderFrameLive()
            << " await_promise=" << await_promise_;

  if (timeout_.is_positive()) {
    timeout_timer_.Start(
        FROM_HERE, timeout_,
        base::BindOnce(&JavaScriptEvaluationRequest::OnTimeout,
                       weak_factory_.GetWeakPtr()));
  }

  auto* frame_impl = static_cast<content::RenderFrameHostImpl*>(frame);
  frame_impl->ExecuteJavaScriptForTests(
      base::UTF8ToUTF16(expression_), /*has_user_gesture=*/true,
      await_promise_, /*honor_js_content_settings=*/false,
      content::ISOLATED_WORLD_ID_GLOBAL,
      base::BindOnce(&JavaScriptEvaluationRequest::OnResult,
                     weak_factory_.GetWeakPtr()));
}

void JavaScriptEvaluationRequest::OnResult(
    blink::mojom::JavaScriptExecutionResultType result_type,
    base::Value value) {
  LOG(INFO) << "browserd direct JS evaluate callback type="
            << static_cast<int>(result_type)
            << " value_type=" << static_cast<int>(value.type());

  if (result_type != blink::mojom::JavaScriptExecutionResultType::kSuccess) {
    std::string message = "JavaScript evaluation exception";
    if (value.is_string()) {
      message = value.GetString();
    } else {
      base::JSONWriter::Write(value, &message);
    }
    Complete(BrowserResult<DevToolsEvaluationResult>::Error(
        BrowserStatusCode::kInternalError, std::move(message)));
    return;
  }

  DevToolsEvaluationResult evaluation;
  if (value.is_none()) {
    evaluation.is_undefined = true;
  } else {
    evaluation.value = std::move(value);
  }
  Complete(BrowserResult<DevToolsEvaluationResult>::Ok(
      std::move(evaluation)));
}

void JavaScriptEvaluationRequest::OnTimeout() {
  LOG(ERROR) << "browserd direct JS evaluation timed out";
  Complete(BrowserResult<DevToolsEvaluationResult>::Error(
      BrowserStatusCode::kTimeout, "JavaScript evaluation timeout"));
}

void JavaScriptEvaluationRequest::Complete(
    BrowserResult<DevToolsEvaluationResult> result) {
  if (!callback_) {
    return;
  }

  timeout_timer_.Stop();
  DevToolsEvaluationCallback callback = std::move(callback_);
  std::move(callback).Run(std::move(result));
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(
          [](JavaScriptEvaluationRequest* request) { delete request; }, this));
}

ScreenshotCaptureRequest::ScreenshotCaptureRequest(
    content::WebContents* web_contents,
    content::RenderWidgetHostView* view,
    BrowserController::ScreenshotCallback callback)
    : web_contents_(web_contents), view_(view), callback_(std::move(callback)) {}

ScreenshotCaptureRequest::~ScreenshotCaptureRequest() = default;

void ScreenshotCaptureRequest::Start() {
  if (!view_) {
    Complete(BrowserResult<std::vector<uint8_t>>::Error(
        BrowserStatusCode::kScreenshotFailed, "No render view available"));
    return;
  }

  auto* render_widget_host = view_->GetRenderWidgetHost();
  auto* render_widget_host_impl =
      static_cast<content::RenderWidgetHostImpl*>(render_widget_host);
  if (!render_widget_host_impl) {
    Complete(BrowserResult<std::vector<uint8_t>>::Error(
        BrowserStatusCode::kScreenshotFailed, "No render widget available"));
    return;
  }

  timeout_timer_.Start(
      FROM_HERE, base::Seconds(8),
      base::BindOnce(&ScreenshotCaptureRequest::OnTimeout,
                     weak_factory_.GetWeakPtr()));
  LOG(INFO) << "browserd screenshot GetSnapshotFromBrowser start"
            << " surface_available=" << view_->IsSurfaceAvailableForCopy();
  render_widget_host_impl->GetSnapshotFromBrowser(
      base::BindOnce(&ScreenshotCaptureRequest::OnSnapshotFromBrowser,
                     weak_factory_.GetWeakPtr()),
      /*from_surface=*/false);
}

void ScreenshotCaptureRequest::OnSnapshotFromBrowser(
    const gfx::Image& image) {
  LOG(INFO) << "browserd screenshot GetSnapshotFromBrowser callback empty="
            << image.IsEmpty();
  if (image.IsEmpty()) {
    Complete(BrowserResult<std::vector<uint8_t>>::Error(
        BrowserStatusCode::kScreenshotFailed, "Screenshot failed"));
    return;
  }

  SkBitmap bitmap = image.AsBitmap();
  if (bitmap.drawsNothing()) {
    Complete(BrowserResult<std::vector<uint8_t>>::Error(
        BrowserStatusCode::kScreenshotFailed, "Screenshot is empty"));
    return;
  }

  auto png_data = gfx::PNGCodec::EncodeBGRASkBitmap(bitmap, false);
  if (!png_data) {
    Complete(BrowserResult<std::vector<uint8_t>>::Error(
        BrowserStatusCode::kScreenshotFailed, "PNG encoding failed"));
    return;
  }

  Complete(BrowserResult<std::vector<uint8_t>>::Ok(std::move(*png_data)));
}

void ScreenshotCaptureRequest::OnCopyFromSurfaceResult(
    const content::CopyFromSurfaceResult& result) {
  LOG(INFO) << "browserd screenshot CopyFromSurface callback has_value="
            << result.has_value();
  if (!result.has_value()) {
    Complete(BrowserResult<std::vector<uint8_t>>::Error(
        BrowserStatusCode::kScreenshotFailed, "Screenshot failed"));
    return;
  }

  const SkBitmap& bitmap = result.value().bitmap;
  if (bitmap.drawsNothing()) {
    Complete(BrowserResult<std::vector<uint8_t>>::Error(
        BrowserStatusCode::kScreenshotFailed, "Screenshot is empty"));
    return;
  }

  auto png_data = gfx::PNGCodec::EncodeBGRASkBitmap(bitmap, false);
  if (!png_data) {
    Complete(BrowserResult<std::vector<uint8_t>>::Error(
        BrowserStatusCode::kScreenshotFailed, "PNG encoding failed"));
    return;
  }

  Complete(BrowserResult<std::vector<uint8_t>>::Ok(std::move(*png_data)));
}

void ScreenshotCaptureRequest::OnTimeout() {
  LOG(ERROR) << "browserd screenshot Chromium readback timed out";
  StartDomFallback();
}

void ScreenshotCaptureRequest::StartDomFallback() {
  if (!web_contents_) {
    Complete(BrowserResult<std::vector<uint8_t>>::Error(
        BrowserStatusCode::kTimeout, "Screenshot timed out"));
    return;
  }

  LOG(INFO) << "browserd screenshot DOM fallback start";
  EvaluateWithDevTools(
      web_contents_, std::string(kCanvasScreenshotScript), true,
      base::Seconds(10),
      base::BindOnce(&ScreenshotCaptureRequest::OnDomFallback,
                     weak_factory_.GetWeakPtr()));
}

void ScreenshotCaptureRequest::OnDomFallback(
    BrowserResult<DevToolsEvaluationResult> result) {
  LOG(INFO) << "browserd screenshot DOM fallback callback ok=" << result.ok();
  if (!result.ok()) {
    Complete(BrowserResult<std::vector<uint8_t>>::Error(
        result.status.code, result.status.message));
    return;
  }
  if (!result.value || !result.value->value.is_string()) {
    Complete(BrowserResult<std::vector<uint8_t>>::Error(
        BrowserStatusCode::kScreenshotFailed,
        "Screenshot fallback returned no data"));
    return;
  }

  constexpr std::string_view kPngDataUrlPrefix = "data:image/png;base64,";
  const std::string& data_url = result.value->value.GetString();
  if (data_url.rfind(kPngDataUrlPrefix, 0) != 0) {
    Complete(BrowserResult<std::vector<uint8_t>>::Error(
        BrowserStatusCode::kScreenshotFailed,
        "Screenshot fallback returned invalid data"));
    return;
  }

  std::optional<std::vector<uint8_t>> bytes =
      base::Base64Decode(std::string_view(data_url).substr(
          kPngDataUrlPrefix.size()));
  if (!bytes || bytes->empty()) {
    Complete(BrowserResult<std::vector<uint8_t>>::Error(
        BrowserStatusCode::kScreenshotFailed,
        "Screenshot fallback PNG decode failed"));
    return;
  }

  Complete(BrowserResult<std::vector<uint8_t>>::Ok(std::move(*bytes)));
}

void ScreenshotCaptureRequest::Complete(
    BrowserResult<std::vector<uint8_t>> result) {
  if (!callback_) {
    return;
  }

  timeout_timer_.Stop();
  BrowserController::ScreenshotCallback callback = std::move(callback_);
  std::move(callback).Run(std::move(result));
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(
          [](ScreenshotCaptureRequest* request) { delete request; }, this));
}

}  // namespace

void EvaluateWithDevTools(content::WebContents* web_contents,
                          std::string expression,
                          bool await_promise,
                          base::TimeDelta timeout,
                          DevToolsEvaluationCallback callback) {
  auto* client = new JavaScriptEvaluationRequest(
      web_contents, std::move(expression), await_promise, timeout,
      std::move(callback));
  client->Start();
}

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
                   GURL target_url,
                   BrowserController::StatusCallback callback)
      : content::WebContentsObserver(web_contents),
        target_url_(std::move(target_url)),
        callback_(std::move(callback)) {
    LOG(INFO) << "browserd NavigationWaiter start target=" << target_url_
              << " current="
              << (web_contents ? web_contents->GetLastCommittedURL() : GURL())
              << " loading=" << (web_contents && web_contents->IsLoading())
              << " visibility="
              << (web_contents ? static_cast<int>(web_contents->GetVisibility())
                               : -1)
              << " frame="
              << (web_contents ? web_contents->GetPrimaryMainFrame() : nullptr)
              << " frame_live="
              << (web_contents && web_contents->GetPrimaryMainFrame() &&
                  web_contents->GetPrimaryMainFrame()->IsRenderFrameLive());
    timeout_timer_.Start(
        FROM_HERE, base::Seconds(15),
        base::BindOnce(&NavigationWaiter::OnTimeout,
                       weak_factory_.GetWeakPtr()));
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&NavigationWaiter::CheckCurrentState,
                                  weak_factory_.GetWeakPtr()));
  }

  void RenderFrameCreated(content::RenderFrameHost* render_frame_host) override {
    LOG(INFO) << "browserd NavigationWaiter RenderFrameCreated target="
              << target_url_ << " frame=" << render_frame_host
              << " primary="
              << (web_contents() &&
                  render_frame_host == web_contents()->GetPrimaryMainFrame())
              << " live="
              << (render_frame_host && render_frame_host->IsRenderFrameLive());
  }

  void PrimaryMainFrameRenderProcessGone(
      base::TerminationStatus status) override {
    LOG(ERROR) << "browserd NavigationWaiter primary renderer gone target="
               << target_url_ << " status=" << static_cast<int>(status);
  }

  void OnRendererUnresponsive(
      content::RenderProcessHost* render_process_host) override {
    LOG(ERROR) << "browserd NavigationWaiter renderer unresponsive target="
               << target_url_ << " process=" << render_process_host;
  }

  void DidStartNavigation(content::NavigationHandle* navigation_handle) override {
    LOG(INFO) << "browserd NavigationWaiter DidStartNavigation target="
              << target_url_ << " url=" << navigation_handle->GetURL()
              << " primary=" << navigation_handle->IsInPrimaryMainFrame();
  }

  void ReadyToCommitNavigation(
      content::NavigationHandle* navigation_handle) override {
    content::RenderFrameHost* frame = navigation_handle->GetRenderFrameHost();
    LOG(INFO) << "browserd NavigationWaiter ReadyToCommit target="
              << target_url_ << " url=" << navigation_handle->GetURL()
              << " primary=" << navigation_handle->IsInPrimaryMainFrame()
              << " frame=" << frame
              << " live=" << (frame && frame->IsRenderFrameLive());
  }

  void DidFinishNavigation(
      content::NavigationHandle* navigation_handle) override {
    content::RenderFrameHost* frame = navigation_handle->GetRenderFrameHost();
    LOG(INFO) << "browserd NavigationWaiter DidFinishNavigation target="
              << target_url_ << " url=" << navigation_handle->GetURL()
              << " primary=" << navigation_handle->IsInPrimaryMainFrame()
              << " committed=" << navigation_handle->HasCommitted()
              << " error_page=" << navigation_handle->IsErrorPage()
              << " net_error=" << navigation_handle->GetNetErrorCode()
              << " frame=" << frame
              << " live=" << (frame && frame->IsRenderFrameLive());
    if (!navigation_handle->IsInPrimaryMainFrame() ||
        !navigation_handle->HasCommitted()) {
      return;
    }

    if (!IsRequestedNavigation(navigation_handle->GetURL())) {
      return;
    }

    seen_requested_navigation_ = true;

    if (navigation_handle->IsErrorPage()) {
      Complete(BrowserStatus::Error(
          BrowserStatusCode::kNavigationFailed,
          "Navigation failed: " +
              net::ErrorToString(navigation_handle->GetNetErrorCode())));
      return;
    }

    if (!web_contents() || !web_contents()->IsLoading()) {
      Complete(BrowserStatus::Ok());
    }
  }

  void DidStopLoading() override {
    LOG(INFO) << "browserd NavigationWaiter DidStopLoading target="
              << target_url_
              << " current="
              << (web_contents() ? web_contents()->GetLastCommittedURL()
                                 : GURL())
              << " frame="
              << (web_contents() ? web_contents()->GetPrimaryMainFrame()
                                 : nullptr)
              << " frame_live="
              << (web_contents() && web_contents()->GetPrimaryMainFrame() &&
                  web_contents()->GetPrimaryMainFrame()->IsRenderFrameLive());
    if (!seen_requested_navigation_) {
      if (!web_contents() ||
          !IsRequestedNavigation(web_contents()->GetLastCommittedURL())) {
        return;
      }
      seen_requested_navigation_ = true;
    }
    Complete(BrowserStatus::Ok());
  }

  void DidFailLoad(content::RenderFrameHost* rfh,
                   const GURL& validated_url,
                   int error_code) override {
    LOG(ERROR) << "browserd NavigationWaiter DidFailLoad target="
               << target_url_ << " url=" << validated_url
               << " error=" << error_code << " frame=" << rfh;
    if (web_contents() && rfh != web_contents()->GetPrimaryMainFrame()) {
      return;
    }
    if (!IsRequestedNavigation(validated_url)) {
      return;
    }
    Complete(BrowserStatus::Error(
        BrowserStatusCode::kNavigationFailed,
        "Navigation failed: " + net::ErrorToString(error_code)));
  }

  void WebContentsDestroyed() override {
    Complete(BrowserStatus::Error(BrowserStatusCode::kTabNotFound,
                                  "Tab closed during navigation"));
  }

 private:
  void CheckCurrentState() {
    LOG(INFO) << "browserd NavigationWaiter CheckCurrentState target="
              << target_url_
              << " current="
              << (web_contents() ? web_contents()->GetLastCommittedURL()
                                 : GURL())
              << " loading="
              << (web_contents() && web_contents()->IsLoading())
              << " frame="
              << (web_contents() ? web_contents()->GetPrimaryMainFrame()
                                 : nullptr)
              << " frame_live="
              << (web_contents() && web_contents()->GetPrimaryMainFrame() &&
                  web_contents()->GetPrimaryMainFrame()->IsRenderFrameLive());
    if (!web_contents() ||
        !IsRequestedNavigation(web_contents()->GetLastCommittedURL())) {
      return;
    }
    seen_requested_navigation_ = true;
    if (!web_contents()->IsLoading()) {
      Complete(BrowserStatus::Ok());
    }
  }

  bool IsRequestedNavigation(const GURL& url) const {
    if (!url.is_valid() || url.spec() == "about:blank") {
      return false;
    }
    if (url == target_url_) {
      return true;
    }
    if (target_url_.SchemeIsHTTPOrHTTPS() && url.SchemeIsHTTPOrHTTPS() &&
        url.host() == target_url_.host()) {
      return true;
    }
    return false;
  }

  void OnTimeout() {
    LOG(ERROR) << "browserd NavigationWaiter timeout target=" << target_url_
               << " current="
               << (web_contents() ? web_contents()->GetLastCommittedURL()
                                  : GURL())
               << " loading="
               << (web_contents() && web_contents()->IsLoading())
               << " frame="
               << (web_contents() ? web_contents()->GetPrimaryMainFrame()
                                  : nullptr)
               << " frame_live="
               << (web_contents() && web_contents()->GetPrimaryMainFrame() &&
                   web_contents()->GetPrimaryMainFrame()->IsRenderFrameLive());
    Complete(BrowserStatus::Error(BrowserStatusCode::kTimeout,
                                  "Navigation timed out"));
  }

  void Complete(BrowserStatus status) {
    if (!callback_) {
      return;
    }

    timeout_timer_.Stop();
    Observe(nullptr);
    BrowserController::StatusCallback callback = std::move(callback_);
    std::move(callback).Run(std::move(status));
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce([](NavigationWaiter* waiter) { delete waiter; }, this));
  }

  GURL target_url_;
  BrowserController::StatusCallback callback_;
  base::OneShotTimer timeout_timer_;
  bool seen_requested_navigation_ = false;
  base::WeakPtrFactory<NavigationWaiter> weak_factory_{this};
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
        if (gurl.spec() == "about:blank") {
          std::move(cb).Run(BrowserResult<BrowserTabInfo>::Ok(std::move(*tab)));
          return;
        }

        const std::string target_id = tab->target_id;
        content::WebContents* web_contents =
            self->ResolveWebContents(target_id);
        if (!web_contents) {
          std::move(cb).Run(BrowserResult<BrowserTabInfo>::Error(
              BrowserStatusCode::kTabNotFound,
              "New tab web contents not found"));
          return;
        }

        new NavigationWaiter(
            web_contents, gurl,
            base::BindOnce(
                [](base::WeakPtr<BrowserController> self,
                   std::string target_id, TabCallback cb,
                   BrowserStatus status) {
                  if (!status.ok()) {
                    std::move(cb).Run(BrowserResult<BrowserTabInfo>::Error(
                        status.code, std::move(status.message)));
                    return;
                  }
                  if (!self || !self->runtime_) {
                    std::move(cb).Run(BrowserResult<BrowserTabInfo>::Error(
                        BrowserStatusCode::kSessionNotReady,
                        "Browser session not ready"));
                    return;
                  }

                  self->RefreshSharedRuntimeBehavior();
                  std::vector<BrowserTabInfo> tabs = self->runtime_->ListTabs();
                  for (BrowserTabInfo& current : tabs) {
                    if (current.target_id == target_id) {
                      std::move(cb).Run(
                          BrowserResult<BrowserTabInfo>::Ok(std::move(current)));
                      return;
                    }
                  }
                  std::move(cb).Run(BrowserResult<BrowserTabInfo>::Error(
                      BrowserStatusCode::kTabNotFound,
                      "Tab not found: " + target_id));
                },
                self, target_id, std::move(cb)));
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
        LOG(INFO) << "browserd BrowserController::CloseTab begin";
        if (!self->runtime_->CloseTab(id)) {
          LOG(INFO) << "browserd BrowserController::CloseTab not found";
          std::move(cb).Run(TabNotFound(id));
          return;
        }
        LOG(INFO) << "browserd BrowserController::CloseTab refresh";
        self->RefreshSharedRuntimeBehavior();
        LOG(INFO) << "browserd BrowserController::CloseTab callback";
        std::move(cb).Run(BrowserStatus::Ok());
        LOG(INFO) << "browserd BrowserController::CloseTab done";
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
        new NavigationWaiter(web_contents, target_url, std::move(cb));
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
        EvaluateWithDevTools(
            web_contents, std::move(js), true, base::Seconds(10),
            base::BindOnce(
                [](EvaluateCallback callback,
                   BrowserResult<DevToolsEvaluationResult> result) {
                  if (!result.ok()) {
                    std::move(callback).Run(
                        BrowserResult<EvaluateResult>::Error(
                            result.status.code, result.status.message));
                    return;
                  }
                  std::move(callback).Run(BrowserResult<EvaluateResult>::Ok(
                      ToEvaluateResult(*result.value)));
                },
                std::move(cb)));
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
        EvaluateWithDevTools(
            web_contents,
            "(() => {"
            "  if (!window.__browserdLogs) return '[]';"
            "  return JSON.stringify(window.__browserdLogs);"
            "})()",
            true, base::Seconds(10),
            base::BindOnce(
                [](SnapshotCallback callback,
                   BrowserResult<DevToolsEvaluationResult> result) {
                  if (!result.ok()) {
                    std::move(callback).Run(
                        BrowserResult<std::string>::Error(
                            result.status.code, result.status.message));
                    return;
                  }
                  if (result.value && result.value->value.is_string()) {
                    std::move(callback).Run(BrowserResult<std::string>::Ok(
                        result.value->value.GetString()));
                    return;
                  }
                  std::move(callback).Run(BrowserResult<std::string>::Ok("[]"));
                },
                std::move(cb)));
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
        EvaluateWithDevTools(
            web_contents, std::string(kDomSnapshotScript), true,
            base::Seconds(10),
            base::BindOnce(
                [](SnapshotCallback callback,
                   BrowserResult<DevToolsEvaluationResult> result) {
                  if (!result.ok()) {
                    std::move(callback).Run(
                        BrowserResult<std::string>::Error(
                            result.status.code, result.status.message));
                    return;
                  }
                  if (!result.value || !result.value->value.is_string()) {
                    std::move(callback).Run(
                        BrowserResult<std::string>::Error(
                            BrowserStatusCode::kInternalError,
                            "DOM snapshot returned no text"));
                    return;
                  }
                  std::move(callback).Run(BrowserResult<std::string>::Ok(
                      result.value->value.GetString()));
                },
                std::move(cb)));
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
        (new ScreenshotCaptureRequest(web_contents, rwhv, std::move(cb)))
            ->Start();
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

        EvaluateWithDevTools(
            web_contents, std::move(expression), true,
            base::Milliseconds(timeout + 1000),
            base::BindOnce(
                [](StatusCallback callback,
                   BrowserResult<DevToolsEvaluationResult> result) {
                  if (!result.ok()) {
                    std::move(callback).Run(BrowserStatus::Error(
                        result.status.code, result.status.message));
                    return;
                  }
                  if (result.value && result.value->value.is_string() &&
                      result.value->value.GetString() == "found") {
                    std::move(callback).Run(BrowserStatus::Ok());
                  } else {
                    std::move(callback).Run(BrowserStatus::Error(
                        BrowserStatusCode::kTimeout, "Wait timeout"));
                  }
                },
                std::move(cb)));
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
