#include "browserd/embed/browserd_embed.h"

#include <iostream>
#include <string>

#include "base/compiler_specific.h"

namespace {

constexpr char kGuiSmokeSwitch[] = "--gui-smoke";

struct SmokeState {
  bool failed = false;
};

bool IsOk(browserd_status_t status, const char* step) {
  if (status.code == BROWSERD_STATUS_OK) {
    return true;
  }
  std::cerr << step << " failed: "
            << std::string(status.message, status.message_len) << "\n";
  return false;
}

void Finish(browserd_session_t* session, SmokeState* state, bool ok) {
  state->failed = state->failed || !ok;
  browserd_shutdown(session);
}

void OnScreenshot(browserd_session_t* session,
                  browserd_status_t status,
                  browserd_bytes_t value,
                  void* user_data) {
  auto* state = static_cast<SmokeState*>(user_data);
  Finish(session, state, IsOk(status, "screenshot") && value.len > 0);
}

void OnSnapshot(browserd_session_t* session,
                browserd_status_t status,
                browserd_string_t value,
                void* user_data) {
  auto* state = static_cast<SmokeState*>(user_data);
  if (!IsOk(status, "snapshot") || value.len == 0) {
    Finish(session, state, false);
    return;
  }
  if (browserd_screenshot(session, nullptr, &OnScreenshot, user_data) != 0) {
    Finish(session, state, false);
  }
}

void OnEvaluate(browserd_session_t* session,
                browserd_status_t status,
                browserd_string_t value,
                void* user_data) {
  auto* state = static_cast<SmokeState*>(user_data);
  if (!IsOk(status, "evaluate") || value.len == 0) {
    Finish(session, state, false);
    return;
  }
  if (browserd_snapshot(session, nullptr, &OnSnapshot, user_data) != 0) {
    Finish(session, state, false);
  }
}

void OnNavigate(browserd_session_t* session,
                browserd_status_t status,
                void* user_data) {
  auto* state = static_cast<SmokeState*>(user_data);
  if (!IsOk(status, "navigate")) {
    Finish(session, state, false);
    return;
  }
  if (browserd_evaluate(session, nullptr, "document.title", &OnEvaluate,
                        user_data) != 0) {
    Finish(session, state, false);
  }
}

void OnReady(browserd_session_t* session,
             browserd_status_t status,
             void* user_data) {
  auto* state = static_cast<SmokeState*>(user_data);
  if (!IsOk(status, "ready") || !session) {
    state->failed = true;
    return;
  }

  const char* url =
      "data:text/html,<html><head><title>browserd embed smoke</title></head>"
      "<body><button id='ok'>ok</button><script>console.log('ready')</script>"
      "</body></html>";
  if (browserd_navigate(session, nullptr, url, &OnNavigate, user_data) != 0) {
    Finish(session, state, false);
  }
}

bool HasSwitch(int argc, const char** argv, const char* switch_name) {
  for (int i = 1; i < argc; ++i) {
    if (std::string(UNSAFE_BUFFERS(argv[i])) == switch_name) {
      return true;
    }
  }
  return false;
}

}  // namespace

int main(int argc, const char** argv) {
  browserd_process_result_t process_result = browserd_process_main(argc, argv);
  if (process_result.handled) {
    return process_result.exit_code;
  }

  SmokeState state;
  browserd_config_t config = {};
  config.size = sizeof(config);
  config.gui = HasSwitch(argc, argv, kGuiSmokeSwitch);

  int exit_code = browserd_run(&config, &OnReady, &state);
  if (state.failed) {
    return 1;
  }
  return exit_code;
}
