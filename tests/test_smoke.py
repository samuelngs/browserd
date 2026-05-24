"""Smoke test: verify browserd starts and basic MCP works."""


def test_browserd_starts(browser):
    result = browser.evaluate("1 + 1")
    assert result == "2" or result == 2


def test_navigate_works(browser):
    browser.navigate("https://example.com")
    browser.wait_for(text="Example Domain", timeout_ms=10000)
    title = browser.evaluate("document.title")
    assert "Example" in title
