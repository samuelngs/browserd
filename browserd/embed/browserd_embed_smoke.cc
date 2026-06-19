#include "browserd/embed/browserd_embed.h"

#include <iostream>
#include <string>

#include "base/compiler_specific.h"

namespace {

constexpr char kGuiSmokeSwitch[] = "--gui-smoke";
constexpr int kMaxScreenshotAttempts = 3;

struct SmokeState {
  bool failed = false;
  bool gui = false;
  bool checked_title = false;
  int screenshot_attempts = 0;
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
  if (status.code == BROWSERD_STATUS_OK && value.len > 0) {
    Finish(session, state, true);
    return;
  }
  if (state->screenshot_attempts < kMaxScreenshotAttempts) {
    ++state->screenshot_attempts;
    if (browserd_screenshot(session, nullptr, &OnScreenshot, user_data) == 0) {
      return;
    }
  }
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
  state->screenshot_attempts = 1;
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

  if (state->gui && !state->checked_title) {
    state->checked_title = true;
    std::string title(value.data, value.len);
    if (title.find("Example") == std::string::npos) {
      Finish(session, state, false);
      return;
    }
    const char* webgl_renderer =
        "(() => {"
        "  try {"
        "    const canvas = document.createElement('canvas');"
        "    const gl = canvas.getContext('webgl') || "
        "canvas.getContext('experimental-webgl');"
        "    if (!gl) return 'unavailable';"
        "    const ext = gl.getExtension('WEBGL_debug_renderer_info');"
        "    return ext ? gl.getParameter(ext.UNMASKED_RENDERER_WEBGL) : 'available';"
        "  } catch (e) { return 'unavailable'; }"
        "})()";
    if (browserd_evaluate(session, nullptr, webgl_renderer, &OnEvaluate,
                          user_data) != 0) {
      Finish(session, state, false);
    }
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
  if (state->gui) {
    url = "https://example.com";
  }
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
  state.gui = HasSwitch(argc, argv, kGuiSmokeSwitch);
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
  config.gui = state.gui;
  if (state.gui) {
    config.switches = switches;
    config.switches_len = sizeof(switches) / sizeof(switches[0]);
  }

  int exit_code = browserd_run(&config, &OnReady, &state);
  if (state.failed) {
    return 1;
  }
  return exit_code;
}
