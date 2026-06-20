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

# Funktionstest des Arbeiterthreads (Schritt 1) -- linkt worker.c gegen
# Stubs, braucht weder curl noch Netz. Laeuft nativ auf dem Host.
test: test/worker_test.c src/worker.c
	$(CC) $(CFLAGS) -o /tmp/flux_worker_test src/worker.c test/worker_test.c -lpthread
	/tmp/flux_worker_test

clean:
	rm -f $(BIN) /tmp/flux_worker_test

.PHONY: clean test
