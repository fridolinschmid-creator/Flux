# Flux -- top-level Build- und Test-Orchestrierung.
#
#   make            baut Daemon + Shell (nativer Host-Build)
#   make test       baut den Daemon + Testbinaries und fuehrt die Tests aus
#   make clean      raeumt alle Artefakte auf
#
# Cross-Compile fuer das Zielgeraet weiterhin pro Komponente, z.B.:
#   make -C shell  CROSS_COMPILE=aarch64-linux-gnu-
.PHONY: all daemon shell test clean

CC     ?= cc
CFLAGS ?= -O2 -Wall -Wextra -std=gnu11 -D_GNU_SOURCE

all: daemon shell

daemon:
	$(MAKE) -C fluxai

shell:
	$(MAKE) -C shell

# Unit-Tests (lokale Intents + Config/Crypto) + E2E-Protokolltest.
test: daemon tests/test_actions tests/test_config tests/protocol_client
	@echo "== unit: actions =="
	./tests/test_actions
	@echo "== unit: config (encryption at rest) =="
	./tests/test_config
	@echo "== e2e: protocol =="
	./tests/run_protocol_test.sh

tests/test_actions: tests/test_actions.c fluxai/src/actions.c
	$(CC) $(CFLAGS) -o $@ $^

tests/test_config: tests/test_config.c common/flux_config.c common/flux_secret.c
	$(CC) $(CFLAGS) -o $@ $^ -lcrypto

tests/protocol_client: tests/protocol_client.c
	$(CC) $(CFLAGS) -o $@ $^

clean:
	$(MAKE) -C fluxai clean
	$(MAKE) -C shell clean
	rm -f tests/test_actions tests/test_config tests/protocol_client
