# fluxweb -- WPE-Browser-Companion-Prozess. Siehe README.md.
#
# Standard-Build (Host, zum Testen des Socket-/Protokoll-Teils):
#   make
# Mit echtem WPE WebKit (braucht pkg-config wpe-webkit-2.0/wpe-2.0 --
# NICHT verifiziert, siehe README.md/webview.c):
#   make WPE=1 CROSS_COMPILE=aarch64-linux-gnu-
CROSS_COMPILE ?=
CC := $(CROSS_COMPILE)gcc
# PKG_CONFIG ueberschreibbar (z.B. auf den Buildroot-host-pkg-config mit
# dem richtigen Sysroot zeigen -- siehe build/build-pi5.sh). Default
# faellt auf CROSS_COMPILE-praefigiertes pkg-config zurueck.
PKG_CONFIG ?= $(CROSS_COMPILE)pkg-config
HARDEN_CFLAGS  := -fstack-protector-strong -fPIE -Wformat -Wformat-security
HARDEN_LDFLAGS := -pie -Wl,-z,relro,-z,now
CFLAGS  ?= -O2 -Wall -Wextra -std=gnu11 -D_GNU_SOURCE $(HARDEN_CFLAGS)

GLIB_CFLAGS := $(shell $(PKG_CONFIG) --cflags glib-2.0 gio-unix-2.0)
GLIB_LIBS   := $(shell $(PKG_CONFIG) --libs glib-2.0 gio-unix-2.0)

SRC := src/main.c src/webview.c
BIN := fluxweb

ifdef WPE
CFLAGS  += -DFLUXWEB_USE_WPE $(shell $(PKG_CONFIG) --cflags wpe-webkit-2.0 wpe-2.0 egl glesv2)
LDFLAGS += $(shell $(PKG_CONFIG) --libs wpe-webkit-2.0 wpe-2.0 egl glesv2)
endif

$(BIN): $(SRC)
	$(CC) $(CFLAGS) $(GLIB_CFLAGS) -o $@ $(SRC) $(GLIB_LIBS) $(LDFLAGS) $(HARDEN_LDFLAGS)

clean:
	rm -f $(BIN)

.PHONY: clean
