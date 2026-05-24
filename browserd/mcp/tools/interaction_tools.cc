#include "browserd/mcp/tools/interaction_tools.h"

#include <cmath>

#include "base/functional/bind.h"
#include "base/rand_util.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "browserd/mcp/mcp_server.h"
#include "browserd/mcp/mcp_tool.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_widget_host.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/isolated_world_ids.h"
#include "components/input/native_web_keyboard_event.h"
#include "third_party/blink/public/common/input/web_mouse_event.h"
#include "third_party/blink/public/common/input/web_mouse_wheel_event.h"
#include "ui/accessibility/ax_action_data.h"
#include "ui/accessibility/ax_mode.h"
#include "ui/accessibility/ax_node_data.h"
#include "ui/accessibility/ax_tree_update.h"
#include "ui/events/base_event_utils.h"
#include "ui/events/keycodes/dom/dom_code.h"
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

BezierPoint CubicBezier(BezierPoint p0, BezierPoint p1, BezierPoint p2,
                        BezierPoint p3, double t) {
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

std::vector<BezierPoint> GenerateMousePath(double from_x, double from_y,
                                           double to_x, double to_y) {
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
                        double x, double y) {
  blink::WebMouseEvent event(type, blink::WebInputEvent::kNoModifiers,
                             ui::EventTimeForNow());
  event.SetPositionInWidget(static_cast<float>(x), static_cast<float>(y));
  event.SetPositionInScreen(static_cast<float>(x), static_cast<float>(y));
  event.button = blink::WebMouseEvent::Button::kLeft;
  event.click_count = 1;
  rwh->ForwardMouseEvent(event);
}

void AnimateMousePath(content::WebContents* wc,
                      std::vector<BezierPoint> path,
                      size_t index,
                      base::OnceClosure on_done) {
  if (index >= path.size()) {
    std::move(on_done).Run();
    return;
  }

  auto* rwhv = wc->GetRenderWidgetHostView();
  if (!rwhv) {
    std::move(on_done).Run();
    return;
  }

  ForwardMouseMove(rwhv->GetRenderWidgetHost(), path[index].x, path[index].y);

  int delay_ms = 8 + base::RandInt(0, 12);
  base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&AnimateMousePath, base::Unretained(wc), std::move(path),
                     index + 1, std::move(on_done)),
      base::Milliseconds(delay_ms));
}

void EvalJS(content::WebContents* web_contents,
            const std::string& expression,
            const std::string& success_msg,
            ToolResultCallback callback) {
  auto* rfh = web_contents->GetPrimaryMainFrame();
  if (!rfh) {
    std::move(callback).Run(TextContent("No active frame"), true);
    return;
  }

  rfh->ExecuteJavaScriptForTests(
      base::UTF8ToUTF16(expression),
      base::BindOnce(
          [](std::string msg, ToolResultCallback cb, base::Value result) {
            if (result.is_string()) {
              const std::string& s = result.GetString();
              if (s.find("Error") != std::string::npos ||
                  s.find("not found") != std::string::npos) {
                std::move(cb).Run(TextContent(s), true);
                return;
              }
            }
            std::move(cb).Run(TextContent(msg), false);
          },
          std::move(success_msg), std::move(callback)),
      content::ISOLATED_WORLD_ID_GLOBAL);
}

