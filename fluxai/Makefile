# fluxaid -- System-KI-Daemon. Cross-Compile fuer das Zielgeraet:
#   make CROSS_COMPILE=aarch64-linux-gnu-
# Nativer Test-Build (auf dem Host):
#   make
CROSS_COMPILE ?=
CC      := $(CROSS_COMPILE)gcc
CFLAGS  ?= -O2 -Wall -Wextra -std=gnu11 -D_GNU_SOURCE
LDFLAGS ?= -lcurl -lpthread

SRC := $(wildcard src/*.c) $(wildcard ../common/*.c)
BIN := fluxaid

$(BIN): $(SRC)
	$(CC) $(CFLAGS) -o $@ $(SRC) $(LDFLAGS)

# Funktionstests (Schritt 1+2) -- linken gegen Stubs, brauchen weder curl
# noch Netz. Laufen nativ auf dem Host.
#   worker_test: Arbeiterthread, Q:/X:-Routing, P:-Frames, Nebenlaeufigkeit
#   sse_test:    SSE-Parser + TOOL:/ACTION:-Praefix-Schutz
test: test/worker_test.c src/worker.c test/sse_test.c src/sse.c
	$(CC) $(CFLAGS) -o /tmp/flux_worker_test src/worker.c test/worker_test.c -lpthread
	$(CC) $(CFLAGS) -o /tmp/flux_sse_test    src/sse.c    test/sse_test.c
	/tmp/flux_worker_test
	/tmp/flux_sse_test

clean:
	rm -f $(BIN) /tmp/flux_worker_test /tmp/flux_sse_test

.PHONY: clean test
