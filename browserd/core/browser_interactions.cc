#include "browserd/core/browser_controller.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/json/string_escape.h"
#include "base/location.h"
#include "base/rand_util.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/time.h"
#include "components/input/native_web_keyboard_event.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_widget_host.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/isolated_world_ids.h"
#include "third_party/blink/public/common/input/web_mouse_event.h"
#include "third_party/blink/public/common/input/web_mouse_wheel_event.h"
#include "ui/accessibility/ax_action_data.h"
#include "ui/accessibility/ax_mode.h"
#include "ui/accessibility/ax_node_data.h"
#include "ui/accessibility/ax_tree_update.h"
#include "ui/events/base_event_utils.h"
#include "ui/events/keycodes/dom/dom_key.h"
#include "ui/events/keycodes/keyboard_codes.h"

namespace browserd {
namespace {

struct MouseState {
  double x = 500.0;
  double y = 400.0;
};

MouseState& GetMouseState() {
  static MouseState state;
  return state;
}

struct BezierPoint {
  double x;
  double y;
};

BrowserStatus SessionNotReady() {
  return BrowserStatus::Error(BrowserStatusCode::kSessionNotReady,
                              "Browser session not ready");
}

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

BrowserStatus NoRenderView() {
  return BrowserStatus::Error(BrowserStatusCode::kInternalError,
                              "No render view");
}

BrowserStatus InvalidRef() {
  return BrowserStatus::Error(BrowserStatusCode::kInvalidArgument,
                              "Invalid ref");
}

bool ParseRef(const std::string& ref, int* node_id) {
  return base::StringToInt(ref, node_id);
}

BezierPoint CubicBezier(BezierPoint p0,
                        BezierPoint p1,
                        BezierPoint p2,
                        BezierPoint p3,
                        double t) {
  double u = 1.0 - t;
  double uu = u * u;
  double uuu = uu * u;
  double tt = t * t;
  double ttt = tt * t;
  return {
      uuu * p0.x + 3 * uu * t * p1.x + 3 * u * tt * p2.x + ttt * p3.x,
      uuu * p0.y + 3 * uu * t * p1.y + 3 * u * tt * p2.y + ttt * p3.y,
  };
}

std::vector<BezierPoint> GenerateMousePath(double from_x,
                                           double from_y,
                                           double to_x,
                                           double to_y) {
  double dx = to_x - from_x;
  double dy = to_y - from_y;
  double dist = std::sqrt(dx * dx + dy * dy);
  int steps = std::clamp(static_cast<int>(dist / 40.0), 4, 12);

  double perp_x = -dy;
  double perp_y = dx;
  double norm = std::sqrt(perp_x * perp_x + perp_y * perp_y);
  if (norm > 0) {
    perp_x /= norm;
    perp_y /= norm;
  }

  double curve = (base::RandDouble() - 0.5) * dist * 0.3;
  BezierPoint p0 = {from_x, from_y};
  BezierPoint p1 = {from_x + dx * 0.3 + perp_x * curve,
                     from_y + dy * 0.3 + perp_y * curve};
  BezierPoint p2 = {from_x + dx * 0.7 + perp_x * curve * 0.5,
                     from_y + dy * 0.7 + perp_y * curve * 0.5};
  BezierPoint p3 = {to_x, to_y};

  std::vector<BezierPoint> path;
  for (int i = 1; i <= steps; ++i) {
    double t = static_cast<double>(i) / steps;
    auto pt = CubicBezier(p0, p1, p2, p3, t);
    pt.x += (base::RandDouble() - 0.5) * 2.0;
    pt.y += (base::RandDouble() - 0.5) * 2.0;
    path.push_back(pt);
  }
  path.back() = {to_x + (base::RandDouble() - 0.5) * 1.5,
                 to_y + (base::RandDouble() - 0.5) * 1.5};
  return path;
}

void ForwardMouseMove(content::RenderWidgetHost* rwh, double x, double y) {
  blink::WebMouseEvent event(blink::WebInputEvent::Type::kMouseMove,
                             blink::WebInputEvent::kNoModifiers,
                             ui::EventTimeForNow());
  event.SetPositionInWidget(static_cast<float>(x), static_cast<float>(y));
  event.SetPositionInScreen(static_cast<float>(x), static_cast<float>(y));
  rwh->ForwardMouseEvent(event);
  GetMouseState().x = x;
  GetMouseState().y = y;
}

void ForwardMouseButton(content::RenderWidgetHost* rwh,
                        blink::WebInputEvent::Type type,
                        double x,
                        double y) {
  blink::WebMouseEvent event(type, blink::WebInputEvent::kNoModifiers,
                             ui::EventTimeForNow());
  event.SetPositionInWidget(static_cast<float>(x), static_cast<float>(y));
  event.SetPositionInScreen(static_cast<float>(x), static_cast<float>(y));
  event.button = blink::WebMouseEvent::Button::kLeft;
  event.click_count = 1;
  rwh->ForwardMouseEvent(event);
}

void AnimateMousePath(content::WebContents* web_contents,
                      std::vector<BezierPoint> path,
                      size_t index,
                      base::OnceClosure on_done) {
  if (index >= path.size()) {
    std::move(on_done).Run();
    return;
  }

  auto* rwhv = web_contents->GetRenderWidgetHostView();
  if (!rwhv) {
    std::move(on_done).Run();
    return;
  }

  ForwardMouseMove(rwhv->GetRenderWidgetHost(), path[index].x, path[index].y);

  int delay_ms = 8 + base::RandInt(0, 12);
  base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&AnimateMousePath, base::Unretained(web_contents),
                     std::move(path), index + 1, std::move(on_done)),
      base::Milliseconds(delay_ms));
}