void HandleClick(content::WebContents* web_contents,
                 base::DictValue args,
                 ToolResultCallback callback) {
  const std::string* ref = args.FindString("ref");
  if (!ref) {
    std::move(callback).Run(TextContent("Missing required parameter: ref"),
                            true);
    return;
  }

  int node_id = 0;
  if (!base::StringToInt(*ref, &node_id)) {
    std::move(callback).Run(TextContent("Invalid ref"), true);
    return;
  }

  auto* rfh = web_contents->GetPrimaryMainFrame();
  if (!rfh) {
    std::move(callback).Run(TextContent("No active frame"), true);
    return;
  }

  ui::AXActionData action;
  action.action = ax::mojom::Action::kScrollToMakeVisible;
  action.target_node_id = static_cast<ui::AXNodeID>(node_id);
  rfh->AccessibilityPerformAction(action);

  web_contents->RequestAXTreeSnapshot(
      base::BindOnce(
          [](content::WebContents* wc, int target_id, ToolResultCallback cb,
             ui::AXTreeUpdate& tree) {
            for (const auto& node : tree.nodes) {
              if (node.id != static_cast<ui::AXNodeID>(target_id))
                continue;

              gfx::RectF bounds = node.relative_bounds.bounds;
              double cx = bounds.x() + bounds.width() / 2.0 +
                          (base::RandDouble() - 0.5) * bounds.width() * 0.3;
              double cy = bounds.y() + bounds.height() / 2.0 +
                          (base::RandDouble() - 0.5) * bounds.height() * 0.3;

              auto& ms = GetMouseState();
              auto path = GenerateMousePath(ms.x, ms.y, cx, cy);

              AnimateMousePath(
                  wc, std::move(path), 0,
                  base::BindOnce(
                      [](content::WebContents* wc, double cx, double cy,
                         ToolResultCallback cb) {
                        if (!wc->GetRenderWidgetHostView()) {
                          std::move(cb).Run(TextContent("No render view"),
                                            true);
                          return;
                        }

                        int down_delay = 15 + base::RandInt(0, 30);
                        base::SequencedTaskRunner::GetCurrentDefault()
                            ->PostDelayedTask(
                                FROM_HERE,
                                base::BindOnce(
                                    [](content::WebContents* wc, double x,
                                       double y, ToolResultCallback cb) {
                                      auto* rwhv =
                                          wc->GetRenderWidgetHostView();
                                      if (!rwhv) {
                                        std::move(cb).Run(
                                            TextContent("No render view"),
                                            true);
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
                                                             WebContents* wc,
                                                         double x, double y,
                                                         ToolResultCallback
                                                             cb) {
                                                        auto* rwhv =
                                                            wc->GetRenderWidgetHostView();
                                                        if (!rwhv) {
                                                          std::move(cb).Run(
                                                              TextContent(
                                                                  "No render "
                                                                  "view"),
                                                              true);
                                                          return;
                                                        }
                                                        auto* rwh =
                                                            rwhv
                                                                ->GetRenderWidgetHost();

                                                        ForwardMouseButton(
                                                            rwh,
                                                            blink::
                                                                WebInputEvent::
                                                                    Type::
                                                                        kMouseUp,
                                                            x, y);

                                                        std::move(cb).Run(
                                                            TextContent(
                                                                "Clicked "
                                                                "element"),
                                                            false);
                                                      },
                                                      base::Unretained(wc), x,
                                                      y, std::move(cb)),
                                                  base::Milliseconds(
                                                      up_delay));
                                    },
                                    base::Unretained(wc), cx, cy,
                                    std::move(cb)),
                                base::Milliseconds(down_delay));
                      },
                      base::Unretained(wc), cx, cy, std::move(cb)));
              return;
            }
            std::move(cb).Run(TextContent("Element not found"), true);
          },
          base::Unretained(web_contents), node_id, std::move(callback)),
      ui::AXMode::kWebContents, 0, base::Seconds(3),
      content::WebContents::AXTreeSnapshotPolicy::kAll);
}

void HandleHover(content::WebContents* web_contents,
                 base::DictValue args,
                 ToolResultCallback callback) {
  const std::string* ref = args.FindString("ref");
  if (!ref) {
    std::move(callback).Run(TextContent("Missing required parameter: ref"),
                            true);
    return;
  }

  int node_id = 0;
  if (!base::StringToInt(*ref, &node_id)) {
    std::move(callback).Run(TextContent("Invalid ref"), true);
    return;
  }

  auto* rfh = web_contents->GetPrimaryMainFrame();
  if (!rfh) {
    std::move(callback).Run(TextContent("No active frame"), true);
    return;
  }

  ui::AXActionData action;
  action.action = ax::mojom::Action::kScrollToMakeVisible;
  action.target_node_id = static_cast<ui::AXNodeID>(node_id);
  rfh->AccessibilityPerformAction(action);

  web_contents->RequestAXTreeSnapshot(
      base::BindOnce(
          [](content::WebContents* wc, int target_id, ToolResultCallback cb,
             ui::AXTreeUpdate& tree) {
            for (const auto& node : tree.nodes) {
              if (node.id != static_cast<ui::AXNodeID>(target_id))
                continue;

              gfx::RectF bounds = node.relative_bounds.bounds;
              double cx = bounds.x() + bounds.width() / 2.0;
              double cy = bounds.y() + bounds.height() / 2.0;

              auto& ms = GetMouseState();
              auto path = GenerateMousePath(ms.x, ms.y, cx, cy);

              AnimateMousePath(
                  wc, std::move(path), 0,
                  base::BindOnce(
                      [](ToolResultCallback cb) {
                        std::move(cb).Run(TextContent("Hovered over element"),
                                          false);
                      },
                      std::move(cb)));
              return;
            }
            std::move(cb).Run(TextContent("Element not found"), true);
          },
          base::Unretained(web_contents), node_id, std::move(callback)),
      ui::AXMode::kWebContents, 0, base::Seconds(3),
      content::WebContents::AXTreeSnapshotPolicy::kAll);
}

