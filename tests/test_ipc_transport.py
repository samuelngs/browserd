import json
import os
from pathlib import Path
import shutil
import socket
import subprocess
import tempfile
import threading
import time

import pytest


pytestmark = pytest.mark.skipif(
    not hasattr(socket, "AF_UNIX"),
    reason="Unix domain socket IPC tests run only on POSIX platforms",
)


class IPCMCPClient:
    def __init__(self, binary_path, socket_path):
        self.binary_path = binary_path
        self.socket_path = str(socket_path)
        self.process = None
        self.sock = None
        self.file = None
        self.stderr_lines = []
        self._next_id = 0

    def start(self, extra_args=None):
        args = [self.binary_path, f"--mcp-ipc-path={self.socket_path}"]
        if extra_args:
            args.extend(extra_args)

        self.process = subprocess.Popen(
            args,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
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

    def connect(self, timeout=20):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if self.process.poll() is not None:
                raise RuntimeError(
                    f"browserd exited early: {self.process.returncode}\n"
                    + "\n".join(self.stderr_lines)
                )

            sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            try:
                sock.connect(self.socket_path)
                self.sock = sock
                self.file = sock.makefile("rwb", buffering=0)
                return
            except OSError:
                sock.close()
                time.sleep(0.1)

        raise TimeoutError("browserd IPC transport did not become ready")

    def rpc(self, method, params=None, request_id=None):
        if request_id is None:
            self._next_id += 1
            request_id = self._next_id
        message = {"jsonrpc": "2.0", "id": request_id, "method": method}
        if params is not None:
            message["params"] = params
        self.file.write(json.dumps(message).encode("utf-8") + b"\n")
        raw = self.file.readline()
        assert raw, "IPC server closed connection before response"
        return json.loads(raw.decode("utf-8"))

    def notify(self, method, params=None):
        message = {"jsonrpc": "2.0", "method": method}
        if params is not None:
            message["params"] = params
        self.file.write(json.dumps(message).encode("utf-8") + b"\n")

    def initialize(self):
        return self.rpc(
            "initialize",
            {
                "protocolVersion": "2024-11-05",
                "capabilities": {},
                "clientInfo": {"name": "browserd-ipc-test", "version": "1.0"},
            },
        )

    def close(self):
        if self.file:
            try:
                self.file.close()
            except Exception:
                pass
        if self.sock:
            try:
                self.sock.close()
            except Exception:
                pass
        if not self.process:
            return
        try:
            self.process.terminate()
            self.process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            self.process.kill()
            self.process.wait(timeout=5)


def make_private_dir(path):
    path.mkdir()
    path.chmod(0o700)
    return path


@pytest.fixture
def short_tmp_path():
    path = Path(tempfile.mkdtemp(prefix="bd-ipc-", dir="/tmp"))
    try:
        yield path
    finally:
        shutil.rmtree(path, ignore_errors=True)


def wait_for_exit(client, timeout=10):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if client.process.poll() is not None:
            return client.process.returncode
        time.sleep(0.1)
    raise TimeoutError("browserd did not exit")


def test_ipc_mcp_lifecycle(browserd_binary, short_tmp_path):
    ipc_dir = make_private_dir(short_tmp_path / "ipc")
    client = IPCMCPClient(browserd_binary, ipc_dir / "browserd.sock")
    client.start()
    try:
        client.connect()

        initialize = client.initialize()
        assert initialize["result"]["serverInfo"]["name"] == "browserd"

        client.notify("notifications/initialized")

        tools = client.rpc("tools/list")
        tool_names = {tool["name"] for tool in tools["result"]["tools"]}
        assert "browser_tab_list" in tool_names

        tabs = client.rpc(
            "tools/call",
            {"name": "browser_tab_list", "arguments": {}},
        )
        assert "result" in tabs
        assert tabs["result"]["content"][0]["type"] == "text"
    finally:
        client.close()


def test_ipc_rejects_missing_parent(browserd_binary, short_tmp_path):
    client = IPCMCPClient(
        browserd_binary, short_tmp_path / "missing" / "browserd.sock"
    )
    client.start()
    try:
        assert wait_for_exit(client) != 0
        assert any("parent directory does not exist" in line for line in client.stderr_lines)
    finally:
        client.close()


def test_ipc_rejects_regular_file_without_unlinking(browserd_binary, short_tmp_path):
    ipc_dir = make_private_dir(short_tmp_path / "ipc")
    socket_path = ipc_dir / "browserd.sock"
    socket_path.write_text("not a socket")

    client = IPCMCPClient(browserd_binary, socket_path)
    client.start()
    try:
        assert wait_for_exit(client) != 0
        assert socket_path.read_text() == "not a socket"
        assert any("is not a socket" in line for line in client.stderr_lines)
    finally:
        client.close()


def test_ipc_cleans_up_stale_socket(browserd_binary, short_tmp_path):
    ipc_dir = make_private_dir(short_tmp_path / "ipc")
    socket_path = ipc_dir / "browserd.sock"
    stale = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    try:
        stale.bind(str(socket_path))
    finally:
        stale.close()

    client = IPCMCPClient(browserd_binary, socket_path)
    client.start()
    try:
        client.connect()
        initialize = client.initialize()
        assert initialize["result"]["serverInfo"]["name"] == "browserd"
    finally:
        client.close()


def test_ipc_rejects_active_socket(browserd_binary, short_tmp_path):
    ipc_dir = make_private_dir(short_tmp_path / "ipc")
    socket_path = ipc_dir / "browserd.sock"
    active = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    active.bind(str(socket_path))
    active.listen(1)

    client = IPCMCPClient(browserd_binary, socket_path)
    client.start()
    try:
        assert wait_for_exit(client) != 0
        assert any("already active" in line for line in client.stderr_lines)
    finally:
        client.close()
        active.close()
        try:
            os.unlink(socket_path)
        except FileNotFoundError:
            pass


def test_ipc_rejects_group_or_other_writable_parent(browserd_binary, short_tmp_path):
    ipc_dir = make_private_dir(short_tmp_path / "ipc")
    ipc_dir.chmod(0o777)

    client = IPCMCPClient(browserd_binary, ipc_dir / "browserd.sock")
    client.start()
    try:
        assert wait_for_exit(client) != 0
        assert any("must not be group/other writable" in line for line in client.stderr_lines)
    finally:
        ipc_dir.chmod(0o700)
        client.close()


def test_ipc_closes_second_concurrent_client(browserd_binary, short_tmp_path):
    ipc_dir = make_private_dir(short_tmp_path / "ipc")
    client = IPCMCPClient(browserd_binary, ipc_dir / "browserd.sock")
    client.start()
    try:
        client.connect()
        initialize = client.initialize()
        assert initialize["result"]["serverInfo"]["name"] == "browserd"

        second = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        second.settimeout(3)
        try:
            second.connect(client.socket_path)
            deadline = time.monotonic() + 3
            while time.monotonic() < deadline:
                try:
                    second.sendall(
                        b'{"jsonrpc":"2.0","id":1,"method":"ping"}\n'
                    )
                    data = second.recv(1)
                    assert data == b""
                    return
                except (BrokenPipeError, ConnectionResetError):
                    return
                except TimeoutError:
                    pass
                time.sleep(0.1)
            pytest.fail("second IPC client was not closed")
        finally:
            second.close()
    finally:
        client.close()
