# fluxaid -- System-KI-Daemon. Cross-Compile fuer das Zielgeraet:
#   make CROSS_COMPILE=aarch64-linux-gnu-
# Nativer Test-Build (auf dem Host):
#   make
# Sicherheits-Haertung: Stack-Protector, FORTIFY (Bounds-Checks fuer
# str/mem-Funktionen), PIE (ASLR) und Full-RELRO (GOT read-only).
CROSS_COMPILE ?=
CC      := $(CROSS_COMPILE)gcc
HARDEN_CFLAGS  := -fstack-protector-strong -fPIE -Wformat -Wformat-security
HARDEN_LDFLAGS := -pie -Wl,-z,relro,-z,now
CFLAGS  ?= -O2 -Wall -Wextra -std=gnu11 -D_GNU_SOURCE $(HARDEN_CFLAGS)
LDFLAGS ?= -lcurl -lpthread -lcrypto $(HARDEN_LDFLAGS)

SRC := $(wildcard src/*.c) $(wildcard ../common/*.c)
BIN := fluxaid

$(BIN): $(SRC)
	$(CC) $(CFLAGS) -o $@ $(SRC) $(LDFLAGS)

clean:
	rm -f $(BIN)

.PHONY: clean