void EvalStatusJS(content::WebContents* web_contents,
                  std::string expression,
                  BrowserController::StatusCallback callback) {
  auto* rfh = web_contents->GetPrimaryMainFrame();
  if (!rfh) {
    std::move(callback).Run(NoActiveFrame());
    return;
  }

  rfh->ExecuteJavaScriptForTests(
      base::UTF8ToUTF16(expression),
      base::BindOnce(
          [](BrowserController::StatusCallback cb, base::Value result) {
            if (result.is_string()) {
              const std::string& s = result.GetString();
              if (s.find("Error") != std::string::npos ||
                  s.find("not found") != std::string::npos) {
                std::move(cb).Run(BrowserStatus::Error(
                    BrowserStatusCode::kInternalError, s));
                return;
              }
            }
            std::move(cb).Run(BrowserStatus::Ok());
          },
          std::move(callback)),
      content::ISOLATED_WORLD_ID_GLOBAL);
}

struct TypeState {
  std::string text;
  size_t index = 0;
};

struct KeyMapping {
  int windows_key_code = 0;
  int modifiers = blink::WebInputEvent::kNoModifiers;
  ui::DomKey dom_key = ui::DomKey::NONE;
};

KeyMapping MapCharacter(char c) {
  KeyMapping mapping;
  mapping.dom_key = ui::DomKey::FromCharacter(c);

  if (c >= 'a' && c <= 'z') {
    mapping.windows_key_code = ui::VKEY_A + (c - 'a');
  } else if (c >= 'A' && c <= 'Z') {
    mapping.windows_key_code = ui::VKEY_A + (c - 'A');
    mapping.modifiers = blink::WebInputEvent::kShiftKey;
  } else if (c >= '0' && c <= '9') {
    mapping.windows_key_code = ui::VKEY_0 + (c - '0');
  } else if (c == ' ') {
    mapping.windows_key_code = ui::VKEY_SPACE;
  } else {
    switch (c) {
      case '!':
        mapping.windows_key_code = ui::VKEY_1;
        mapping.modifiers = blink::WebInputEvent::kShiftKey;
        break;
      case '@':
        mapping.windows_key_code = ui::VKEY_2;
        mapping.modifiers = blink::WebInputEvent::kShiftKey;
        break;
      case '#':
        mapping.windows_key_code = ui::VKEY_3;
        mapping.modifiers = blink::WebInputEvent::kShiftKey;
        break;
      case '$':
        mapping.windows_key_code = ui::VKEY_4;
        mapping.modifiers = blink::WebInputEvent::kShiftKey;
        break;
      case '%':
        mapping.windows_key_code = ui::VKEY_5;
        mapping.modifiers = blink::WebInputEvent::kShiftKey;
        break;
      case '^':
        mapping.windows_key_code = ui::VKEY_6;
        mapping.modifiers = blink::WebInputEvent::kShiftKey;
        break;
      case '&':
        mapping.windows_key_code = ui::VKEY_7;
        mapping.modifiers = blink::WebInputEvent::kShiftKey;
        break;
      case '*':
        mapping.windows_key_code = ui::VKEY_8;
        mapping.modifiers = blink::WebInputEvent::kShiftKey;
        break;
      case '(':
        mapping.windows_key_code = ui::VKEY_9;
        mapping.modifiers = blink::WebInputEvent::kShiftKey;
        break;
      case ')':
        mapping.windows_key_code = ui::VKEY_0;
        mapping.modifiers = blink::WebInputEvent::kShiftKey;
        break;
      case '-':
        mapping.windows_key_code = ui::VKEY_OEM_MINUS;
        break;
      case '_':
        mapping.windows_key_code = ui::VKEY_OEM_MINUS;
        mapping.modifiers = blink::WebInputEvent::kShiftKey;
        break;
      case '=':
        mapping.windows_key_code = ui::VKEY_OEM_PLUS;
        break;
      case '+':
        mapping.windows_key_code = ui::VKEY_OEM_PLUS;
        mapping.modifiers = blink::WebInputEvent::kShiftKey;
        break;
      case '[':
        mapping.windows_key_code = ui::VKEY_OEM_4;
        break;
      case '{':
        mapping.windows_key_code = ui::VKEY_OEM_4;
        mapping.modifiers = blink::WebInputEvent::kShiftKey;
        break;
      case ']':
        mapping.windows_key_code = ui::VKEY_OEM_6;
        break;
      case '}':
        mapping.windows_key_code = ui::VKEY_OEM_6;
        mapping.modifiers = blink::WebInputEvent::kShiftKey;
        break;
      case '\\':
        mapping.windows_key_code = ui::VKEY_OEM_5;
        break;
      case '|':
        mapping.windows_key_code = ui::VKEY_OEM_5;
        mapping.modifiers = blink::WebInputEvent::kShiftKey;
        break;
      case ';':
        mapping.windows_key_code = ui::VKEY_OEM_1;
        break;
      case ':':
        mapping.windows_key_code = ui::VKEY_OEM_1;
        mapping.modifiers = blink::WebInputEvent::kShiftKey;
        break;
      case '\'':
        mapping.windows_key_code = ui::VKEY_OEM_7;
        break;
      case '"':
        mapping.windows_key_code = ui::VKEY_OEM_7;
        mapping.modifiers = blink::WebInputEvent::kShiftKey;
        break;
      case ',':
        mapping.windows_key_code = ui::VKEY_OEM_COMMA;
        break;
      case '<':
        mapping.windows_key_code = ui::VKEY_OEM_COMMA;
        mapping.modifiers = blink::WebInputEvent::kShiftKey;
        break;
      case '.':
        mapping.windows_key_code = ui::VKEY_OEM_PERIOD;
        break;
      case '>':
        mapping.windows_key_code = ui::VKEY_OEM_PERIOD;
        mapping.modifiers = blink::WebInputEvent::kShiftKey;
        break;
      case '/':
        mapping.windows_key_code = ui::VKEY_OEM_2;
        break;
      case '?':
        mapping.windows_key_code = ui::VKEY_OEM_2;
        mapping.modifiers = blink::WebInputEvent::kShiftKey;
        break;
      case '`':
        mapping.windows_key_code = ui::VKEY_OEM_3;
        break;
      case '~':
        mapping.windows_key_code = ui::VKEY_OEM_3;
        mapping.modifiers = blink::WebInputEvent::kShiftKey;
        break;
      default:
        break;
    }
  }

  return mapping;
}

