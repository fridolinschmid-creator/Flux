# fluxaid -- System-KI-Daemon. Cross-Compile fuer das Zielgeraet:
#   make CROSS_COMPILE=aarch64-linux-gnu-
# Nativer Test-Build (auf dem Host):
#   make
CROSS_COMPILE ?=
CC      := $(CROSS_COMPILE)gcc
CFLAGS  ?= -O2 -Wall -Wextra -Wformat-security -std=gnu11 -D_GNU_SOURCE
LDFLAGS ?= -lcurl

SRC := $(wildcard src/*.c) $(wildcard ../common/*.c)
BIN := fluxaid

$(BIN): $(SRC)
	$(CC) $(CFLAGS) -o $@ $(SRC) $(LDFLAGS)

clean:
	rm -f $(BIN)

.PHONY: clean
