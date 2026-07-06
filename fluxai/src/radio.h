/* radio.h -- Flugmodus / Funk-Steuerung fuer fluxaid.
 *
 * Bewusst als austauschbares Backend geschnitten (gleiche Haltung wie
 * telephony.c): auf echter Hardware steuert das ueber /dev/rfkill alle
 * Funkmodule (WLAN/Bluetooth/Mobilfunk). Fehlt /dev/rfkill (z.B. QEMU
 * `virt` ohne Funkhardware), wird das ehrlich gemeldet statt einen
 * Erfolg zu erfinden.
 *
 * Protokoll/Aufrufer (tools.c, flight_mode) bleiben stabil, wenn spaeter
 * ein anderes Backend (NetworkManager/ofono) dahinter kommt.
 */
#ifndef FLUX_RADIO_H
#define FLUX_RADIO_H

#include <stddef.h>

/* Liest den aktuellen Funk-Zustand ueber /dev/rfkill und schreibt eine
 * menschenlesbare Zusammenfassung nach out (Flugmodus an/aus, je Modul).
 * Meldet ehrlich, wenn /dev/rfkill nicht existiert. */
void flux_radio_status(char *out, size_t out_cap);

/* Schaltet den Flugmodus: on != 0 blockt alle Funkmodule (soft-block),
 * on == 0 hebt die Blockade auf. Schreibt eine ehrliche Rueckmeldung
 * nach out (auch wenn keine Funkhardware vorhanden ist). */
void flux_radio_set_airplane(int on, char *out, size_t out_cap);

#endif