KeyMapping MapKey(const std::string& key) {
  KeyMapping mapping;
  if (key == "Enter") {
    mapping.windows_key_code = ui::VKEY_RETURN;
    mapping.dom_key = ui::DomKey::ENTER;
  } else if (key == "Tab") {
    mapping.windows_key_code = ui::VKEY_TAB;
    mapping.dom_key = ui::DomKey::TAB;
  } else if (key == "Escape") {
    mapping.windows_key_code = ui::VKEY_ESCAPE;
    mapping.dom_key = ui::DomKey::ESCAPE;
  } else if (key == "Backspace") {
    mapping.windows_key_code = ui::VKEY_BACK;
    mapping.dom_key = ui::DomKey::BACKSPACE;
  } else if (key == "Delete") {
    mapping.windows_key_code = ui::VKEY_DELETE;
    mapping.dom_key = ui::DomKey::DEL;
  } else if (key == "ArrowUp") {
    mapping.windows_key_code = ui::VKEY_UP;
    mapping.dom_key = ui::DomKey::ARROW_UP;
  } else if (key == "ArrowDown") {
    mapping.windows_key_code = ui::VKEY_DOWN;
    mapping.dom_key = ui::DomKey::ARROW_DOWN;
  } else if (key == "ArrowLeft") {
    mapping.windows_key_code = ui::VKEY_LEFT;
    mapping.dom_key = ui::DomKey::ARROW_LEFT;
  } else if (key == "ArrowRight") {
    mapping.windows_key_code = ui::VKEY_RIGHT;
    mapping.dom_key = ui::DomKey::ARROW_RIGHT;
  } else if (key == " " || key == "Space") {
    mapping.windows_key_code = ui::VKEY_SPACE;
    mapping.dom_key = ui::DomKey::FromCharacter(' ');
  } else if (key.length() == 1) {
    mapping = MapCharacter(key[0]);
  }
  return mapping;
}