struct TypeState {
  std::string text;
  size_t index = 0;
};

void TypeNextChar(content::WebContents* wc,
                  std::shared_ptr<TypeState> state,
                  ToolResultCallback callback) {
  if (state->index >= state->text.size()) {
    auto* rfh = wc->GetPrimaryMainFrame();
    if (rfh) {
      rfh->ExecuteJavaScriptForTests(
          u"document.activeElement?.dispatchEvent("
          u"  new Event('change', {bubbles: true}));",
          base::BindOnce(
              [](ToolResultCallback cb, base::Value) {
                std::move(cb).Run(TextContent("Typed text"), false);
              },
              std::move(callback)),
          content::ISOLATED_WORLD_ID_GLOBAL);
      return;
    }
    std::move(callback).Run(TextContent("Typed text"), false);
    return;
  }

  auto* rwhv = wc->GetRenderWidgetHostView();
  if (!rwhv) {
    std::move(callback).Run(TextContent("No render view"), true);
    return;
  }
  auto* rwh = rwhv->GetRenderWidgetHost();

  char c = state->text[state->index];
  int windows_key_code = 0;
  int modifiers = blink::WebInputEvent::kNoModifiers;
  ui::DomKey dom_key = ui::DomKey::FromCharacter(c);

  if (c >= 'a' && c <= 'z') {
    windows_key_code = ui::VKEY_A + (c - 'a');
  } else if (c >= 'A' && c <= 'Z') {
    windows_key_code = ui::VKEY_A + (c - 'A');
    modifiers = blink::WebInputEvent::kShiftKey;
  } else if (c >= '0' && c <= '9') {
    windows_key_code = ui::VKEY_0 + (c - '0');
  } else if (c == ' ') {
    windows_key_code = ui::VKEY_SPACE;
  } else {
    // Special characters — map to base key + shift where needed.
    switch (c) {
      case '!': windows_key_code = ui::VKEY_1; modifiers = blink::WebInputEvent::kShiftKey; break;
      case '@': windows_key_code = ui::VKEY_2; modifiers = blink::WebInputEvent::kShiftKey; break;
      case '#': windows_key_code = ui::VKEY_3; modifiers = blink::WebInputEvent::kShiftKey; break;
      case '$': windows_key_code = ui::VKEY_4; modifiers = blink::WebInputEvent::kShiftKey; break;
      case '%': windows_key_code = ui::VKEY_5; modifiers = blink::WebInputEvent::kShiftKey; break;
      case '^': windows_key_code = ui::VKEY_6; modifiers = blink::WebInputEvent::kShiftKey; break;
      case '&': windows_key_code = ui::VKEY_7; modifiers = blink::WebInputEvent::kShiftKey; break;
      case '*': windows_key_code = ui::VKEY_8; modifiers = blink::WebInputEvent::kShiftKey; break;
      case '(': windows_key_code = ui::VKEY_9; modifiers = blink::WebInputEvent::kShiftKey; break;
      case ')': windows_key_code = ui::VKEY_0; modifiers = blink::WebInputEvent::kShiftKey; break;
      case '-': windows_key_code = ui::VKEY_OEM_MINUS; break;
      case '_': windows_key_code = ui::VKEY_OEM_MINUS; modifiers = blink::WebInputEvent::kShiftKey; break;
      case '=': windows_key_code = ui::VKEY_OEM_PLUS; break;
      case '+': windows_key_code = ui::VKEY_OEM_PLUS; modifiers = blink::WebInputEvent::kShiftKey; break;
      case '[': windows_key_code = ui::VKEY_OEM_4; break;
      case '{': windows_key_code = ui::VKEY_OEM_4; modifiers = blink::WebInputEvent::kShiftKey; break;
      case ']': windows_key_code = ui::VKEY_OEM_6; break;
      case '}': windows_key_code = ui::VKEY_OEM_6; modifiers = blink::WebInputEvent::kShiftKey; break;
      case '\\': windows_key_code = ui::VKEY_OEM_5; break;
      case '|': windows_key_code = ui::VKEY_OEM_5; modifiers = blink::WebInputEvent::kShiftKey; break;
      case ';': windows_key_code = ui::VKEY_OEM_1; break;
      case ':': windows_key_code = ui::VKEY_OEM_1; modifiers = blink::WebInputEvent::kShiftKey; break;
      case '\'': windows_key_code = ui::VKEY_OEM_7; break;
      case '"': windows_key_code = ui::VKEY_OEM_7; modifiers = blink::WebInputEvent::kShiftKey; break;
      case ',': windows_key_code = ui::VKEY_OEM_COMMA; break;
      case '<': windows_key_code = ui::VKEY_OEM_COMMA; modifiers = blink::WebInputEvent::kShiftKey; break;
      case '.': windows_key_code = ui::VKEY_OEM_PERIOD; break;
      case '>': windows_key_code = ui::VKEY_OEM_PERIOD; modifiers = blink::WebInputEvent::kShiftKey; break;
      case '/': windows_key_code = ui::VKEY_OEM_2; break;
      case '?': windows_key_code = ui::VKEY_OEM_2; modifiers = blink::WebInputEvent::kShiftKey; break;
      case '`': windows_key_code = ui::VKEY_OEM_3; break;
      case '~': windows_key_code = ui::VKEY_OEM_3; modifiers = blink::WebInputEvent::kShiftKey; break;
      default: windows_key_code = 0; break;
    }
  }

  input::NativeWebKeyboardEvent down(
      blink::WebInputEvent::Type::kRawKeyDown,
      modifiers,
      ui::EventTimeForNow());
  down.windows_key_code = windows_key_code;
  down.dom_key = static_cast<uint32_t>(dom_key);
  rwh->ForwardKeyboardEvent(down);

  input::NativeWebKeyboardEvent char_event(
      blink::WebInputEvent::Type::kChar,
      modifiers,
      ui::EventTimeForNow());
  char_event.windows_key_code = c;
  char_event.dom_key = static_cast<uint32_t>(dom_key);
  char_event.text[0] = c;
  char_event.unmodified_text[0] = c;
  rwh->ForwardKeyboardEvent(char_event);

  int up_delay = 20 + base::RandInt(0, 40);
  base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(
          [](content::WebContents* wc, int key_code, ui::DomKey dk,
             int mods, std::shared_ptr<TypeState> st, ToolResultCallback cb) {
            auto* rwhv = wc->GetRenderWidgetHostView();
            if (!rwhv) {
              std::move(cb).Run(TextContent("No render view"), true);
              return;
            }

            input::NativeWebKeyboardEvent up(
                blink::WebInputEvent::Type::kKeyUp,
                mods,
                ui::EventTimeForNow());
            up.windows_key_code = key_code;
            up.dom_key = static_cast<uint32_t>(dk);
            rwhv->GetRenderWidgetHost()->ForwardKeyboardEvent(up);

            st->index++;

            int next_delay = 30 + base::RandInt(0, 90);
            base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
                FROM_HERE,
                base::BindOnce(&TypeNextChar, base::Unretained(wc),
                               std::move(st), std::move(cb)),
                base::Milliseconds(next_delay));
          },
          base::Unretained(wc), windows_key_code, dom_key, modifiers,
          std::move(state), std::move(callback)),
      base::Milliseconds(up_delay));
}

