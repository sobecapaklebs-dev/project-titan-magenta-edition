.DEFAULT_GOAL := all

EE_BIN = stage2-debug.elf
STAGE2_RELEASE_BIN = stage2.elf

BIN2C ?= $(PS2SDK)/bin/bin2c

STAGE2_LOGO_RAW = assets/titan_stage2_logo.psm16
STAGE2_LOGO_HEADER = generated/titan_stage2_logo_data.h

# TITAN_RC4_COLD_IOP_HANDOFF_R1
STAGE2_IOMANX_IRX = $(PS2SDK)/iop/irx/iomanX.irx
STAGE2_FILEXIO_IRX = $(PS2SDK)/iop/irx/fileXio.irx
STAGE2_USBD_IRX = $(PS2SDK)/iop/irx/usbd.irx
STAGE2_BDM_IRX = $(PS2SDK)/iop/irx/bdm.irx
STAGE2_BDMFS_FATFS_IRX = $(PS2SDK)/iop/irx/bdmfs_fatfs.irx
STAGE2_USBMASS_BD_IRX = $(PS2SDK)/iop/irx/usbmass_bd.irx

STAGE2_IOMANX_HEADER = generated/titan_stage2_iomanx_irx.h
STAGE2_FILEXIO_HEADER = generated/titan_stage2_filexio_irx.h
STAGE2_USBD_HEADER = generated/titan_stage2_usbd_irx.h
STAGE2_BDM_HEADER = generated/titan_stage2_bdm_irx.h
STAGE2_BDMFS_FATFS_HEADER = generated/titan_stage2_bdmfs_fatfs_irx.h
STAGE2_USBMASS_BD_HEADER = generated/titan_stage2_usbmass_bd_irx.h

EE_OBJS = \
src/main.o \
src/stage2_video.o \
src/titan_ps2rd_engine.o

EE_INCS += \
-I../include \
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
-lfileXio \
-lpatches \
-ldraw \
-lgraph \
-lpacket2 \
-ldma

EE_NEWLIB_NANO ?= 1
EE_COMPACT_EXECUTABLE ?= 1
EE_LINKFILE = linkfile

.PHONY: all clean clean-generated

all: $(STAGE2_RELEASE_BIN)

$(STAGE2_RELEASE_BIN): $(EE_BIN)
	cp -f $(EE_BIN) $(STAGE2_RELEASE_BIN)
	$(EE_STRIP) --strip-all $(STAGE2_RELEASE_BIN)

$(STAGE2_LOGO_HEADER): $(STAGE2_LOGO_RAW)
	@mkdir -p $(dir $@)
	@test -x "$(BIN2C)" || \
		(echo "ERRO: bin2c nao encontrado em $(BIN2C)" && exit 1)
	$(BIN2C) $< $@ titan_stage2_logo

$(STAGE2_IOMANX_HEADER): $(STAGE2_IOMANX_IRX)
	@mkdir -p $(dir $@)
	@test -x "$(BIN2C)" || (echo "ERRO: bin2c nao encontrado em $(BIN2C)" && exit 1)
	@test -f "$<" || (echo "ERRO: iomanX.irx nao encontrado em $<" && exit 1)
	$(BIN2C) $< $@ titan_stage2_iomanx_irx

$(STAGE2_FILEXIO_HEADER): $(STAGE2_FILEXIO_IRX)
	@mkdir -p $(dir $@)
	@test -x "$(BIN2C)" || (echo "ERRO: bin2c nao encontrado em $(BIN2C)" && exit 1)
	@test -f "$<" || (echo "ERRO: fileXio.irx nao encontrado em $<" && exit 1)
	$(BIN2C) $< $@ titan_stage2_filexio_irx

$(STAGE2_USBD_HEADER): $(STAGE2_USBD_IRX)
	@mkdir -p $(dir $@)
	@test -x "$(BIN2C)" || (echo "ERRO: bin2c nao encontrado em $(BIN2C)" && exit 1)
	@test -f "$<" || (echo "ERRO: usbd.irx nao encontrado em $<" && exit 1)
	$(BIN2C) $< $@ titan_stage2_usbd_irx

$(STAGE2_BDM_HEADER): $(STAGE2_BDM_IRX)
	@mkdir -p $(dir $@)
	@test -x "$(BIN2C)" || (echo "ERRO: bin2c nao encontrado em $(BIN2C)" && exit 1)
	@test -f "$<" || (echo "ERRO: bdm.irx nao encontrado em $<" && exit 1)
	$(BIN2C) $< $@ titan_stage2_bdm_irx

$(STAGE2_BDMFS_FATFS_HEADER): $(STAGE2_BDMFS_FATFS_IRX)
	@mkdir -p $(dir $@)
	@test -x "$(BIN2C)" || (echo "ERRO: bin2c nao encontrado em $(BIN2C)" && exit 1)
	@test -f "$<" || (echo "ERRO: bdmfs_fatfs.irx nao encontrado em $<" && exit 1)
	$(BIN2C) $< $@ titan_stage2_bdmfs_fatfs_irx

$(STAGE2_USBMASS_BD_HEADER): $(STAGE2_USBMASS_BD_IRX)
	@mkdir -p $(dir $@)
	@test -x "$(BIN2C)" || (echo "ERRO: bin2c nao encontrado em $(BIN2C)" && exit 1)
	@test -f "$<" || (echo "ERRO: usbmass_bd.irx nao encontrado em $<" && exit 1)
	$(BIN2C) $< $@ titan_stage2_usbmass_bd_irx

src/main.o: \
$(STAGE2_IOMANX_HEADER) \
$(STAGE2_FILEXIO_HEADER) \
$(STAGE2_USBD_HEADER) \
$(STAGE2_BDM_HEADER) \
$(STAGE2_BDMFS_FATFS_HEADER) \
$(STAGE2_USBMASS_BD_HEADER)

src/stage2_video.o: $(STAGE2_LOGO_HEADER)

clean:
	rm -f $(EE_OBJS) $(EE_BIN) $(STAGE2_RELEASE_BIN)

clean-generated:
	rm -rf generated

include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.eeglobal