void TypeNextChar(content::WebContents* web_contents,
                  std::shared_ptr<TypeState> state,
                  BrowserController::StatusCallback callback) {
  if (state->index >= state->text.size()) {
    auto* rfh = web_contents->GetPrimaryMainFrame();
    if (rfh) {
      rfh->ExecuteJavaScriptForTests(
          u"document.activeElement?.dispatchEvent("
          u"  new Event('change', {bubbles: true}));",
          base::BindOnce(
              [](BrowserController::StatusCallback cb, base::Value) {
                std::move(cb).Run(BrowserStatus::Ok());
              },
              std::move(callback)),
          content::ISOLATED_WORLD_ID_GLOBAL);
      return;
    }
    std::move(callback).Run(BrowserStatus::Ok());
    return;
  }

  auto* rwhv = web_contents->GetRenderWidgetHostView();
  if (!rwhv) {
    std::move(callback).Run(NoRenderView());
    return;
  }

  char c = state->text[state->index];
  KeyMapping mapping = MapCharacter(c);
  auto* rwh = rwhv->GetRenderWidgetHost();

  input::NativeWebKeyboardEvent down(
      blink::WebInputEvent::Type::kRawKeyDown, mapping.modifiers,
      ui::EventTimeForNow());
  down.windows_key_code = mapping.windows_key_code;
  down.dom_key = static_cast<uint32_t>(mapping.dom_key);
  rwh->ForwardKeyboardEvent(down);

  input::NativeWebKeyboardEvent char_event(
      blink::WebInputEvent::Type::kChar, mapping.modifiers,
      ui::EventTimeForNow());
  char_event.windows_key_code = c;
  char_event.dom_key = static_cast<uint32_t>(mapping.dom_key);
  char_event.text[0] = c;
  char_event.unmodified_text[0] = c;
  rwh->ForwardKeyboardEvent(char_event);

  int up_delay = 20 + base::RandInt(0, 40);
  base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(
          [](content::WebContents* wc, KeyMapping key_mapping,
             std::shared_ptr<TypeState> st,
             BrowserController::StatusCallback cb) {
            auto* rwhv = wc->GetRenderWidgetHostView();
            if (!rwhv) {
              std::move(cb).Run(NoRenderView());
              return;
            }

            input::NativeWebKeyboardEvent up(
                blink::WebInputEvent::Type::kKeyUp, key_mapping.modifiers,
                ui::EventTimeForNow());
            up.windows_key_code = key_mapping.windows_key_code;
            up.dom_key = static_cast<uint32_t>(key_mapping.dom_key);
            rwhv->GetRenderWidgetHost()->ForwardKeyboardEvent(up);

            st->index++;
            int next_delay = 30 + base::RandInt(0, 90);
            base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
                FROM_HERE,
                base::BindOnce(&TypeNextChar, base::Unretained(wc),
                               std::move(st), std::move(cb)),
                base::Milliseconds(next_delay));
          },
          base::Unretained(web_contents), mapping, std::move(state),
          std::move(callback)),
      base::Milliseconds(up_delay));
}

struct ScrollState {
  int remaining = 0;
};

void ScrollStep(content::WebContents* web_contents,
                int delta_x,
                int delta_y,
                std::shared_ptr<ScrollState> state,
                BrowserController::StatusCallback callback) {
  if (state->remaining <= 0) {
    std::move(callback).Run(BrowserStatus::Ok());
    return;
  }

  auto* rwhv = web_contents->GetRenderWidgetHostView();
  if (!rwhv) {
    std::move(callback).Run(NoRenderView());
    return;
  }

  int step_x = 0;
  int step_y = 0;
  int chunk = std::min(state->remaining, 40 + base::RandInt(0, 40));
  if (delta_y != 0) {
    step_y = (delta_y > 0 ? 1 : -1) * chunk;
  }
  if (delta_x != 0) {
    step_x = (delta_x > 0 ? 1 : -1) * chunk;
  }
  state->remaining -= chunk;

  auto& ms = GetMouseState();
  blink::WebMouseWheelEvent wheel(blink::WebInputEvent::Type::kMouseWheel,
                                  blink::WebInputEvent::kNoModifiers,
                                  ui::EventTimeForNow());
  wheel.SetPositionInWidget(static_cast<float>(ms.x), static_cast<float>(ms.y));
  wheel.SetPositionInScreen(static_cast<float>(ms.x), static_cast<float>(ms.y));
  wheel.delta_x = static_cast<float>(-step_x);
  wheel.delta_y = static_cast<float>(-step_y);
  wheel.phase = blink::WebMouseWheelEvent::kPhaseBegan;
  rwhv->GetRenderWidgetHost()->ForwardWheelEvent(wheel);

  int delay = 12 + base::RandInt(0, 18);
  base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&ScrollStep, base::Unretained(web_contents), delta_x,
                     delta_y, std::move(state), std::move(callback)),
      base::Milliseconds(delay));
}

}  // namespace