void HandleType(content::WebContents* web_contents,
                base::DictValue args,
                ToolResultCallback callback) {
  const std::string* ref = args.FindString("ref");
  const std::string* text = args.FindString("text");
  if (!ref || !text) {
    std::move(callback).Run(
        TextContent("Missing required parameters: ref, text"), true);
    return;
  }

  int node_id = 0;
  if (!base::StringToInt(*ref, &node_id)) {
    std::move(callback).Run(TextContent("Invalid ref"), true);
    return;
  }

  auto* rfh = web_contents->GetPrimaryMainFrame();
  if (!rfh) {
    std::move(callback).Run(TextContent("No active frame"), true);
    return;
  }

  bool clear = args.FindBool("clear").value_or(false);

  ui::AXActionData focus_action;
  focus_action.action = ax::mojom::Action::kFocus;
  focus_action.target_node_id = static_cast<ui::AXNodeID>(node_id);
  rfh->AccessibilityPerformAction(focus_action);

  if (clear) {
    ui::AXActionData clear_action;
    clear_action.action = ax::mojom::Action::kSetValue;
    clear_action.target_node_id = static_cast<ui::AXNodeID>(node_id);
    clear_action.value = "";
    rfh->AccessibilityPerformAction(clear_action);
  }

  auto state = std::make_shared<TypeState>();
  state->text = *text;

  int initial_delay = 50 + base::RandInt(0, 80);
  base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&TypeNextChar, base::Unretained(web_contents),
                     std::move(state), std::move(callback)),
      base::Milliseconds(initial_delay));
}

