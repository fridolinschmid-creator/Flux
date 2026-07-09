# flux-shell -- Framebuffer-UI. Cross-Compile fuer das Zielgeraet:
#   make CROSS_COMPILE=aarch64-linux-gnu-
# Nativer Test-Build (auf dem Host, ohne echten /dev/fb0):
#   make
# Sicherheits-Haertung: Stack-Protector, FORTIFY (Bounds-Checks fuer
# str/mem-Funktionen), PIE (ASLR) und Full-RELRO (GOT read-only).
#
# Gemischter C/C++-Build: der Text-Browser (Wetter, Listen, UI) bleibt
# reines C11 wie der Rest des Projekts. Fuer echtes visuelles HTML/CSS-
# Rendering im Browser-Screen (browser_render.cpp) ist litehtml (BSD) +
# gumbo-parser (Apache 2.0) vendored unter src/litehtml/ -- die einzige
# C++-Komponente im Projekt, hinter einer reinen C-Schnittstelle
# versteckt (siehe browser_render.h). Deshalb zwei Compiler (CC fuer .c,
# CXX fuer .cpp) und ein objektbasierter statt Ein-Kommando-Build.
CROSS_COMPILE ?=
CC      := $(CROSS_COMPILE)gcc
CXX     := $(CROSS_COMPILE)g++
HARDEN_CFLAGS  := -fstack-protector-strong -fPIE -Wformat -Wformat-security
HARDEN_LDFLAGS := -pie -Wl,-z,relro,-z,now
CFLAGS   ?= -O2 -Wall -Wextra -std=gnu11 -D_GNU_SOURCE $(HARDEN_CFLAGS)
CXXFLAGS ?= -O2 -Wall -Wextra -std=c++17 $(HARDEN_CFLAGS)
LDFLAGS  ?= -lm -lcrypto -lpthread -lcurl $(HARDEN_LDFLAGS)

LH_DIR      := src/litehtml
LH_INCLUDES := -I$(LH_DIR)/src -I$(LH_DIR)/include -I$(LH_DIR)/include/litehtml \
               -I$(LH_DIR)/src/gumbo/include -I$(LH_DIR)/src/gumbo/include/gumbo

C_SRC      := $(wildcard src/*.c) $(wildcard ../common/*.c)
CXX_SRC    := src/browser_render.cpp
GUMBO_SRC  := $(wildcard $(LH_DIR)/src/gumbo/*.c)
LITEHTML_SRC := $(wildcard $(LH_DIR)/src/*.cpp)

OBJDIR := build
C_OBJ         := $(patsubst %.c,$(OBJDIR)/%.o,$(C_SRC))
CXX_OBJ       := $(patsubst %.cpp,$(OBJDIR)/%.o,$(CXX_SRC))
GUMBO_OBJ     := $(patsubst %.c,$(OBJDIR)/%.o,$(GUMBO_SRC))
LITEHTML_OBJ  := $(patsubst %.cpp,$(OBJDIR)/%.o,$(LITEHTML_SRC))

BIN := flux-shell

$(BIN): $(C_OBJ) $(CXX_OBJ) $(GUMBO_OBJ) $(LITEHTML_OBJ)
	$(CXX) -o $@ $^ $(LDFLAGS)

$(OBJDIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -Isrc -c $< -o $@

$(OBJDIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -Isrc $(LH_INCLUDES) -c $< -o $@

$(OBJDIR)/$(LH_DIR)/src/gumbo/%.o: $(LH_DIR)/src/gumbo/%.c
	@mkdir -p $(dir $@)
	$(CC) -O2 -std=c99 -Wall -fPIE $(LH_INCLUDES) -c $< -o $@

clean:
	rm -rf $(OBJDIR) $(BIN)

.PHONY: clean
