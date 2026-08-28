.DEFAULT_GOAL := all
.DELETE_ON_ERROR:

UPSTREAM := ../upstream/installer
BUILD := build
GEN := $(BUILD)/generated
LIB := $(BUILD)/libtitanfmcb_integrated.a
BIN2C ?= $(PS2SDK)/bin/bin2c

EE_INCS := -I. -I$(UPSTREAM) -I$(UPSTREAM)/irx/source/secrsif/src -I$(UPSTREAM)/irx/source/mctools/src \
           -I$(PS2SDK)/ports/include -I$(PS2SDK)/ee/include -I$(PS2SDK)/common/include
EE_CFLAGS += -D_EE -DNEWLIB_PORT_AWARE -DFIO_MT_RDWR=FILEXIO_MOUNTFLAG_NORMAL -DFIO_MT_RDONLY=FILEXIO_MOUNTFLAG_READONLY -Os -std=gnu99 -ffunction-sections -fdata-sections -DTITAN_FMCB_INTEGRATED=1

CORE_OBJS := \
 $(BUILD)/titan_fmcb_core.o \
 $(BUILD)/system.o \
 $(BUILD)/ReqSpaceCalc.o \
 $(BUILD)/libsecr.o \
 $(BUILD)/mctools_rpc.o \
 $(BUILD)/IOMANX_irx.o \
 $(BUILD)/FILEXIO_irx.o \
 $(BUILD)/SIO2MAN_irx.o \
 $(BUILD)/MCMAN_irx.o \
 $(BUILD)/MCSERV_irx.o \
 $(BUILD)/USBD_irx.o \
 $(BUILD)/USBHDFSD_irx.o \
 $(BUILD)/SECRSIF_irx.o \
 $(BUILD)/MCTOOLS_irx.o \
 $(BUILD)/IOPRP_img.o

.PHONY: all clean
all: $(LIB)

$(BUILD):
	@mkdir -p $@ $(GEN)

$(BUILD)/titan_fmcb_core.o: titan_fmcb_core.c titan_fmcb_core.h | $(BUILD)
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@

$(BUILD)/system.o: $(UPSTREAM)/system.c titan_fmcb_upstream_compat.h | $(BUILD)
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -include titan_fmcb_upstream_compat.h -c $< -o $@

$(BUILD)/ReqSpaceCalc.o: $(UPSTREAM)/ReqSpaceCalc.c titan_fmcb_upstream_compat.h | $(BUILD)
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -include titan_fmcb_upstream_compat.h -c $< -o $@

$(BUILD)/libsecr.o: $(UPSTREAM)/libsecr.c titan_fmcb_upstream_compat.h | $(BUILD)
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -include titan_fmcb_upstream_compat.h -c $< -o $@

$(BUILD)/mctools_rpc.o: $(UPSTREAM)/mctools_rpc.c | $(BUILD)
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@

# Preserve the exact upstream SECRMAN environment and RPC helpers required by
# PerformInstallation()/SignKELF(), but embed them in Titan instead of shipping
# a second user-facing installer ELF.
$(GEN)/IOPRP_img.c: $(UPSTREAM)/irx/compiled/IOPRP.img | $(BUILD)
	$(BIN2C) $< $@ IOPRP_img
$(GEN)/SECRSIF_irx.c: $(UPSTREAM)/irx/compiled/secrsif.irx | $(BUILD)
	$(BIN2C) $< $@ SECRSIF_irx
$(GEN)/MCTOOLS_irx.c: $(UPSTREAM)/irx/compiled/mctools.irx | $(BUILD)
	$(BIN2C) $< $@ MCTOOLS_irx
$(GEN)/IOMANX_irx.c: $(PS2SDK)/iop/irx/iomanX.irx | $(BUILD)
	$(BIN2C) $< $@ IOMANX_irx
$(GEN)/FILEXIO_irx.c: $(PS2SDK)/iop/irx/fileXio.irx | $(BUILD)
	$(BIN2C) $< $@ FILEXIO_irx
$(GEN)/SIO2MAN_irx.c: $(PS2SDK)/iop/irx/freesio2.irx | $(BUILD)
	$(BIN2C) $< $@ SIO2MAN_irx
$(GEN)/MCMAN_irx.c: $(PS2SDK)/iop/irx/mcman.irx | $(BUILD)
	$(BIN2C) $< $@ MCMAN_irx
$(GEN)/MCSERV_irx.c: $(PS2SDK)/iop/irx/mcserv.irx | $(BUILD)
	$(BIN2C) $< $@ MCSERV_irx
$(GEN)/USBD_irx.c: $(PS2SDK)/iop/irx/usbd.irx | $(BUILD)
	$(BIN2C) $< $@ USBD_irx
$(GEN)/USBHDFSD_irx.c: $(PS2SDK)/iop/irx/usbhdfsd.irx | $(BUILD)
	$(BIN2C) $< $@ USBHDFSD_irx

$(BUILD)/%_irx.o: $(GEN)/%_irx.c | $(BUILD)
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@
$(BUILD)/IOPRP_img.o: $(GEN)/IOPRP_img.c | $(BUILD)
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@

$(LIB): $(CORE_OBJS)
	$(EE_AR) rcs $@ $(CORE_OBJS)
	@test -s $@
	@echo "Titan FMCB integrated backend: $@"

clean:
	rm -rf $(BUILD)

include $(PS2SDK)/Defs.make
