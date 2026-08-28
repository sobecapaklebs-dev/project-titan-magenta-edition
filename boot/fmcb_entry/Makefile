EE_BIN = ../../titan-fmcb-entry.elf
EE_OBJS = main.o
EE_LIBS = -lkernel
EE_CFLAGS += -Os

.PHONY: all clean

all: $(EE_BIN)

clean:
	rm -f $(EE_OBJS) $(EE_BIN)

include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.eeglobal
