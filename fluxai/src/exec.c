#include "exec.h"
#include "mail.h"
#include "telephony.h"
#include "../../common/flux_config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <dirent.h>

#define CONTACTS_PATH "/etc/flux/contacts.txt"

/* "Name <addr>" -> "addr". Sonst unveraendert. */
static void strip_angle_addr(char *s) {
    char *lt = strchr(s, '<');
    char *gt = lt ? strchr(lt, '>') : NULL;
    if (lt && gt && gt > lt) {
        size_t n = (size_t)(gt - lt - 1);
        memmove(s, lt + 1, n);
        s[n] = '\0';
    }
}

/* Loest einen Namen ueber /etc/flux/contacts.txt auf.
 * want_email: 1 = E-Mail-Feld (mit '@'), 0 = Telefon-Feld (Ziffern).
 * Schreibt das Ergebnis nach out und gibt 1 zurueck, wenn gefunden.
 * Format je Zeile: "Name,Telefon,Email[,...]". */
static int resolve_contact(const char *name, int want_email,
                           char *out, size_t cap) {
    if (!name || !*name) return 0;
    FILE *f = fopen(CONTACTS_PATH, "r");
    if (!f) return 0;

    /* Suchbegriff in Kleinbuchstaben */
    char want[128]; size_t wi = 0;
    for (const char *q = name; *q && wi < sizeof(want)-1; q++)
        want[wi++] = (char)tolower((unsigned char)*q);
    want[wi] = '\0';

    char line[256];
    int found = 0;
    while (!found && fgets(line, sizeof(line), f)) {
        size_t l = strlen(line);
        while (l > 0 && (line[l-1] == '\n' || line[l-1] == '\r')) line[--l] = '\0';
        if (!line[0] || line[0] == '#') continue;

        /* Name = erstes Feld */
        char *c1 = strchr(line, ',');
        char namebuf[128];
        size_t nl = c1 ? (size_t)(c1 - line) : strlen(line);
        if (nl >= sizeof(namebuf)) nl = sizeof(namebuf)-1;
        memcpy(namebuf, line, nl); namebuf[nl] = '\0';
        char namelo[128]; size_t ni = 0;
        for (const char *q = namebuf; *q && ni < sizeof(namelo)-1; q++)
            namelo[ni++] = (char)tolower((unsigned char)*q);
        namelo[ni] = '\0';
        if (!strstr(namelo, want)) continue;

        /* passendes Feld suchen: mit '@' (Mail) bzw. mit Ziffer (Telefon) */
        char *tok = c1 ? c1 + 1 : NULL;
        char *save;
        for (tok = strtok_r(c1 ? c1 + 1 : NULL, ",", &save);
             tok; tok = strtok_r(NULL, ",", &save)) {
            while (*tok == ' ') tok++;
            int has_at = strchr(tok, '@') != NULL;
            int has_digit = 0;
            for (const char *t = tok; *t; t++) if (isdigit((unsigned char)*t)) has_digit = 1;
            if (want_email && has_at) { snprintf(out, cap, "%s", tok); found = 1; break; }
            if (!want_email && has_digit && !has_at) { snprintf(out, cap, "%s", tok); found = 1; break; }
        }
    }
    fclose(f);
    return found;
}

/* ===================================================================
 * Einstellung aendern (X:setting) -- maßgebliche Sicherheits-Schicht.
 *
 * KERNREGEL: Die Allowlist UND die Wert-Validierung werden HIER im
 * Daemon durchgesetzt, nicht (nur) in der Shell. Selbst wenn die KI
 * oder ein manipuliertes Frontend einen Key ausserhalb der Allowlist
 * schickt, wird er hier verworfen. Geheimnisse (z.B. pin_hash) stehen
 * bewusst NICHT in der Allowlist und sind so nie per Sprache aenderbar.
 * =================================================================== */

/* Normalisiert einen on/off-/an-aus-Wert nach "on"/"off". Gibt 1 bei
 * Erfolg zurueck, 0 wenn der Wert nicht eindeutig boolesch ist. */
static int norm_bool(const char *v, char *out, size_t cap) {
    if (strcasecmp(v, "on") == 0 || strcasecmp(v, "an") == 0 ||
        strcasecmp(v, "ein") == 0 || strcasecmp(v, "1") == 0 ||
        strcasecmp(v, "true") == 0 || strcasecmp(v, "aktiv") == 0) {
        snprintf(out, cap, "on"); return 1;
    }
    if (strcasecmp(v, "off") == 0 || strcasecmp(v, "aus") == 0 ||
        strcasecmp(v, "0") == 0 || strcasecmp(v, "false") == 0 ||
        strcasecmp(v, "inaktiv") == 0) {
        snprintf(out, cap, "off"); return 1;
    }
    return 0;
}