void BrowserController::Click(std::optional<std::string> target_id,
                              RefOptions options,
                              StatusCallback callback) {
  RunOrPost(base::BindOnce(
      [](base::WeakPtr<BrowserController> self,
         std::optional<std::string> id, RefOptions click_options,
         StatusCallback cb) {
        if (!self || !self->runtime_) {
          std::move(cb).Run(SessionNotReady());
          return;
        }
        content::WebContents* web_contents = self->ResolveWebContents(id);
        if (!web_contents) {
          std::move(cb).Run(TabNotFound(id));
          return;
        }

        int node_id = 0;
        if (!ParseRef(click_options.ref, &node_id)) {
          std::move(cb).Run(InvalidRef());
          return;
        }

        auto* rfh = web_contents->GetPrimaryMainFrame();
        if (!rfh) {
          std::move(cb).Run(NoActiveFrame());
          return;
        }

        ui::AXActionData action;
        action.action = ax::mojom::Action::kScrollToMakeVisible;
        action.target_node_id = static_cast<ui::AXNodeID>(node_id);
        rfh->AccessibilityPerformAction(action);

        web_contents->RequestAXTreeSnapshot(
            base::BindOnce(
                [](content::WebContents* wc, int target_node_id,
                   StatusCallback callback, ui::AXTreeUpdate& tree) {
                  for (const auto& node : tree.nodes) {
                    if (node.id != static_cast<ui::AXNodeID>(target_node_id)) {
                      continue;
                    }

                    gfx::RectF bounds = node.relative_bounds.bounds;
                    double cx =
                        bounds.x() + bounds.width() / 2.0 +
                        (base::RandDouble() - 0.5) * bounds.width() * 0.3;
                    double cy =
                        bounds.y() + bounds.height() / 2.0 +
                        (base::RandDouble() - 0.5) * bounds.height() * 0.3;

                    auto& ms = GetMouseState();
                    auto path = GenerateMousePath(ms.x, ms.y, cx, cy);
                    AnimateMousePath(
                        wc, std::move(path), 0,
                        base::BindOnce(
                            [](content::WebContents* wc, double x, double y,
                               StatusCallback cb) {
                              if (!wc->GetRenderWidgetHostView()) {
                                std::move(cb).Run(NoRenderView());
                                return;
                              }

                              int down_delay = 15 + base::RandInt(0, 30);
                              base::SequencedTaskRunner::GetCurrentDefault()
                                  ->PostDelayedTask(
                                      FROM_HERE,
                                      base::BindOnce(
                                          [](content::WebContents* wc,
                                             double x, double y,
                                             StatusCallback cb) {
                                            auto* rwhv =
                                                wc->GetRenderWidgetHostView();
                                            if (!rwhv) {
                                              std::move(cb).Run(
                                                  NoRenderView());
                                              return;
                                            }
                                            ForwardMouseButton(
                                                rwhv->GetRenderWidgetHost(),
                                                blink::WebInputEvent::Type::
                                                    kMouseDown,
                                                x, y);

                                            int up_delay =
                                                50 + base::RandInt(0, 80);
                                            base::SequencedTaskRunner::
                                                GetCurrentDefault()
                                                    ->PostDelayedTask(
                                                        FROM_HERE,
                                                        base::BindOnce(
                                                            [](content::
                                                                   WebContents*
                                                                       wc,
                                                               double x,
                                                               double y,
                                                               StatusCallback
                                                                   cb) {
                                                              auto* rwhv =
                                                                  wc->GetRenderWidgetHostView();
                                                              if (!rwhv) {
                                                                std::move(cb)
                                                                    .Run(
                                                                        NoRenderView());
                                                                return;
                                                              }
                                                              ForwardMouseButton(
                                                                  rwhv->GetRenderWidgetHost(),
                                                                  blink::
                                                                      WebInputEvent::
                                                                          Type::
                                                                              kMouseUp,
                                                                  x, y);
                                                              std::move(cb).Run(
                                                                  BrowserStatus::
                                                                      Ok());
                                                            },
                                                            base::Unretained(
                                                                wc),
                                                            x, y,
                                                            std::move(cb)),
                                                        base::Milliseconds(
                                                            up_delay));
                                          },
                                          base::Unretained(wc), x, y,
                                          std::move(cb)),
                                      base::Milliseconds(down_delay));
                            },
                            base::Unretained(wc), cx, cy, std::move(callback)));
                    return;
                  }
                  std::move(callback).Run(BrowserStatus::Error(
                      BrowserStatusCode::kNotFound, "Element not found"));
                },
                base::Unretained(web_contents), node_id, std::move(cb)),
            ui::AXMode::kWebContents, 0, base::Seconds(3),
            content::WebContents::AXTreeSnapshotPolicy::kAll);
      },
      weak_factory_.GetWeakPtr(), std::move(target_id), std::move(options),
      std::move(callback)));
}

void BrowserController::Hover(std::optional<std::string> target_id,
                              RefOptions options,
                              StatusCallback callback) {
  RunOrPost(base::BindOnce(
      [](base::WeakPtr<BrowserController> self,
         std::optional<std::string> id, RefOptions hover_options,
         StatusCallback cb) {
        if (!self || !self->runtime_) {
          std::move(cb).Run(SessionNotReady());
          return;
        }
        content::WebContents* web_contents = self->ResolveWebContents(id);
        if (!web_contents) {
          std::move(cb).Run(TabNotFound(id));
          return;
        }

        int node_id = 0;
        if (!ParseRef(hover_options.ref, &node_id)) {
          std::move(cb).Run(InvalidRef());
          return;
        }

        auto* rfh = web_contents->GetPrimaryMainFrame();
        if (!rfh) {
          std::move(cb).Run(NoActiveFrame());
          return;
        }

        ui::AXActionData action;
        action.action = ax::mojom::Action::kScrollToMakeVisible;
        action.target_node_id = static_cast<ui::AXNodeID>(node_id);
        rfh->AccessibilityPerformAction(action);

        web_contents->RequestAXTreeSnapshot(
            base::BindOnce(
                [](content::WebContents* wc, int target_node_id,
                   StatusCallback callback, ui::AXTreeUpdate& tree) {
                  for (const auto& node : tree.nodes) {
                    if (node.id != static_cast<ui::AXNodeID>(target_node_id)) {
                      continue;
                    }

                    gfx::RectF bounds = node.relative_bounds.bounds;
                    double cx = bounds.x() + bounds.width() / 2.0;
                    double cy = bounds.y() + bounds.height() / 2.0;

                    auto& ms = GetMouseState();
                    auto path = GenerateMousePath(ms.x, ms.y, cx, cy);
                    AnimateMousePath(
                        wc, std::move(path), 0,
                        base::BindOnce(
                            [](StatusCallback cb) {
                              std::move(cb).Run(BrowserStatus::Ok());
                            },
                            std::move(callback)));
                    return;
                  }
                  std::move(callback).Run(BrowserStatus::Error(
                      BrowserStatusCode::kNotFound, "Element not found"));
                },
                base::Unretained(web_contents), node_id, std::move(cb)),
            ui::AXMode::kWebContents, 0, base::Seconds(3),
            content::WebContents::AXTreeSnapshotPolicy::kAll);
      },
      weak_factory_.GetWeakPtr(), std::move(target_id), std::move(options),
      std::move(callback)));
}

