BITCOIN_DIR := $(CURDIR)/../bitcoin
BITCOIN_DEPENDS_DIR := $(BITCOIN_DIR)/depends
PATCH_DIR := $(CURDIR)/patches/qt
PATCH_STATE_DIR := $(CURDIR)/patches/.applied

PATCH_FILES := $(sort $(wildcard $(PATCH_DIR)/*.patch))
PATCH_STAMPS := $(patsubst $(PATCH_DIR)/%.patch,$(PATCH_STATE_DIR)/%.stamp,$(PATCH_FILES))

.DEFAULT_GOAL := all

.PHONY: all clean patches

all: patches
	$(MAKE) -C $(BITCOIN_DEPENDS_DIR)

patches: $(PATCH_STAMPS)

$(PATCH_STATE_DIR)/%.stamp: $(PATCH_DIR)/%.patch
	@mkdir -p $(PATCH_STATE_DIR)
	@echo "Applying $< to bitcoin submodule"
	@if git -C $(BITCOIN_DIR) apply --check --reverse $< >/dev/null 2>&1; then \
		echo "Patch $< already applied"; \
	else \
		git -C $(BITCOIN_DIR) apply $<; \
	fi
	@touch $@

clean:
	rm -rf $(PATCH_STATE_DIR)
	$(MAKE) -C $(BITCOIN_DEPENDS_DIR) clean

%: patches
	$(MAKE) -C $(BITCOIN_DEPENDS_DIR) $@
