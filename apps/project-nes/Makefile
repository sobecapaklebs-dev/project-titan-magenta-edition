EE_CC ?= mips64r5900el-ps2-elf-gcc
PYTHON ?= python3
BUILD := build
PKG_SOURCE := $(BUILD)/source
TMOD := $(PKG_SOURCE)/project_nes.tmod
PKG := $(BUILD)/PROJECTNES.pkg

CFLAGS := -D_EE -G0 -Os -std=c99 -Wall -Wextra -Werror
# PS2 EE uses the R5900 N32 ABI. Do not force the legacy MIPS O32 ABI:
# raw O32 objects are rejected by the PS2DEV EE/N32 linker emulation.
CFLAGS += -march=r5900 -mabi=n32 -mhard-float -msingle-float
CFLAGS += -ffreestanding -fno-common -fno-pic -mno-abicalls
CFLAGS += -Iinclude -Isrc

CORE_DIR := ../../third_party/fceumm/modern/src
STATIC_LINKING := 1
HAVE_HDPACK := 0
HAVE_NTSC := 0
WANT_32BPP := 0
include ../../third_party/fceumm/modern/Makefile.common
# FCEUmm is frozen third-party code. Keep Titan first-party -Werror strictness,
# but do not promote vendor warnings from newer GCC releases to hard failures.
FCEUMM_CFLAGS := $(filter-out -Werror,$(CFLAGS)) $(INCFLAGS) $(COREDEFINES) -DSTATIC_LINKING=1
FCEUMM_CFLAGS += -Dmalloc=pn_fceumm_malloc -Dcalloc=pn_fceumm_calloc
FCEUMM_CFLAGS += -Drealloc=pn_fceumm_realloc -Dfree=pn_fceumm_free
FCEUMM_CFLAGS += -include src/project_nes_fceumm_compat.h
FCEUMM_OBJS := $(patsubst %.c,$(BUILD)/fceumm/%.o,$(notdir $(SOURCES_C)))

.PHONY: all clean verify host-test check-ee-compiler
all: $(PKG)

check-ee-compiler:
	@machine=`$(EE_CC) -dumpmachine 2>/dev/null` || { \
		echo "Project Nes requires the PS2DEV EE compiler: mips64r5900el-ps2-elf-gcc" >&2; exit 1; }; \
	test "$$machine" = "mips64r5900el-ps2-elf" || { \
		echo "Refusing non-PS2 .tmod compiler: $$machine" >&2; \
		echo "Use PS2DEV mips64r5900el-ps2-elf-gcc for hardware packages." >&2; exit 1; }

