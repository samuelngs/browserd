#ifndef BROWSERD_EMBED_BROWSERD_EMBED_H_
#define BROWSERD_EMBED_BROWSERD_EMBED_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
#if defined(BROWSERD_EMBED_IMPLEMENTATION)
#define BROWSERD_EMBED_EXPORT __declspec(dllexport)
#else
#define BROWSERD_EMBED_EXPORT __declspec(dllimport)
#endif
#else
#define BROWSERD_EMBED_EXPORT __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct browserd_session browserd_session_t;

typedef enum browserd_status_code {
  BROWSERD_STATUS_OK = 0,
  BROWSERD_STATUS_INVALID_ARGUMENT = 1,
  BROWSERD_STATUS_SESSION_NOT_READY = 2,
  BROWSERD_STATUS_TAB_NOT_FOUND = 3,
  BROWSERD_STATUS_NO_ACTIVE_FRAME = 4,
  BROWSERD_STATUS_NAVIGATION_FAILED = 5,
  BROWSERD_STATUS_SCREENSHOT_FAILED = 6,
  BROWSERD_STATUS_INTERNAL_ERROR = 7,
  BROWSERD_STATUS_TIMEOUT = 8,
  BROWSERD_STATUS_NOT_FOUND = 9,
} browserd_status_code_t;

typedef struct browserd_status {
  browserd_status_code_t code;
  const char* message;
  size_t message_len;
} browserd_status_t;

typedef struct browserd_process_result {
  bool handled;
  int exit_code;
} browserd_process_result_t;

typedef enum browserd_switch_scope {
  BROWSERD_SWITCH_BROWSER = 1 << 0,
  BROWSERD_SWITCH_GPU_CHILD = 1 << 1,
  BROWSERD_SWITCH_RENDERER_CHILD = 1 << 2,
  BROWSERD_SWITCH_ALL_CHILDREN = 1 << 3,
} browserd_switch_scope_t;

typedef struct browserd_switch {
  uint32_t size;
  const char* name;
  const char* value;
  uint32_t scope;
} browserd_switch_t;

typedef struct browserd_config {
  uint32_t size;
  bool gui;
  const char* user_data_dir;
  const browserd_switch_t* switches;
  size_t switches_len;
} browserd_config_t;

typedef struct browserd_string {
  const char* data;
  size_t len;
} browserd_string_t;

typedef struct browserd_bytes {
  const uint8_t* data;
  size_t len;
} browserd_bytes_t;

typedef struct browserd_tab_info {
  const char* target_id;
  size_t target_id_len;
  const char* title;
  size_t title_len;
  const char* url;
  size_t url_len;
} browserd_tab_info_t;

typedef struct browserd_cookie {
  const char* name;
  size_t name_len;
  const char* value;
  size_t value_len;
  const char* domain;
  size_t domain_len;
  const char* path;
  size_t path_len;
  bool secure;
  bool http_only;
  double expires_unix_seconds;
} browserd_cookie_t;

typedef void (*browserd_ready_callback)(browserd_session_t* session,
                                        browserd_status_t status,
                                        void* user_data);
typedef void (*browserd_status_callback)(browserd_session_t* session,
                                         browserd_status_t status,
                                         void* user_data);
typedef void (*browserd_string_callback)(browserd_session_t* session,
                                         browserd_status_t status,
                                         browserd_string_t value,
                                         void* user_data);
typedef void (*browserd_bytes_callback)(browserd_session_t* session,
                                        browserd_status_t status,
                                        browserd_bytes_t value,
                                        void* user_data);
typedef void (*browserd_tab_callback)(browserd_session_t* session,
                                      browserd_status_t status,
                                      const browserd_tab_info_t* tab,
                                      void* user_data);
typedef void (*browserd_tabs_callback)(browserd_session_t* session,
                                       browserd_status_t status,
                                       const browserd_tab_info_t* tabs,
                                       size_t tabs_len,
                                       void* user_data);
typedef void (*browserd_cookie_callback)(browserd_session_t* session,
                                         browserd_status_t status,
                                         const browserd_cookie_t* cookie,
                                         void* user_data);
typedef void (*browserd_cookies_callback)(browserd_session_t* session,
                                          browserd_status_t status,
                                          const browserd_cookie_t* cookies,
                                          size_t cookies_len,
                                          void* user_data);

typedef struct browserd_ref_options {
  uint32_t size;
  const char* ref;
} browserd_ref_options_t;

typedef struct browserd_type_options {
  uint32_t size;
  const char* ref;
  const char* text;
  bool clear;
} browserd_type_options_t;

typedef struct browserd_key_options {
  uint32_t size;
  const char* key;
} browserd_key_options_t;

typedef struct browserd_select_options {
  uint32_t size;
  const char* ref;
  const char* const* values;
  size_t values_len;
} browserd_select_options_t;

typedef struct browserd_drag_options {
  uint32_t size;
  const char* start_ref;
  const char* end_ref;
} browserd_drag_options_t;

typedef struct browserd_scroll_options {
  uint32_t size;
  int delta_x;
  int delta_y;
} browserd_scroll_options_t;

typedef struct browserd_wait_options {
  uint32_t size;
  const char* selector;
  const char* text;
  int timeout_ms;
} browserd_wait_options_t;

typedef struct browserd_cookie_list_options {
  uint32_t size;
  const char* url;
} browserd_cookie_list_options_t;

typedef struct browserd_cookie_get_options {
  uint32_t size;
  const char* name;
} browserd_cookie_get_options_t;

typedef struct browserd_cookie_set_options {
  uint32_t size;
  const char* name;
  const char* value;
  const char* domain;
  const char* path;
  const char* url;
  bool secure;
  bool http_only;
} browserd_cookie_set_options_t;

