/* wifi.h -- WLAN-Verwaltung fuer Flux: Netze scannen und verbinden.
 *
 * Nutzt wpa_supplicant/wpa_cli (Standard auf Buildroot/Embedded-Linux) und
 * udhcpc/dhclient fuer die IP. Ohne diese Werkzeuge meldet
 * flux_wifi_available() 0 und die UI zeigt einen ehrlichen Hinweis.
 */
#ifndef FLUX_WIFI_H
#define FLUX_WIFI_H

#include <stddef.h>

typedef struct {
    char ssid[64];
    int  signal_pct;   /* 0-100 */
    int  secured;      /* 1 = verschluesselt (WPA/WPA2/WEP), 0 = offen */
} flux_wifi_net_t;

/* 1, wenn ein WLAN-Interface und wpa_cli vorhanden sind. */
int flux_wifi_available(void);

/* Parst die Ausgabe von "wpa_cli scan_results" in out (dedupliziert,
 * staerkstes Signal je SSID, nach Signal sortiert). Gibt die Anzahl
 * zurueck. Reine Funktion -- fuer Unit-Tests exponiert. */
int flux_wifi_parse_scan(const char *text, flux_wifi_net_t *out, int max);

/* Scannt aktiv (Trigger + Ergebnisse). Gibt Anzahl Netze (>=0) oder -1. */
int flux_wifi_scan(flux_wifi_net_t *out, int max);

/* Verbindet mit ssid (pass darf "" sein fuer offene Netze).
 * Schreibt eine Status-/Fehlermeldung nach msg. Gibt 1 bei Erfolg. */
int flux_wifi_connect(const char *ssid, const char *pass, char *msg, size_t msgcap);

/* Aktuell verbundene SSID nach out (leer, wenn nicht verbunden). */
void flux_wifi_current(char *out, size_t cap);

#endif
