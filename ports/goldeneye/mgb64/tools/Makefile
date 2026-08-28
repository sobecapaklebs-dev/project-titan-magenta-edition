CC := gcc
CXX := g++
MAKE := make
CFLAGS := -I . -Wall -Wextra -Wno-unused-parameter -pedantic -std=c99 -O2 -s
LDFLAGS := -lm
PROGRAMS := n64cksum

# if armips is not found on the system, build it in tools
ifeq (, $(shell which armips 2> /dev/null))
CXX_PROGRAMS += armips
endif

default: all

armips: armips.cpp
	$(CXX) $(CXXFLAGS) -fno-exceptions -fno-rtti -pipe $^ -o $@ -lpthread $(ARMIPS_FLAGS)

n64cksum_SOURCES := n64cksum.c utils.c
n64cksum_CFLAGS := -DN64CKSUM_STANDALONE

GZIP := gzipsrc/gzip

$(GZIP):
	@$(MAKE) -C gzipsrc

ifneq ($(IDO_RECOMP), NO)

IDO53 := ido5.3_recomp/cc
IDO_IRIX_SENTINELS := irix/root/usr/bin/cc irix/root/usr/lib/err.english.cc
IDO_RECOMP_SENTINELS := ido5.3_recomp/Makefile ido5.3_recomp/recomp.cpp

$(IDO53):
	@missing=0; \
	for path in $(IDO_IRIX_SENTINELS); do \
		if [ ! -e "$$path" ]; then \
			echo "MGB64: missing matching-target IDO input: tools/$$path" >&2; \
			missing=1; \
		fi; \
	done; \
	if [ "$$missing" -ne 0 ]; then \
		echo "MGB64: the public repo does not redistribute proprietary SGI IDO/IRIX compiler files." >&2; \
		echo "MGB64: the native CMake port does not need them; the N64 matching target does." >&2; \
		echo "MGB64: see docs/BUILDING.md and tools/ido5.3_recomp/README.md." >&2; \
		exit 1; \
	fi; \
	for path in $(IDO_RECOMP_SENTINELS); do \
		if [ ! -e "$$path" ]; then \
			echo "MGB64: missing local matching-target IDO recompilation tooling: tools/$$path" >&2; \
			missing=1; \
		fi; \
	done; \
	if [ "$$missing" -ne 0 ]; then \
		echo "MGB64: the public repo does not vendor IDO static-recompilation source." >&2; \
		echo "MGB64: populate tools/ido5.3_recomp locally from external tooling, or use IDO_RECOMP=NO with qemu-irix." >&2; \
		echo "MGB64: the native CMake port does not need this matching-target toolchain." >&2; \
		echo "MGB64: see docs/BUILDING.md and tools/ido5.3_recomp/README.md." >&2; \
		exit 1; \
	fi
	@$(MAKE) -C ido5.3_recomp
endif

all: $(GZIP) $(PROGRAMS) $(CXX_PROGRAMS) $(IDO53)

clean:
	$(RM) $(PROGRAMS) $(CXX_PROGRAMS)
	$(MAKE) -C gzipsrc clean

define COMPILE
$(1): $($1_SOURCES)
	$(CC) $(CFLAGS) $($1_CFLAGS) $$^ -o $$@ $(LDFLAGS) $($1_LDFLAGS)
endef

$(foreach p,$(PROGRAMS),$(eval $(call COMPILE,$(p))))

.PHONY: all clean default