/* tts nutzt historisch 0/1 statt off/on. */
static int norm_bool01(const char *v, char *out, size_t cap) {
    char b[8];
    if (!norm_bool(v, b, sizeof(b))) return 0;
    snprintf(out, cap, "%s", strcmp(b, "on") == 0 ? "1" : "0");
    return 1;
}

/* Erstes Backlight-Verzeichnis unter /sys/class/backlight finden. */
static int exec_find_backlight(char *buf, size_t cap) {
    static const char *known[] = {
        "/sys/class/backlight/backlight",
        "/sys/class/backlight/lcd-backlight",
        "/sys/class/backlight/intel_backlight", NULL };
    for (int i = 0; known[i]; i++) {
        char probe[256];
        snprintf(probe, sizeof(probe), "%s/brightness", known[i]);
        FILE *f = fopen(probe, "r");
        if (f) { fclose(f); snprintf(buf, cap, "%s", known[i]); return 1; }
    }
    DIR *d = opendir("/sys/class/backlight");
    if (!d) return 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') continue;
        snprintf(buf, cap, "/sys/class/backlight/%s", e->d_name);
        closedir(d); return 1;
    }
    closedir(d);
    return 0;
}

/* Setzt die Helligkeit real ueber sysfs (analog tools.c/brightness_set).
 * EHRLICH: kein Backlight-Knoten (QEMU) -> wahrheitsgemaesse Meldung,
 * kein Fake-Erfolg. */
static void exec_set_brightness(long pct, char *out, size_t cap) {
    char dir[512];
    if (!exec_find_backlight(dir, sizeof(dir))) {
        snprintf(out, cap,
                 "Helligkeit nicht aenderbar: kein Backlight-Knoten gefunden "
                 "(z.B. in QEMU ohne /sys/class/backlight).");
        return;
    }
    char path_cur[560], path_max[560];
    snprintf(path_cur, sizeof(path_cur), "%s/brightness",     dir);
    snprintf(path_max, sizeof(path_max), "%s/max_brightness", dir);
    long max = 0;
    FILE *f = fopen(path_max, "r");
    if (f) { if (fscanf(f, "%ld", &max) != 1) max = 0; fclose(f); }
    if (max <= 0) {
        snprintf(out, cap, "Helligkeit nicht aenderbar: max_brightness nicht lesbar (%s).", path_max);
        return;
    }
    long raw = pct * max / 100;
    f = fopen(path_cur, "w");
    if (!f) {
        snprintf(out, cap, "Helligkeit nicht aenderbar: %s nicht schreibbar (Root-Rechte noetig?).", path_cur);
        return;
    }
    fprintf(f, "%ld\n", raw);
    fclose(f);
    snprintf(out, cap, "Helligkeit auf %ld%% gesetzt.", pct);
}

/* Fuehrt eine bestaetigte Einstellungs-Aenderung aus.
 * payload-Felder: KEY:<key>\nVALUE:<value>\n  */