typedef struct browserd_cookie_delete_options {
  uint32_t size;
  const char* name;
  const char* url;
  const char* domain;
} browserd_cookie_delete_options_t;

BROWSERD_EMBED_EXPORT browserd_process_result_t
browserd_process_main(int argc, const char** argv);
BROWSERD_EMBED_EXPORT int browserd_run(const browserd_config_t* config,
                                       browserd_ready_callback ready,
                                       void* user_data);
BROWSERD_EMBED_EXPORT void browserd_shutdown(browserd_session_t* session);

BROWSERD_EMBED_EXPORT int browserd_tab_list(browserd_session_t* session,
                                            browserd_tabs_callback callback,
                                            void* user_data);
BROWSERD_EMBED_EXPORT int browserd_tab_new(browserd_session_t* session,
                                           const char* url,
                                           browserd_tab_callback callback,
                                           void* user_data);
BROWSERD_EMBED_EXPORT int browserd_tab_close(browserd_session_t* session,
                                             const char* target_id,
                                             browserd_status_callback callback,
                                             void* user_data);
BROWSERD_EMBED_EXPORT int browserd_resize(browserd_session_t* session,
                                          int width,
                                          int height,
                                          browserd_status_callback callback,
                                          void* user_data);

BROWSERD_EMBED_EXPORT int browserd_navigate(browserd_session_t* session,
                                            const char* target_id,
                                            const char* url,
                                            browserd_status_callback callback,
                                            void* user_data);
BROWSERD_EMBED_EXPORT int browserd_navigate_back(
    browserd_session_t* session,
    const char* target_id,
    browserd_status_callback callback,
    void* user_data);
BROWSERD_EMBED_EXPORT int browserd_navigate_forward(
    browserd_session_t* session,
    const char* target_id,
    browserd_status_callback callback,
    void* user_data);
BROWSERD_EMBED_EXPORT int browserd_reload(browserd_session_t* session,
                                          const char* target_id,
                                          browserd_status_callback callback,
                                          void* user_data);

BROWSERD_EMBED_EXPORT int browserd_evaluate(browserd_session_t* session,
                                            const char* target_id,
                                            const char* expression,
                                            browserd_string_callback callback,
                                            void* user_data);
BROWSERD_EMBED_EXPORT int browserd_console_messages(
    browserd_session_t* session,
    const char* target_id,
    browserd_string_callback callback,
    void* user_data);
BROWSERD_EMBED_EXPORT int browserd_snapshot(browserd_session_t* session,
                                            const char* target_id,
                                            browserd_string_callback callback,
                                            void* user_data);
BROWSERD_EMBED_EXPORT int browserd_screenshot(browserd_session_t* session,
                                              const char* target_id,
                                              browserd_bytes_callback callback,
                                              void* user_data);

BROWSERD_EMBED_EXPORT int browserd_wait_for(
    browserd_session_t* session,
    const char* target_id,
    const browserd_wait_options_t* options,
    browserd_status_callback callback,
    void* user_data);

BROWSERD_EMBED_EXPORT int browserd_click(browserd_session_t* session,
                                         const char* target_id,
                                         const browserd_ref_options_t* options,
                                         browserd_status_callback callback,
                                         void* user_data);
BROWSERD_EMBED_EXPORT int browserd_hover(browserd_session_t* session,
                                         const char* target_id,
                                         const browserd_ref_options_t* options,
                                         browserd_status_callback callback,
                                         void* user_data);
BROWSERD_EMBED_EXPORT int browserd_type(browserd_session_t* session,
                                        const char* target_id,
                                        const browserd_type_options_t* options,
                                        browserd_status_callback callback,
                                        void* user_data);
BROWSERD_EMBED_EXPORT int browserd_press_key(
    browserd_session_t* session,
    const char* target_id,
    const browserd_key_options_t* options,
    browserd_status_callback callback,
    void* user_data);
BROWSERD_EMBED_EXPORT int browserd_select_option(
    browserd_session_t* session,
    const char* target_id,
    const browserd_select_options_t* options,
    browserd_status_callback callback,
    void* user_data);
BROWSERD_EMBED_EXPORT int browserd_drag(browserd_session_t* session,
                                        const char* target_id,
                                        const browserd_drag_options_t* options,
                                        browserd_status_callback callback,
                                        void* user_data);
BROWSERD_EMBED_EXPORT int browserd_scroll(
    browserd_session_t* session,
    const char* target_id,
    const browserd_scroll_options_t* options,
    browserd_status_callback callback,
    void* user_data);

BROWSERD_EMBED_EXPORT int browserd_cookie_list(
    browserd_session_t* session,
    const char* target_id,
    const browserd_cookie_list_options_t* options,
    browserd_cookies_callback callback,
    void* user_data);
BROWSERD_EMBED_EXPORT int browserd_cookie_get(
    browserd_session_t* session,
    const char* target_id,
    const browserd_cookie_get_options_t* options,
    browserd_cookie_callback callback,
    void* user_data);
BROWSERD_EMBED_EXPORT int browserd_cookie_set(
    browserd_session_t* session,
    const char* target_id,
    const browserd_cookie_set_options_t* options,
    browserd_status_callback callback,
    void* user_data);
BROWSERD_EMBED_EXPORT int browserd_cookie_delete(
    browserd_session_t* session,
    const char* target_id,
    const browserd_cookie_delete_options_t* options,
    browserd_status_callback callback,
    void* user_data);
BROWSERD_EMBED_EXPORT int browserd_cookie_clear(
    browserd_session_t* session,
    const char* target_id,
    browserd_status_callback callback,
    void* user_data);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // BROWSERD_EMBED_BROWSERD_EMBED_H_
