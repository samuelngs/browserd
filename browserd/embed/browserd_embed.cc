#define BROWSERD_EMBED_IMPLEMENTATION
#include "browserd/embed/browserd_embed.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/containers/span.h"
#include "base/command_line.h"
#include "base/environment.h"
#include "base/files/file_path.h"
#include "base/functional/bind.h"
#include "base/location.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/single_thread_task_runner.h"
#include "browserd/browser_runtime.h"
#include "browserd/core/browser_controller.h"
#include "browserd/headless_runtime.h"
#include "browserd/startup/browserd_main.h"
#include "browserd/switches.h"
#include "content/public/common/content_switches.h"
#include "headless/public/headless_browser.h"
#include "ui/gfx/geometry/size.h"
#include "url/gurl.h"

struct browserd_session {
  std::unique_ptr<browserd::BrowserController> controller;
};

namespace {

using browserd::BrowserController;
using browserd::BrowserResult;
using browserd::BrowserStatus;
using browserd::BrowserStatusCode;
using browserd::BrowserTabInfo;
using browserd::CookieInfo;
using browserd::EvaluateResult;

constexpr int kScheduleOk = 0;
constexpr int kScheduleRejected = 1;

browserd_status_code_t ToCStatusCode(BrowserStatusCode code) {
  switch (code) {
    case BrowserStatusCode::kOk:
      return BROWSERD_STATUS_OK;
    case BrowserStatusCode::kInvalidArgument:
      return BROWSERD_STATUS_INVALID_ARGUMENT;
    case BrowserStatusCode::kSessionNotReady:
      return BROWSERD_STATUS_SESSION_NOT_READY;
    case BrowserStatusCode::kTabNotFound:
      return BROWSERD_STATUS_TAB_NOT_FOUND;
    case BrowserStatusCode::kNoActiveFrame:
      return BROWSERD_STATUS_NO_ACTIVE_FRAME;
    case BrowserStatusCode::kNavigationFailed:
      return BROWSERD_STATUS_NAVIGATION_FAILED;
    case BrowserStatusCode::kScreenshotFailed:
      return BROWSERD_STATUS_SCREENSHOT_FAILED;
    case BrowserStatusCode::kInternalError:
      return BROWSERD_STATUS_INTERNAL_ERROR;
    case BrowserStatusCode::kTimeout:
      return BROWSERD_STATUS_TIMEOUT;
    case BrowserStatusCode::kNotFound:
      return BROWSERD_STATUS_NOT_FOUND;
  }
}

browserd_status_t ToCStatus(const BrowserStatus& status) {
  browserd_status_t c_status;
  c_status.code = ToCStatusCode(status.code);
  c_status.message = status.message.empty() ? "" : status.message.c_str();
  c_status.message_len = status.message.size();
  return c_status;
}

std::optional<std::string> OptionalString(const char* value) {
  if (!value || value[0] == '\0') {
    return std::nullopt;
  }
  return std::string(value);
}

browserd_string_t ToCStringView(const std::string& value) {
  return {value.data(), value.size()};
}

browserd_bytes_t ToCBytesView(const std::vector<uint8_t>& value) {
  return {value.data(), value.size()};
}

browserd_tab_info_t ToCTabInfo(const BrowserTabInfo& tab) {
  return {
      tab.target_id.data(),
      tab.target_id.size(),
      tab.title.data(),
      tab.title.size(),
      tab.url.data(),
      tab.url.size(),
  };
}

browserd_cookie_t ToCCookie(const CookieInfo& cookie) {
  return {
      cookie.name.data(),
      cookie.name.size(),
      cookie.value.data(),
      cookie.value.size(),
      cookie.domain.data(),
      cookie.domain.size(),
      cookie.path.data(),
      cookie.path.size(),
      cookie.secure,
      cookie.http_only,
      cookie.expires_unix_seconds,
  };
}

using ControllerTask = base::OnceCallback<void(BrowserController*)>;

int Schedule(browserd_session_t* session, ControllerTask task) {
  if (!session || !session->controller) {
    return kScheduleRejected;
  }
  BrowserController* controller = session->controller.get();
  scoped_refptr<base::SequencedTaskRunner> runner = controller->task_runner();
  if (!runner) {
    return kScheduleRejected;
  }
  runner->PostTask(
      FROM_HERE,
      base::BindOnce(
          [](BrowserController* controller, ControllerTask task) {
            std::move(task).Run(controller);
          },
          base::Unretained(controller), std::move(task)));
  return kScheduleOk;
}

void InvokeStatus(browserd_session_t* session,
                  browserd_status_callback callback,
                  void* user_data,
                  BrowserStatus status) {
  callback(session, ToCStatus(status), user_data);
}

void InvokeString(browserd_session_t* session,
                  browserd_string_callback callback,
                  void* user_data,
                  BrowserStatus status,
                  const std::string& value) {
  callback(session, ToCStatus(status), ToCStringView(value), user_data);
}

void InvokeBytes(browserd_session_t* session,
                 browserd_bytes_callback callback,
                 void* user_data,
                 BrowserStatus status,
                 const std::vector<uint8_t>& value) {
  callback(session, ToCStatus(status), ToCBytesView(value), user_data);
}

void CompleteStatus(browserd_session_t* session,
                    browserd_status_callback callback,
                    void* user_data,
                    BrowserStatus status) {
  InvokeStatus(session, callback, user_data, std::move(status));
}

void CompleteEvaluate(browserd_session_t* session,
                      browserd_string_callback callback,
                      void* user_data,
                      BrowserResult<EvaluateResult> result) {
  if (!result.ok()) {
    InvokeString(session, callback, user_data, result.status, std::string());
    return;
  }
  std::string value =
      result.value->kind == EvaluateResult::Kind::kUndefined
          ? "undefined"
          : result.value->json;
  InvokeString(session, callback, user_data, BrowserStatus::Ok(), value);
}

void CompleteString(browserd_session_t* session,
                    browserd_string_callback callback,
                    void* user_data,
                    BrowserResult<std::string> result) {
  if (!result.ok()) {
    InvokeString(session, callback, user_data, result.status, std::string());
    return;
  }
  InvokeString(session, callback, user_data, BrowserStatus::Ok(),
               result.value.value());
}

void CompleteBytes(browserd_session_t* session,
                   browserd_bytes_callback callback,
                   void* user_data,
                   BrowserResult<std::vector<uint8_t>> result) {
  if (!result.ok()) {
    InvokeBytes(session, callback, user_data, result.status,
                std::vector<uint8_t>());
    return;
  }
  InvokeBytes(session, callback, user_data, BrowserStatus::Ok(),
              result.value.value());
}

void CompleteTab(browserd_session_t* session,
                 browserd_tab_callback callback,
                 void* user_data,
                 BrowserResult<BrowserTabInfo> result) {
  if (!result.ok()) {
    callback(session, ToCStatus(result.status), nullptr, user_data);
    return;
  }
  browserd_tab_info_t tab = ToCTabInfo(result.value.value());
  callback(session, ToCStatus(BrowserStatus::Ok()), &tab, user_data);
}

void CompleteTabs(browserd_session_t* session,
                  browserd_tabs_callback callback,
                  void* user_data,
                  BrowserResult<std::vector<BrowserTabInfo>> result) {
  if (!result.ok()) {
    callback(session, ToCStatus(result.status), nullptr, 0, user_data);
    return;
  }
  std::vector<browserd_tab_info_t> tabs;
  tabs.reserve(result.value->size());
  for (const auto& tab : result.value.value()) {
    tabs.push_back(ToCTabInfo(tab));
  }
  callback(session, ToCStatus(BrowserStatus::Ok()), tabs.data(), tabs.size(),
           user_data);
}

void CompleteCookie(browserd_session_t* session,
                    browserd_cookie_callback callback,
                    void* user_data,
                    BrowserResult<CookieInfo> result) {
  if (!result.ok()) {
    callback(session, ToCStatus(result.status), nullptr, user_data);
    return;
  }
  browserd_cookie_t cookie = ToCCookie(result.value.value());
  callback(session, ToCStatus(BrowserStatus::Ok()), &cookie, user_data);
}

void CompleteCookies(browserd_session_t* session,
                     browserd_cookies_callback callback,
                     void* user_data,
                     BrowserResult<std::vector<CookieInfo>> result) {
  if (!result.ok()) {
    callback(session, ToCStatus(result.status), nullptr, 0, user_data);
    return;
  }
  std::vector<browserd_cookie_t> cookies;
  cookies.reserve(result.value->size());
  for (const auto& cookie : result.value.value()) {
    cookies.push_back(ToCCookie(cookie));
  }
  callback(session, ToCStatus(BrowserStatus::Ok()), cookies.data(),
           cookies.size(), user_data);
}

void DestroySessionController(browserd_session_t* session) {
  if (!session || !session->controller) {
    return;
  }

  std::unique_ptr<BrowserController> controller =
      std::move(session->controller);
  if (browserd::BrowserRuntime* runtime = controller->runtime()) {
    runtime->Shutdown();
  }
}

struct RunState {
  browserd_ready_callback ready = nullptr;
  void* user_data = nullptr;
  std::unique_ptr<browserd_session_t> session;
};

void NotifyReady(RunState* state,
                 std::unique_ptr<browserd::BrowserRuntime> runtime,
                 scoped_refptr<base::SequencedTaskRunner> task_runner) {
  state->session = std::make_unique<browserd_session_t>();
  state->session->controller =
      std::make_unique<BrowserController>(std::move(runtime), task_runner);
  state->ready(state->session.get(), ToCStatus(BrowserStatus::Ok()),
               state->user_data);
}

void OnHeadlessBrowserStart(RunState* state,
                            headless::HeadlessBrowser* browser) {
  auto runtime = std::make_unique<browserd::HeadlessRuntime>(browser);
  if (!runtime->Initialize()) {
    BrowserStatus status = BrowserStatus::Error(
        BrowserStatusCode::kInternalError,
        "Failed to create initial web contents");
    state->ready(nullptr, ToCStatus(status), state->user_data);
    browser->Shutdown();
    return;
  }
  NotifyReady(state, std::move(runtime), browser->BrowserMainThread());
}

bool ConfigFieldPresent(const browserd_config_t* config, size_t field_end) {
  return config && config->size >= field_end;
}

bool ConfigRequestsGui(const browserd_config_t* config) {
  return ConfigFieldPresent(config,
                            offsetof(browserd_config_t, gui) + sizeof(bool)) &&
         config->gui;
}

const char* ConfigUserDataDir(const browserd_config_t* config) {
  if (!ConfigFieldPresent(config, offsetof(browserd_config_t, user_data_dir) +
                                      sizeof(const char*))) {
    return nullptr;
  }
  return config->user_data_dir;
}

void ApplyRunConfig(const browserd_config_t* config) {
  base::CommandLine* command_line = base::CommandLine::ForCurrentProcess();
  if (ConfigRequestsGui(config)) {
    command_line->AppendSwitch(browserd::switches::kGui);
  }
  if (const char* user_data_dir = ConfigUserDataDir(config)) {
    command_line->AppendSwitchPath(
        browserd::switches::kUserDataDir,
        base::FilePath::FromUTF8Unsafe(user_data_dir));
  }
}

}  // namespace

