#include "wifi.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>

/* ---- Interface-Erkennung -------------------------------------------- */

/* Erstes WLAN-Interface aus /sys/class/net/<if>/wireless. */
static const char *wifi_iface(void) {
    static char iface[32];
    if (iface[0]) return iface;
    DIR *d = opendir("/sys/class/net");
    if (!d) return NULL;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') continue;
        char probe[128];
        snprintf(probe, sizeof(probe), "/sys/class/net/%s/wireless", e->d_name);
        if (access(probe, F_OK) == 0) {
            snprintf(iface, sizeof(iface), "%s", e->d_name);
            closedir(d);
            return iface;
        }
    }
    closedir(d);
    return NULL;
}

static int have_cmd(const char *path) { return access(path, X_OK) == 0; }

int flux_wifi_available(void) {
    if (!wifi_iface()) return 0;
    return have_cmd("/usr/sbin/wpa_cli") || have_cmd("/sbin/wpa_cli") ||
           have_cmd("/usr/bin/wpa_cli");
}

/* ---- Shell-Quoting (einfache Anfuehrungszeichen) -------------------- */

/* Schreibt 'src' shell-sicher in einfache Anfuehrungszeichen nach dst. */
static void shquote(const char *src, char *dst, size_t cap) {
    size_t o = 0;
    if (o < cap - 1) dst[o++] = '\'';
    for (const char *p = src; *p && o + 4 < cap; p++) {
        if (*p == '\'') { /* ' -> '\'' */
            dst[o++] = '\''; dst[o++] = '\\'; dst[o++] = '\''; dst[o++] = '\'';
        } else {
            dst[o++] = *p;
        }
    }
    if (o < cap - 1) dst[o++] = '\'';
    dst[o] = '\0';
}

/* ---- Scan-Parser (testbar) ------------------------------------------ */

/* Signalpegel (dBm) -> grobe Prozentangabe. */
static int dbm_to_pct(int dbm) {
    int pct = 2 * (dbm + 100);   /* -100 dBm -> 0%, -50 dBm -> 100% */
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    return pct;
}

int flux_wifi_parse_scan(const char *text, flux_wifi_net_t *out, int max) {
    if (!text) return 0;
    int n = 0;
    const char *line = text;
    int first = 1;
    while (*line && n < max) {
        const char *eol = strchr(line, '\n');
        size_t len = eol ? (size_t)(eol - line) : strlen(line);

        char buf[512];
        if (len >= sizeof(buf)) len = sizeof(buf) - 1;
        memcpy(buf, line, len); buf[len] = '\0';
        line = eol ? eol + 1 : line + len;

        /* Kopfzeile "bssid / frequency / signal level / flags / ssid" ueberspringen */
        if (first) { first = 0; if (strstr(buf, "bssid")) continue; }
        if (!buf[0]) continue;

        /* Tab-getrennt: [0]bssid [1]freq [2]signal [3]flags [4]ssid */
        char *fields[5] = {0};
        int fn = 0;
        char *p = buf, *save;
        for (char *tok = strtok_r(p, "\t", &save); tok && fn < 5;
             tok = strtok_r(NULL, "\t", &save))
            fields[fn++] = tok;
        if (fn < 5) continue;            /* keine SSID-Spalte -> ignorieren */
        const char *ssid = fields[4];
        if (!ssid || !ssid[0]) continue; /* versteckte Netze ohne SSID */

        int dbm = atoi(fields[2]);
        const char *flags = fields[3];
        int secured = (strstr(flags, "WPA") || strstr(flags, "RSN") ||
                       strstr(flags, "WEP") || strstr(flags, "PSK")) ? 1 : 0;
        int pct = dbm_to_pct(dbm);

        /* Duplikat? -> staerkeres Signal behalten */
        int dup = -1;
        for (int i = 0; i < n; i++)
            if (strcmp(out[i].ssid, ssid) == 0) { dup = i; break; }
        if (dup >= 0) {
            if (pct > out[dup].signal_pct) {
                out[dup].signal_pct = pct;
                out[dup].secured = secured;
            }
            continue;
        }
        snprintf(out[n].ssid, sizeof(out[n].ssid), "%s", ssid);
        out[n].signal_pct = pct;
        out[n].secured = secured;
        n++;
    }

    /* nach Signal absteigend sortieren (kleine Liste -> simpel) */
    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++)
            if (out[j].signal_pct > out[i].signal_pct) {
                flux_wifi_net_t t = out[i]; out[i] = out[j]; out[j] = t;
            }
    return n;
}

/* ---- Live-Scan ------------------------------------------------------- */

