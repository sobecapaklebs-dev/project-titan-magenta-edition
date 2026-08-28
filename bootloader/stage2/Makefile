.DEFAULT_GOAL := all

EE_BIN = stage2-debug.elf
STAGE2_RELEASE_BIN = stage2.elf

BIN2C ?= $(PS2SDK)/bin/bin2c

STAGE2_LOGO_RAW = assets/titan_stage2_logo.psm16
STAGE2_LOGO_HEADER = generated/titan_stage2_logo_data.h

EE_OBJS = \
src/main.o \
src/stage2_video.o

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

src/stage2_video.o: $(STAGE2_LOGO_HEADER)

clean:
	rm -f $(EE_OBJS) $(EE_BIN) $(STAGE2_RELEASE_BIN)

clean-generated:
	rm -rf generated

include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.eeglobal
