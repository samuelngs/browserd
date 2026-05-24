"""Tier 1: iphey.com — browser fingerprint trust analysis."""

import time

import pytest

pytestmark = pytest.mark.tier1


@pytest.fixture(scope="module")
def iphey_result(browser):
    """Navigate to iphey.com, wait for analysis."""
    browser.navigate("https://iphey.com")
    time.sleep(15)
    return browser.evaluate("document.body.innerText")


def test_iphey_trust_rating(iphey_result):
    """iphey.com should rate the browser as Trustworthy (not Suspicious)."""
    lower = iphey_result.lower()

    if "trustworthy" in lower:
        return

    if "suspicious" in lower:
        pytest.fail(
            f"iphey rated browser as Suspicious.\n"
            f"Page excerpt: {iphey_result[:800]}"
        )

    pytest.fail(
        f"Could not determine iphey rating.\n"
        f"Page excerpt: {iphey_result[:800]}"
    )


def test_iphey_no_cdp_detected(iphey_result):
    """iphey should not detect CDP protocol."""
    assert "hasCDP\nfalse" in iphey_result or "hasCDP\n false" in iphey_result, (
        "CDP detected by iphey"
    )


def test_iphey_no_webdriver_detected(iphey_result):
    """iphey should not detect webdriver flag."""
    assert "hasWebdriver\nfalse" in iphey_result or "hasWebdriver\n false" in iphey_result, (
        "Webdriver detected by iphey"
    )


def test_iphey_hardware_ok(iphey_result):
    """Hardware checks should pass."""
    assert "HARDWARE" in iphey_result and "Everything is fine" in iphey_result, (
        f"Hardware checks failed. Excerpt: {iphey_result[:800]}"
    )


def test_iphey_software_ok(iphey_result):
    """Software checks should pass."""
    assert "SOFTWARE" in iphey_result and "Everything is fine" in iphey_result, (
        f"Software checks failed. Excerpt: {iphey_result[:800]}"
    )
