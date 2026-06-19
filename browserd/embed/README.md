# browserd embedding API

`browserd_embed` exposes browserd as an in-process C ABI for Rust or other
native hosts. The embedding API does not speak MCP and does not start stdio or
Streamable HTTP transports.

Host lifecycle:

1. Call `browserd_process_main(argc, argv)` at the top of `main`.
2. If it returns `handled = true`, exit with `exit_code`.
3. Fill `browserd_config_t`.
4. Call `browserd_run(&config, ready_callback, user_data)`.
5. Use the `browserd_session_t*` received by the ready callback.
6. Call operation-specific functions such as `browserd_navigate`,
   `browserd_evaluate`, `browserd_screenshot`, `browserd_click`, and cookie
   functions directly.
7. Call `browserd_shutdown(session)` when the host is done.

`browserd_run` blocks until browserd shuts down. Chromium `ContentMain` is a
one-runtime-per-process entry point for the embedding API: after
`browserd_run` returns, a later `browserd_run` in the same process is rejected
instead of re-entering Chromium. For GUI mode, call it on the process main
thread. Callbacks run on Chromium's UI thread and should return quickly.

Embedded GUI hosts that want lazy visible windows should set
`browserd_config_t.start_empty = true` and
`browserd_config_t.idle_on_zero_tabs = true`. With those options, the ready
callback starts with zero tabs and no visible window, `browserd_tab_new` creates
the first visible content window, and closing the final tab returns to an idle
zero-tab state. Call `browserd_shutdown(session)` only during final host
teardown.

Callback payload memory is borrowed. Strings, byte buffers, tabs, and cookies
are valid only during the callback. Rust hosts must copy anything they need to
keep after returning.

`browserd_config_t.switches` lets embedders pass command-line policy without
patching Chromium. Browser-scoped switches are applied to the browser process
before browserd's default switches. GPU-child, renderer-child, and all-child
switches are copied into browserd-owned memory and forwarded to GUI child
processes from `ContentBrowserClient::AppendExtraCommandLineSwitches`.

Use child-scoped switches when Chromium does not automatically forward a flag
from the browser process. For example, pass `disable-features` with
`FallbackToSWIfGLES3NotSupported` scoped to `BROWSERD_SWITCH_GPU_CHILD` to make
sure the GPU process receives it.
