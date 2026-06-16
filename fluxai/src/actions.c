#include "actions.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <time.h>

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

int flux_actions_try(const char *question, char *out, size_t out_cap) {
    if (try_battery(question, out, out_cap)) return 1;
    if (try_time(question, out, out_cap))    return 1;
    if (try_date(question, out, out_cap))    return 1;
    if (try_uptime(question, out, out_cap))  return 1;
    return 0;
}
