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

`browserd_run` blocks until browserd shuts down. For GUI mode, call it on the
process main thread. Callbacks run on Chromium's UI thread and should return
quickly.

Callback payload memory is borrowed. Strings, byte buffers, tabs, and cookies
are valid only during the callback. Rust hosts must copy anything they need to
keep after returning.
