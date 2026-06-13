# browserd

Stealth headless Chromium for AI agents with MCP support

## MCP Tools

| Tool | Description |
|------|-------------|
| `browser_navigate` | Navigate to a URL |
| `browser_navigate_back` | Go back in browser history |
| `browser_navigate_forward` | Go forward in browser history |
| `browser_reload` | Reload the current page |
| `browser_snapshot` | Capture accessibility snapshot of current page |
| `browser_take_screenshot` | Take a screenshot of current page |
| `browser_click` | Click an element using its accessibility ref |
| `browser_hover` | Hover over an element |
| `browser_type` | Type text into a focused element |
| `browser_press_key` | Press a keyboard key or key combination |
| `browser_select_option` | Select option(s) in a select element |
| `browser_drag` | Drag and drop from one element to another |
| `browser_scroll` | Scroll the page |
| `browser_evaluate` | Execute JavaScript in the browser console |
| `browser_console_messages` | Get console messages from the page |
| `browser_wait_for` | Wait for a selector or text to appear |
| `browser_tab_list` | List all open browser tabs |
| `browser_tab_new` | Open a new browser tab |
| `browser_close` | Close a browser tab |
| `browser_resize` | Resize the browser viewport |
| `browser_cookie_list` | List cookies, optionally filtered by URL |
| `browser_cookie_get` | Get a specific cookie by name |
| `browser_cookie_set` | Set a cookie |
| `browser_cookie_delete` | Delete a cookie by name |
| `browser_cookie_clear` | Clear all cookies |

## Quick Start

```bash
make fetch    # Clone Chromium source
make build    # Apply patches and build

./out/browserd
```

MCP over stdin/stdout. Point any MCP client at it:

```json
{
  "mcpServers": {
    "browserd": {
      "command": "./out/browserd"
    }
  }
}
```

Streamable HTTP can be enabled explicitly:

```bash
BROWSERD_MCP_HTTP_TOKEN=secret ./out/browserd --mcp-http-port=9223
```

The HTTP transport listens on `127.0.0.1` by default and serves `POST /mcp`.
Use `--mcp-http-host=localhost|127.0.0.1|::1` to choose a loopback bind
host. HTTP requests must include `Authorization: Bearer <token>`.

## Requirements

- macOS or Linux
- depot_tools

## Testing

```bash
make test          # All tests
make test-tier1    # Must-pass fingerprint tests
make test-tier2    # Commercial detection tests
```

## License

[MIT](LICENSE)
