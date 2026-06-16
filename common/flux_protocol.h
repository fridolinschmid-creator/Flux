/* flux_protocol.h -- gemeinsames Mini-Protokoll zwischen flux-shell
 * (UI) und fluxaid (System-KI-Daemon) ueber einen Unix-Domain-Socket.
 *
 * Bewusst kein JSON: auf einem Telefon-Kernel-Daemon zaehlt jede
 * Zuweisung. Zeilenbasiert, ein Request pro Verbindung.
 *
 *   Client -> Server:  "Q:<frage ohne newline>\n"
 *   Server -> Client:   beliebig viele "A:<zeile>\n", danach "END\n"
 *                        oder einmalig "ERR:<meldung>\n" "END\n"
 */
#ifndef FLUX_PROTOCOL_H
#define FLUX_PROTOCOL_H

#define FLUX_SOCK_PATH   "/run/flux/fluxai.sock"
#define FLUX_MAX_LINE    2048

#endif
