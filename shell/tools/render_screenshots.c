/* render_screenshots.c -- rendert alle UI-Bildschirme als PPM-Dateien.
 * Kein /dev/fb0 noetig: flux_fb_open_null() ersetzt das Framebuffer-mmap
 * durch malloc'd Speicher. ImageMagick/netpbm kann die PPMs in PNG wandeln.
 *
 * Aufruf: ./render_screenshots [ausgabepfad]
 * Default-Pfad: /tmp/flux_screenshots/
 */
#include "../src/fb.h"
#include "../src/ui.h"
#include "../src/action.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>

static void save_ppm(const flux_fb_t *fb, const char *dir, const char *name) {
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.ppm", dir, name);
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); return; }
    fprintf(f, "P6\n%d %d\n255\n", fb->width, fb->height);
    for (int i = 0; i < fb->width * fb->height; i++) {
        uint32_t px = fb->back[i];
        fputc((px >> 16) & 0xff, f);
        fputc((px >>  8) & 0xff, f);
        fputc( px        & 0xff, f);
    }
    fclose(f);
    printf("  gespeichert: %s\n", path);
}

int main(int argc, char *argv[]) {
    const char *outdir = (argc > 1) ? argv[1] : "/tmp/flux_screenshots";
    mkdir(outdir, 0755);

    flux_fb_t fb;
    if (flux_fb_open_null(&fb, 480, 854) != 0) {
        fprintf(stderr, "flux_fb_open_null fehlgeschlagen\n");
        return 1;
    }

    printf("Rendere Flux UI-Screenshots (480x854) nach %s/ ...\n", outdir);

    /* 01 -- Lockscreen */
    flux_ui_draw_lock(&fb);
    save_ppm(&fb, outdir, "01_lockscreen");

    /* 02 -- PIN-Eingabe (2 von 4 Ziffern) */
    flux_ui_draw_pin(&fb, 2, 0);
    save_ppm(&fb, outdir, "02_pin");

    /* 03 -- PIN falsch */
    flux_ui_draw_pin(&fb, 0, 1);
    save_ppm(&fb, outdir, "03_pin_fehler");

    /* 04 -- Assistent leer */
    flux_ui_draw_assistant(&fb, "", "", "", 0);
    save_ppm(&fb, outdir, "04_assistent_leer");

    /* 05 -- Assistent: tippt Frage */
    flux_ui_draw_assistant(&fb, "", "schreibe eine E-Mail an Max", "", 0);
    save_ppm(&fb, outdir, "05_assistent_tipp");

    /* 06 -- Assistent: denkt nach (Nutzer-Blase + Lade-Blase) */
    flux_ui_draw_assistant(&fb, "schreibe eine E-Mail an Max", "", "", 1);
    save_ppm(&fb, outdir, "06_assistent_denkt");

    /* 07 -- Assistent: normale Textantwort (keine Aktion) */
    flux_ui_draw_assistant(&fb,
        "wie spaet ist es?",
        "",
        "Es ist 14:35 Uhr.",
        0);
    save_ppm(&fb, outdir, "07_assistent_antwort");

    /* 08 -- Assistent: laengere Antwort */
    flux_ui_draw_assistant(&fb,
        "erklaer mir kurz wie Flux funktioniert",
        "",
        "Flux ist ein KI-zentriertes Mobil-OS. "
        "Du entsperrst das Geraet und sprichst direkt mit der KI -- "
        "kein App-Grid, kein Suchen. "
        "Einstellungen und Dateien erreichst du ueber die zwei "
        "Knoepfe oben oder indem du sie einfach eintippst.",
        0);
    save_ppm(&fb, outdir, "08_assistent_lange_antwort");

    /* 09 -- Bestaetigungs-Dialog: E-Mail */
    flux_ui_draw_confirm(&fb,
        "E-Mail",
        "max@example.com",
        "Bin heute krank",
        "Hallo Max!\n\n"
        "Ich muss dir leider sagen, dass ich heute krank bin "
        "und nicht ins Buero komme.\n\n"
        "Viele Gruesse");
    save_ppm(&fb, outdir, "09_bestaetigung_email");

    /* 10 -- Bestaetigungs-Dialog: SMS */
    flux_ui_draw_confirm(&fb,
        "SMS",
        "+49 151 12345678",
        "",
        "Ich komme heute etwas spaeter, alles gut!");
    save_ppm(&fb, outdir, "10_bestaetigung_sms");

    /* 11 -- Bestaetigungs-Dialog: Anruf */
    flux_ui_draw_confirm(&fb,
        "Anruf",
        "+49 151 12345678",
        "",
        "");
    save_ppm(&fb, outdir, "11_bestaetigung_anruf");

    /* 12 -- Text bearbeiten */
    flux_ui_draw_edit_body(&fb,
        "Hallo Max!\n\n"
        "Ich muss dir leider sagen, dass ich heute krank bin "
        "und nicht ins Buero komme.\n\n"
        "Viele Gruesse");
    save_ppm(&fb, outdir, "12_text_bearbeiten");

    /* 13 -- Einstellungen */
    const char *setting_labels[] = {
        "PIN-Code", "SMTP-Server", "SMTP-Port",
        "SMTP-Benutzer", "SMTP-Passwort", "Absender-Adresse", "Cloud-API-Key"
    };
    const char *setting_values[] = {
        "gesetzt", "smtp.icloud.com", "587",
        "ich@icloud.com", "********", "ich@icloud.com", "gesetzt"
    };
    flux_ui_draw_settings(&fb, setting_labels, setting_values, 7);
    save_ppm(&fb, outdir, "13_einstellungen");

    /* 14 -- Dateibrowser (keine Auswahl) */
    const char *names[] = { "..", "Documents", "Pictures", "Music", "Videos", "flux.conf" };
    const char *metas[] = { "Ordner", "Ordner", "Ordner", "Ordner", "Ordner", "1.2 KB" };
    flux_ui_draw_files(&fb, "/home/user", names, metas, 6, 0, -1);
    save_ppm(&fb, outdir, "14_dateien");

    /* 15 -- Dateibrowser mit markierter Datei */
    flux_ui_draw_files(&fb, "/home/user", names, metas, 6, 0, 5);
    save_ppm(&fb, outdir, "15_dateien_ausgewaehlt");

    /* 16 -- Datei-Betrachter */
    flux_ui_draw_file_viewer(&fb, "/home/user/notizen.txt",
        "Einkaufliste:\n"
        "- Milch\n- Brot\n- Kaese\n- Aepfel\n\n"
        "TODO:\n"
        "- Arzt anrufen\n"
        "- Mail an Chef schreiben\n"
        "- Auto in Werkstatt\n",
        0);
    save_ppm(&fb, outdir, "16_datei_betrachter");

    /* 17 -- Benachrichtigungs-Overlay */
    flux_ui_draw_notify(&fb);
    save_ppm(&fb, outdir, "17_benachrichtigungen");

    /* 18 -- Assistenten-Bildschirm mit blauem Farbthema */
    flux_ui_set_accent(0x3B82F6);
    flux_ui_draw_assistant(&fb, "wie spaet ist es?", "", "Es ist 14:35 Uhr.", 0);
    save_ppm(&fb, outdir, "18_assistent_blau");

    /* 19 -- Kalender (Juni 2026, Tag 18 ausgewaehlt) */
    flux_ui_set_accent(0x4FD1C5);
    {
        const char *evs[] = {
            "2026-06-20 14:00 Arzttermin",
            "2026-06-25 09:00 Meeting mit Team",
        };
        flux_ui_draw_calendar(&fb, 2026, 6, 18, 18, evs, 2);
    }
    save_ppm(&fb, outdir, "19_kalender");

    /* 20 -- Kontakte */
    {
        const char *cnames[] = { "Max Mueller", "Anna Schmidt", "Dr. Weber" };
        const char *cdetails[] = {
            "+49 151 12345678, max@example.com",
            "+49 170 9876543, anna@example.com",
            "+49 89 123456, weber@klinik.de",
        };
        flux_ui_draw_contacts(&fb, cnames, cdetails, 3, 0);
    }
    save_ppm(&fb, outdir, "20_kontakte");

    /* 21 -- Fotogalerie (3 Fotos) */
    {
        const char *gnames[] = { "IMG_20260618_143022.ppm", "IMG_20260617_091530.ppm", "IMG_20260615_180240.ppm" };
        const char *gdates[] = { "18.06.2026", "17.06.2026", "15.06.2026" };
        flux_ui_draw_gallery(&fb, gnames, gdates, 3, 0);
    }
    save_ppm(&fb, outdir, "21_fotogalerie");

    /* 22 -- Bild-Betrachter mit KI-Analyse */
    {
        /* Test-Bild: Himmel-Gradient als Pixel-Array */
        const int IW = 480, IH = 380;
        uint32_t *test_img = malloc((size_t)IW * IH * sizeof(uint32_t));
        if (test_img) {
            for (int y = 0; y < IH; y++) {
                for (int x = 0; x < IW; x++) {
                    uint32_t r, g, b;
                    if (y < IH * 2 / 5) {
                        r = 80  + (uint32_t)y * 60 / (IH * 2 / 5);
                        g = 140 + (uint32_t)y * 50 / (IH * 2 / 5);
                        b = 220;
                    } else {
                        r = 80; g = 110; b = 40;
                    }
                    test_img[y * IW + x] = (r << 16) | (g << 8) | b;
                }
            }
            flux_ui_draw_image_viewer(&fb, "IMG_20260618_143022.ppm",
                test_img, IW, IH,
                "Das Bild zeigt einen klaren blauen Himmel\n"
                "mit gruener Wiese. Aufgenommen im Freien,\n"
                "vermutlich Mitteleuropa.",
                0);
            free(test_img);
        }
    }
    save_ppm(&fb, outdir, "22_bild_betrachter");

    /* 23 -- KI-Overlay ueber Datei-Betrachter (Wisch nach rechts) */
    flux_ui_draw_file_viewer(&fb, "/home/user/mietvertrag.txt",
        "Mietvertrag\n\nParagraph 1: Mietbeginn 01.07.2026\n"
        "Paragraph 4: Kuendigungsfrist 3 Monate\n"
        "Paragraph 8: Zutritt mit 24h Vorankuendigung\n",
        0);
    flux_ui_draw_ai_overlay(&fb,
        "Datei: mietvertrag.txt",
        "Was ist ungewoehnlich an diesem Vertrag?",
        "Paragraph 8 enthaelt eine unuebliche Klausel:\n"
        "Der Vermieter darf die Wohnung mit nur 24h\n"
        "Vorankuendigung betreten. Ueblich sind 48h.\n"
        "Rechtlich ist das in Deutschland grenzwertig.");
    save_ppm(&fb, outdir, "23_ki_overlay_datei");

    /* 24 -- KI-Overlay ueber Kalender */
    {
        const char *evs[] = { "2026-06-20 14:00 Arzttermin" };
        flux_ui_draw_calendar(&fb, 2026, 6, 18, 18, evs, 1);
    }
    flux_ui_draw_ai_overlay(&fb,
        "Kalender: Juni 2026",
        "Welche freien Tage habe ich diese Woche?",
        "");
    save_ppm(&fb, outdir, "24_ki_overlay_kalender");

    /* 25 -- Lockscreen mit proaktiver KI-Benachrichtigung */
    {
        /* Simuliere die /tmp/flux_proactive.txt Datei */
        FILE *pf = fopen("/tmp/flux_proactive.txt", "w");
        if (pf) {
            fprintf(pf, "Morgen hat Laura Geburtstag! Soll ich dir helfen eine Nachricht zu schreiben?\n");
            fclose(pf);
        }
        flux_ui_set_accent(0x4FD1C5);
        flux_ui_draw_lock(&fb);
        save_ppm(&fb, outdir, "25_lockscreen_proaktiv");
        unlink("/tmp/flux_proactive.txt");
    }

    /* 26 -- Meeting-Mitschrift (Aufnahme laueft) */
    flux_ui_set_accent(0xF97316); /* Orange fuer Meeting */
    flux_ui_draw_meeting(&fb, 1, 423,
        "Max: Wir muessen das Produkt bis Q3 fertig haben.\n"
        "Anna: Der Backend-Service braucht noch 3 Wochen.\n"
        "Max: Okay, dann priorisieren wir den MVP.",
        "Aufnahme laueft...");
    save_ppm(&fb, outdir, "26_meeting_aufnahme");

    /* 27 -- KI-Gedaechtnis (memory screen) */
    flux_ui_set_accent(0x4FD1C5);
    {
        const char *mem_entries[] = {
            "[2026-06-15 10:22] Meine Frau heisst Laura und hat am 15. Maerz Geburtstag",
            "[2026-06-16 14:05] Ich bin Softwareentwickler und arbeite bei Acme GmbH",
            "[2026-06-17 08:30] Ich trinke morgens keinen Kaffee, nur Tee",
            "[2026-06-18 19:47] Lieblingsrestaurant: Trattoria Bella Vista in der Hauptstrasse",
            "[2026-06-19 11:13] Mein Auto ist ein blauer VW Golf, Kennzeichen M-AB 1234",
        };
        flux_ui_draw_memory(&fb, mem_entries, 5, 0);
    }
    save_ppm(&fb, outdir, "27_ki_gedaechtnis");

    flux_fb_close(&fb);
    printf("\nFertig! PPM -> PNG: convert %s/XX.ppm %s/XX.png\n", outdir, outdir);
    return 0;
}
