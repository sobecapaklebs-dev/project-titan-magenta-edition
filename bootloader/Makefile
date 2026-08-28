.DEFAULT_GOAL := all

EE_BIN = titan-debug.elf
TITAN_RELEASE_BIN = BOOT.ELF

EE_OBJS = \
src/main.o \
src/boot_path.o \
src/elf_validate.o \
src/boot_video.o

EE_INCS += \
-Iinclude \
-Isrc \
-I.

EE_CFLAGS += \
-std=gnu99 \
-Os \
-Wall \
-Wextra \
-Werror=implicit-function-declaration

EE_LIBS += \
-ldebug \
-lmc \
-lfileXio \
-lpatches \
-ldraw \
-lgraph \
-lpacket2 \
-ldma

BIN2C ?= $(PS2SDK)/bin/bin2c
PYTHON ?= python3

BOOT_SOURCE = assets/boot/titan_boot_source.png
BOOT_PREVIEW = assets/boot/titan_boot_640x448.png
BOOT_RAW = assets/boot/titan_boot_640x448.psm16
BOOT_HEADER = generated/titan_boot_screen_data.h

STAGE2_ELF = stage2/stage2.elf
STAGE2_HEADER = generated/titan_stage2_elf_data.h

TITAN_USBD_IRX = $(PS2SDK)/iop/irx/usbd.irx
TITAN_BDM_IRX = $(PS2SDK)/iop/irx/bdm.irx
TITAN_BDMFS_FATFS_IRX = $(PS2SDK)/iop/irx/bdmfs_fatfs.irx
TITAN_USBMASS_BD_IRX = $(PS2SDK)/iop/irx/usbmass_bd.irx
TITAN_IOMANX_IRX = $(PS2SDK)/iop/irx/iomanX.irx
TITAN_FILEXIO_IRX = $(PS2SDK)/iop/irx/fileXio.irx
TITAN_MCMAN_IRX = $(PS2SDK)/iop/irx/mcman-old.irx
TITAN_MCSERV_IRX = $(PS2SDK)/iop/irx/mcserv-old.irx

TITAN_USBD_HEADER = generated/titan_usbd_irx.h
TITAN_BDM_HEADER = generated/titan_bdm_irx.h
TITAN_BDMFS_FATFS_HEADER = generated/titan_bdmfs_fatfs_irx.h
TITAN_USBMASS_BD_HEADER = generated/titan_usbmass_bd_irx.h
TITAN_IOMANX_HEADER = generated/titan_iomanx_irx.h
TITAN_FILEXIO_HEADER = generated/titan_filexio_irx.h
TITAN_MCMAN_HEADER = generated/titan_mcman_irx.h
TITAN_MCSERV_HEADER = generated/titan_mcserv_irx.h

.PHONY: \
all \
clean \
clean-generated \
deploy \
prepare-boot-image \
test-host

all: $(TITAN_RELEASE_BIN)

$(TITAN_RELEASE_BIN): $(EE_BIN)
	cp -f $(EE_BIN) $(TITAN_RELEASE_BIN)
	$(EE_STRIP) --strip-all $(TITAN_RELEASE_BIN)

deploy: $(TITAN_RELEASE_BIN)
	cp -f $(TITAN_RELEASE_BIN) ../BOOT.ELF
	@echo "Bootloader implantado em ../BOOT.ELF"

prepare-boot-image:
	$(PYTHON) tools/prepare_boot_image.py \
		--source $(BOOT_SOURCE) \
		--preview $(BOOT_PREVIEW) \
		--raw $(BOOT_RAW)

$(STAGE2_ELF):
	$(MAKE) -C stage2

$(BOOT_HEADER): $(BOOT_RAW)
	@mkdir -p $(dir $@)
	@test -x "$(BIN2C)" || \
		(echo "ERRO: bin2c nao encontrado em $(BIN2C)" && exit 1)
	$(BIN2C) $< $@ titan_boot_screen


$(TITAN_USBD_HEADER): $(TITAN_USBD_IRX)
	@mkdir -p $(dir $@)
	$(BIN2C) $< $@ titan_usbd_irx

$(TITAN_BDM_HEADER): $(TITAN_BDM_IRX)
	@mkdir -p $(dir $@)
	$(BIN2C) $< $@ titan_bdm_irx

$(TITAN_BDMFS_FATFS_HEADER): $(TITAN_BDMFS_FATFS_IRX)
	@mkdir -p $(dir $@)
	$(BIN2C) $< $@ titan_bdmfs_fatfs_irx

$(TITAN_USBMASS_BD_HEADER): $(TITAN_USBMASS_BD_IRX)
	@mkdir -p $(dir $@)
	$(BIN2C) $< $@ titan_usbmass_bd_irx

$(TITAN_IOMANX_HEADER): $(TITAN_IOMANX_IRX)
	@mkdir -p $(dir $@)
	$(BIN2C) $< $@ titan_iomanx_irx

$(TITAN_FILEXIO_HEADER): $(TITAN_FILEXIO_IRX)
	@mkdir -p $(dir $@)
	$(BIN2C) $< $@ titan_filexio_irx

$(TITAN_MCMAN_HEADER): $(TITAN_MCMAN_IRX)
	@mkdir -p $(dir $@)
	$(BIN2C) $< $@ titan_mcman_irx

$(TITAN_MCSERV_HEADER): $(TITAN_MCSERV_IRX)
	@mkdir -p $(dir $@)
	$(BIN2C) $< $@ titan_mcserv_irx

$(STAGE2_HEADER): $(STAGE2_ELF)
	@mkdir -p $(dir $@)
	@test -x "$(BIN2C)" || \
		(echo "ERRO: bin2c nao encontrado em $(BIN2C)" && exit 1)
	$(BIN2C) $< $@ titan_stage2_elf

src/main.o: $(STAGE2_HEADER) $(TITAN_IOMANX_HEADER) $(TITAN_FILEXIO_HEADER) $(TITAN_MCMAN_HEADER) $(TITAN_MCSERV_HEADER) $(TITAN_USBD_HEADER) $(TITAN_BDM_HEADER) $(TITAN_BDMFS_FATFS_HEADER) $(TITAN_USBMASS_BD_HEADER)
src/boot_video.o: $(BOOT_HEADER)

test-host:
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror \
		-Isrc tests/test_boot_path.c src/boot_path.c \
		-o tests/test_boot_path
	./tests/test_boot_path
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror \
		-Isrc tests/test_elf_validate.c src/elf_validate.c \
		-o tests/test_elf_validate
	./tests/test_elf_validate

clean:
	rm -f $(EE_OBJS) $(EE_BIN) $(TITAN_RELEASE_BIN)
	rm -f tests/test_boot_path tests/test_elf_validate
	$(MAKE) -C stage2 clean

clean-generated:
	rm -rf generated
	$(MAKE) -C stage2 clean-generated

HOST_CC ?= cc

include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.eeglobal