extern "C" {

browserd_process_result_t browserd_process_main(int argc, const char** argv) {
  browserd_process_result_t result = {false, 0};
  browserd::startup::EnsureCommandLineInitialized(argc, argv);

  base::CommandLine* command_line = base::CommandLine::ForCurrentProcess();
  if (!command_line->HasSwitch(::switches::kProcessType)) {
    return result;
  }

  auto environment = base::Environment::Create();
  const bool requested_gui = browserd::startup::IsGuiRequested(
      *command_line, environment.get(), false);
  browserd::startup::ApplyDefaultCommandLineSwitches(
      argc, argv, requested_gui, !requested_gui, environment.get());

  result.handled = true;
  if (requested_gui) {
    result.exit_code = browserd::startup::RunGuiContentMain(
        argc, argv,
        browserd::gui::BrowserMainParts::RuntimeReadyCallback());
    return result;
  }
  result.exit_code = browserd::startup::RunHeadlessContentMain(
      argc, argv,
      base::BindOnce([](headless::HeadlessBrowser*) {}));
  return result;
}

int browserd_run(const browserd_config_t* config,
                 browserd_ready_callback ready,
                 void* user_data) {
  if (!ready) {
    return kScheduleRejected;
  }

  browserd::startup::EnsureCommandLineInitialized(0, nullptr);
  ApplyRunConfig(config);

  base::CommandLine* command_line = base::CommandLine::ForCurrentProcess();
  auto environment = base::Environment::Create();
  const bool requested_gui = browserd::startup::IsGuiRequested(
      *command_line, environment.get(), true);
  browserd::startup::ApplyDefaultCommandLineSwitches(
      0, nullptr, requested_gui, !requested_gui, environment.get());

  RunState state;
  state.ready = ready;
  state.user_data = user_data;

  if (requested_gui) {
    return browserd::startup::RunGuiContentMain(
        0, nullptr,
        base::BindOnce(&NotifyReady, base::Unretained(&state)));
  }
  return browserd::startup::RunHeadlessContentMain(
      0, nullptr,
      base::BindOnce(&OnHeadlessBrowserStart, base::Unretained(&state)));
}

void browserd_shutdown(browserd_session_t* session) {
  if (!session || !session->controller) {
    return;
  }

  BrowserController* controller = session->controller.get();
  scoped_refptr<base::SequencedTaskRunner> runner = controller->task_runner();
  if (!runner || runner->RunsTasksInCurrentSequence()) {
    DestroySessionController(session);
    return;
  }
  runner->PostTask(FROM_HERE,
                   base::BindOnce(&DestroySessionController,
                                  base::Unretained(session)));
}

int browserd_tab_list(browserd_session_t* session,
                      browserd_tabs_callback callback,
                      void* user_data) {
  if (!callback) {
    return kScheduleRejected;
  }
  return Schedule(
      session,
      base::BindOnce(
          [](browserd_session_t* session, browserd_tabs_callback callback,
             void* user_data, BrowserController* controller) {
            controller->ListTabs(base::BindOnce(&CompleteTabs, session,
                                                callback, user_data));
          },
          session, callback, user_data));
}

int browserd_tab_new(browserd_session_t* session,
                     const char* url,
                     browserd_tab_callback callback,
                     void* user_data) {
  if (!callback) {
    return kScheduleRejected;
  }
  std::string target_url = url ? url : "about:blank";
  return Schedule(
      session,
      base::BindOnce(
          [](browserd_session_t* session, std::string url,
             browserd_tab_callback callback, void* user_data,
             BrowserController* controller) {
            controller->CreateTab(GURL(url),
                                  base::BindOnce(&CompleteTab, session,
                                                 callback, user_data));
          },
          session, std::move(target_url), callback, user_data));
}

int browserd_tab_close(browserd_session_t* session,
                       const char* target_id,
                       browserd_status_callback callback,
                       void* user_data) {
  if (!callback) {
    return kScheduleRejected;
  }
  std::optional<std::string> id = OptionalString(target_id);
  return Schedule(
      session,
      base::BindOnce(
          [](browserd_session_t* session, std::optional<std::string> id,
             browserd_status_callback callback, void* user_data,
             BrowserController* controller) {
            controller->CloseTab(std::move(id),
                                 base::BindOnce(&CompleteStatus, session,
                                                callback, user_data));
          },
          session, std::move(id), callback, user_data));
}

int browserd_resize(browserd_session_t* session,
                    int width,
                    int height,
                    browserd_status_callback callback,
                    void* user_data) {
  if (!callback) {
    return kScheduleRejected;
  }
  return Schedule(
      session,
      base::BindOnce(
          [](browserd_session_t* session, int width, int height,
             browserd_status_callback callback, void* user_data,
             BrowserController* controller) {
            controller->ResizeActive(
                gfx::Size(width, height),
                base::BindOnce(&CompleteStatus, session, callback, user_data));
          },
          session, width, height, callback, user_data));
}

int browserd_navigate(browserd_session_t* session,
                      const char* target_id,
                      const char* url,
                      browserd_status_callback callback,
                      void* user_data) {
  if (!callback || !url) {
    return kScheduleRejected;
  }
  std::optional<std::string> id = OptionalString(target_id);
  std::string target_url(url);
  return Schedule(
      session,
      base::BindOnce(
          [](browserd_session_t* session, std::optional<std::string> id,
             std::string url, browserd_status_callback callback,
             void* user_data, BrowserController* controller) {
            controller->Navigate(
                std::move(id), GURL(url),
                base::BindOnce(&CompleteStatus, session, callback, user_data));
          },
          session, std::move(id), std::move(target_url), callback, user_data));
}

int browserd_navigate_back(browserd_session_t* session,
                           const char* target_id,
                           browserd_status_callback callback,
                           void* user_data) {
  if (!callback) {
    return kScheduleRejected;
  }
  std::optional<std::string> id = OptionalString(target_id);
  return Schedule(
      session,
      base::BindOnce(
          [](browserd_session_t* session, std::optional<std::string> id,
             browserd_status_callback callback, void* user_data,
             BrowserController* controller) {
            controller->NavigateBack(
                std::move(id),
                base::BindOnce(&CompleteStatus, session, callback, user_data));
          },
          session, std::move(id), callback, user_data));
}

int browserd_navigate_forward(browserd_session_t* session,
                              const char* target_id,
                              browserd_status_callback callback,
                              void* user_data) {
  if (!callback) {
    return kScheduleRejected;
  }
  std::optional<std::string> id = OptionalString(target_id);
  return Schedule(
      session,
      base::BindOnce(
          [](browserd_session_t* session, std::optional<std::string> id,
             browserd_status_callback callback, void* user_data,
             BrowserController* controller) {
            controller->NavigateForward(
                std::move(id),
                base::BindOnce(&CompleteStatus, session, callback, user_data));
          },
          session, std::move(id), callback, user_data));
}

int browserd_reload(browserd_session_t* session,
                    const char* target_id,
                    browserd_status_callback callback,
                    void* user_data) {
  if (!callback) {
    return kScheduleRejected;
  }
  std::optional<std::string> id = OptionalString(target_id);
  return Schedule(
      session,
      base::BindOnce(
          [](browserd_session_t* session, std::optional<std::string> id,
             browserd_status_callback callback, void* user_data,
             BrowserController* controller) {
            controller->Reload(
                std::move(id),
                base::BindOnce(&CompleteStatus, session, callback, user_data));
          },
          session, std::move(id), callback, user_data));
}

int browserd_evaluate(browserd_session_t* session,
                      const char* target_id,
                      const char* expression,
                      browserd_string_callback callback,
                      void* user_data) {
  if (!callback || !expression) {
    return kScheduleRejected;
  }
  std::optional<std::string> id = OptionalString(target_id);
  std::string js(expression);
  return Schedule(
      session,
      base::BindOnce(
          [](browserd_session_t* session, std::optional<std::string> id,
             std::string expression, browserd_string_callback callback,
             void* user_data, BrowserController* controller) {
            controller->Evaluate(std::move(id), std::move(expression),
                                 base::BindOnce(&CompleteEvaluate, session,
                                                callback, user_data));
          },
          session, std::move(id), std::move(js), callback, user_data));
}

int browserd_console_messages(browserd_session_t* session,
                              const char* target_id,
                              browserd_string_callback callback,
                              void* user_data) {
  if (!callback) {
    return kScheduleRejected;
  }
  std::optional<std::string> id = OptionalString(target_id);
  return Schedule(
      session,
      base::BindOnce(
          [](browserd_session_t* session, std::optional<std::string> id,
             browserd_string_callback callback, void* user_data,
             BrowserController* controller) {
            controller->ConsoleMessages(
                std::move(id),
                base::BindOnce(&CompleteString, session, callback, user_data));
          },
          session, std::move(id), callback, user_data));
}

int browserd_snapshot(browserd_session_t* session,
                      const char* target_id,
                      browserd_string_callback callback,
                      void* user_data) {
  if (!callback) {
    return kScheduleRejected;
  }
  std::optional<std::string> id = OptionalString(target_id);
  return Schedule(
      session,
      base::BindOnce(
          [](browserd_session_t* session, std::optional<std::string> id,
             browserd_string_callback callback, void* user_data,
             BrowserController* controller) {
            controller->Snapshot(
                std::move(id),
                base::BindOnce(&CompleteString, session, callback, user_data));
          },
          session, std::move(id), callback, user_data));
}

int browserd_screenshot(browserd_session_t* session,
                        const char* target_id,
                        browserd_bytes_callback callback,
                        void* user_data) {
  if (!callback) {
    return kScheduleRejected;
  }
  std::optional<std::string> id = OptionalString(target_id);
  return Schedule(
      session,
      base::BindOnce(
          [](browserd_session_t* session, std::optional<std::string> id,
             browserd_bytes_callback callback, void* user_data,
             BrowserController* controller) {
            controller->Screenshot(
                std::move(id),
                base::BindOnce(&CompleteBytes, session, callback, user_data));
          },
          session, std::move(id), callback, user_data));
}

int browserd_wait_for(browserd_session_t* session,
                      const char* target_id,
                      const browserd_wait_options_t* options,
                      browserd_status_callback callback,
                      void* user_data) {
  if (!callback || !options || options->size < sizeof(*options)) {
    return kScheduleRejected;
  }
  std::optional<std::string> id = OptionalString(target_id);
  browserd::WaitOptions wait_options;
  wait_options.selector = OptionalString(options->selector);
  wait_options.text = OptionalString(options->text);
  wait_options.timeout_ms = options->timeout_ms;
  return Schedule(
      session,
      base::BindOnce(
          [](browserd_session_t* session, std::optional<std::string> id,
             browserd::WaitOptions options, browserd_status_callback callback,
             void* user_data, BrowserController* controller) {
            controller->WaitFor(
                std::move(id), std::move(options),
                base::BindOnce(&CompleteStatus, session, callback, user_data));
          },
          session, std::move(id), std::move(wait_options), callback,
          user_data));
}

int browserd_click(browserd_session_t* session,
                   const char* target_id,
                   const browserd_ref_options_t* options,
                   browserd_status_callback callback,
                   void* user_data) {
  if (!callback || !options || options->size < sizeof(*options) ||
      !options->ref) {
    return kScheduleRejected;
  }
  std::optional<std::string> id = OptionalString(target_id);
  browserd::RefOptions ref_options;
  ref_options.ref = options->ref;
  return Schedule(
      session,
      base::BindOnce(
          [](browserd_session_t* session, std::optional<std::string> id,
             browserd::RefOptions options, browserd_status_callback callback,
             void* user_data, BrowserController* controller) {
            controller->Click(
                std::move(id), std::move(options),
                base::BindOnce(&CompleteStatus, session, callback, user_data));
          },
          session, std::move(id), std::move(ref_options), callback,
          user_data));
}

int browserd_hover(browserd_session_t* session,
                   const char* target_id,
                   const browserd_ref_options_t* options,
                   browserd_status_callback callback,
                   void* user_data) {
  if (!callback || !options || options->size < sizeof(*options) ||
      !options->ref) {
    return kScheduleRejected;
  }
  std::optional<std::string> id = OptionalString(target_id);
  browserd::RefOptions ref_options;
  ref_options.ref = options->ref;
  return Schedule(
      session,
      base::BindOnce(
          [](browserd_session_t* session, std::optional<std::string> id,
             browserd::RefOptions options, browserd_status_callback callback,
             void* user_data, BrowserController* controller) {
            controller->Hover(
                std::move(id), std::move(options),
                base::BindOnce(&CompleteStatus, session, callback, user_data));
          },
          session, std::move(id), std::move(ref_options), callback,
          user_data));
}

int browserd_type(browserd_session_t* session,
                  const char* target_id,
                  const browserd_type_options_t* options,
                  browserd_status_callback callback,
                  void* user_data) {
  if (!callback || !options || options->size < sizeof(*options) ||
      !options->ref || !options->text) {
    return kScheduleRejected;
  }
  std::optional<std::string> id = OptionalString(target_id);
  browserd::TypeOptions type_options;
  type_options.ref = options->ref;
  type_options.text = options->text;
  type_options.clear = options->clear;
  return Schedule(
      session,
      base::BindOnce(
          [](browserd_session_t* session, std::optional<std::string> id,
             browserd::TypeOptions options, browserd_status_callback callback,
             void* user_data, BrowserController* controller) {
            controller->Type(
                std::move(id), std::move(options),
                base::BindOnce(&CompleteStatus, session, callback, user_data));
          },
          session, std::move(id), std::move(type_options), callback,
          user_data));
}

int browserd_press_key(browserd_session_t* session,
                       const char* target_id,
                       const browserd_key_options_t* options,
                       browserd_status_callback callback,
                       void* user_data) {
  if (!callback || !options || options->size < sizeof(*options) ||
      !options->key) {
    return kScheduleRejected;
  }
  std::optional<std::string> id = OptionalString(target_id);
  browserd::KeyOptions key_options;
  key_options.key = options->key;
  return Schedule(
      session,
      base::BindOnce(
          [](browserd_session_t* session, std::optional<std::string> id,
             browserd::KeyOptions options, browserd_status_callback callback,
             void* user_data, BrowserController* controller) {
            controller->PressKey(
                std::move(id), std::move(options),
                base::BindOnce(&CompleteStatus, session, callback, user_data));
          },
          session, std::move(id), std::move(key_options), callback,
          user_data));
}

int browserd_select_option(browserd_session_t* session,
                           const char* target_id,
                           const browserd_select_options_t* options,
                           browserd_status_callback callback,
                           void* user_data) {
  if (!callback || !options || options->size < sizeof(*options) ||
      !options->ref || (!options->values && options->values_len > 0)) {
    return kScheduleRejected;
  }
  std::optional<std::string> id = OptionalString(target_id);
  browserd::SelectOptions select_options;
  select_options.ref = options->ref;
  auto values = UNSAFE_BUFFERS(base::span(options->values, options->values_len));
  for (const char* value : values) {
    if (!value) {
      return kScheduleRejected;
    }
    select_options.values.push_back(value);
  }
  return Schedule(
      session,
      base::BindOnce(
          [](browserd_session_t* session, std::optional<std::string> id,
             browserd::SelectOptions options,
             browserd_status_callback callback, void* user_data,
             BrowserController* controller) {
            controller->SelectOption(
                std::move(id), std::move(options),
                base::BindOnce(&CompleteStatus, session, callback, user_data));
          },
          session, std::move(id), std::move(select_options), callback,
          user_data));
}

int browserd_drag(browserd_session_t* session,
                  const char* target_id,
                  const browserd_drag_options_t* options,
                  browserd_status_callback callback,
                  void* user_data) {
  if (!callback || !options || options->size < sizeof(*options) ||
      !options->start_ref || !options->end_ref) {
    return kScheduleRejected;
  }
  std::optional<std::string> id = OptionalString(target_id);
  browserd::DragOptions drag_options;
  drag_options.start_ref = options->start_ref;
  drag_options.end_ref = options->end_ref;
  return Schedule(
      session,
      base::BindOnce(
          [](browserd_session_t* session, std::optional<std::string> id,
             browserd::DragOptions options, browserd_status_callback callback,
             void* user_data, BrowserController* controller) {
            controller->Drag(
                std::move(id), std::move(options),
                base::BindOnce(&CompleteStatus, session, callback, user_data));
          },
          session, std::move(id), std::move(drag_options), callback,
          user_data));
}

int browserd_scroll(browserd_session_t* session,
                    const char* target_id,
                    const browserd_scroll_options_t* options,
                    browserd_status_callback callback,
                    void* user_data) {
  if (!callback || !options || options->size < sizeof(*options)) {
    return kScheduleRejected;
  }
  std::optional<std::string> id = OptionalString(target_id);
  browserd::ScrollOptions scroll_options;
  scroll_options.delta_x = options->delta_x;
  scroll_options.delta_y = options->delta_y;
  return Schedule(
      session,
      base::BindOnce(
          [](browserd_session_t* session, std::optional<std::string> id,
             browserd::ScrollOptions options,
             browserd_status_callback callback, void* user_data,
             BrowserController* controller) {
            controller->Scroll(
                std::move(id), options,
                base::BindOnce(&CompleteStatus, session, callback, user_data));
          },
          session, std::move(id), scroll_options, callback, user_data));
}

int browserd_cookie_list(browserd_session_t* session,
                         const char* target_id,
                         const browserd_cookie_list_options_t* options,
                         browserd_cookies_callback callback,
                         void* user_data) {
  if (!callback || (options && options->size < sizeof(*options))) {
    return kScheduleRejected;
  }
  std::optional<std::string> id = OptionalString(target_id);
  browserd::CookieListOptions list_options;
  if (options) {
    list_options.url = OptionalString(options->url);
  }
  return Schedule(
      session,
      base::BindOnce(
          [](browserd_session_t* session, std::optional<std::string> id,
             browserd::CookieListOptions options,
             browserd_cookies_callback callback, void* user_data,
             BrowserController* controller) {
            controller->ListCookies(
                std::move(id), std::move(options),
                base::BindOnce(&CompleteCookies, session, callback,
                               user_data));
          },
          session, std::move(id), std::move(list_options), callback,
          user_data));
}

int browserd_cookie_get(browserd_session_t* session,
                        const char* target_id,
                        const browserd_cookie_get_options_t* options,
                        browserd_cookie_callback callback,
                        void* user_data) {
  if (!callback || !options || options->size < sizeof(*options) ||
      !options->name) {
    return kScheduleRejected;
  }
  std::optional<std::string> id = OptionalString(target_id);
  browserd::CookieGetOptions get_options;
  get_options.name = options->name;
  return Schedule(
      session,
      base::BindOnce(
          [](browserd_session_t* session, std::optional<std::string> id,
             browserd::CookieGetOptions options,
             browserd_cookie_callback callback, void* user_data,
             BrowserController* controller) {
            controller->GetCookie(
                std::move(id), std::move(options),
                base::BindOnce(&CompleteCookie, session, callback, user_data));
          },
          session, std::move(id), std::move(get_options), callback,
          user_data));
}

int browserd_cookie_set(browserd_session_t* session,
                        const char* target_id,
                        const browserd_cookie_set_options_t* options,
                        browserd_status_callback callback,
                        void* user_data) {
  if (!callback || !options || options->size < sizeof(*options) ||
      !options->name || !options->value) {
    return kScheduleRejected;
  }
  std::optional<std::string> id = OptionalString(target_id);
  browserd::CookieSetOptions set_options;
  set_options.name = options->name;
  set_options.value = options->value;
  set_options.domain = OptionalString(options->domain);
  set_options.path = OptionalString(options->path);
  set_options.url = OptionalString(options->url);
  set_options.secure = options->secure;
  set_options.http_only = options->http_only;
  return Schedule(
      session,
      base::BindOnce(
          [](browserd_session_t* session, std::optional<std::string> id,
             browserd::CookieSetOptions options,
             browserd_status_callback callback, void* user_data,
             BrowserController* controller) {
            controller->SetCookie(
                std::move(id), std::move(options),
                base::BindOnce(&CompleteStatus, session, callback, user_data));
          },
          session, std::move(id), std::move(set_options), callback,
          user_data));
}

int browserd_cookie_delete(browserd_session_t* session,
                           const char* target_id,
                           const browserd_cookie_delete_options_t* options,
                           browserd_status_callback callback,
                           void* user_data) {
  if (!callback || !options || options->size < sizeof(*options) ||
      !options->name) {
    return kScheduleRejected;
  }
  std::optional<std::string> id = OptionalString(target_id);
  browserd::CookieDeleteOptions delete_options;
  delete_options.name = options->name;
  delete_options.url = OptionalString(options->url);
  delete_options.domain = OptionalString(options->domain);
  return Schedule(
      session,
      base::BindOnce(
          [](browserd_session_t* session, std::optional<std::string> id,
             browserd::CookieDeleteOptions options,
             browserd_status_callback callback, void* user_data,
             BrowserController* controller) {
            controller->DeleteCookie(
                std::move(id), std::move(options),
                base::BindOnce(&CompleteStatus, session, callback, user_data));
          },
          session, std::move(id), std::move(delete_options), callback,
          user_data));
}

int browserd_cookie_clear(browserd_session_t* session,
                          const char* target_id,
                          browserd_status_callback callback,
                          void* user_data) {
  if (!callback) {
    return kScheduleRejected;
  }
  std::optional<std::string> id = OptionalString(target_id);
  return Schedule(
      session,
      base::BindOnce(
          [](browserd_session_t* session, std::optional<std::string> id,
             browserd_status_callback callback, void* user_data,
             BrowserController* controller) {
            controller->ClearCookies(
                std::move(id),
                base::BindOnce(&CompleteStatus, session, callback, user_data));
          },
          session, std::move(id), callback, user_data));
}

}  // extern "C"
