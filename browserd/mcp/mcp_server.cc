#include "browserd/mcp/mcp_server.h"

#include <cmath>
#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/rand_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "base/values.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_widget_host.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/isolated_world_ids.h"
#include "third_party/blink/public/common/input/web_mouse_event.h"
#include "ui/events/base_event_utils.h"

namespace browserd {

MCPServer::PendingToolCall::PendingToolCall(std::string name,
                                            base::DictValue args,
                                            base::Value id)
    : name(std::move(name)),
      args(std::move(args)),
      id(std::move(id)) {}
MCPServer::PendingToolCall::~PendingToolCall() = default;
MCPServer::PendingToolCall::PendingToolCall(PendingToolCall&&) = default;

MCPServer::MCPServer() = default;
MCPServer::~MCPServer() = default;

void MCPServer::Start(scoped_refptr<base::SequencedTaskRunner> task_runner) {
  transport_.Start(task_runner,
                   base::BindRepeating(&MCPServer::OnMessage,
                                       base::Unretained(this)));
  StartBehaviorSimulation();
}

void MCPServer::SetBrowser(headless::HeadlessBrowser* browser,
                           headless::HeadlessBrowserContext* context) {
  browser_ = browser;
  browser_context_ = context;
}

void MCPServer::SetWebContents(content::WebContents* web_contents) {
  web_contents_ = web_contents;
  Observe(web_contents);
}

void MCPServer::InjectScriptOnNewDocument(const std::string& source) {
  injected_scripts_.push_back(source);
}

void MCPServer::ReadyToCommitNavigation(
    content::NavigationHandle* navigation_handle) {
  if (!navigation_handle->IsInPrimaryMainFrame())
    return;

  auto* rfh = navigation_handle->GetRenderFrameHost();
  if (!rfh)
    return;

  for (const auto& script : injected_scripts_) {
    rfh->ExecuteJavaScriptWithUserGestureForTests(
        base::UTF8ToUTF16(script), base::NullCallback(),
        content::ISOLATED_WORLD_ID_GLOBAL);
  }
}

void MCPServer::RegisterTool(MCPToolDef tool) {
  tools_.push_back(std::move(tool));
}

void MCPServer::OnMessage(base::DictValue message) {
  const std::string* method = message.FindString("method");
  if (!method) {
    LOG(WARNING) << "MCP message missing method";
    return;
  }

  const base::DictValue* params = message.FindDict("params");
  base::DictValue empty_params;

  base::Value null_id;
  const base::Value* id_ptr = message.Find("id");
  const base::Value& id = id_ptr ? *id_ptr : null_id;

  if (*method == "initialize") {
    HandleInitialize(params ? *params : empty_params, id);
  } else if (*method == "notifications/initialized") {
    initialized_ = true;
  } else if (*method == "tools/list") {
    HandleToolsList(id);
  } else if (*method == "tools/call") {
    HandleToolsCall(params ? *params : empty_params, id);
  } else {
    if (id_ptr) {
      SendError(id, -32601, "Method not found: " + *method);
    }
  }
}

void MCPServer::HandleInitialize(const base::DictValue& params,
                                 const base::Value& id) {
  base::DictValue result;

  base::DictValue server_info;
  server_info.Set("name", "browserd");
  server_info.Set("version", "1.0.0");
  result.Set("serverInfo", std::move(server_info));

  result.Set("protocolVersion", "2024-11-05");

  base::DictValue capabilities;
  base::DictValue tools_cap;
  capabilities.Set("tools", std::move(tools_cap));
  result.Set("capabilities", std::move(capabilities));

  SendResult(id, std::move(result));
}

void MCPServer::HandleToolsList(const base::Value& id) {
  base::DictValue result;
  base::ListValue tool_list;

  for (const auto& tool : tools_) {
    base::DictValue tool_info;
    tool_info.Set("name", tool.name);
    tool_info.Set("description", tool.description);
    tool_info.Set("inputSchema", tool.input_schema.Clone());
    tool_list.Append(std::move(tool_info));
  }

  result.Set("tools", std::move(tool_list));
  SendResult(id, std::move(result));
}

void MCPServer::HandleToolsCall(const base::DictValue& params,
                                const base::Value& id) {
  const std::string* tool_name = params.FindString("name");
  if (!tool_name) {
    SendError(id, -32602, "Missing tool name");
    return;
  }

  const base::DictValue* arguments = params.FindDict("arguments");
  base::DictValue args = arguments ? arguments->Clone() : base::DictValue();

  tool_call_queue_.emplace(*tool_name, std::move(args), id.Clone());

  if (!tool_call_in_progress_) {
    ExecuteNextToolCall();
  }
}

void MCPServer::ExecuteNextToolCall() {
  if (tool_call_queue_.empty()) {
    tool_call_in_progress_ = false;
    return;
  }

  tool_call_in_progress_ = true;
  PendingToolCall call = std::move(tool_call_queue_.front());
  tool_call_queue_.pop();

  for (auto& tool : tools_) {
    if (tool.name == call.name) {
      tool.handler.Run(
          web_contents_, std::move(call.args),
          base::BindOnce(&MCPServer::OnToolCallComplete,
                         base::Unretained(this), std::move(call.id)));
      return;
    }
  }

  SendError(call.id, -32602, "Unknown tool: " + call.name);
  ExecuteNextToolCall();
}

void MCPServer::OnToolCallComplete(base::Value id,
                                   base::ListValue content,
                                   bool is_error) {
  base::DictValue result;
  result.Set("content", std::move(content));
  if (is_error) {
    result.Set("isError", true);
  }
  SendResult(id, std::move(result));

  ExecuteNextToolCall();
}

void MCPServer::SendResult(const base::Value& id, base::DictValue result) {
  base::DictValue response;
  response.Set("jsonrpc", "2.0");
  response.Set("id", id.Clone());
  response.Set("result", std::move(result));
  transport_.SendMessage(response);
}

void MCPServer::SendError(const base::Value& id,
                          int code,
                          const std::string& message) {
  base::DictValue response;
  response.Set("jsonrpc", "2.0");
  response.Set("id", id.Clone());

  base::DictValue error;
  error.Set("code", code);
  error.Set("message", message);
  response.Set("error", std::move(error));

  transport_.SendMessage(response);
}

void MCPServer::StartBehaviorSimulation() {
  mouse_x_ = 400.0 + base::RandDouble() * 200.0;
  mouse_y_ = 300.0 + base::RandDouble() * 200.0;
  behavior_timer_.Start(
      FROM_HERE, base::Milliseconds(80 + base::RandInt(0, 60)),
      base::BindRepeating(&MCPServer::DispatchMouseMove,
                           base::Unretained(this)));
}

void MCPServer::DispatchMouseMove() {
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

  if (!web_contents_)
    return;

  auto* rwhv = web_contents_->GetRenderWidgetHostView();
  if (!rwhv)
    return;

  auto* rwh = rwhv->GetRenderWidgetHost();
  if (!rwh)
    return;

  blink::WebMouseEvent event(
      blink::WebInputEvent::Type::kMouseMove,
      blink::WebInputEvent::kNoModifiers,
      ui::EventTimeForNow());
  event.SetPositionInWidget(
      static_cast<float>(mouse_x_), static_cast<float>(mouse_y_));
  event.SetPositionInScreen(
      static_cast<float>(mouse_x_), static_cast<float>(mouse_y_));
  rwh->ForwardMouseEvent(event);

  int next_ms = 70 + base::RandInt(0, 80);
  behavior_timer_.Start(
      FROM_HERE, base::Milliseconds(next_ms),
      base::BindRepeating(&MCPServer::DispatchMouseMove,
                           base::Unretained(this)));
}

}  // namespace browserd
