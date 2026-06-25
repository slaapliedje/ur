# Ur — top-level build orchestration.
#
# Drives TWO toolchains:
#   cc65  -> 6502 targets: atari, c64, apple2
#   z88dk -> Z80 target:   adam
# Plus a host build of src/common for fast unit tests.
#
# Most of this is a SKELETON: there is no game code yet, so the platform targets
# print a notice and exit cleanly until sources exist. See docs/development.md.

include makefiles/common.mk
include makefiles/atari.mk
include makefiles/adam.mk
include makefiles/c64.mk
include makefiles/apple2.mk
include makefiles/host-test.mk

.PHONY: all clean deps help
.DEFAULT_GOAL := help

all: atari adam c64 apple2 ## Build every platform

deps: $(ATARI_LIB) $(ADAM_LIB) $(EOSLIB) ## Fetch dependencies (fujinet-lib + eoslib for adam)
	@echo "fujinet-lib $(FNLIB_VERSION) ready for: atari adam (+ eoslib for adam)."
	@echo "(Other targets download their lib once they gain sources — see their makefiles/*.mk.)"

clean: ## Remove build output
	rm -rf $(BUILD_DIR)

help: ## Show this help
	@echo "Ur build targets:"
	@grep -hE '^[a-zA-Z_-]+:.*?## .*$$' $(MAKEFILE_LIST) | \
	  sort | awk 'BEGIN{FS=":.*?## "}{printf "  \033[36m%-8s\033[0m %s\n",$$1,$$2}'
