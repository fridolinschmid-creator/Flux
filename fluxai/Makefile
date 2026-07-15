# fluxaid -- System-KI-Daemon. Cross-Compile fuer das Zielgeraet:
#   make CROSS_COMPILE=aarch64-linux-gnu-
# Nativer Test-Build (auf dem Host):
#   make
# Sicherheits-Haertung: Stack-Protector, FORTIFY (Bounds-Checks fuer
# str/mem-Funktionen), PIE (ASLR) und Full-RELRO (GOT read-only).
CROSS_COMPILE ?=
CC      := $(CROSS_COMPILE)gcc
HARDEN_CFLAGS  := -fstack-protector-strong -fPIE -Wformat -Wformat-security
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
HARDEN_LDFLAGS :=
else
HARDEN_LDFLAGS := -pie -Wl,-z,relro,-z,now
endif
CFLAGS  ?= -O2 -Wall -Wextra -std=gnu11 -D_GNU_SOURCE $(HARDEN_CFLAGS)
LDFLAGS ?= -lcurl -lpthread $(HARDEN_LDFLAGS)
# Homebrew und andere Unix-Hosts installieren OpenSSL oft ausserhalb der
# Standard-Include-Pfade. Auf Linux bleibt die Variable leer und der
# Buildroot-Cross-Compiler setzt seine eigenen Pfade.
OPENSSL_CFLAGS := $(shell pkg-config --cflags openssl 2>/dev/null)
OPENSSL_LDFLAGS := $(shell pkg-config --libs openssl 2>/dev/null)
CFLAGS += $(OPENSSL_CFLAGS)
LDFLAGS += $(if $(OPENSSL_LDFLAGS),$(OPENSSL_LDFLAGS),-lcrypto)

SRC := $(wildcard src/*.c) $(wildcard ../common/*.c)
BIN := fluxaid

$(BIN): $(SRC)
	$(CC) $(CFLAGS) -o $@ $(SRC) $(LDFLAGS)

clean:
	rm -f $(BIN)

.PHONY: clean
