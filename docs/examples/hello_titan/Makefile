ROOT ?= $(abspath ../../..)
EE_CC ?= mips64r5900el-ps2-elf-gcc
PYTHON ?= python3
BUILD := build
SOURCE := $(BUILD)/source
TMOD := $(SOURCE)/hello_titan.tmod
ICON := $(SOURCE)/icon.rgb565
PKG := $(BUILD)/HELLOTITAN.pkg

CFLAGS := -D_EE -G0 -Os -Wall -Wextra -ffreestanding -fno-common -fno-pic -mno-abicalls
CFLAGS += -I$(ROOT)/kernel/include

.PHONY: all clean verify
all: $(PKG)

$(TMOD): app.c $(ROOT)/kernel/include/titan_app_sdk.h
	mkdir -p $(SOURCE)
	$(EE_CC) $(CFLAGS) -c app.c -o $@

$(ICON): make_example_assets.py
	$(PYTHON) make_example_assets.py

$(PKG): $(TMOD) $(ICON) $(ROOT)/kernel/tools/titan_pkg.py
	$(PYTHON) $(ROOT)/kernel/tools/titan_pkg.py build \
		--source $(SOURCE) \
		--output $(PKG) \
		--title-id HELLOTITAN \
		--title "Hello Titan" \
		--version 1.0.0 \
		--type titan-native \
		--entrypoint hello_titan.tmod \
		--author "Project Titan SDK Example" \
		--description "Exemplo minimo de um aplicativo Titan-native"

verify: $(PKG)
	$(PYTHON) $(ROOT)/kernel/tools/titan_pkg.py verify $(PKG)

clean:
	rm -rf $(BUILD)
