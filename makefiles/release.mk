# Release packaging — build every platform and bundle the distributable images.
#
# `make release` builds every shippable image and collects them into
# build/release/ur-<ver>/ with a MANIFEST, SHA256SUMS, and a zip.  Version defaults
# to `git describe`; the release CI workflow passes the tag (UR_VERSION=v1.2.3).

UR_VERSION ?= $(shell git describe --tags --always --dirty 2>/dev/null || echo dev)

# Every shippable client: the default local-play image per platform, PLUS the
# FujiNet-online variants for the two platforms that gate online behind a build
# flag (Atari and Adam already bake online into their default binary; the C64 and
# Apple II ship an extra ur-online.* alongside their local default), PLUS the
# 16-bit Atari ST family (ST + STe + TT + Falcon variants of the shared layer).
RELEASE_TARGETS := atari a5200 c64 c64-online apple2 apple2-online \
                   adam coleco sms gamegear gb nes \
                   st st-ste st-tt st-falcon

.PHONY: release release-clean
release: $(RELEASE_TARGETS) ## Build all platforms and bundle distributables into build/release/
	UR_VERSION="$(UR_VERSION)" BUILD_DIR="$(BUILD_DIR)" tools/package-release.sh

release-clean: ## Remove the packaged release bundle
	rm -rf $(BUILD_DIR)/release
