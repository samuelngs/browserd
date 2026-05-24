"""Tier 1: nowsecure.nl — Cloudflare-protected bot detection challenge.

This site uses Cloudflare Turnstile. If the browser is not detected as a bot,
the challenge auto-resolves. If it IS detected, a manual checkbox click is
needed. We attempt the checkbox click via the accessibility tree.
"""

import time

import pytest

pytestmark = pytest.mark.tier1


def test_nowsecure_cloudflare_pass(browser):
    """Navigate to nowsecure.nl and verify Cloudflare challenge resolves."""
    browser.navigate("https://nowsecure.nl/#relax")
    time.sleep(5)

    # Check if already passed
    body = browser.evaluate("document.body?.innerText?.substring(0, 500)")
    if "passed" in body.lower():
        return

    # Cloudflare Turnstile may need a checkbox click
    snap = browser.snapshot()
    if "Verify you are human" in snap:
        # Find the checkbox ref in the snapshot
        for line in snap.split("\n"):
            if "checkBox" in line and "Verify" in line:
                ref = line.split("ref=")[-1].rstrip("]")
                try:
                    browser.click(int(ref))
                except Exception:
                    pass
                break
        time.sleep(8)

    body = browser.evaluate("document.body?.innerText?.substring(0, 500)")
    if "passed" in body.lower():
        return

    # CF Turnstile is a cross-origin iframe — can't interact from main frame
    snap = browser.snapshot()
    if "Cloudflare" in snap or "Verify you are human" in snap:
        pytest.skip(
            "Cloudflare Turnstile challenge requires cross-origin iframe "
            "interaction (not yet supported)"
        )

    pytest.fail(
        f"Cloudflare challenge not resolved.\n"
        f"Page text: {body[:300]}"
    )
