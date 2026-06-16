/* telephony.h -- Stub-Backend fuer SMS/Anruf.
 *
 * Ehrlicher Hinweis: weder die QEMU-virt-Maschine noch der aktuelle
 * Geraete-Port haben ein Mobilfunk-Modem oder einen eSIM-Chip -- es
 * gibt also nichts, worueber tatsaechlich eine SMS/ein Anruf laufen
 * koennte. Statt das vorzutaeuschen, wird hier ehrlich gemeldet, dass
 * keine Hardware vorhanden ist (gleiches Muster wie der fehlende
 * Akku-Sensor in actions.c).
 *
 * Bewusst als eigene, austauschbare Datei: sobald echte Modem-Hardware
 * angebunden wird (README-Roadmap, Punkt 7), ersetzt eine echte
 * Implementierung (z.B. ueber ofono/ModemManager per D-Bus oder
 * direkte AT-Kommandos an den Modem-Treiber) nur telephony.c -- das
 * Protokoll und der Bestaetigungs-Dialog in der Shell bleiben gleich.
 */
#ifndef FLUX_TELEPHONY_H
#define FLUX_TELEPHONY_H

#include <stddef.h>

void flux_telephony_send_sms(const char *to, const char *body, char *out, size_t out_cap);
void flux_telephony_call(const char *to, char *out, size_t out_cap);

#endif
