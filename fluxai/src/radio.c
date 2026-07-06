#include "radio.h"

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/rfkill.h>

#define RFKILL_DEV "/dev/rfkill"

/* Klartext-Name je rfkill-Typ (fuer die Zusammenfassung). */
static const char *type_name(unsigned char t) {
    switch (t) {
        case RFKILL_TYPE_WLAN:      return "WLAN";
        case RFKILL_TYPE_BLUETOOTH: return "Bluetooth";
        case RFKILL_TYPE_UWB:       return "UWB";
        case RFKILL_TYPE_WIMAX:     return "WiMAX";
        case RFKILL_TYPE_WWAN:      return "Mobilfunk";
        case RFKILL_TYPE_GPS:       return "GPS";
        case RFKILL_TYPE_FM:        return "FM";
        case RFKILL_TYPE_NFC:       return "NFC";
        default:                    return "Funk";
    }
}

void flux_radio_status(char *out, size_t out_cap) {
    int fd = open(RFKILL_DEV, O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        snprintf(out, out_cap,
            "Keine Funkhardware erkannt -- %s ist auf diesem Geraet nicht "
            "vorhanden (z.B. QEMU `virt` ohne WLAN/Mobilfunk). Ein echtes "
            "Geraet mit rfkill-faehigen Funkmodulen wuerde hier den "
            "Flugmodus-Status melden.", RFKILL_DEV);
        return;
    }

    /* Beim Oeffnen liefert /dev/rfkill je vorhandenem Modul ein
     * RFKILL_OP_ADD-Event mit dem aktuellen Soft-Block-Zustand. */
    struct rfkill_event ev;
    int total = 0, blocked = 0;
    char detail[768];
    size_t pos = 0;
    ssize_t n;
    while ((n = read(fd, &ev, sizeof(ev))) == sizeof(ev)) {
        if (ev.op != RFKILL_OP_ADD) continue;
        total++;
        if (ev.soft) blocked++;
        if (pos < sizeof(detail))
            pos += snprintf(detail + pos, sizeof(detail) - pos, "%s%s: %s",
                            pos ? ", " : "", type_name(ev.type),
                            ev.soft ? "blockiert" : "an");
    }
    close(fd);

    if (total == 0) {
        snprintf(out, out_cap,
            "Keine Funkmodule registriert (%s ist vorhanden, meldet aber "
            "kein Geraet). Flugmodus laesst sich daher nicht sinnvoll "
            "schalten.", RFKILL_DEV);
        return;
    }

    if (blocked == total)
        snprintf(out, out_cap, "Flugmodus AN -- alle Funkmodule blockiert (%s).", detail);
    else if (blocked == 0)
        snprintf(out, out_cap, "Flugmodus AUS -- Funk aktiv (%s).", detail);
    else
        snprintf(out, out_cap,
            "Flugmodus teilweise: %d von %d Modulen blockiert (%s).",
            blocked, total, detail);
}

void flux_radio_set_airplane(int on, char *out, size_t out_cap) {
    int fd = open(RFKILL_DEV, O_RDWR);
    if (fd < 0) {
        if (errno == EACCES || errno == EPERM) {
            snprintf(out, out_cap,
                "Flugmodus konnte nicht geschaltet werden: keine Rechte fuer "
                "%s (Root noetig).", RFKILL_DEV);
        } else {
            snprintf(out, out_cap,
                "Keine Funkhardware erkannt -- Flugmodus kann auf diesem "
                "Geraet nicht geschaltet werden (%s fehlt, z.B. QEMU `virt` "
                "ohne Funkmodule). Ein echtes Geraet wuerde hier alle Funk-"
                "module ueber rfkill %s.", RFKILL_DEV,
                on ? "blockieren" : "freigeben");
        }
        return;
    }

    /* Ein CHANGE_ALL-Event mit Typ ALL setzt den Soft-Block fuer alle
     * registrierten Module auf einmal. */
    struct rfkill_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.op   = RFKILL_OP_CHANGE_ALL;
    ev.type = RFKILL_TYPE_ALL;
    ev.soft = on ? 1 : 0;

    ssize_t w = write(fd, &ev, sizeof(ev));
    close(fd);

    if (w != (ssize_t)sizeof(ev)) {
        snprintf(out, out_cap,
            "Flugmodus konnte nicht geschaltet werden (Schreibfehler auf %s).",
            RFKILL_DEV);
        return;
    }

    if (on)
        snprintf(out, out_cap,
            "Flugmodus eingeschaltet -- alle Funkmodule (WLAN/Bluetooth/"
            "Mobilfunk) sind jetzt blockiert.");
    else
        snprintf(out, out_cap,
            "Flugmodus ausgeschaltet -- Funkmodule sind wieder freigegeben.");
}
