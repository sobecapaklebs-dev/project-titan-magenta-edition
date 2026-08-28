EE_CC ?= mips64r5900el-ps2-elf-gcc
EE_LD ?= mips64r5900el-ps2-elf-ld
PYTHON ?= python3
PROJECT_TITAN_ROOT ?= ../../..
BUILD := build
OBJECT := $(BUILD)/minimal_game.o
MODULE := $(BUILD)/minimal_game.tmod

CPPFLAGS := -I$(PROJECT_TITAN_ROOT)/kernel/include
CFLAGS := -D_EE -G0 -Os -std=gnu11 -march=r5900 -mabi=32 -mhard-float \
    -msingle-float -ffreestanding -fno-common -fno-pic -mno-abicalls \
    -fno-stack-protector -Wall -Wextra

.PHONY: all verify clean check-ee-compiler
all: verify

check-ee-compiler:
	@machine=`$(EE_CC) -dumpmachine 2>/dev/null` || { \
		echo "Titan Game SDK requires the PS2DEV R5900 compiler" >&2; exit 1; }; \
	test "$$machine" = "mips64r5900el-ps2-elf" || { \
		echo "Refusing non-R5900 compiler: $$machine" >&2; exit 1; }

$(OBJECT): minimal_game.c | check-ee-compiler
	@mkdir -p $(BUILD)
	$(EE_CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(MODULE): $(OBJECT)
	$(EE_LD) -r --no-relax -o $@ $^ `$(EE_CC) -print-libgcc-file-name`

verify: $(MODULE)
	$(PYTHON) $(PROJECT_TITAN_ROOT)/apps/project-nes/tools/check_r5900_tmod.py $(MODULE)

clean:
	rm -rf $(BUILD)
