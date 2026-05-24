import os
import sys
from pathlib import Path

import pytest

sys.path.insert(0, os.path.dirname(__file__))
from mcp_client import MCPClient


def pytest_addoption(parser):
    parser.addoption(
        "--browserd-binary",
        default=None,
        help="Path to browserd binary",
    )


@pytest.fixture(scope="session")
def browserd_binary(request):
    path = request.config.getoption("--browserd-binary")
    if not path:
        path = os.environ.get("BROWSERD_BINARY")
    if not path:
        root = Path(__file__).parent.parent
        path = str(root / "out" / "browserd")
    assert os.path.isfile(path), f"browserd not found at {path}"
    return path


@pytest.fixture(scope="module")
def browser(browserd_binary):
    client = MCPClient(browserd_binary)
    client.start()
    client.initialize()
    yield client
    client.close()


@pytest.hookimpl(tryfirst=True, hookwrapper=True)
def pytest_runtest_makereport(item, call):
    outcome = yield
    rep = outcome.get_result()
    setattr(item, f"rep_{rep.when}", rep)


@pytest.fixture(autouse=True)
def save_artifact_on_failure(request):
    yield
    rep = getattr(request.node, "rep_call", None)
    if not rep or not rep.failed:
        return
    try:
        browser = request.getfixturevalue("browser")
        png = browser.screenshot(timeout=5)
        if png:
            artifacts = Path(__file__).parent / "artifacts"
            artifacts.mkdir(exist_ok=True)
            dest = artifacts / f"{request.node.name}.png"
            dest.write_bytes(png)
    except Exception:
        pass
