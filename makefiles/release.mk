# Release packaging — build every platform and bundle the distributable images.
#
# `make release` builds all 11 targets (the 4 FujiNet machines + the bonus console/
# handheld ports) and collects each one's final image into build/release/ur-<ver>/
# with a MANIFEST, SHA256SUMS, and a zip.  Version defaults to `git describe`; the
# release CI workflow passes the tag explicitly (UR_VERSION=v1.2.3).

UR_VERSION ?= $(shell git describe --tags --always --dirty 2>/dev/null || echo dev)

# Every shippable client. (Online/DHGR variants are separate builds — see CI;
# the release ships the default local-play image per platform.)
RELEASE_TARGETS := atari a5200 c64 apple2 adam coleco sms gamegear gb nes

.PHONY: release release-clean
release: $(RELEASE_TARGETS) ## Build all platforms and bundle distributables into build/release/
	UR_VERSION="$(UR_VERSION)" BUILD_DIR="$(BUILD_DIR)" tools/package-release.sh

release-clean: ## Remove the packaged release bundle
	rm -rf $(BUILD_DIR)/release
