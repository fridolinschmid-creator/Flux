/* flux_protocol.h -- gemeinsames Mini-Protokoll zwischen flux-shell
 * (UI) und fluxaid (System-KI-Daemon) ueber einen Unix-Domain-Socket.
 *
 * Bewusst kein JSON: auf einem Telefon-Kernel-Daemon zaehlt jede
 * Zuweisung. Zeilenbasiert, ein Request pro Verbindung.
 *
 *   Client -> Server:  "Q:<frage ohne newline>\n"
 *                        oder "X:<typ>\nTO:<empfaenger>\nSUBJECT:<betreff>\n
 *                              BODY:\n<text...>" (vom Nutzer bestaetigte
 *                              Aktion, siehe shell/src/action.h)
 *   Server -> Client:   "A:<antwort, darf eingebettete \n enthalten>\nEND\n"
 *                        oder einmalig "ERR:<meldung>\n" "END\n"
 *
 * Fuer "Q:" kann die Antwort selbst wieder ein strukturiertes Format
 * sein, wenn die KI eine Aktion vorschlaegt (siehe provider.c
 * System-Prompt und shell/src/action.h fuer den genauen Aufbau:
 * "ACTION:<typ>\nTO:...\nSUBJECT:...\nBODY:\n...").
 */
#ifndef FLUX_PROTOCOL_H
#define FLUX_PROTOCOL_H

#define FLUX_SOCK_PATH    "/run/flux/fluxai.sock"
#define FLUX_MAX_LINE     8192   /* max. Request-Groesse (Frage oder Aktion) */
#define FLUX_MAX_RESPONSE 8400   /* max. Antwort-Groesse inkl. Protokoll-Rahmen */

#endif