int flux_wifi_scan(flux_wifi_net_t *out, int max) {
    const char *iface = wifi_iface();
    if (!iface) return -1;

    char cmd[256];
    /* Scan anstossen (Ergebnis ignorieren) */
    snprintf(cmd, sizeof(cmd), "wpa_cli -i %s scan >/dev/null 2>&1", iface);
    if (system(cmd) != 0) { /* trotzdem versuchen, Ergebnisse zu lesen */ }
    sleep(2); /* dem Treiber Zeit zum Scannen geben */

    snprintf(cmd, sizeof(cmd), "wpa_cli -i %s scan_results 2>/dev/null", iface);
    FILE *f = popen(cmd, "r");
    if (!f) return -1;
    char text[8192]; size_t total = 0;
    size_t r;
    while (total + 1 < sizeof(text) &&
           (r = fread(text + total, 1, sizeof(text) - 1 - total, f)) > 0)
        total += r;
    text[total] = '\0';
    pclose(f);

    return flux_wifi_parse_scan(text, out, max);
}

/* ---- Verbinden ------------------------------------------------------- */

int flux_wifi_connect(const char *ssid, const char *pass, char *msg, size_t msgcap) {
    const char *iface = wifi_iface();
    if (!iface) { snprintf(msg, msgcap, "Kein WLAN-Interface gefunden."); return 0; }
    if (!ssid || !ssid[0]) { snprintf(msg, msgcap, "Keine SSID angegeben."); return 0; }

    /* Neues Netzwerk anlegen und ID auslesen */
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "wpa_cli -i %s add_network 2>/dev/null", iface);
    FILE *f = popen(cmd, "r");
    if (!f) { snprintf(msg, msgcap, "wpa_cli nicht verfuegbar."); return 0; }
    char idbuf[32] = {0};
    if (!fgets(idbuf, sizeof(idbuf), f)) { pclose(f); snprintf(msg, msgcap, "add_network fehlgeschlagen."); return 0; }
    pclose(f);
    int netid = atoi(idbuf);

    char qssid[160], qpass[160], wrapped[200];
    /* wpa_cli erwartet den Wert als "...". -> erst "..." bauen, dann shell-quoten */
    snprintf(wrapped, sizeof(wrapped), "\"%s\"", ssid);
    shquote(wrapped, qssid, sizeof(qssid));

    snprintf(cmd, sizeof(cmd), "wpa_cli -i %s set_network %d ssid %s >/dev/null 2>&1",
             iface, netid, qssid);
    system(cmd);

    if (pass && pass[0]) {
        snprintf(wrapped, sizeof(wrapped), "\"%s\"", pass);
        shquote(wrapped, qpass, sizeof(qpass));
        snprintf(cmd, sizeof(cmd), "wpa_cli -i %s set_network %d psk %s >/dev/null 2>&1",
                 iface, netid, qpass);
        system(cmd);
    } else {
        snprintf(cmd, sizeof(cmd), "wpa_cli -i %s set_network %d key_mgmt NONE >/dev/null 2>&1",
                 iface, netid);
        system(cmd);
    }

    snprintf(cmd, sizeof(cmd), "wpa_cli -i %s enable_network %d >/dev/null 2>&1", iface, netid);
    system(cmd);
    snprintf(cmd, sizeof(cmd), "wpa_cli -i %s save_config >/dev/null 2>&1", iface);
    system(cmd);

    /* IP per DHCP holen (udhcpc bevorzugt, sonst dhclient) */
    if (have_cmd("/sbin/udhcpc") || have_cmd("/usr/sbin/udhcpc"))
        snprintf(cmd, sizeof(cmd), "udhcpc -i %s -n -q >/dev/null 2>&1", iface);
    else
        snprintf(cmd, sizeof(cmd), "dhclient %s >/dev/null 2>&1", iface);
    system(cmd);

    /* Verbindung pruefen */
    sleep(2);
    char cur[64] = {0};
    flux_wifi_current(cur, sizeof(cur));
    if (cur[0] && strcmp(cur, ssid) == 0) {
        snprintf(msg, msgcap, "Verbunden mit \"%s\".", ssid);
        return 1;
    }
    snprintf(msg, msgcap,
             "Verbindung zu \"%s\" angestossen. Falls es nicht klappt, pruefe "
             "das Passwort.", ssid);
    return cur[0] ? 1 : 0;
}

void flux_wifi_current(char *out, size_t cap) {
    out[0] = '\0';
    const char *iface = wifi_iface();
    if (!iface) return;
    char cmd[160];
    snprintf(cmd, sizeof(cmd), "wpa_cli -i %s status 2>/dev/null", iface);
    FILE *f = popen(cmd, "r");
    if (!f) return;
    char line[256];
    char ssid[64] = {0}; int completed = 0;
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "ssid=", 5) == 0) {
            snprintf(ssid, sizeof(ssid), "%s", line + 5);
            size_t l = strlen(ssid);
            while (l > 0 && (ssid[l-1] == '\n' || ssid[l-1] == '\r')) ssid[--l] = '\0';
        }
        if (strstr(line, "wpa_state=COMPLETED")) completed = 1;
    }
    pclose(f);
    if (completed && ssid[0]) snprintf(out, cap, "%s", ssid);
}
