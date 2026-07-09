#include "actions.h"
#include "browser.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <dirent.h>

static int contains(const char *haystack, const char *needle) {
    return strcasestr(haystack, needle) != NULL;
}

static int read_int_file(const char *path, long *out) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    int ok = fscanf(f, "%ld", out) == 1;
    fclose(f);
    return ok ? 0 : -1;
}

static int try_battery(const char *q, char *out, size_t cap) {
    if (!contains(q, "akku") && !contains(q, "battery"))
        return 0;

    long capacity = -1;
    if (read_int_file("/sys/class/power_supply/battery/capacity", &capacity) != 0 &&
        read_int_file("/sys/class/power_supply/BAT0/capacity", &capacity) != 0) {
        snprintf(out, cap, "Kein Akku-Sensor gefunden (laeuft das hier in QEMU ohne power_supply-Knoten?).");
        return 1;
    }
    snprintf(out, cap, "Akkustand: %ld%%.", capacity);
    return 1;
}

static int try_time(const char *q, char *out, size_t cap) {
    if (!contains(q, "uhrzeit") && !contains(q, "zeit") && !contains(q, "time") &&
        !contains(q, "spaet") && !contains(q, "uhr "))
        return 0;
    time_t t = time(NULL);
    struct tm tmv;
    localtime_r(&t, &tmv);
    char buf[64];
    strftime(buf, sizeof(buf), "%H:%M", &tmv);
    snprintf(out, cap, "Es ist %s Uhr.", buf);
    return 1;
}

static int try_date(const char *q, char *out, size_t cap) {
    if (!contains(q, "datum") && !contains(q, "date"))
        return 0;
    time_t t = time(NULL);
    struct tm tmv;
    localtime_r(&t, &tmv);
    char buf[64];
    strftime(buf, sizeof(buf), "%d.%m.%Y", &tmv);
    snprintf(out, cap, "Heute ist der %s.", buf);
    return 1;
}

static int try_uptime(const char *q, char *out, size_t cap) {
    if (!contains(q, "uptime") && !contains(q, "laeuft schon"))
        return 0;
    FILE *f = fopen("/proc/uptime", "r");
    if (!f) return 0;
    double secs = 0;
    int ok = fscanf(f, "%lf", &secs) == 1;
    fclose(f);
    if (!ok) return 0;
    snprintf(out, cap, "Flux laeuft seit %d Minuten.", (int)(secs / 60));
    return 1;
}

/* Liest den rfkill-Soft-Block-Status eines bestimmten Typs ("wlan", "bluetooth",
 * "wwan") aus /sys/class/rfkill/. Gibt 1 zurueck wenn gesperrt, 0 wenn aktiv,
 * -1 wenn kein Eintrag des Typs vorhanden. */
static int rfkill_soft_blocked(const char *type) {
    DIR *d = opendir("/sys/class/rfkill");
    if (!d) return -1;
    struct dirent *e;
    int result = -1;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') continue;
        char path[128];
        snprintf(path, sizeof(path), "/sys/class/rfkill/%.80s/type", e->d_name);
        FILE *f = fopen(path, "r");
        if (!f) continue;
        char buf[32];
        if (!fgets(buf, sizeof(buf), f)) { fclose(f); continue; }
        fclose(f);
        /* Typ-String endet mit '\n' */
        size_t len = strlen(buf);
        if (len > 0 && buf[len-1] == '\n') buf[len-1] = '\0';
        if (strcasecmp(buf, type) != 0) continue;

        snprintf(path, sizeof(path), "/sys/class/rfkill/%.80s/soft", e->d_name);
        f = fopen(path, "r");
        if (!f) continue;
        long val = 0;
        int ok = fscanf(f, "%ld", &val) == 1;
        fclose(f);
        if (ok) { result = (int)val; break; }
    }
    closedir(d);
    return result;
}

