import base64
import json
import subprocess
import threading


class MCPError(Exception):
    def __init__(self, message, code=None):
        super().__init__(message)
        self.code = code


class MCPClient:
    def __init__(self, binary_path):
        self._binary_path = binary_path
        self._process = None
        self._reader_thread = None
        self._stderr_thread = None
        self._next_id = 0
        self._lock = threading.Lock()
        self._responses = {}
        self._events = {}
        self._running = False
        self.stderr_lines = []

    def start(self):
        self._process = subprocess.Popen(
            [self._binary_path],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        self._running = True
        self._reader_thread = threading.Thread(
            target=self._read_stdout, daemon=True
        )
        self._reader_thread.start()
        self._stderr_thread = threading.Thread(
            target=self._read_stderr, daemon=True
        )
        self._stderr_thread.start()

    def _read_stdout(self):
        try:
            for raw in self._process.stdout:
                line = raw.decode("utf-8", errors="replace").strip()
                if not line:
                    continue
                try:
                    msg = json.loads(line)
                except json.JSONDecodeError:
                    continue
                mid = msg.get("id")
                if mid is not None:
                    with self._lock:
                        self._responses[mid] = msg
                        event = self._events.get(mid)
                    if event:
                        event.set()
        except Exception:
            pass
        finally:
            self._running = False
            with self._lock:
                for event in self._events.values():
                    event.set()

    def _read_stderr(self):
        try:
            for raw in self._process.stderr:
                self.stderr_lines.append(
                    raw.decode("utf-8", errors="replace").rstrip()
                )
        except Exception:
            pass

    def _get_id(self):
        with self._lock:
            self._next_id += 1
            return self._next_id

    def send(self, method, params=None, timeout=30):
        if self._process and self._process.poll() is not None:
            raise MCPError(
                f"browserd exited with code {self._process.returncode}"
            )

        mid = self._get_id()
        event = threading.Event()
        with self._lock:
            self._events[mid] = event

        msg = {"jsonrpc": "2.0", "id": mid, "method": method}
        if params is not None:
            msg["params"] = params

        data = (json.dumps(msg) + "\n").encode("utf-8")
        self._process.stdin.write(data)
        self._process.stdin.flush()

        if not event.wait(timeout):
            with self._lock:
                self._events.pop(mid, None)
                self._responses.pop(mid, None)
            raise TimeoutError(
                f"MCP {method} (id={mid}) timed out after {timeout}s"
            )

        with self._lock:
            self._events.pop(mid, None)
            response = self._responses.pop(mid, None)

        if response is None:
            raise MCPError("browserd process exited during request")

        if "error" in response:
            err = response["error"]
            raise MCPError(err.get("message", "Unknown error"), err.get("code"))

        return response.get("result", {})

    def initialize(self):
        return self.send(
            "initialize",
            {
                "protocolVersion": "2024-11-05",
                "capabilities": {},
                "clientInfo": {"name": "browserd-test", "version": "1.0"},
            },
        )

    def call_tool(self, name, args=None, timeout=30):
        result = self.send(
            "tools/call",
            {"name": name, "arguments": args or {}},
            timeout=timeout,
        )

        if result.get("isError"):
            text = self._extract_text(result)
            raise MCPError(f"Tool {name} failed: {text}")

        return result

    def _extract_text(self, result):
        for item in result.get("content", []):
            if item.get("type") == "text":
                return item.get("text", "")
        return ""

    def _extract_image(self, result):
        for item in result.get("content", []):
            if item.get("type") == "image":
                return base64.b64decode(item.get("data", ""))
        return None

    # ---- Convenience methods ----

    def navigate(self, url, timeout=30):
        result = self.call_tool("browser_navigate", {"url": url}, timeout)
        return self._extract_text(result)

    def wait_for(self, selector=None, text=None, timeout_ms=15000):
        import time

        deadline = time.monotonic() + (timeout_ms / 1000)
        while time.monotonic() < deadline:
            if selector:
                found = self.evaluate(
                    f"!!document.querySelector('{selector}')"
                )
                if found == "true" or found is True:
                    return "Element found"
            if text:
                found = self.evaluate(
                    "document.body && document.body.innerText.includes("
                    + json.dumps(text)
                    + ")"
                )
                if found == "true" or found is True:
                    return "Text found"
            time.sleep(0.3)
        raise MCPError(
            f"wait_for timed out after {timeout_ms}ms "
            f"(selector={selector!r}, text={text!r})"
        )

    def evaluate(self, expression, timeout=30):
        result = self.call_tool(
            "browser_evaluate", {"expression": expression}, timeout
        )
        text = self._extract_text(result)
        if text.startswith('"') and text.endswith('"'):
            try:
                return json.loads(text)
            except json.JSONDecodeError:
                pass
        return text

    def snapshot(self, timeout=10):
        result = self.call_tool("browser_snapshot", {}, timeout)
        return self._extract_text(result)

    def screenshot(self, timeout=10):
        result = self.call_tool("browser_take_screenshot", {}, timeout)
        return self._extract_image(result)

    def click(self, ref, timeout=30):
        result = self.call_tool("browser_click", {"ref": str(ref)}, timeout)
        return self._extract_text(result)

    def type_text(self, ref, text, clear=False, timeout=60):
        args = {"ref": str(ref), "text": text}
        if clear:
            args["clear"] = True
        result = self.call_tool("browser_type", args, timeout)
        return self._extract_text(result)

    def close(self):
        if not self._process:
            return
        try:
            self._process.stdin.close()
        except Exception:
            pass
        try:
            self._process.wait(timeout=10)
        except subprocess.TimeoutExpired:
            self._process.kill()
            try:
                self._process.wait(timeout=5)
            except Exception:
                pass
        self._running = False