void HandlePressKey(content::WebContents* web_contents,
                    base::DictValue args,
                    ToolResultCallback callback) {
  const std::string* key = args.FindString("key");
  if (!key) {
    std::move(callback).Run(TextContent("Missing required parameter: key"),
                            true);
    return;
  }

  auto* rwhv = web_contents->GetRenderWidgetHostView();
  if (!rwhv) {
    std::move(callback).Run(TextContent("No render view"), true);
    return;
  }
  auto* rwh = rwhv->GetRenderWidgetHost();

  int windows_key_code = 0;
  ui::DomKey dom_key = ui::DomKey::NONE;

  if (*key == "Enter") {
    windows_key_code = ui::VKEY_RETURN;
    dom_key = ui::DomKey::ENTER;
  } else if (*key == "Tab") {
    windows_key_code = ui::VKEY_TAB;
    dom_key = ui::DomKey::TAB;
  } else if (*key == "Escape") {
    windows_key_code = ui::VKEY_ESCAPE;
    dom_key = ui::DomKey::ESCAPE;
  } else if (*key == "Backspace") {
    windows_key_code = ui::VKEY_BACK;
    dom_key = ui::DomKey::BACKSPACE;
  } else if (*key == "Delete") {
    windows_key_code = ui::VKEY_DELETE;
    dom_key = ui::DomKey::DEL;
  } else if (*key == "ArrowUp") {
    windows_key_code = ui::VKEY_UP;
    dom_key = ui::DomKey::ARROW_UP;
  } else if (*key == "ArrowDown") {
    windows_key_code = ui::VKEY_DOWN;
    dom_key = ui::DomKey::ARROW_DOWN;
  } else if (*key == "ArrowLeft") {
    windows_key_code = ui::VKEY_LEFT;
    dom_key = ui::DomKey::ARROW_LEFT;
  } else if (*key == "ArrowRight") {
    windows_key_code = ui::VKEY_RIGHT;
    dom_key = ui::DomKey::ARROW_RIGHT;
  } else if (*key == " " || *key == "Space") {
    windows_key_code = ui::VKEY_SPACE;
    dom_key = ui::DomKey::FromCharacter(' ');
  } else if (key->length() == 1) {
    char c = (*key)[0];
    if (c >= 'a' && c <= 'z')
      windows_key_code = ui::VKEY_A + (c - 'a');
    else if (c >= 'A' && c <= 'Z')
      windows_key_code = ui::VKEY_A + (c - 'A');
    else if (c >= '0' && c <= '9')
      windows_key_code = ui::VKEY_0 + (c - '0');
    dom_key = ui::DomKey::FromCharacter(c);
  }

  input::NativeWebKeyboardEvent down(
      blink::WebInputEvent::Type::kRawKeyDown,
      blink::WebInputEvent::kNoModifiers,
      ui::EventTimeForNow());
  down.windows_key_code = windows_key_code;
  down.dom_key = static_cast<uint32_t>(dom_key);
  rwh->ForwardKeyboardEvent(down);

  int up_delay = 40 + base::RandInt(0, 50);
  std::string key_name = *key;
  base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(
          [](content::WebContents* wc, int key_code, ui::DomKey dk,
             std::string key_name, ToolResultCallback cb) {
            auto* rwhv = wc->GetRenderWidgetHostView();
            if (!rwhv) {
              std::move(cb).Run(TextContent("No render view"), true);
              return;
            }

            input::NativeWebKeyboardEvent up(
                blink::WebInputEvent::Type::kKeyUp,
                blink::WebInputEvent::kNoModifiers,
                ui::EventTimeForNow());
            up.windows_key_code = key_code;
            up.dom_key = static_cast<uint32_t>(dk);
            rwhv->GetRenderWidgetHost()->ForwardKeyboardEvent(up);

            std::move(cb).Run(TextContent("Pressed key: " + key_name), false);
          },
          base::Unretained(web_contents), windows_key_code, dom_key,
          std::move(key_name), std::move(callback)),
      base::Milliseconds(up_delay));
}