static void exec_setting(const char *payload, char *out, size_t out_cap) {
    char key[64] = {0}, value[256] = {0};
    const char *p = payload;
    while (*p) {
        const char *e = strchr(p, '\n');
        size_t llen = e ? (size_t)(e - p) : strlen(p);
        if (strncmp(p, "KEY:", 4) == 0) {
            size_t vl = llen - 4; if (vl >= sizeof(key)) vl = sizeof(key) - 1;
            memcpy(key, p + 4, vl); key[vl] = '\0';
        } else if (strncmp(p, "VALUE:", 6) == 0) {
            size_t vl = llen - 6; if (vl >= sizeof(value)) vl = sizeof(value) - 1;
            memcpy(value, p + 6, vl); value[vl] = '\0';
        }
        p = e ? e + 1 : p + llen;
    }
    /* Whitespace trimmen */
    char *k = key;   while (*k == ' ' || *k == '\t') k++;   if (k != key) memmove(key, k, strlen(k) + 1);
    char *v = value; while (*v == ' ' || *v == '\t') v++;   if (v != value) memmove(value, v, strlen(v) + 1);
    { size_t l = strlen(key);   while (l && (key[l-1]==' '||key[l-1]=='\r'))   key[--l]   = '\0'; }
    { size_t l = strlen(value); while (l && (value[l-1]==' '||value[l-1]=='\r')) value[--l] = '\0'; }

    if (!key[0]) { snprintf(out, out_cap, "Keine Einstellung angegeben."); return; }

    /* ---- ALLOWLIST + Wert-Validierung (maßgeblich) ---- */
    char norm[256];

    if (strcasecmp(key, "brightness") == 0) {
        char *end; long pct = strtol(value, &end, 10);
        if (end == value || pct < 0 || pct > 100) {
            snprintf(out, out_cap, "Ungueltige Helligkeit '%s' (erlaubt: 0-100).", value);
            return;
        }
        exec_set_brightness(pct, out, out_cap);   /* setzt sysfs ODER meldet ehrlich */
        return;
    }
    if (strcasecmp(key, "theme") == 0) {
        static const char *themes[] = {"teal","blau","lila","orange","gruen","rot",NULL};
        int ok = 0;
        for (int i = 0; themes[i]; i++) if (strcasecmp(value, themes[i]) == 0) {
            snprintf(norm, sizeof(norm), "%s", themes[i]); ok = 1; break;
        }
        if (!ok) {
            snprintf(out, out_cap, "Ungueltiges Theme '%s' (erlaubt: teal, blau, lila, orange, gruen, rot).", value);
            return;
        }
        if (flux_config_set("theme", norm) != 0) { snprintf(out, out_cap, "Konnte Theme nicht speichern."); return; }
        snprintf(out, out_cap, "Farbthema auf %s gesetzt.", norm);
        return;
    }
    if (strcasecmp(key, "ai_provider") == 0) {
        static const char *provs[] = {"anthropic","deepseek","nvidia","llamacpp",NULL};
        int ok = 0;
        for (int i = 0; provs[i]; i++) if (strcasecmp(value, provs[i]) == 0) {
            snprintf(norm, sizeof(norm), "%s", provs[i]); ok = 1; break;
        }
        if (!ok) {
            snprintf(out, out_cap, "Ungueltiger KI-Anbieter '%s' (erlaubt: anthropic, deepseek, nvidia, llamacpp).", value);
            return;
        }
        if (flux_config_set("ai_provider", norm) != 0) { snprintf(out, out_cap, "Konnte Anbieter nicht speichern."); return; }
        snprintf(out, out_cap, "KI-Anbieter auf %s gesetzt.", norm);
        return;
    }
    if (strcasecmp(key, "ai_router") == 0 || strcasecmp(key, "ai_router_battery") == 0 ||
        strcasecmp(key, "wakeword") == 0  || strcasecmp(key, "voice_unlock_lock") == 0) {
        if (!norm_bool(value, norm, sizeof(norm))) {
            snprintf(out, out_cap, "Ungueltiger Wert '%s' (erlaubt: ein/aus).", value);
            return;
        }
        if (flux_config_set(key, norm) != 0) { snprintf(out, out_cap, "Konnte Einstellung nicht speichern."); return; }
        snprintf(out, out_cap, "%s auf %s gesetzt.", key, strcmp(norm,"on")==0 ? "Ein" : "Aus");
        return;
    }
    if (strcasecmp(key, "tts") == 0) {
        if (!norm_bool01(value, norm, sizeof(norm))) {
            snprintf(out, out_cap, "Ungueltiger Wert '%s' (erlaubt: ein/aus).", value);
            return;
        }
        if (flux_config_set("tts", norm) != 0) { snprintf(out, out_cap, "Konnte Sprachausgabe nicht speichern."); return; }
        snprintf(out, out_cap, "Sprachausgabe (TTS) %s.", strcmp(norm,"1")==0 ? "eingeschaltet" : "ausgeschaltet");
        return;
    }
    if (strcasecmp(key, "vision_backend") == 0) {
        if (strcasecmp(value,"cloud") == 0)      snprintf(norm,sizeof(norm),"cloud");
        else if (strcasecmp(value,"local") == 0 || strcasecmp(value,"lokal") == 0) snprintf(norm,sizeof(norm),"local");
        else { snprintf(out, out_cap, "Ungueltiger Wert '%s' (erlaubt: cloud, local).", value); return; }
        if (flux_config_set("vision_backend", norm) != 0) { snprintf(out, out_cap, "Konnte Bild-KI nicht speichern."); return; }
        snprintf(out, out_cap, "Bild-KI auf %s gesetzt.", strcmp(norm,"cloud")==0 ? "Cloud" : "Lokal");
        return;
    }
    if (strcasecmp(key, "auto_lock") == 0) {
        char *end; long s = strtol(value, &end, 10);
        /* erlaubte Stufen wie in der Shell: 0(aus)/30/60/120/300 */
        if (end == value || (s != 0 && s != 30 && s != 60 && s != 120 && s != 300)) {
            snprintf(out, out_cap, "Ungueltige Auto-Sperre '%s' (erlaubt: 0, 30, 60, 120, 300 Sekunden).", value);
            return;
        }
        snprintf(norm, sizeof(norm), "%ld", s);
        if (flux_config_set("auto_lock", norm) != 0) { snprintf(out, out_cap, "Konnte Auto-Sperre nicht speichern."); return; }
        if (s == 0) snprintf(out, out_cap, "Auto-Sperre ausgeschaltet.");
        else        snprintf(out, out_cap, "Auto-Sperre auf %ld Sekunden gesetzt.", s);
        return;
    }

    /* Nicht in der Allowlist -> ehrlich ablehnen (z.B. pin_hash, smtp_pass). */
    snprintf(out, out_cap,
             "Diese Einstellung ('%s') kann nicht per Sprache geaendert werden "
             "(nicht in der Allowlist). Erlaubt sind u.a.: brightness, theme, "
             "ai_provider, ai_router, ai_router_battery, tts, wakeword, "
             "vision_backend, auto_lock.", key);
}

