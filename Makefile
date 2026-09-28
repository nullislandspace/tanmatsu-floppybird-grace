PORT ?= /dev/ttyACM0
BADGELINKPORT ?= $(PORT)

SHELL := /usr/bin/env bash

# App installation settings
APP_SLUG_NAME ?= at.cavac.floppybird
APP_INSTALL_BASE_PATH ?= /int/apps/
APP_INSTALL_PATH = $(APP_INSTALL_BASE_PATH)$(APP_SLUG_NAME)

# ESP-IDF tools path (needed for the RISC-V cross-compiler)
IDF_PATH ?= $(shell cat .IDF_PATH 2>/dev/null || echo `pwd`/esp-idf)
IDF_TOOLS_PATH ?= $(shell cat .IDF_TOOLS_PATH 2>/dev/null || echo `pwd`/esp-idf-tools)
IDF_VERSION ?= v6.0.2
IDF_GITHUB_ASSETS ?= dl.espressif.com/github_assets

# The ESP-IDF environment script. Only mode_badgelink needs it (for pyserial
# out of the IDF python env -- the app build itself just needs the toolchain
# via IDF_TOOLS_PATH). Honours an IDF_SOURCE already exported by the shell.
IDF_SOURCE ?= $(shell cat .IDF_PATH 2>/dev/null && echo '$(IDF_PATH)/export.sh' || test -d `pwd`/esp-idf && echo '$(IDF_PATH)/export.sh' || echo '$(HOME)/.espressif/tools/activate_idf_$(IDF_VERSION).sh')

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

# SynthEngine3D, the 3D engine: not part of the template, added per app as a
# git submodule (CMakeLists.txt builds it when synthengine3D/ is there, and is
# untouched otherwise). Run this once in a new app that wants 3D; afterwards a
# fresh clone needs `git clone --recursive` or `git submodule update --init`.
ENGINE_URL ?= git@github.com:nullislandspace/synthengine3D.git
ENGINE_REF ?= main

.PHONY: engine
engine:
	if test -d synthengine3D; then \
	  echo "synthengine3D/ is already there -- 'git submodule update --remote synthengine3D' updates it"; exit 1; \
	fi
	git submodule add -b $(ENGINE_REF) $(ENGINE_URL) synthengine3D
	git submodule update --init --recursive synthengine3D
	@echo "=== SynthEngine3D added. Commit .gitmodules and synthengine3D, then #include \"synthengine3d.h\" ==="

# Test automation (main/testkit/devtest.h, tools/testrun.py). Opt-in:
# the app has to compile main/testkit/*.c and call devtest_start(). See
# README, "Automated device tests".
#
#   make testrun TEST="perf scene=title secs=20"      the app must be running
#   make cycle   TEST="shots scene=title ms=0,2500"   build, install, run, test
#   make testrefs    TEST="shots ..."                 cycle, then store the shot hashes as references
#   make testcompare TEST="shots ..."                 cycle, then compare against them
#   make recover                                      after a crash or a hang
#
# The app runs the test and returns to the launcher by itself, so the
# whole cycle is hands-free. Shot images stay on the SD card; the hashes
# come back over the console, which is what a regression check compares.
# TESTFLAGS=--fetch downloads the images too (slow).
TEST ?=
TESTFLAGS ?=

.PHONY: testrun cycle testrefs testcompare recover
testrun:
	source "$(IDF_SOURCE)" >/dev/null && \
	python3 -u tools/testrun.py --port "$(PORT)" --badgelink-conn "$(BADGELINK_CONN)" $(TESTFLAGS) -- $(TEST)

cycle: build install run
	$(MAKE) testrun

testrefs:
	$(MAKE) cycle TESTFLAGS="--capture-refs"

testcompare:
	$(MAKE) cycle TESTFLAGS="--compare"

recover:
	source "$(IDF_SOURCE)" >/dev/null && python3 tools/recover.py --port "$(PORT)"

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

# USB mode switching
#
# The device's USB peripheral is in one of two modes: BadgeLink (USB_DEVICE),
# which is what install / run need, or flash-and-monitor (USB_DEBUG). An
# `install` that fails to reach the device usually means the launcher left it
# in debug mode -- `make mode_badgelink` puts it back.
#
# Ask the firmware (in USB_DEBUG mode) to switch its USB into BadgeLink mode
# by sending the token "BADGELINK\n" on the USB-serial/JTAG peripheral. The
# launcher listens for it (see ../tanmatsu-launcher/main/usb_device.c), so
# this only works against firmware that implements the listener.
#
# PORT accepts either a local device path (e.g. /dev/ttyACM0) or an rfc2217://
# URL pointing at ../tanmatsu-badgefs/rfc2217proxy when the device is
# forwarded over the network.
.PHONY: mode_badgelink
mode_badgelink:
	source "$(IDF_SOURCE)" >/dev/null && \
	python3 -c "import serial, sys; s=serial.serial_for_url('$(PORT)', timeout=1); s.write(b'BADGELINK\n'); s.flush(); sys.stdout.write(s.read(128).decode(errors='replace')); s.close()"

# The other direction: ask the firmware (in BadgeLink mode) to switch its USB
# back to flash/monitor mode, through BadgeLink's own `mode` command. Uses the
# badgelink checkout that `make badgelink` creates, and the same BADGELINK_CONN
# as install / run, so it follows a networked device too.
BADGELINK_SH := badgelink/tools/badgelink.sh

.PHONY: mode_debug
mode_debug:
	if [ ! -x "$(BADGELINK_SH)" ]; then \
	  echo "$(BADGELINK_SH) not found -- run 'make badgelink' first"; \
	  exit 1; \
	fi; \
	echo "Using $(BADGELINK_SH)"; \
	"$(BADGELINK_SH)" $(BADGELINK_CONN) mode debug

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
	git clone --recursive --branch "$(IDF_VERSION)" https://github.com/espressif/esp-idf.git "$(IDF_PATH)" --depth=1 --shallow-submodules
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
