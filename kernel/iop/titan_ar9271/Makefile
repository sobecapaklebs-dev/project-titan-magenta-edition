IOP_BIN = titan_ar9271.irx
IOP_OBJS = ar9271_scanner.o ar9271_hif_rx.o ar9271_tx.o compiler_rt.o titan_crypto.o titan_wpa2.o titan_net.o titan_assoc_rates.o titan_assoc_wire.o titan_mlme.o titan_full_assoc.o titan_ath9k_rate.o titan_postvif168_runtime.o ar9271_radio.o ar9271_initvals.o titan_flightrec.o generated/htc_9271_fw.o imports.o
IOP_CFLAGS += -Wall -Wextra -Werror=misleading-indentation -Werror=implicit-function-declaration
BIN2C ?= $(PS2SDK)/bin/bin2c
TITAN_AR9271_FW ?=

.PHONY: all clean rebuild check-firmware
all: check-firmware $(IOP_BIN)

check-firmware:
	@test -n "$(TITAN_AR9271_FW)" || (echo "ERRO: informe TITAN_AR9271_FW=/caminho/htc_9271-1.4.0-titan-data-crypto-tt1424.fw" && exit 1)
	@test -f "$(TITAN_AR9271_FW)" || (echo "ERRO: firmware AR9271 nao encontrada: $(TITAN_AR9271_FW)" && exit 1)

generated/htc_9271_fw.c: check-firmware
	@mkdir -p generated
	@test -x "$(BIN2C)" || (echo "ERRO: bin2c nao encontrado em $(BIN2C)" && exit 1)
	$(BIN2C) "$(TITAN_AR9271_FW)" $@ htc_9271_fw

ar9271_scanner.o: generated/htc_9271_fw.c

clean:
	rm -f $(IOP_BIN) $(IOP_OBJS) titan_linux_coldstart_trace.o obj/titan_linux_coldstart_trace.o generated/htc_9271_fw.c

rebuild: clean all

include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.iopglobal
