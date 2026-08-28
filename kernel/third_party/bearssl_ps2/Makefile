.DELETE_ON_ERROR:

SRC_ROOT ?=
OUT ?= libbearssl.a
EE_CC ?= mips64r5900el-ps2-elf-gcc
EE_AR ?= mips64r5900el-ps2-elf-ar

BEARSSL_SRCS := $(sort $(wildcard $(SRC_ROOT)/src/*.c) $(wildcard $(SRC_ROOT)/src/*/*.c))
BEARSSL_OBJS := $(patsubst $(SRC_ROOT)/src/%.c,obj/%.o,$(BEARSSL_SRCS))
CFLAGS := -G0 -Os -fno-builtin -Wall -I$(SRC_ROOT)/inc -I$(SRC_ROOT)/src

.PHONY: all clean
all: $(OUT)

$(OUT): $(BEARSSL_OBJS)
	@mkdir -p $(dir $@)
	$(EE_AR) rcs $@ $^

obj/%.o: $(SRC_ROOT)/src/%.c
	@mkdir -p $(dir $@)
	$(EE_CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf obj $(OUT)