void BrowserController::Type(std::optional<std::string> target_id,
                             TypeOptions options,
                             StatusCallback callback) {
  RunOrPost(base::BindOnce(
      [](base::WeakPtr<BrowserController> self,
         std::optional<std::string> id, TypeOptions type_options,
         StatusCallback cb) {
        if (!self || !self->runtime_) {
          std::move(cb).Run(SessionNotReady());
          return;
        }
        content::WebContents* web_contents = self->ResolveWebContents(id);
        if (!web_contents) {
          std::move(cb).Run(TabNotFound(id));
          return;
        }

        int node_id = 0;
        if (!ParseRef(type_options.ref, &node_id)) {
          std::move(cb).Run(InvalidRef());
          return;
        }

        auto* rfh = web_contents->GetPrimaryMainFrame();
        if (!rfh) {
          std::move(cb).Run(NoActiveFrame());
          return;
        }

        ui::AXActionData focus_action;
        focus_action.action = ax::mojom::Action::kFocus;
        focus_action.target_node_id = static_cast<ui::AXNodeID>(node_id);
        rfh->AccessibilityPerformAction(focus_action);

        if (type_options.clear) {
          ui::AXActionData clear_action;
          clear_action.action = ax::mojom::Action::kSetValue;
          clear_action.target_node_id = static_cast<ui::AXNodeID>(node_id);
          clear_action.value = "";
          rfh->AccessibilityPerformAction(clear_action);
        }

        auto state = std::make_shared<TypeState>();
        state->text = std::move(type_options.text);

        int initial_delay = 50 + base::RandInt(0, 80);
        base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
            FROM_HERE,
            base::BindOnce(&TypeNextChar, base::Unretained(web_contents),
                           std::move(state), std::move(cb)),
            base::Milliseconds(initial_delay));
      },
      weak_factory_.GetWeakPtr(), std::move(target_id), std::move(options),
      std::move(callback)));
}

void BrowserController::PressKey(std::optional<std::string> target_id,
                                 KeyOptions options,
                                 StatusCallback callback) {
  RunOrPost(base::BindOnce(
      [](base::WeakPtr<BrowserController> self,
         std::optional<std::string> id, KeyOptions key_options,
         StatusCallback cb) {
        if (!self || !self->runtime_) {
          std::move(cb).Run(SessionNotReady());
          return;
        }
        content::WebContents* web_contents = self->ResolveWebContents(id);
        if (!web_contents) {
          std::move(cb).Run(TabNotFound(id));
          return;
        }

        auto* rwhv = web_contents->GetRenderWidgetHostView();
        if (!rwhv) {
          std::move(cb).Run(NoRenderView());
          return;
        }

        KeyMapping mapping = MapKey(key_options.key);
        auto* rwh = rwhv->GetRenderWidgetHost();
        input::NativeWebKeyboardEvent down(
            blink::WebInputEvent::Type::kRawKeyDown,
            blink::WebInputEvent::kNoModifiers, ui::EventTimeForNow());
        down.windows_key_code = mapping.windows_key_code;
        down.dom_key = static_cast<uint32_t>(mapping.dom_key);
        rwh->ForwardKeyboardEvent(down);

        int up_delay = 40 + base::RandInt(0, 50);
        base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
            FROM_HERE,
            base::BindOnce(
                [](content::WebContents* wc, KeyMapping key_mapping,
                   StatusCallback cb) {
                  auto* rwhv = wc->GetRenderWidgetHostView();
                  if (!rwhv) {
                    std::move(cb).Run(NoRenderView());
                    return;
                  }

                  input::NativeWebKeyboardEvent up(
                      blink::WebInputEvent::Type::kKeyUp,
                      blink::WebInputEvent::kNoModifiers,
                      ui::EventTimeForNow());
                  up.windows_key_code = key_mapping.windows_key_code;
                  up.dom_key = static_cast<uint32_t>(key_mapping.dom_key);
                  rwhv->GetRenderWidgetHost()->ForwardKeyboardEvent(up);
                  std::move(cb).Run(BrowserStatus::Ok());
                },
                base::Unretained(web_contents), mapping, std::move(cb)),
            base::Milliseconds(up_delay));
      },
      weak_factory_.GetWeakPtr(), std::move(target_id), std::move(options),
      std::move(callback)));
}