void HandleSelectOption(content::WebContents* web_contents,
                        base::DictValue args,
                        ToolResultCallback callback) {
  const std::string* ref = args.FindString("ref");
  const base::ListValue* values = args.FindList("values");
  if (!ref || !values) {
    std::move(callback).Run(
        TextContent("Missing required parameters: ref, values"), true);
    return;
  }

  int node_id = 0;
  if (!base::StringToInt(*ref, &node_id)) {
    std::move(callback).Run(TextContent("Invalid ref"), true);
    return;
  }

  auto* rfh = web_contents->GetPrimaryMainFrame();
  if (!rfh) {
    std::move(callback).Run(TextContent("No active frame"), true);
    return;
  }

  ui::AXActionData focus_action;
  focus_action.action = ax::mojom::Action::kFocus;
  focus_action.target_node_id = static_cast<ui::AXNodeID>(node_id);
  rfh->AccessibilityPerformAction(focus_action);

  std::string js_values;
  for (const auto& v : *values) {
    const std::string* s = v.GetIfString();
    if (!s) continue;
    if (!js_values.empty()) js_values += ",";
    js_values += "'" + *s + "'";
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

  EvalJS(web_contents, expression, "Options selected", std::move(callback));
}

void HandleDragAndDrop(content::WebContents* web_contents,
                       base::DictValue args,
                       ToolResultCallback callback) {
  const std::string* start_ref = args.FindString("startRef");
  const std::string* end_ref = args.FindString("endRef");
  if (!start_ref || !end_ref) {
    std::move(callback).Run(
        TextContent("Missing required parameters: startRef, endRef"), true);
    return;
  }

  int start_id = 0, end_id = 0;
  if (!base::StringToInt(*start_ref, &start_id) ||
      !base::StringToInt(*end_ref, &end_id)) {
    std::move(callback).Run(TextContent("Invalid ref"), true);
    return;
  }

  web_contents->RequestAXTreeSnapshot(
      base::BindOnce(
          [](content::WebContents* wc, int s_id, int e_id,
             ToolResultCallback cb, ui::AXTreeUpdate& tree) {
            gfx::RectF start_bounds, end_bounds;
            bool found_start = false, found_end = false;
            for (const auto& node : tree.nodes) {
              if (node.id == static_cast<ui::AXNodeID>(s_id)) {
                start_bounds = node.relative_bounds.bounds;
                found_start = true;
              }
              if (node.id == static_cast<ui::AXNodeID>(e_id)) {
                end_bounds = node.relative_bounds.bounds;
                found_end = true;
              }
              if (found_start && found_end) break;
            }
            if (!found_start || !found_end) {
              std::move(cb).Run(TextContent("Element not found for drag"),
                                true);
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
                       double ex, double ey, ToolResultCallback cb) {
                      auto* rwhv = wc->GetRenderWidgetHostView();
                      if (!rwhv) {
                        std::move(cb).Run(TextContent("No render view"), true);
                        return;
                      }

                      ForwardMouseButton(
                          rwhv->GetRenderWidgetHost(),
                          blink::WebInputEvent::Type::kMouseDown, sx, sy);

                      auto* rfh = wc->GetPrimaryMainFrame();
                      if (rfh) {
                        std::string js =
                            "(() => {"
                            "  var el = document.elementFromPoint(" +
                            base::NumberToString(static_cast<int>(sx)) + "," +
                            base::NumberToString(static_cast<int>(sy)) +
                            ");"
                            "  if (el) {"
                            "    var dt = new DataTransfer();"
                            "    el.dispatchEvent(new DragEvent('dragstart', {bubbles:true, dataTransfer:dt}));"
                            "  }"
                            "})()";
                        rfh->ExecuteJavaScriptForTests(
                            base::UTF8ToUTF16(js),
                            base::NullCallback(),
                            content::ISOLATED_WORLD_ID_GLOBAL);
                      }

                      auto drag_path = GenerateMousePath(sx, sy, ex, ey);
                      base::SequencedTaskRunner::GetCurrentDefault()
                          ->PostDelayedTask(
                              FROM_HERE,
                              base::BindOnce(
                                  [](content::WebContents* wc,
                                     std::vector<BezierPoint> path, double ex,
                                     double ey, ToolResultCallback cb) {
                                    AnimateMousePath(
                                        wc, std::move(path), 0,
                                        base::BindOnce(
                                            [](content::WebContents* wc,
                                               double ex, double ey,
                                               ToolResultCallback cb) {
                                              auto* rwhv =
                                                  wc
                                                      ->GetRenderWidgetHostView();
                                              if (!rwhv) {
                                                std::move(cb).Run(
                                                    TextContent(
                                                        "No render view"),
                                                    true);
                                                return;
                                              }
                                              ForwardMouseButton(
                                                  rwhv->GetRenderWidgetHost(),
                                                  blink::WebInputEvent::Type::
                                                      kMouseUp,
                                                  ex, ey);

                                              auto* rfh = wc->GetPrimaryMainFrame();
                                              if (rfh) {
                                                std::string js =
                                                    "(() => {"
                                                    "  var el = document.elementFromPoint(" +
                                                    base::NumberToString(static_cast<int>(ex)) + "," +
                                                    base::NumberToString(static_cast<int>(ey)) +
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
                                                    content::ISOLATED_WORLD_ID_GLOBAL);
                                              }

                                              std::move(cb).Run(
                                                  TextContent("Drag and drop "
                                                              "completed"),
                                                  false);
                                            },
                                            base::Unretained(wc), ex, ey,
                                            std::move(cb)));
                                  },
                                  base::Unretained(wc), std::move(drag_path),
                                  ex, ey, std::move(cb)),
                              base::Milliseconds(50 + base::RandInt(0, 50)));
                    },
                    base::Unretained(wc), sx, sy, ex, ey, std::move(cb)));
          },
          base::Unretained(web_contents), start_id, end_id,
          std::move(callback)),
      ui::AXMode::kWebContents, 0, base::Seconds(3),
      content::WebContents::AXTreeSnapshotPolicy::kAll);
}

