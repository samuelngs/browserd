"""Tier 1: bot.sannysoft.com — 55+ browser fingerprint and bot detection tests."""

import time

import pytest

pytestmark = pytest.mark.tier1


@pytest.fixture(scope="module")
def sannysoft_snapshot(browser):
    """Navigate to sannysoft once, wait for load, return snapshot."""
    browser.navigate("https://bot.sannysoft.com")
    time.sleep(8)
    return browser.snapshot()


@pytest.fixture(scope="module")
def sannysoft_body(browser, sannysoft_snapshot):
    """Return page body text."""
    return browser.evaluate("document.body.innerText")


def test_webdriver_not_detected(sannysoft_snapshot):
    assert "missing (passed)" in sannysoft_snapshot, (
        "WebDriver should be missing (passed)"
    )


def test_webdriver_advanced(sannysoft_snapshot):
    snap = sannysoft_snapshot
    idx = snap.find('"WebDriver Advanced"')
    assert idx != -1, "WebDriver Advanced test not found"
    after = snap[idx:]
    assert '"passed"' in after[:200], "WebDriver Advanced should pass"


def test_chrome_object_present(sannysoft_snapshot):
    assert "present (passed)" in sannysoft_snapshot, (
        "Chrome object should be present (passed)"
    )


def test_user_agent_clean(sannysoft_body):
    assert "HeadlessChrome" not in sannysoft_body, (
        "UA must not contain HeadlessChrome"
    )
    assert "Chrome/" in sannysoft_body, "UA should contain Chrome/"


def test_webgl_real_gpu(sannysoft_snapshot):
    assert "Google Inc." in sannysoft_snapshot, (
        "WebGL vendor should be Google Inc."
    )
    assert "ANGLE" in sannysoft_snapshot, "WebGL renderer should use ANGLE"


def test_plugins_present(sannysoft_body):
    assert "5" in sannysoft_body or "PDF Viewer" in sannysoft_body, (
        "Should report 5 plugins"
    )


SCANNER_TESTS = [
    "PHANTOM_UA",
    "PHANTOM_PROPERTIES",
    "PHANTOM_ETSL",
    "PHANTOM_LANGUAGE",
    "PHANTOM_WEBSOCKET",
    "MQ_SCREEN",
    "PHANTOM_OVERFLOW",
    "PHANTOM_WINDOW_HEIGHT",
    "HEADCHR_UA",
    "HEADCHR_CHROME_OBJ",
    "HEADCHR_PERMISSIONS",
    "HEADCHR_PLUGINS",
    "HEADCHR_IFRAME",
    "CHR_DEBUG_TOOLS",
    "SELENIUM_DRIVER",
    "CHR_BATTERY",
    "CHR_MEMORY",
    "TRANSPARENT_PIXEL",
    "SEQUENTUM",
    "VIDEO_CODECS",
]


@pytest.mark.parametrize("test_name", SCANNER_TESTS)
def test_scanner(sannysoft_body, test_name):
    """Each Fingerprint Scanner test should report 'ok'."""
    idx = sannysoft_body.find(test_name)
    assert idx != -1, f"Test {test_name} not found on page"
    section = sannysoft_body[idx : idx + 100]
    assert "ok" in section, f"Test {test_name} did not return 'ok': {section!r}"
