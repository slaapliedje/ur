# Host unit tests for the portable core (src/common), compiled with the HOST C
# compiler so rules/AI/protocol logic can be tested fast, off-target. The plat_*
# interface is stubbed by the test harness. See src/common/CLAUDE.md.
HOST_CC   ?= cc
TEST_DIR  := tests
TEST_OUT  := $(BUILD_DIR)/host-test
TEST_SRCS := $(wildcard $(TEST_DIR)/*.c) $(wildcard $(COMMON_DIR)/*.c)

.PHONY: test
test: | $(TEST_OUT) ## Build & run host unit tests for src/common
ifeq ($(strip $(wildcard $(TEST_DIR)/*.c)),)
	@echo "[test] no tests yet — add tests/*.c. (skeleton)"
else
	$(HOST_CC) -std=c99 -Wall -Wextra $(COMMON_INC) -o $(TEST_OUT)/run $(TEST_SRCS)
	$(TEST_OUT)/run
endif

$(TEST_OUT):
	mkdir -p $@
