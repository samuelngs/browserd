#include "browserd/embed/browserd_embed.h"

#include <iostream>
#include <string>

namespace {

constexpr char kExampleUrl[] = "https://example.com";
constexpr char kSecondUrl[] =
    "data:text/html,<title>second</title><body>ok</body>";

enum class Step {
  kInitialList,
  kCreateFirstTab,
  kNavigateFirstTab,
  kEvaluateFirstTab,
  kCloseFirstTab,
  kListAfterClose,
  kCreateSecondTab,
  kNavigateSecondTab,
  kEvaluateSecondTab,
};

struct LifecycleState {
  bool failed = false;
  bool completed = false;
  browserd_session_t* session = nullptr;
  Step step = Step::kInitialList;
  std::string first_target_id;
  std::string second_target_id;
};

bool IsOk(browserd_status_t status) {
  return status.code == BROWSERD_STATUS_OK;
}

std::string StatusMessage(browserd_status_t status) {
  return std::string(status.message, status.message_len);
}

std::string StringView(browserd_string_t value) {
  return std::string(value.data, value.len);
}

std::string TargetId(const browserd_tab_info_t* tab) {
  return tab ? std::string(tab->target_id, tab->target_id_len) : std::string();
}

void Fail(browserd_session_t* session,
          LifecycleState* state,
          const std::string& message) {
  std::cerr << message << "\n";
  state->failed = true;
  if (session) {
    browserd_shutdown(session);
  }
}

void OnTabs(browserd_session_t* session,
            browserd_status_t status,
            const browserd_tab_info_t* tabs,
            size_t tabs_len,
            void* user_data);
void OnTab(browserd_session_t* session,
           browserd_status_t status,
           const browserd_tab_info_t* tab,
           void* user_data);
void OnStatus(browserd_session_t* session,
              browserd_status_t status,
              void* user_data);
void OnEvaluate(browserd_session_t* session,
                browserd_status_t status,
                browserd_string_t value,
                void* user_data);

void OnReady(browserd_session_t* session,
             browserd_status_t status,
             void* user_data) {
  auto* state = static_cast<LifecycleState*>(user_data);
  if (!IsOk(status) || !session) {
    Fail(session, state, "ready failed: " + StatusMessage(status));
    return;
  }

  state->session = session;
  state->step = Step::kInitialList;
  if (browserd_tab_list(session, &OnTabs, user_data) != 0) {
    Fail(session, state, "initial tab_list schedule failed");
  }
}

void OnTabs(browserd_session_t* session,
            browserd_status_t status,
            const browserd_tab_info_t* tabs,
            size_t tabs_len,
            void* user_data) {
  auto* state = static_cast<LifecycleState*>(user_data);
  if (!IsOk(status)) {
    Fail(session, state, "tab_list failed: " + StatusMessage(status));
    return;
  }

  switch (state->step) {
    case Step::kInitialList:
      if (tabs_len != 0) {
        Fail(session, state, "expected empty initial tab list");
        return;
      }
      state->step = Step::kCreateFirstTab;
      if (browserd_tab_new(session, kExampleUrl, &OnTab, user_data) != 0) {
        Fail(session, state, "first tab_new schedule failed");
      }
      return;
    case Step::kListAfterClose:
      if (tabs_len != 0) {
        Fail(session, state, "expected empty tab list after final close");
        return;
      }
      state->step = Step::kCreateSecondTab;
      if (browserd_tab_new(session, kSecondUrl, &OnTab, user_data) != 0) {
        Fail(session, state, "second tab_new schedule failed");
      }
      return;
    default:
      Fail(session, state, "unexpected tab_list callback");
      return;
  }
}

void OnTab(browserd_session_t* session,
           browserd_status_t status,
           const browserd_tab_info_t* tab,
           void* user_data) {
  auto* state = static_cast<LifecycleState*>(user_data);
  if (!IsOk(status) || !tab) {
    Fail(session, state, "tab_new failed: " + StatusMessage(status));
    return;
  }

  switch (state->step) {
    case Step::kCreateFirstTab:
      state->first_target_id = TargetId(tab);
      state->step = Step::kNavigateFirstTab;
      if (browserd_navigate(session, state->first_target_id.c_str(),
                            kExampleUrl, &OnStatus, user_data) != 0) {
        Fail(session, state, "first navigate schedule failed");
      }
      return;
    case Step::kCreateSecondTab:
      state->second_target_id = TargetId(tab);
      state->step = Step::kNavigateSecondTab;
      if (browserd_navigate(session, state->second_target_id.c_str(),
                            kSecondUrl, &OnStatus, user_data) != 0) {
        Fail(session, state, "second navigate schedule failed");
      }
      return;
    default:
      Fail(session, state, "unexpected tab_new callback");
      return;
  }
}

void OnStatus(browserd_session_t* session,
              browserd_status_t status,
              void* user_data) {
  auto* state = static_cast<LifecycleState*>(user_data);
  if (!IsOk(status)) {
    Fail(session, state, "status callback failed: " + StatusMessage(status));
    return;
  }

  switch (state->step) {
    case Step::kNavigateFirstTab:
      state->step = Step::kEvaluateFirstTab;
      if (browserd_evaluate(session, state->first_target_id.c_str(),
                            "document.title", &OnEvaluate, user_data) != 0) {
        Fail(session, state, "first evaluate schedule failed");
      }
      return;
    case Step::kCloseFirstTab:
      state->step = Step::kListAfterClose;
      if (browserd_tab_list(session, &OnTabs, user_data) != 0) {
        Fail(session, state, "post-close tab_list schedule failed");
      }
      return;
    case Step::kNavigateSecondTab:
      state->step = Step::kEvaluateSecondTab;
      if (browserd_evaluate(session, state->second_target_id.c_str(),
                            "document.title", &OnEvaluate, user_data) != 0) {
        Fail(session, state, "second evaluate schedule failed");
      }
      return;
    default:
      Fail(session, state, "unexpected status callback");
      return;
  }
}

void OnEvaluate(browserd_session_t* session,
                browserd_status_t status,
                browserd_string_t value,
                void* user_data) {
  auto* state = static_cast<LifecycleState*>(user_data);
  if (!IsOk(status)) {
    Fail(session, state, "evaluate failed: " + StatusMessage(status));
    return;
  }

  std::string result = StringView(value);
  switch (state->step) {
    case Step::kEvaluateFirstTab:
      if (result.find("Example") == std::string::npos) {
        Fail(session, state, "unexpected first title: " + result);
        return;
      }
      state->step = Step::kCloseFirstTab;
      if (browserd_tab_close(session, state->first_target_id.c_str(),
                             &OnStatus, user_data) != 0) {
        Fail(session, state, "first tab_close schedule failed");
      }
      return;
    case Step::kEvaluateSecondTab:
      if (result.find("second") == std::string::npos) {
        Fail(session, state, "unexpected second title: " + result);
        return;
      }
      state->completed = true;
      browserd_shutdown(session);
      return;
    default:
      Fail(session, state, "unexpected evaluate callback");
      return;
  }
}

void OnUnexpectedReady(browserd_session_t* session,
                       browserd_status_t status,
                       void* user_data) {
  auto* ready_called = static_cast<bool*>(user_data);
  *ready_called = true;
  if (session) {
    browserd_shutdown(session);
  }
}

}  // namespace

int main(int argc, const char** argv) {
  browserd_process_result_t process_result = browserd_process_main(argc, argv);
  if (process_result.handled) {
    return process_result.exit_code;
  }

  browserd_switch_t switches[] = {
      {
          sizeof(browserd_switch_t),
          "disable-features",
          "FallbackToSWIfGLES3NotSupported",
          BROWSERD_SWITCH_BROWSER | BROWSERD_SWITCH_GPU_CHILD,
      },
  };

  browserd_config_t config = {};
  config.size = sizeof(config);
  config.gui = true;
  config.switches = switches;
  config.switches_len = sizeof(switches) / sizeof(switches[0]);
  config.start_empty = true;
  config.idle_on_zero_tabs = true;

  LifecycleState state;
  int exit_code = browserd_run(&config, &OnReady, &state);
  if (exit_code != 0 || state.failed || !state.completed) {
    return 1;
  }

  bool unexpected_ready = false;
  int second_exit_code =
      browserd_run(&config, &OnUnexpectedReady, &unexpected_ready);
  if (second_exit_code == 0 || unexpected_ready) {
    return 1;
  }

  return 0;
}
