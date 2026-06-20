/* worker.h -- Hintergrund-Arbeiterthread fuer fluxaid.
 *
 * Schritt 1 der Hybrid-KI-Architektur (siehe docs/HYBRID_AI_ARCHITECTURE.md):
 * Der blockierende KI-Aufruf (provider.c, bis 20s curl) wird aus der
 * accept()-Schleife herausgeloest. Die Hauptschleife nimmt nur noch
 * Verbindungen an und stellt Jobs in eine Warteschlange; ein einzelner
 * Arbeiterthread bearbeitet sie der Reihe nach.
 *
 * Bewusst EIN Arbeiter, kein Pool und kein fork():
 *   - Der Gespraechskontext (ctx_history in provider.c) ist prozessglobaler
 *     Zustand. fork() wuerde ihn pro Verbindung verlieren, ein Threadpool
 *     wuerde darauf rennen. Ein einziger Arbeiter haelt allen Provider-
 *     Zugriff auf einem Thread -- keine Sperren, identische Antwortsemantik.
 *   - Die Hauptschleife (accept + proaktiver Timer) bleibt jederzeit
 *     reaktionsfaehig; ein langsamer Upstream blockiert sie nicht mehr.
 *
 * Vorbereitung auf Streaming: der Arbeiter besitzt den Client-Socket fuer
 * die gesamte Bearbeitungsdauer, ein spaeterer P:-Frame-Pfad kommt ohne
 * Aenderung am Nebenlaeufigkeitsmodell aus.
 */
#ifndef FLUX_WORKER_H
#define FLUX_WORKER_H

/* Startet den Arbeiterthread. Muss nach flux_provider_init() und vor dem
 * ersten submit aufgerufen werden. */
void flux_worker_start(void);

/* Uebergibt eine angenommene Client-Verbindung zur Bearbeitung. Der
 * Arbeiter uebernimmt den Besitz von cfd und schliesst ihn. */
void flux_worker_submit_client(int cfd);

/* Stellt einen proaktiven Pruef-Job (Benachrichtigungen, Journal,
 * Morgen-Briefing) in die Warteschlange. */
void flux_worker_submit_proactive(void);

#endif
