"""Tier 1: pixelscan.net — bot detection scanner.

Pixelscan requires clicking "Scan My Browser Now" to start analysis.
"""

import time

import pytest

pytestmark = pytest.mark.tier1


@pytest.fixture(scope="module")
def pixelscan_result(browser):
    """Navigate to pixelscan, start scan, wait for results."""
    browser.navigate("https://pixelscan.net/")
    time.sleep(5)

    # Click "Scan My Browser Now" button
    snap = browser.snapshot()
    for line in snap.split("\n"):
        if "Scan My Browser" in line and "ref=" in line:
            ref = line.split("ref=")[-1].rstrip("]")
            try:
                browser.click(int(ref))
            except Exception:
                pass
            break

    # Wait for scan to complete
    time.sleep(15)
    return browser.evaluate("document.body.innerText")


def test_pixelscan_bot_detection(pixelscan_result):
    """Pixelscan should not detect automation framework."""
    lower = pixelscan_result.lower()
    detected = (
        "automation framework detected" in lower
        and "no automation" not in lower
    )
    assert not detected, (
        f"Automation framework detected: {pixelscan_result[:500]}"
    )


def test_pixelscan_fingerprint(pixelscan_result):
    """Pixelscan fingerprint check should not flag masking."""
    lower = pixelscan_result.lower()
    masking_flagged = (
        "masking your fingerprint" in lower
        and "not masking" not in lower
    )
    assert not masking_flagged, (
        f"Fingerprint masking flagged: {pixelscan_result[:500]}"
    )