void BrowserController::SelectOption(std::optional<std::string> target_id,
                                     SelectOptions options,
                                     StatusCallback callback) {
  RunOrPost(base::BindOnce(
      [](base::WeakPtr<BrowserController> self,
         std::optional<std::string> id, SelectOptions select_options,
         StatusCallback cb) {
        if (!self || !self->runtime_) {
          std::move(cb).Run(SessionNotReady());
          return;
        }
        content::WebContents* web_contents = self->ResolveWebContents(id);
        if (!web_contents) {
          std::move(cb).Run(TabNotFound(id));
          return;
        }

        int node_id = 0;
        if (!ParseRef(select_options.ref, &node_id)) {
          std::move(cb).Run(InvalidRef());
          return;
        }

        auto* rfh = web_contents->GetPrimaryMainFrame();
        if (!rfh) {
          std::move(cb).Run(NoActiveFrame());
          return;
        }

        ui::AXActionData focus_action;
        focus_action.action = ax::mojom::Action::kFocus;
        focus_action.target_node_id = static_cast<ui::AXNodeID>(node_id);
        rfh->AccessibilityPerformAction(focus_action);

        std::string js_values;
        for (const auto& value : select_options.values) {
          if (!js_values.empty()) {
            js_values += ",";
          }
          js_values += base::GetQuotedJSONString(value);
        }

        std::string expression =
            "(() => {"
            "  const el = document.activeElement;"
            "  if (!el || el.tagName !== 'SELECT')"
            "    return 'Error: focused element is not a select';"
            "  const vals = [" +
            js_values +
            "];"
            "  Array.from(el.options).forEach(o => {"
            "    o.selected = vals.includes(o.value);"
            "  });"
            "  el.dispatchEvent(new Event('change', {bubbles: true}));"
            "  return 'Selected ' + vals.length + ' option(s)';"
            "})()";

        EvalStatusJS(web_contents, std::move(expression), std::move(cb));
      },
      weak_factory_.GetWeakPtr(), std::move(target_id), std::move(options),
      std::move(callback)));
}

