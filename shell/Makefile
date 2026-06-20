# flux-shell -- Framebuffer-UI. Cross-Compile fuer das Zielgeraet:
#   make CROSS_COMPILE=aarch64-linux-gnu-
# Nativer Test-Build (auf dem Host, ohne echten /dev/fb0):
#   make
CROSS_COMPILE ?=
CC      := $(CROSS_COMPILE)gcc
CFLAGS  ?= -O2 -Wall -Wextra -std=gnu11 -D_GNU_SOURCE
LDFLAGS ?= -lm

SRC := $(wildcard src/*.c) $(wildcard ../common/*.c)
BIN := flux-shell

$(BIN): $(SRC)
	$(CC) $(CFLAGS) -o $@ $(SRC) $(LDFLAGS)

# Test der Client-IPC (Schritt 2): Stream-Konsum + Rueckwaertskompatibilitaet.
# Linkt nur src/ipc.c, braucht weder Framebuffer noch curl.
test: test/ipc_test.c src/ipc.c
	$(CC) $(CFLAGS) -o /tmp/flux_ipc_test src/ipc.c test/ipc_test.c -lpthread
	/tmp/flux_ipc_test

clean:
	rm -f $(BIN) /tmp/flux_ipc_test

.PHONY: clean test