$(TMOD): check-ee-compiler src/project_nes.c src/project_nes_fceumm_provider.c src/project_nes_fceumm_compat.c src/project_nes_core.h include/titan_app_sdk.h $(SOURCES_C)
	rm -rf $(BUILD)/target-obj
	mkdir -p $(BUILD)/target-obj $(PKG_SOURCE)/data/roms
	$(EE_CC) $(CFLAGS) -c src/project_nes.c -o $(BUILD)/target-obj/project_nes.o
	$(EE_CC) $(CFLAGS) -DPROJECT_NES_FCEUMM_LINKED -c src/project_nes_fceumm_provider.c -o $(BUILD)/target-obj/project_nes_fceumm_provider.o
	$(EE_CC) $(CFLAGS) $(INCFLAGS) -c src/project_nes_fceumm_compat.c -o $(BUILD)/target-obj/project_nes_fceumm_compat.o
	@set -e; n=0; for source in $(SOURCES_C); do \
		obj=$(BUILD)/target-obj/fceumm_$$n.o; \
		echo "FCEUmm $$source"; \
		$(EE_CC) $(FCEUMM_CFLAGS) -c $$source -o $$obj; n=$$((n+1)); \
	done
	# Use the EE compiler driver for the relocatable link so the linker emulation
	# and ABI selection exactly match the objects produced above.
	$(EE_CC) -march=r5900 -mabi=n32 -nostdlib -r -o $@ $(BUILD)/target-obj/*.o

$(PKG_SOURCE)/icon.rgb565: resources/icon.rgb565
	mkdir -p $(PKG_SOURCE)
	cp $< $@

$(PKG_SOURCE)/tile.rgb565: resources/tile.rgb565
	mkdir -p $(PKG_SOURCE)
	cp $< $@

$(PKG_SOURCE)/background.rgb565: resources/background.rgb565
	mkdir -p $(PKG_SOURCE)
	cp $< $@

$(PKG_SOURCE)/data/roms/ProjectNesTest.nes: tools/make_test_rom.py
	$(PYTHON) tools/make_test_rom.py
	mkdir -p $(PKG_SOURCE)/data/roms
	cp data/roms/ProjectNesTest.nes $@

$(PKG_SOURCE)/README.txt: resources/README.txt
	mkdir -p $(PKG_SOURCE)
	cp $< $@

$(PKG_SOURCE)/LICENSE.txt: resources/COMBINED_LICENSE.txt
	mkdir -p $(PKG_SOURCE)
	cp $< $@

$(PKG_SOURCE)/LICENSE-MIT.txt: LICENSE
	mkdir -p $(PKG_SOURCE)
	cp $< $@

$(PKG_SOURCE)/THIRD_PARTY_FCEUMM_LICENSE.txt: ../../third_party/fceumm/modern/Copying
	mkdir -p $(PKG_SOURCE)
	cp $< $@

$(PKG_SOURCE)/FCEUMM_SOURCE_PROVENANCE.txt: resources/FCEUMM_SOURCE_PROVENANCE.txt ../../external/fceumm/FCEUMM_IMPORTED_SHA256SUMS
	mkdir -p $(PKG_SOURCE)
	cp $< $@

$(PKG): $(TMOD) $(PKG_SOURCE)/icon.rgb565 $(PKG_SOURCE)/tile.rgb565 $(PKG_SOURCE)/background.rgb565 $(PKG_SOURCE)/data/roms/ProjectNesTest.nes $(PKG_SOURCE)/README.txt $(PKG_SOURCE)/LICENSE.txt $(PKG_SOURCE)/LICENSE-MIT.txt $(PKG_SOURCE)/THIRD_PARTY_FCEUMM_LICENSE.txt $(PKG_SOURCE)/FCEUMM_SOURCE_PROVENANCE.txt
	$(PYTHON) tools/titan_pkg.py build \
		--source $(PKG_SOURCE) \
		--output $(PKG) \
		--title-id PROJECTNES \
		--title "Project Nes" \
		--version 0.1.5 \
		--type titan-native \
		--entrypoint project_nes.tmod \
		--author "Project Titan" \
		--description "A powerfull NES emulator for Project Titan. Powered by: VlombisCore(TM) Technology"

verify: $(PKG)
	$(PYTHON) tools/check_r5900_tmod.py $(TMOD)
	$(PYTHON) ../../tools/check_titan_native_n32.py $(TMOD)
	$(PYTHON) tools/titan_pkg.py verify $(PKG)

host-test: data/roms/ProjectNesTest.nes $(PKG_SOURCE)/LICENSE.txt $(PKG_SOURCE)/LICENSE-MIT.txt $(PKG_SOURCE)/THIRD_PARTY_FCEUMM_LICENSE.txt $(PKG_SOURCE)/FCEUMM_SOURCE_PROVENANCE.txt
	mkdir -p $(BUILD)/host
	cc -std=c99 -O2 -Wall -Wextra -Werror -Iinclude tests/host_core_test.c -o $(BUILD)/host/host_core_test
	cc -std=c99 -O2 -Wall -Wextra -Werror -Iinclude tests/host_audio_test.c -o $(BUILD)/host/host_audio_test
	cc -std=c99 -O2 -Wall -Wextra -Werror -Iinclude tests/host_lifecycle_test.c -o $(BUILD)/host/host_lifecycle_test
	cc -std=c99 -O2 -Wall -Wextra -Werror -Iinclude tests/host_save_test.c -o $(BUILD)/host/host_save_test
	cc -std=c99 -O2 -Wall -Wextra -Werror -Iinclude tests/host_conformance_test.c -o $(BUILD)/host/host_conformance_test
	cc -std=c99 -O2 -Wall -Wextra -Werror -Iinclude tests/host_provider_selection_test.c -o $(BUILD)/host/host_provider_selection_test
	cc -std=c99 -O2 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -DPROJECT_NES_FCEUMM_LINKED -Isrc src/project_nes_fceumm_provider.c tests/host_fceumm_adapter_test.c -o $(BUILD)/host/host_fceumm_adapter_test
	$(BUILD)/host/host_core_test data/roms/ProjectNesTest.nes
	$(BUILD)/host/host_audio_test data/roms/ProjectNesTest.nes
	$(BUILD)/host/host_lifecycle_test data/roms/ProjectNesTest.nes
	$(BUILD)/host/host_save_test data/roms/ProjectNesTest.nes
	$(BUILD)/host/host_conformance_test
	$(BUILD)/host/host_provider_selection_test
	ASAN_OPTIONS=detect_leaks=1 $(BUILD)/host/host_fceumm_adapter_test
	$(PYTHON) tools/check_provider_package.py --provider fceumm --source-dir . --package-dir $(PKG_SOURCE)
	@echo "project-nes-host-tests: OK"

clean:
	rm -rf $(BUILD)
