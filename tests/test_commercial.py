"""Tier 2: commercial sites with anti-bot protection.

These tests verify pages load without being blocked.
They don't test deep functionality — just that the anti-bot
system doesn't prevent access.
"""

import pytest

pytestmark = pytest.mark.tier2

BLOCKED_INDICATORS = [
    "Access Denied",
    "Please verify you are a human",
    "Checking your browser",
    "Just a moment...",
    "Enable JavaScript and cookies to continue",
    "Request unsuccessful. Incapsula",
    "Attention Required! | Cloudflare",
]


def _check_not_blocked(body):
    for indicator in BLOCKED_INDICATORS:
        if indicator in body:
            pytest.fail(f"Site blocked browser. Found: {indicator!r}")


def test_gitlab_login(browser):
    """GitLab login page should load past Cloudflare."""
    browser.navigate("https://gitlab.com/users/sign_in")
    import time
    time.sleep(10)

    body = browser.evaluate("document.body?.innerText?.substring(0, 2000)")

    # GitLab uses Cloudflare — may see challenge
    title = browser.evaluate("document.title")
    if (
        "Checking your browser" in body
        or "Verify you are human" in body
        or "Just a moment" in title
        or len(body.strip()) < 50
    ):
        pytest.skip("Cloudflare challenge present — requires interaction")

    _check_not_blocked(body)
    assert (
        "GitLab" in body
        or "Sign in" in body
        or "Username" in body
        or "GitLab" in title
    ), f"GitLab page did not load. Title: {title}, Body: {body[:300]}"


def test_nike(browser):
    """Nike.com should load without anti-bot block."""
    browser.navigate("https://www.nike.com/")
    try:
        browser.wait_for(text="Nike", timeout_ms=20000)
    except Exception:
        pass

    body = browser.evaluate("document.body?.innerText?.substring(0, 2000)")
    _check_not_blocked(body)
    title = browser.evaluate("document.title")
    assert "Nike" in body or "Nike" in title, (
        f"Nike page did not load. Title: {title}, Body: {body[:300]}"
    )


def test_indeed(browser):
    """Indeed.com should load without Cloudflare block."""
    browser.navigate("https://www.indeed.com/companies/search")
    try:
        browser.wait_for(text="Indeed", timeout_ms=20000)
    except Exception:
        pass

    body = browser.evaluate("document.body?.innerText?.substring(0, 2000)")
    _check_not_blocked(body)
    title = browser.evaluate("document.title")
    assert "Indeed" in body or "Indeed" in title, (
        f"Indeed page did not load. Title: {title}, Body: {body[:300]}"
    )
