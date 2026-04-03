PORT ?= /dev/ttyACM0
BADGELINKPORT ?= $(PORT)

SHELL := /usr/bin/env bash

# App installation settings
APP_SLUG_NAME ?= at.cavac.floppybird-grace
APP_INSTALL_BASE_PATH ?= /int/apps/
APP_INSTALL_PATH = $(APP_INSTALL_BASE_PATH)$(APP_SLUG_NAME)

# ESP-IDF tools path (needed for the RISC-V cross-compiler)
IDF_PATH ?= $(shell cat .IDF_PATH 2>/dev/null || echo `pwd`/esp-idf)
IDF_TOOLS_PATH ?= $(shell cat .IDF_TOOLS_PATH 2>/dev/null || echo `pwd`/esp-idf-tools)
IDF_BRANCH ?= v5.5.1
IDF_GITHUB_ASSETS ?= dl.espressif.com/github_assets

export IDF_TOOLS_PATH
export IDF_GITHUB_ASSETS

BUILD ?= build

MAKEFLAGS += --silent

####

.PHONY: all
all: build

.PHONY: build
build:
	@echo "=== Building app.so ==="
	mkdir -p $(BUILD)
	cd $(BUILD) && cmake .. && make
	@echo "=== Build complete: $(BUILD)/app.so ==="

# Badgelink
.PHONY: badgelink
badgelink:
	rm -rf badgelink
	#git clone https://github.com/badgeteam/esp32-component-badgelink.git badgelink
	git clone https:///github.com/nullislandspace/esp32-component-badgelink.git badgelink
	cd badgelink/tools; ./install.sh

# Determine badgelink connection argument: --tcp for host:port, --port for serial devices
BADGELINK_CONN := $(if $(findstring :,$(BADGELINKPORT)),--tcp $(BADGELINKPORT),--port $(BADGELINKPORT))

.PHONY: install
install: build
	@echo "=== Installing to device ==="
	@echo "Creating directory $(APP_INSTALL_PATH)..."
	cd badgelink/tools; ./badgelink.sh $(BADGELINK_CONN) fs mkdir $(APP_INSTALL_PATH) || true
	@echo "Uploading metadata.json..."
	cd badgelink/tools; ./badgelink.sh $(BADGELINK_CONN) fs upload $(APP_INSTALL_PATH)/metadata.json ../../metadata/metadata.json
	@echo "Uploading icon16.png..."
	cd badgelink/tools; ./badgelink.sh $(BADGELINK_CONN) fs upload $(APP_INSTALL_PATH)/icon16.png ../../metadata/icon16.png
	@echo "Uploading icon32.png..."
	cd badgelink/tools; ./badgelink.sh $(BADGELINK_CONN) fs upload $(APP_INSTALL_PATH)/icon32.png ../../metadata/icon32.png
	@echo "Uploading icon64.png..."
	cd badgelink/tools; ./badgelink.sh $(BADGELINK_CONN) fs upload $(APP_INSTALL_PATH)/icon64.png ../../metadata/icon64.png
	@echo "Uploading app.so..."
	cd badgelink/tools; ./badgelink.sh $(BADGELINK_CONN) fs upload $(APP_INSTALL_PATH)/app.so ../../$(BUILD)/app.so
	@echo "=== Installation complete ==="

GRACELOADER_SLUG ?= at.cavac.graceloader

.PHONY: run
run:
	cd badgelink/tools; ./badgelink.sh $(BADGELINK_CONN) start $(GRACELOADER_SLUG) $(APP_INSTALL_PATH)/app.so

APP_REPO_PATH ?= ../tanmatsu-app-repository/$(APP_SLUG_NAME)

.PHONY: apprepo
apprepo: build
	@echo "=== Updating app repository ==="
	mkdir -p $(APP_REPO_PATH)
	cp metadata/metadata.json $(APP_REPO_PATH)/metadata.json
	cp metadata/icon16.png $(APP_REPO_PATH)/icon16.png
	cp metadata/icon32.png $(APP_REPO_PATH)/icon32.png
	cp metadata/icon64.png $(APP_REPO_PATH)/icon64.png
	cp $(BUILD)/app.so $(APP_REPO_PATH)/app.so
	@echo "=== App repository updated at $(APP_REPO_PATH) ==="

# Preparation

.PHONY: prepare
prepare: sdk

.PHONY: sdk
sdk:
	if test -d "$(IDF_PATH)"; then echo -e "ESP-IDF target folder exists!\r\nPlease remove the folder or un-set the environment variable."; exit 1; fi
	if test -d "$(IDF_TOOLS_PATH)"; then echo -e "ESP-IDF tools target folder exists!\r\nPlease remove the folder or un-set the environment variable."; exit 1; fi
	git clone --recursive --branch "$(IDF_BRANCH)" https://github.com/espressif/esp-idf.git "$(IDF_PATH)" --depth=1 --shallow-submodules
	cd "$(IDF_PATH)"; git submodule update --init --recursive
	cd "$(IDF_PATH)"; bash install.sh all

.PHONY: reinstallsdk
reinstallsdk:
	cd "$(IDF_PATH)"; bash install.sh all

.PHONY: removesdk
removesdk:
	rm -rf "$(IDF_PATH)"
	rm -rf "$(IDF_TOOLS_PATH)"

.PHONY: refreshsdk
refreshsdk: removesdk sdk

# Verification

.PHONY: verify
verify: build
	@echo "=== Verifying symbols ==="
	@MISSING=$$(comm -23 \
	  <(nm -D --undefined-only $(BUILD)/app.so | awk '{print $$2}' | sort -u) \
	  <(nm -D --defined-only fakelib/liball.so | awk '{print $$3}' | sort -u)); \
	if [ -n "$$MISSING" ]; then \
	  echo "ERROR: app.so requires symbols not in fakelib:"; \
	  echo "$$MISSING"; \
	  exit 1; \
	else \
	  echo "All symbols satisfied."; \
	fi

# Cleaning

.PHONY: clean
clean:
	rm -rf $(BUILD)

.PHONY: fullclean
fullclean: clean

# Formatting

.PHONY: format
format:
	find main/ -iname '*.h' -o -iname '*.c' -o -iname '*.cpp' | xargs clang-format -i