void BrowserController::Drag(std::optional<std::string> target_id,
                             DragOptions options,
                             StatusCallback callback) {
  RunOrPost(base::BindOnce(
      [](base::WeakPtr<BrowserController> self,
         std::optional<std::string> id, DragOptions drag_options,
         StatusCallback cb) {
        if (!self || !self->runtime_) {
          std::move(cb).Run(SessionNotReady());
          return;
        }
        content::WebContents* web_contents = self->ResolveWebContents(id);
        if (!web_contents) {
          std::move(cb).Run(TabNotFound(id));
          return;
        }

        int start_id = 0;
        int end_id = 0;
        if (!ParseRef(drag_options.start_ref, &start_id) ||
            !ParseRef(drag_options.end_ref, &end_id)) {
          std::move(cb).Run(InvalidRef());
          return;
        }

        web_contents->RequestAXTreeSnapshot(
            base::BindOnce(
                [](content::WebContents* wc, int source_id, int target_id,
                   StatusCallback callback, ui::AXTreeUpdate& tree) {
                  gfx::RectF start_bounds;
                  gfx::RectF end_bounds;
                  bool found_start = false;
                  bool found_end = false;
                  for (const auto& node : tree.nodes) {
                    if (node.id == static_cast<ui::AXNodeID>(source_id)) {
                      start_bounds = node.relative_bounds.bounds;
                      found_start = true;
                    }
                    if (node.id == static_cast<ui::AXNodeID>(target_id)) {
                      end_bounds = node.relative_bounds.bounds;
                      found_end = true;
                    }
                    if (found_start && found_end) {
                      break;
                    }
                  }
                  if (!found_start || !found_end) {
                    std::move(callback).Run(BrowserStatus::Error(
                        BrowserStatusCode::kNotFound,
                        "Element not found for drag"));
                    return;
                  }

                  double sx = start_bounds.x() + start_bounds.width() / 2.0;
                  double sy = start_bounds.y() + start_bounds.height() / 2.0;
                  double ex = end_bounds.x() + end_bounds.width() / 2.0;
                  double ey = end_bounds.y() + end_bounds.height() / 2.0;

                  auto& ms = GetMouseState();
                  auto path_to_start = GenerateMousePath(ms.x, ms.y, sx, sy);
                  AnimateMousePath(
                      wc, std::move(path_to_start), 0,
                      base::BindOnce(
                          [](content::WebContents* wc, double sx, double sy,
                             double ex, double ey, StatusCallback cb) {
                            auto* rwhv = wc->GetRenderWidgetHostView();
                            if (!rwhv) {
                              std::move(cb).Run(NoRenderView());
                              return;
                            }

                            ForwardMouseButton(
                                rwhv->GetRenderWidgetHost(),
                                blink::WebInputEvent::Type::kMouseDown, sx,
                                sy);

                            auto* rfh = wc->GetPrimaryMainFrame();
                            if (rfh) {
                              std::string js =
                                  "(() => {"
                                  "  var el = document.elementFromPoint(" +
                                  base::NumberToString(
                                      static_cast<int>(sx)) +
                                  "," +
                                  base::NumberToString(
                                      static_cast<int>(sy)) +
                                  ");"
                                  "  if (el) {"
                                  "    var dt = new DataTransfer();"
                                  "    el.dispatchEvent(new DragEvent('dragstart', {bubbles:true, dataTransfer:dt}));"
                                  "  }"
                                  "})()";
                              rfh->ExecuteJavaScriptForTests(
                                  base::UTF8ToUTF16(js), base::NullCallback(),
                                  content::ISOLATED_WORLD_ID_GLOBAL);
                            }

                            auto drag_path = GenerateMousePath(sx, sy, ex, ey);
                            base::SequencedTaskRunner::GetCurrentDefault()
                                ->PostDelayedTask(
                                    FROM_HERE,
                                    base::BindOnce(
                                        [](content::WebContents* wc,
                                           std::vector<BezierPoint> path,
                                           double ex, double ey,
                                           StatusCallback cb) {
                                          AnimateMousePath(
                                              wc, std::move(path), 0,
                                              base::BindOnce(
                                                  [](content::WebContents* wc,
                                                     double ex, double ey,
                                                     StatusCallback cb) {
                                                    auto* rwhv =
                                                        wc->GetRenderWidgetHostView();
                                                    if (!rwhv) {
                                                      std::move(cb).Run(
                                                          NoRenderView());
                                                      return;
                                                    }
                                                    ForwardMouseButton(
                                                        rwhv
                                                            ->GetRenderWidgetHost(),
                                                        blink::WebInputEvent::
                                                            Type::kMouseUp,
                                                        ex, ey);

                                                    auto* rfh =
                                                        wc->GetPrimaryMainFrame();
                                                    if (rfh) {
                                                      std::string js =
                                                          "(() => {"
                                                          "  var el = document.elementFromPoint(" +
                                                          base::NumberToString(
                                                              static_cast<int>(
                                                                  ex)) +
                                                          "," +
                                                          base::NumberToString(
                                                              static_cast<int>(
                                                                  ey)) +
                                                          ");"
                                                          "  if (el) {"
                                                          "    var dt = new DataTransfer();"
                                                          "    el.dispatchEvent(new DragEvent('drop', {bubbles:true, dataTransfer:dt}));"
                                                          "    el.dispatchEvent(new DragEvent('dragend', {bubbles:true, dataTransfer:dt}));"
                                                          "  }"
                                                          "})()";
                                                      rfh->ExecuteJavaScriptForTests(
                                                          base::UTF8ToUTF16(js),
                                                          base::NullCallback(),
                                                          content::
                                                              ISOLATED_WORLD_ID_GLOBAL);
                                                    }
                                                    std::move(cb).Run(
                                                        BrowserStatus::Ok());
                                                  },
                                                  base::Unretained(wc), ex, ey,
                                                  std::move(cb)));
                                        },
                                        base::Unretained(wc),
                                        std::move(drag_path), ex, ey,
                                        std::move(cb)),
                                    base::Milliseconds(50 +
                                                       base::RandInt(0, 50)));
                          },
                          base::Unretained(wc), sx, sy, ex, ey,
                          std::move(callback)));
                },
                base::Unretained(web_contents), start_id, end_id,
                std::move(cb)),
            ui::AXMode::kWebContents, 0, base::Seconds(3),
            content::WebContents::AXTreeSnapshotPolicy::kAll);
      },
      weak_factory_.GetWeakPtr(), std::move(target_id), std::move(options),
      std::move(callback)));
}

void BrowserController::Scroll(std::optional<std::string> target_id,
                               ScrollOptions options,
                               StatusCallback callback) {
  RunOrPost(base::BindOnce(
      [](base::WeakPtr<BrowserController> self,
         std::optional<std::string> id, ScrollOptions scroll_options,
         StatusCallback cb) {
        if (!self || !self->runtime_) {
          std::move(cb).Run(SessionNotReady());
          return;
        }
        content::WebContents* web_contents = self->ResolveWebContents(id);
        if (!web_contents) {
          std::move(cb).Run(TabNotFound(id));
          return;
        }

        int total =
            std::max(std::abs(scroll_options.delta_x),
                     std::abs(scroll_options.delta_y));
        if (total == 0) {
          std::move(cb).Run(BrowserStatus::Ok());
          return;
        }

        auto state = std::make_shared<ScrollState>();
        state->remaining = total;
        ScrollStep(web_contents, scroll_options.delta_x,
                   scroll_options.delta_y, std::move(state), std::move(cb));
      },
      weak_factory_.GetWeakPtr(), std::move(target_id), std::move(options),
      std::move(callback)));
}

}  // namespace browserd