void flux_exec_action(const char *payload, char *out, size_t out_cap) {
    const char *line_end = strchr(payload, '\n');
    char type[16] = {0};
    size_t type_len = line_end ? (size_t)(line_end - payload) : strlen(payload);
    if (type_len >= sizeof(type)) type_len = sizeof(type) - 1;
    memcpy(type, payload, type_len);

    /* Einstellungs-Aktion: eigenes KEY/VALUE-Format, eigener Pfad mit
     * Allowlist + Wert-Validierung (maßgebliche Sicherheits-Schicht). */
    if (strcasecmp(type, "setting") == 0 || strcasecmp(type, "einstellung") == 0) {
        exec_setting(line_end ? line_end + 1 : "", out, out_cap);
        return;
    }

    char to[256] = {0}, subject[256] = {0}, body[4096] = {0};
    const char *p = line_end ? line_end + 1 : "";
    while (*p) {
        if (strncmp(p, "TO:", 3) == 0) {
            p += 3;
            const char *e = strchr(p, '\n');
            size_t len = e ? (size_t)(e - p) : strlen(p);
            if (len >= sizeof(to)) len = sizeof(to) - 1;
            memcpy(to, p, len); to[len] = '\0';
            p = e ? e + 1 : p + len;
        } else if (strncmp(p, "SUBJECT:", 8) == 0) {
            p += 8;
            const char *e = strchr(p, '\n');
            size_t len = e ? (size_t)(e - p) : strlen(p);
            if (len >= sizeof(subject)) len = sizeof(subject) - 1;
            memcpy(subject, p, len); subject[len] = '\0';
            p = e ? e + 1 : p + len;
        } else if (strncmp(p, "BODY:", 5) == 0) {
            p += 5;
            if (*p == '\n') p++;
            snprintf(body, sizeof(body), "%s", p);
            break;
        } else {
            const char *e = strchr(p, '\n');
            p = e ? e + 1 : p + strlen(p);
        }
    }

    /* "email"/"e-mail" als Synonym fuer "mail" akzeptieren */
    if (strcasecmp(type, "email") == 0 || strcasecmp(type, "e-mail") == 0)
        snprintf(type, sizeof(type), "mail");

    /* Empfaenger robust aufloesen -- die KI liefert manchmal nur den Namen.
     * "Name <addr>" -> "addr"; reiner Name -> aus Kontakten nachschlagen. */
    strip_angle_addr(to);
    if (strcasecmp(type, "mail") == 0) {
        if (!strchr(to, '@')) {
            char resolved[256];
            if (resolve_contact(to, 1, resolved, sizeof(resolved)))
                snprintf(to, sizeof(to), "%s", resolved);
        }
    } else { /* sms / call: Telefonnummer noetig */
        int has_digit = 0;
        for (const char *t = to; *t; t++) if (isdigit((unsigned char)*t)) has_digit = 1;
        if (!has_digit) {
            char resolved[256];
            if (resolve_contact(to, 0, resolved, sizeof(resolved)))
                snprintf(to, sizeof(to), "%s", resolved);
        }
    }

    if (strcasecmp(type, "mail") == 0) {
        if (!strchr(to, '@')) {
            snprintf(out, out_cap,
                     "Keine E-Mail-Adresse fuer \"%s\" gefunden. Lege den Kontakt "
                     "mit E-Mail an oder gib die Adresse direkt an.", to);
            return;
        }
        flux_mail_send(to, subject, body, out, out_cap);
    } else if (strcasecmp(type, "sms") == 0) {
        flux_telephony_send_sms(to, body, out, out_cap);
    } else if (strcasecmp(type, "call") == 0) {
        flux_telephony_call(to, out, out_cap);
    } else {
        snprintf(out, out_cap, "Unbekannter Aktionstyp '%s'.", type);
    }
}
