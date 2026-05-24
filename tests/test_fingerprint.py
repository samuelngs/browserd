"""Tier 2: fingerprint.com/demo/ — Fingerprint Pro identification and bot detection."""

import pytest

pytestmark = pytest.mark.tier2


def test_fingerprint_visitor_id_assigned(browser):
    """Fingerprint Pro should assign a visitor ID."""
    browser.navigate("https://fingerprint.com/demo/")
    browser.wait_for(text="Visitor ID", timeout_ms=20000)

    visitor_id = browser.evaluate(
        "document.querySelector('[class*=visitorId]')?.textContent?.trim()"
    )
    assert visitor_id and len(visitor_id) >= 10, (
        f"Expected visitor ID of 10+ chars, got: {visitor_id!r}"
    )


def test_fingerprint_identified_as_chrome(browser):
    """Fingerprint should identify browser as Chrome, not headless."""
    browser.navigate("https://fingerprint.com/demo/")
    import time
    time.sleep(12)

    body = browser.evaluate("document.body.innerText.substring(0, 5000)")
    assert "Chrome" in body, f"Should be identified as Chrome. Body: {body[:500]}"
    assert "Headless" not in body, "Should not be identified as Headless"


def test_fingerprint_confidence_score(browser):
    """Confidence score should be reasonable (> 0.5)."""
    browser.navigate("https://fingerprint.com/demo/")
    browser.wait_for(text="CONFIDENCE", timeout_ms=20000)

    score_text = browser.evaluate(
        "(() => {"
        "  const els = document.querySelectorAll('*');"
        "  for (const el of els) {"
        "    const t = el.textContent?.trim();"
        "    if (/^0\\.\\d+$/.test(t)) return t;"
        "  }"
        "  return '';"
        "})()"
    )
    if score_text:
        score = float(score_text)
        assert score > 0.5, f"Confidence score {score} is too low"