struct ScrollState {
  int remaining;
  int step_direction;
};

void ScrollStep(content::WebContents* wc,
                int delta_x, int delta_y,
                std::shared_ptr<ScrollState> state,
                ToolResultCallback callback) {
  if (state->remaining <= 0) {
    std::move(callback).Run(TextContent("Scrolled page"), false);
    return;
  }

  auto* rwhv = wc->GetRenderWidgetHostView();
  if (!rwhv) {
    std::move(callback).Run(TextContent("No render view"), true);
    return;
  }

  int step_x = 0, step_y = 0;
  int chunk = std::min(state->remaining, 40 + base::RandInt(0, 40));
  if (delta_y != 0)
    step_y = (delta_y > 0 ? 1 : -1) * chunk;
  if (delta_x != 0)
    step_x = (delta_x > 0 ? 1 : -1) * chunk;
  state->remaining -= chunk;

  auto& ms = GetMouseState();
  blink::WebMouseWheelEvent wheel(blink::WebInputEvent::Type::kMouseWheel,
                                  blink::WebInputEvent::kNoModifiers,
                                  ui::EventTimeForNow());
  wheel.SetPositionInWidget(static_cast<float>(ms.x),
                            static_cast<float>(ms.y));
  wheel.SetPositionInScreen(static_cast<float>(ms.x),
                            static_cast<float>(ms.y));
  wheel.delta_x = static_cast<float>(-step_x);
  wheel.delta_y = static_cast<float>(-step_y);
  wheel.phase = blink::WebMouseWheelEvent::kPhaseBegan;
  rwhv->GetRenderWidgetHost()->ForwardWheelEvent(wheel);

  int delay = 12 + base::RandInt(0, 18);
  base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&ScrollStep, base::Unretained(wc), delta_x, delta_y,
                     std::move(state), std::move(callback)),
      base::Milliseconds(delay));
}