static int try_wifi_status(const char *q, char *out, size_t cap) {
    if (!contains(q, "wlan") && !contains(q, "wifi") && !contains(q, "wireless") &&
        !contains(q, "internet") && !contains(q, "netz"))
        return 0;
    /* Nur bei klarer Status-Frage antworten, nicht bei Aktions-Anfragen */
    if (!contains(q, "ist") && !contains(q, "status") && !contains(q, "an") &&
        !contains(q, "aus") && !contains(q, "aktiv") && !contains(q, "verbunden"))
        return 0;

    int blocked = rfkill_soft_blocked("wlan");
    if (blocked < 0) {
        /* Kein rfkill -- pruefen ob Interface in /proc/net/wireless sichtbar */
        FILE *f = fopen("/proc/net/wireless", "r");
        if (!f) {
            snprintf(out, cap, "Kein WLAN-Hardware erkannt (kein rfkill, kein /proc/net/wireless).");
            return 1;
        }
        char line[256];
        int lineno = 0, found = 0;
        while (fgets(line, sizeof(line), f)) {
            if (++lineno > 2 && line[0] != '\n') { found = 1; break; }
        }
        fclose(f);
        snprintf(out, cap, found ? "WLAN ist aktiv (Interface verbunden)."
                                 : "WLAN-Interface vorhanden, aber kein Netz verbunden.");
        return 1;
    }
    snprintf(out, cap, blocked ? "WLAN ist ausgeschaltet (rfkill: gesperrt)."
                               : "WLAN ist eingeschaltet (rfkill: aktiv).");
    return 1;
}

static int try_flight_mode(const char *q, char *out, size_t cap) {
    if (!contains(q, "flugmodus") && !contains(q, "flight") && !contains(q, "airplane") &&
        !contains(q, "flugzeug"))
        return 0;

    /* Alle rfkill-Typen pruefen: wenn alle soft-blocked, ist Flugmodus aktiv */
    DIR *d = opendir("/sys/class/rfkill");
    if (!d) {
        snprintf(out, cap, "Flugmodus-Status nicht lesbar: kein rfkill-Subsystem gefunden.");
        return 1;
    }
    struct dirent *e;
    int total = 0, blocked = 0;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') continue;
        char path[128];
        snprintf(path, sizeof(path), "/sys/class/rfkill/%.80s/soft", e->d_name);
        FILE *f = fopen(path, "r");
        if (!f) continue;
        long val = 0;
        if (fscanf(f, "%ld", &val) == 1) {
            total++;
            if (val) blocked++;
        }
        fclose(f);
    }
    closedir(d);

    if (total == 0) {
        snprintf(out, cap, "Keine Funkschnittstellen vorhanden (QEMU ohne Funk-Hardware).");
    } else if (blocked == total) {
        snprintf(out, cap, "Flugmodus ist aktiv (%d/%d Schnittstellen gesperrt).", blocked, total);
    } else if (blocked == 0) {
        snprintf(out, cap, "Flugmodus ist deaktiviert (alle %d Schnittstellen aktiv).", total);
    } else {
        snprintf(out, cap, "Flugmodus teilweise aktiv (%d von %d Schnittstellen gesperrt).",
                 blocked, total);
    }
    return 1;
}

/* Direkte Browser-Navigation vom Browser-Screen (shell/src/ui.c) --
 * bewusst ein exaktes Praefix statt Schlagwort-Suche wie die anderen
 * lokalen Aktionen: die Shell konstruiert diese Anfrage selbst (Nutzer
 * tippt nur die URL/Nummer ein), es soll nie mit echtem Freitext
 * kollidieren. Laeuft komplett lokal -- kein LLM-Umweg fuer eine simple
 * Navigation, dieselbe "lokal zuerst" Idee wie Uhrzeit/Akku/etc. */
#define BROWSER_OPEN_PREFIX  "__flux_browser_open__ "
#define BROWSER_CLICK_PREFIX "__flux_browser_click__ "

static int try_browser(const char *q, char *out, size_t cap) {
    if (strncmp(q, BROWSER_OPEN_PREFIX, strlen(BROWSER_OPEN_PREFIX)) == 0)
        return flux_browser_open(q + strlen(BROWSER_OPEN_PREFIX), out, cap);
    if (strncmp(q, BROWSER_CLICK_PREFIX, strlen(BROWSER_CLICK_PREFIX)) == 0)
        return flux_browser_click(q + strlen(BROWSER_CLICK_PREFIX), out, cap);
    return 0;
}

int flux_actions_try(const char *question, char *out, size_t out_cap) {
    if (try_browser(question, out, out_cap))      return 1;
    if (try_battery(question, out, out_cap))      return 1;
    if (try_time(question, out, out_cap))         return 1;
    if (try_date(question, out, out_cap))         return 1;
    if (try_uptime(question, out, out_cap))       return 1;
    if (try_wifi_status(question, out, out_cap))  return 1;
    if (try_flight_mode(question, out, out_cap))  return 1;
    return 0;
}
