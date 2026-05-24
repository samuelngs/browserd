.PHONY: all fetch apply unapply build rebuild export rebase verify test test-tier1 test-tier2 clean help

JOBS ?= $(shell sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 8)
VERSION := $(shell cat chromium_version.txt | tr -d '[:space:]')

all: build

## Source management

fetch:
	scripts/fetch.sh

apply:
	scripts/apply.sh

unapply:
	scripts/unapply.sh

export:
	scripts/export.sh

rebase:
	@[ -n "$(NEW_VERSION)" ] || { echo "Usage: make rebase NEW_VERSION=131.0.6778.69"; exit 1; }
	scripts/rebase.sh $(NEW_VERSION)

## Build

build:
	JOBS=$(JOBS) scripts/build.sh

rebuild: unapply apply build

## Verification & testing

verify:
	scripts/verify.sh

test:
	pytest

test-tier1:
	pytest -m tier1

test-tier2:
	pytest -m tier2

## Cleanup

clean:
	rm -rf chromium/src/out/Release
	rm -f out

help:
	@echo "browserd $(VERSION)"
	@echo ""
	@echo "Source:"
	@echo "  make fetch           Shallow-clone Chromium $(VERSION)"
	@echo "  make apply           Apply patch groups (core, tls, optional)"
	@echo "  make unapply         Reset source to clean tag"
	@echo "  make export          Export commits back to patch files"
	@echo "  make rebase          Rebase to new version (NEW_VERSION=x.x.x.x)"
	@echo ""
	@echo "Build:"
	@echo "  make build           Full build (fetch + apply if needed) [JOBS=$(JOBS)]"
	@echo "  make rebuild         Unapply, re-apply, build from scratch"
	@echo ""
	@echo "Test:"
	@echo "  make verify          Fingerprint verification suite"
	@echo "  make test            Run all pytest tests"
	@echo "  make test-tier1      Run tier1 (must-pass) tests only"
	@echo "  make test-tier2      Run tier2 (commercial) tests only"
	@echo ""
	@echo "Other:"
	@echo "  make clean           Remove build output"
