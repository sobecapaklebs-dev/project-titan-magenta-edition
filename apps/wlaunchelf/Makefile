EE_CC ?= mips64r5900el-ps2-elf-gcc
PYTHON ?= python3
BUILD := build
PKG_SOURCE := $(BUILD)/source
TMOD := $(PKG_SOURCE)/wlaunchelf.tmod
PKG := $(BUILD)/WLAUNCHELF.pkg
INSTALLED_MANIFEST := $(BUILD)/installed/titan_app.ini

CFLAGS := -D_EE -G0 -Os -std=c99 -Wall -Wextra -Werror
CFLAGS += -march=r5900 -mabi=n32 -mhard-float -msingle-float
CFLAGS += -ffreestanding -fno-common -fno-pic -mno-abicalls
CFLAGS += -Iinclude

.PHONY: all clean verify host-test check-ee-compiler
all: $(PKG) $(INSTALLED_MANIFEST)

check-ee-compiler:
	@machine=`$(EE_CC) -dumpmachine 2>/dev/null` || { \
		echo "wLaunchELF Titan port requires mips64r5900el-ps2-elf-gcc" >&2; exit 1; }; \
	test "$$machine" = "mips64r5900el-ps2-elf" || { \
		echo "Refusing non-PS2 .tmod compiler: $$machine" >&2; exit 1; }

$(TMOD): check-ee-compiler src/wlaunchelf_titan.c include/titan_app_sdk.h
	mkdir -p $(PKG_SOURCE)
	$(EE_CC) $(CFLAGS) -c src/wlaunchelf_titan.c -o $@

$(PKG_SOURCE)/icon.rgb565: resources/icon.rgb565
	mkdir -p $(PKG_SOURCE)
	cp $< $@

$(PKG_SOURCE)/tile.rgb565: resources/tile.rgb565
	mkdir -p $(PKG_SOURCE)
	cp $< $@

$(PKG_SOURCE)/background.rgb565: resources/background.rgb565
	mkdir -p $(PKG_SOURCE)
	cp $< $@

$(PKG_SOURCE)/UPSTREAM_NOTICE.txt: resources/UPSTREAM_NOTICE.txt
	mkdir -p $(PKG_SOURCE)
	cp $< $@

$(INSTALLED_MANIFEST): Makefile
	mkdir -p $(dir $@)
	@printf '%s\n' \
		'format=2' \
		'title_id=TITNWLE01' \
		'title=wLaunchELF' \
		'version=0.1.3' \
		'type=titan-native' \
		'entrypoint=wlaunchelf.tmod' \
		'api=1' \
		'author=Project Titan / wLaunchELF upstream' \
		'description=Titan-native wLaunchELF file manager port; storage and ELF handoff are owned by Project Titan.' \
		'icon=icon.rgb565' \
		'tile=tile.rgb565' \
		'background=background.rgb565' > $@

$(PKG): $(TMOD) $(PKG_SOURCE)/icon.rgb565 $(PKG_SOURCE)/tile.rgb565 $(PKG_SOURCE)/background.rgb565 $(PKG_SOURCE)/UPSTREAM_NOTICE.txt
	$(PYTHON) tools/titan_pkg.py build \
		--source $(PKG_SOURCE) \
		--output $(PKG) \
		--title-id TITNWLE01 \
		--title "wLaunchELF" \
		--version 0.1.3 \
		--type titan-native \
		--entrypoint wlaunchelf.tmod \
		--author "Project Titan / wLaunchELF upstream" \
		--description "Titan-native wLaunchELF file manager port; storage and ELF handoff are owned by Project Titan."

verify: $(PKG)
	$(PYTHON) tools/check_r5900_tmod.py $(TMOD)
	$(PYTHON) ../../tools/check_titan_native_n32.py $(TMOD)
	$(PYTHON) tools/titan_pkg.py verify $(PKG)

host-test:
	mkdir -p $(BUILD)/host
	cc -std=c99 -O2 -Wall -Wextra -Werror -DWLE_HOST_TEST -Iinclude \
		src/wlaunchelf_titan.c tests/host_lifecycle_test.c -o $(BUILD)/host/wle_host_test
	$(BUILD)/host/wle_host_test

clean:
	rm -rf $(BUILD)