void HandleScroll(content::WebContents* web_contents,
                  base::DictValue args,
                  ToolResultCallback callback) {
  int delta_x = args.FindInt("deltaX").value_or(0);
  int delta_y = args.FindInt("deltaY").value_or(0);

  int total = std::max(std::abs(delta_x), std::abs(delta_y));
  if (total == 0) {
    std::move(callback).Run(TextContent("Scrolled page"), false);
    return;
  }

  auto state = std::make_shared<ScrollState>();
  state->remaining = total;

  ScrollStep(web_contents, delta_x, delta_y, std::move(state),
             std::move(callback));
}

}  // namespace

void RegisterInteractionTools(MCPServer& server) {
  server.RegisterTool({
      "browser_click",
      "Click an element on the page using its accessibility ref",
      SchemaObject(base::DictValue().Set("ref", SchemaString("Element ref from accessibility snapshot")),
                   {"ref"}),
      base::BindRepeating(&HandleClick),
  });

  server.RegisterTool({
      "browser_hover",
      "Hover over an element on the page",
      SchemaObject(base::DictValue().Set("ref", SchemaString("Element ref from accessibility snapshot")),
                   {"ref"}),
      base::BindRepeating(&HandleHover),
  });

  server.RegisterTool({
      "browser_type",
      "Type text into a focused element",
      SchemaObject(
          base::DictValue().Set("ref", SchemaString("Element ref from accessibility snapshot")).Set("text", SchemaString("Text to type")).Set("clear", SchemaBool("Clear existing text before typing")),
          {"ref", "text"}),
      base::BindRepeating(&HandleType),
  });

  server.RegisterTool({
      "browser_press_key",
      "Press a keyboard key or key combination",
      SchemaObject(base::DictValue().Set("key", SchemaString("Key to press (e.g. Enter, Tab, ArrowDown)")),
                   {"key"}),
      base::BindRepeating(&HandlePressKey),
  });

  server.RegisterTool({
      "browser_select_option",
      "Select option(s) in a select element",
      SchemaObject(
          base::DictValue()
              .Set("ref", SchemaString("Element ref from accessibility snapshot"))
              .Set("values", base::DictValue()
                  .Set("type", "array")
                  .Set("description", "Option values to select")
                  .Set("items", base::DictValue().Set("type", "string"))),
          {"ref", "values"}),
      base::BindRepeating(&HandleSelectOption),
  });

  server.RegisterTool({
      "browser_drag",
      "Drag and drop from one element to another",
      SchemaObject(
          base::DictValue().Set("startRef", SchemaString("Source element ref")).Set("endRef", SchemaString("Target element ref")),
          {"startRef", "endRef"}),
      base::BindRepeating(&HandleDragAndDrop),
  });

  server.RegisterTool({
      "browser_scroll",
      "Scroll the page",
      SchemaObject(base::DictValue().Set("deltaX", SchemaInt("Horizontal scroll amount in pixels")).Set("deltaY", SchemaInt("Vertical scroll amount in pixels")),
                   {}),
      base::BindRepeating(&HandleScroll),
  });
}

}  // namespace browserd
