import http.client
import json
import os
import socket
import subprocess
import threading
import time

import pytest


TOKEN = "test-token"


def _free_port():
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


class HTTPMCPClient:
    def __init__(self, binary_path, token=TOKEN):
        self.binary_path = binary_path
        self.token = token
        self.port = _free_port()
        self.process = None
        self.stderr_lines = []

    def start(self, include_token=True, extra_args=None):
        env = os.environ.copy()
        if include_token:
            env["BROWSERD_MCP_HTTP_TOKEN"] = self.token
        else:
            env.pop("BROWSERD_MCP_HTTP_TOKEN", None)

        args = [
            self.binary_path,
            f"--mcp-http-port={self.port}",
        ]
        if extra_args:
            args.extend(extra_args)

        self.process = subprocess.Popen(
            args,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            env=env,
        )

        threading.Thread(target=self._read_stderr, daemon=True).start()

    def _read_stderr(self):
        try:
            for raw in self.process.stderr:
                self.stderr_lines.append(
                    raw.decode("utf-8", errors="replace").rstrip()
                )
        except Exception:
            pass

    def wait_until_ready(self, timeout=20):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if self.process.poll() is not None:
                raise RuntimeError(
                    f"browserd exited early: {self.process.returncode}"
                )
            try:
                status, _body, _headers = self.request(
                    "GET", "/unknown", token=False
                )
                if status == 404:
                    return
            except OSError:
                time.sleep(0.1)
        raise TimeoutError("browserd HTTP transport did not become ready")

    def request(
        self,
        method,
        path,
        body=None,
        token=True,
        headers=None,
        raw_body=None,
    ):
        request_headers = dict(headers or {})
        if token:
            request_headers["Authorization"] = f"Bearer {self.token}"
        if method == "POST":
            request_headers.setdefault("Accept", "application/json")

        payload = raw_body
        if payload is None and body is not None:
            payload = json.dumps(body)

        conn = http.client.HTTPConnection("127.0.0.1", self.port, timeout=10)
        conn.request(method, path, body=payload, headers=request_headers)
        response = conn.getresponse()
        response_body = response.read().decode("utf-8", errors="replace")
        response_headers = dict(response.getheaders())
        conn.close()
        return response.status, response_body, response_headers

    def rpc(self, method, params=None, request_id=1):
        message = {"jsonrpc": "2.0", "id": request_id, "method": method}
        if params is not None:
            message["params"] = params
        status, body, _headers = self.request("POST", "/mcp", body=message)
        assert status == 200, body
        return json.loads(body)

    def notify(self, method, params=None):
        message = {"jsonrpc": "2.0", "method": method}
        if params is not None:
            message["params"] = params
        return self.request("POST", "/mcp", body=message)

    def close(self):
        if not self.process:
            return
        try:
            self.process.stdin.close()
        except Exception:
            pass
        try:
            self.process.terminate()
            self.process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            self.process.kill()
            self.process.wait(timeout=5)


@pytest.fixture
def http_client(browserd_binary):
    client = HTTPMCPClient(browserd_binary)
    client.start()
    client.wait_until_ready()
    yield client
    client.close()


def test_streamable_http_mcp_lifecycle(http_client):
    initialize = http_client.rpc(
        "initialize",
        {
            "protocolVersion": "2024-11-05",
            "capabilities": {},
            "clientInfo": {"name": "browserd-http-test", "version": "1.0"},
        },
        request_id=1,
    )
    assert initialize["result"]["serverInfo"]["name"] == "browserd"

    status, body, _headers = http_client.notify("notifications/initialized")
    assert status == 202
    assert body == ""

    tools = http_client.rpc("tools/list", request_id=2)
    tool_names = {tool["name"] for tool in tools["result"]["tools"]}
    assert "browser_evaluate" in tool_names

    result = http_client.rpc(
        "tools/call",
        {
            "name": "browser_evaluate",
            "arguments": {"expression": "1 + 1"},
        },
        request_id=3,
    )
    assert result["result"]["content"][0]["text"] == "2"


def test_streamable_http_auth_origin_and_method_failures(http_client):
    status, _body, headers = http_client.request("POST", "/mcp", body={}, token=False)
    assert status == 401
    auth_headers = {key.lower(): value for key, value in headers.items()}
    assert auth_headers["www-authenticate"] == "Bearer"

    status, _body, _headers = http_client.request(
        "POST",
        "/mcp",
        body={},
        token=True,
        headers={"Origin": "https://example.com"},
    )
    assert status == 403

    status, _body, _headers = http_client.request("GET", "/mcp", token=True)
    assert status == 405

    status, _body, _headers = http_client.request("GET", "/unknown", token=False)
    assert status == 404

    status, body, _headers = http_client.request(
        "POST", "/mcp", token=True, raw_body="{"
    )
    assert status == 400
    assert body == "Invalid JSON"


def test_streamable_http_requires_token(browserd_binary):
    client = HTTPMCPClient(browserd_binary)
    client.start(include_token=False)
    try:
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline and client.process.poll() is None:
            time.sleep(0.1)
        assert client.process.poll() is not None
        assert any("BROWSERD_MCP_HTTP_TOKEN" in line for line in client.stderr_lines)
    finally:
        client.close()


def test_streamable_http_all_interfaces_bind(browserd_binary):
    client = HTTPMCPClient(browserd_binary)
    client.start(extra_args=["--mcp-http-host=0.0.0.0"])
    try:
        client.wait_until_ready()
        result = client.rpc(
            "initialize",
            {
                "protocolVersion": "2024-11-05",
                "capabilities": {},
                "clientInfo": {"name": "browserd-http-test", "version": "1.0"},
            },
            request_id=1,
        )
        assert result["result"]["serverInfo"]["name"] == "browserd"
    finally:
        client.close()


def test_streamable_http_rejects_invalid_bind_host(browserd_binary):
    client = HTTPMCPClient(browserd_binary)
    client.start(extra_args=["--mcp-http-host=192.0.2.10"])
    try:
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline and client.process.poll() is None:
            time.sleep(0.1)
        assert client.process.poll() is not None
        assert any("Invalid --mcp-http-host" in line for line in client.stderr_lines)
    finally:
        client.close()
