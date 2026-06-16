#include "telephony.h"

#include <stdio.h>

void flux_telephony_send_sms(const char *to, const char *body, char *out, size_t out_cap) {
    (void)body;
    snprintf(out, out_cap,
        "Kein Mobilfunk-Modem erkannt -- eine SMS an %s kann auf diesem "
        "Geraet nicht wirklich versendet werden (eSIM/Modem-Hardware "
        "fehlt). Bestaetigung und Text waren bereit; ein echtes "
        "Modem-Backend wuerde genau hier einhaken.", to);
}

void flux_telephony_call(const char *to, char *out, size_t out_cap) {
    snprintf(out, out_cap,
        "Kein Mobilfunk-Modem erkannt -- ein Anruf bei %s kann auf "
        "diesem Geraet nicht wirklich aufgebaut werden (eSIM/Modem-"
        "Hardware fehlt). Ein echtes Modem-Backend wuerde genau hier "
        "einhaken.", to);
}
