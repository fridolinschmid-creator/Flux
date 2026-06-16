/* flux_protocol.h -- gemeinsames Mini-Protokoll zwischen flux-shell
 * (UI) und fluxaid (System-KI-Daemon) ueber einen Unix-Domain-Socket.
 *
 * Bewusst kein JSON: auf einem Telefon-Kernel-Daemon zaehlt jede
 * Zuweisung. Zeilenbasiert, ein Request pro Verbindung. Felder innerhalb
 * einer Zeile sind Tab-getrennt (Namen/Adressen duerfen Leerzeichen
 * enthalten, aber kein Tab und kein Newline).
 *
 *   Q:<frage>                       -> A:<antwort> | ERR:<meldung>
 *   C:<name>\t<telefonnummer>       -> A:<bestaetigung> | ERR:<meldung>
 *   F:<name>                        -> A:<name>\t<telefonnummer> | ERR:<meldung>
 *   M:<empfaenger>\t<betreff>\t<text> -> A:<bestaetigung> | ERR:<meldung>
 *
 *   Server -> Client immer einzeilig, danach "END\n":
 *     "A:<antwort>\nEND\n"  oder  "ERR:<meldung>\nEND\n"
 *
 * Das Mehrschritt-"wie soll der Kontakt heissen?"-Dialog lebt komplett
 * in flux-shell (eigene Zustandsmaschine) -- fluxaid bleibt zustandslos,
 * ein Request pro Verbindung, wie bisher. Erst der fertige C:-Request
 * landet hier.
 */
#ifndef FLUX_PROTOCOL_H
#define FLUX_PROTOCOL_H

#define FLUX_SOCK_PATH   "/run/flux/fluxai.sock"
#define FLUX_MAX_LINE    2048

#define FLUX_DATA_DIR       "/var/lib/flux"
#define FLUX_CONTACTS_PATH  FLUX_DATA_DIR "/contacts.tsv"

#endif
