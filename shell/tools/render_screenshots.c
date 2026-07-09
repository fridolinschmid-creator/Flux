/* render_screenshots.c -- rendert alle UI-Bildschirme direkt als PNG.
 * Kein /dev/fb0 noetig: flux_fb_open_null() ersetzt das Framebuffer-mmap
 * durch malloc'd Speicher. libpng schreibt die PNG-Dateien.
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
#include <time.h>
#include <sys/stat.h>
#include <unistd.h>
#include <png.h>

static void save_png(const flux_fb_t *fb, const char *dir, const char *name) {
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.png", dir, name);
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); return; }

    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    png_infop info  = png_create_info_struct(png);
    if (setjmp(png_jmpbuf(png))) { fclose(f); return; }

    png_init_io(png, f);
    png_set_IHDR(png, info, (png_uint_32)fb->width, (png_uint_32)fb->height,
                 8, PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png, info);

    uint8_t *row = malloc((size_t)fb->width * 3);
    for (int y = 0; y < fb->height; y++) {
        for (int x = 0; x < fb->width; x++) {
            uint32_t px = fb->back[y * fb->stride_px + x];
            row[x * 3 + 0] = (px >> 16) & 0xff;
            row[x * 3 + 1] = (px >>  8) & 0xff;
            row[x * 3 + 2] =  px        & 0xff;
        }
        png_write_row(png, row);
    }
    free(row);
    png_write_end(png, NULL);
    png_destroy_write_struct(&png, &info);
    fclose(f);
    printf("  %s\n", path);
}

/* ---- Galerie-Thumbnails fuer das Demo-Raster ------------------------- *
 * Schreibt kleine synthetische P6-.ppm-Testbilder und dekodiert sie als
 * quadratische, center-gecroppte TILE-Kacheln -- exakt dieselbe Fuell-/
 * Crop-Logik wie load_ppm_thumb() in main.c, damit der Screenshot das
 * echte Raster zeigt. */
#define GAL_DEMO_DIR "/tmp/flux_gal_demo"

static uint32_t *demo_load_ppm_thumb(const char *path, int tile) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    char magic[4]; int W, H, maxval;
    if (fscanf(f, "%3s %d %d %d", magic, &W, &H, &maxval) != 4 ||
        strcmp(magic, "P6") != 0 || W <= 0 || H <= 0) { fclose(f); return NULL; }
    fgetc(f);
    size_t npx = (size_t)W * H;
    unsigned char *rgb = malloc(npx * 3);
    if (!rgb) { fclose(f); return NULL; }
    if (fread(rgb, 3, npx, f) != npx) { free(rgb); fclose(f); return NULL; }
    fclose(f);
    uint32_t *out = malloc((size_t)tile * tile * sizeof(uint32_t));
    if (!out) { free(rgb); return NULL; }
    float scale = (W < H) ? (float)tile / W : (float)tile / H;
    int crop = (int)(tile / scale);
    int cw = crop > W ? W : crop, ch = crop > H ? H : crop;
    int sx0 = (W - cw) / 2, sy0 = (H - ch) / 2;
    /* Box-Filter (Flaechenmittel) -- identisch zu load_ppm_thumb() in main.c. */
    for (int ty = 0; ty < tile; ty++) {
        int ay0 = sy0 + (int)(ty       * ch / tile);
        int ay1 = sy0 + (int)((ty + 1) * ch / tile);
        if (ay1 <= ay0) ay1 = ay0 + 1; if (ay1 > H) ay1 = H;
        for (int tx = 0; tx < tile; tx++) {
            int ax0 = sx0 + (int)(tx       * cw / tile);
            int ax1 = sx0 + (int)((tx + 1) * cw / tile);
            if (ax1 <= ax0) ax1 = ax0 + 1; if (ax1 > W) ax1 = W;
            uint32_t r = 0, g = 0, b = 0, c = 0;
            for (int sy = ay0; sy < ay1; sy++) {
                const unsigned char *p = rgb + ((size_t)sy * W + ax0) * 3;
                for (int sx = ax0; sx < ax1; sx++) { r += p[0]; g += p[1]; b += p[2]; p += 3; c++; }
            }
            if (!c) c = 1;
            out[ty * tile + tx] = ((r / c) << 16) | ((g / c) << 8) | (b / c);
        }
    }
    free(rgb);
    return out;
}

/* Mini-Cache fuer den Demo-Provider (Name -> Kachel). */
static struct { char name[128]; uint32_t *tile; } g_demo_cache[32];
static int g_demo_cache_n = 0;

static const uint32_t *demo_thumb_provider(const char *name, void *user) {
    (void)user;
    for (int i = 0; i < g_demo_cache_n; i++)
        if (strcmp(g_demo_cache[i].name, name) == 0) return g_demo_cache[i].tile;
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", GAL_DEMO_DIR, name);
    uint32_t *t = demo_load_ppm_thumb(path, FLUX_GALLERY_TILE);
    if (g_demo_cache_n < 32) {
        snprintf(g_demo_cache[g_demo_cache_n].name, 128, "%s", name);
        g_demo_cache[g_demo_cache_n].tile = t;
        g_demo_cache_n++;
    } else free(t);
    return t;
}

/* Schreibt ein synthetisches P6-Testbild mit Farbverlauf/Muster. */
static void demo_write_ppm(const char *path, int W, int H, int variant) {
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            unsigned char r, g, b;
            switch (variant % 6) {
                case 0: r = 60 + 180*x/W; g = 120; b = 200 - 120*y/H; break;       /* blau/violett */
                case 1: r = 220 - 120*y/H; g = 60 + 160*x/W; b = 70; break;        /* gruen */
                case 2: r = 230; g = 120 + 100*y/H; b = 40 + 120*x/W; break;       /* orange/gelb */
                case 3: r = 40 + 80*((x/16+y/16)&1); g = 50; b = 120 + 120*y/H; break; /* schachbrett */
                case 4: r = 200 - 100*x/W; g = 40 + 180*y/H; b = 180 - 80*x/W; break;  /* magenta/cyan */
                default: r = 80 + 120*y/H; g = 200 - 100*x/W; b = 120 + 100*x/W; break; /* tuerkis */
            }
            fputc(r, f); fputc(g, f); fputc(b, f);
        }
    }
    fclose(f);
}

int main(int argc, char *argv[]) {
    const char *outdir = (argc > 1) ? argv[1] : "/tmp/flux_screenshots";
    mkdir(outdir, 0755);

    /* Aktuelle Zeit fuer realistische Demo-Daten (#1, #2) */
    time_t now = time(NULL);
    struct tm tmv;
    localtime_r(&now, &tmv);
    char ai_time_buf[32];
    snprintf(ai_time_buf, sizeof(ai_time_buf), "Es ist %02d:%02d Uhr.",
             tmv.tm_hour, tmv.tm_min);
    int today_day = tmv.tm_mday;

    flux_fb_t fb;
    if (flux_fb_open_null(&fb, 480, 854) != 0) {
        fprintf(stderr, "flux_fb_open_null fehlgeschlagen\n");
        return 1;
    }

    printf("Rendere Flux UI-Screenshots (480x854) nach %s/ ...\n", outdir);

    /* 01 -- Lockscreen */
    flux_ui_set_accent(0x6366F1);
    flux_ui_draw_lock(&fb);
    save_png(&fb, outdir, "01_lockscreen");

    /* 01b -- Lockscreen mit animiertem Wetter-Widget */
    {
        FILE *wf = fopen("/tmp/flux_weather.txt", "w");
        if (wf) { fprintf(wf, "Berlin: Regen, 14°C, 80%% Feuchte, Wind 12 km/h\n"); fclose(wf); }
        flux_ui_draw_lock(&fb);
        save_png(&fb, outdir, "01b_lockscreen_wetter");
        remove("/tmp/flux_weather.txt");
    }

    /* 02 -- PIN-Eingabe (2 von 4 Ziffern) */
    flux_ui_draw_pin(&fb, 2, 0);
    save_png(&fb, outdir, "02_pin");

    /* 03 -- PIN falsch */
    flux_ui_draw_pin(&fb, 0, 1);
    save_png(&fb, outdir, "03_pin_fehler");

    /* 04 -- Assistent leer */
    flux_ui_set_accent(0x6366F1);
    flux_ui_draw_assistant(&fb, "", "", "", 0);
    save_png(&fb, outdir, "04_assistent_leer");

    /* 04b -- Homescreen-Eingangsanimation (Knoepfe poppen auf) */
    flux_ui_set_quick_reveal(2, 50);
    flux_ui_draw_assistant(&fb, "", "", "", 0);
    save_png(&fb, outdir, "04b_home_anim");
    flux_ui_set_quick_reveal(4, 100);

    /* 04c/d/e -- Wetter-Widget: drei Zeitpunkte derselben Regen-Animation.
     * flux_now_ms() liefert die echte Monotonic-Zeit -- die kurzen usleep()
     * zwischen den Aufnahmen sorgen fuer sichtbar unterschiedliche
     * Tropfenpositionen, ohne die Animationslogik selbst testbar machen
     * zu muessen. */
    {
        FILE *wf = fopen("/tmp/flux_weather.txt", "w");
        if (wf) { fprintf(wf, "Berlin: Regen, 14°C, 80%% Feuchte, Wind 12 km/h\n"); fclose(wf); }
        flux_ui_draw_assistant(&fb, "", "", "", 0);
        save_png(&fb, outdir, "04c_wetter_regen_t0");
        usleep(300000);
        flux_ui_draw_assistant(&fb, "", "", "", 0);
        save_png(&fb, outdir, "04d_wetter_regen_t300");
        usleep(300000);
        flux_ui_draw_assistant(&fb, "", "", "", 0);
        save_png(&fb, outdir, "04e_wetter_regen_t600");

        wf = fopen("/tmp/flux_weather.txt", "w");
        if (wf) { fprintf(wf, "Berlin: Sonnig, 22°C, 40%% Feuchte, Wind 5 km/h\n"); fclose(wf); }
        flux_ui_draw_assistant(&fb, "", "", "", 0);
        save_png(&fb, outdir, "04f_wetter_sonnig");

        wf = fopen("/tmp/flux_weather.txt", "w");
        if (wf) { fprintf(wf, "Berlin: Schnee, -2°C, 85%% Feuchte, Wind 8 km/h\n"); fclose(wf); }
        flux_ui_draw_assistant(&fb, "", "", "", 0);
        save_png(&fb, outdir, "04g_wetter_schnee");

        wf = fopen("/tmp/flux_weather.txt", "w");
        if (wf) { fprintf(wf, "Berlin: Gewitter, 18°C, 90%% Feuchte, Wind 30 km/h\n"); fclose(wf); }
        flux_ui_draw_assistant(&fb, "", "", "", 0);
        save_png(&fb, outdir, "04h_wetter_gewitter");

        remove("/tmp/flux_weather.txt");
    }

    /* 05 -- Assistent: tippt Frage (Tastatur sichtbar: grosse Tasten +
     * Vorschlagsleiste mit Autovervollstaendigung "ei" -> eine/einen). */
    flux_ui_set_kbd_open(1);
    flux_ui_draw_assistant(&fb, "", "Schreibe ei", "", 0);
    save_png(&fb, outdir, "05_assistent_tipp");
    flux_ui_set_kbd_open(0);

    /* 06 -- Assistent: denkt nach (Nutzer-Blase + Lade-Blase) */
    flux_ui_draw_assistant(&fb, "schreibe eine E-Mail an Max", "", "", 1);
    save_png(&fb, outdir, "06_assistent_denkt");

    /* 07 -- Assistent: normale Textantwort (aktuelle Uhrzeit) */
    flux_ui_draw_assistant(&fb,
        "wie spät ist es?",
        "",
        ai_time_buf,   /* #1: echte Uhrzeit */
        0);
    save_png(&fb, outdir, "07_assistent_antwort");

    /* 08 -- Assistent: längere Antwort (Indigo-Akzent bleibt) */
    flux_ui_draw_assistant(&fb,
        "erkläre mir kurz wie Flux funktioniert",
        "",
        "Flux ist ein KI-zentriertes Mobil-OS. "
        "Du entsperrst das Gerät und sprichst direkt mit der KI -- "
        "kein App-Grid, kein Suchen. "
        "Einstellungen und Dateien erreichst du über die zwei "
        "Knöpfe oben oder indem du sie einfach eintippst.",
        0);
    save_png(&fb, outdir, "08_assistent_lange_antwort");

    /* 09 -- Bestätigungs-Dialog: E-Mail */
    flux_ui_draw_confirm(&fb,
        "E-Mail",
        "max@example.com",
        "Bin heute krank",
        "Hallo Max!\n\n"
        "Ich muss dir leider sagen, dass ich heute krank bin "
        "und nicht ins Büro komme.\n\n"
        "Viele Grüße");
    save_png(&fb, outdir, "09_bestaetigung_email");

    /* 10 -- Bestätigungs-Dialog: SMS */
    flux_ui_draw_confirm(&fb,
        "SMS",
        "+49 151 12345678",
        "",
        "Ich komme heute etwas später, alles gut!");
    save_png(&fb, outdir, "10_bestaetigung_sms");

    /* 11 -- Bestätigungs-Dialog: Anruf */
    flux_ui_draw_confirm(&fb,
        "Anruf",
        "+49 151 12345678",
        "",
        "");
    save_png(&fb, outdir, "11_bestaetigung_anruf");

    /* 12 -- Text bearbeiten */
    flux_ui_draw_edit_body(&fb,
        "Hallo Max!\n\n"
        "Ich muss dir leider sagen, dass ich heute krank bin "
        "und nicht ins Büro komme.\n\n"
        "Viele Grüße");
    save_png(&fb, outdir, "12_text_bearbeiten");

    /* 13 -- Einstellungen (#13: TTS "Deutsch", #14: Auto-Sperre "60 s") */
    const char *setting_labels[] = {
        "PIN-Code", "KI-Anbieter", "API-Key (Anbieter)", "Modell (Anbieter)",
        "E-Mail Einstellungen", "WLAN", "Web-Suche (SearXNG)",
        "Auto-Sperre", "Sprache (TTS)"
    };
    const char *setting_values[] = {
        "gesetzt", "DeepSeek", "********", "deepseek-chat",
        "ich@icloud.com", "FritzBox 7590", "http://macbook.local:8888",
        "60 s", "Deutsch"
    };
    static const int setting_icons[] = {
        FLUX_SICON_LOCK, FLUX_SICON_AI, FLUX_SICON_KEY, FLUX_SICON_CHIP,
        FLUX_SICON_MAIL, FLUX_SICON_WIFI, FLUX_SICON_SEARCH,
        FLUX_SICON_CLOCK, FLUX_SICON_SPEAKER
    };
    flux_ui_set_setting_icons(setting_icons);
    flux_ui_draw_settings(&fb, setting_labels, setting_values, 9);
    save_png(&fb, outdir, "13_einstellungen");

    /* 13c -- Eingangsanimation der Einstellungen (Zwischenframe) */
    flux_ui_draw_settings_reveal(&fb, setting_labels, setting_values, 9,
                                 5, 60, 40, 1);
    save_png(&fb, outdir, "13c_einstellungen_anim");

    /* 13b -- E-Mail-Einrichtung: App-Passwort-Schritt */
    flux_ui_set_edit_title("App-Passwort");
    flux_ui_draw_edit_body(&fb, "");
    save_png(&fb, outdir, "13b_email_passwort");
    flux_ui_set_edit_title(NULL);

    /* 14 -- Dateibrowser (#12: kein ".." Eintrag) */
    const char *names[] = { "Documents", "Pictures", "Music", "Videos", "flux.conf" };
    const char *metas[] = { "Ordner", "Ordner", "Ordner", "Ordner", "1.2 KB" };
    flux_ui_draw_files(&fb, "/home/user", names, metas, 5, 0, -1);
    save_png(&fb, outdir, "14_dateien");

    /* 15 -- Dateibrowser mit markierter Datei */
    flux_ui_draw_files(&fb, "/home/user", names, metas, 5, 0, 4);
    save_png(&fb, outdir, "15_dateien_ausgewaehlt");

    /* 16 -- Datei-Betrachter */
    flux_ui_draw_file_viewer(&fb, "/home/user/notizen.txt",
        "Einkaufliste:\n"
        "- Milch\n- Brot\n- Käse\n- Äpfel\n\n"
        "TODO:\n"
        "- Arzt anrufen\n"
        "- Mail an Chef schreiben\n"
        "- Auto in Werkstatt\n",
        0);
    save_png(&fb, outdir, "16_datei_betrachter");

    /* 17 -- Benachrichtigungs-Overlay */
    flux_ui_draw_notify(&fb);
    save_png(&fb, outdir, "17_benachrichtigungen");

    /* 18 -- Assistenten-Bildschirm mit blauem Farbthema */
    flux_ui_set_accent(0x3B82F6);
    flux_ui_draw_assistant(&fb, "wie spät ist es?", "", ai_time_buf, 0);
    save_png(&fb, outdir, "18_assistent_blau");
    flux_ui_set_accent(0x6366F1);  /* #8: Akzent zurücksetzen */

    /* 19 -- Kalender (#2: aktueller Tag) */
    flux_ui_set_accent(0x4FD1C5);
    {
        const char *evs[] = {
            "2026-06-12 19:00 Friedensfest",
            "2026-06-12 09:30 Zahnarzt",
            "2026-06-12 14:00 Team-Sync",
            "2026-06-20 14:00 Arzttermin",
            "2026-06-25 09:00 Meeting mit Team",
            "2026-06-25 18:30 Abendessen",
        };
        flux_ui_draw_calendar(&fb, 2026, 6, today_day, 20, evs, 6);
    }
    save_png(&fb, outdir, "19_kalender");

    /* 20 -- Kontakte */
    {
        const char *cnames[] = { "Max Müller", "Anna Schmidt", "Dr. Weber" };
        const char *cdetails[] = {
            "+49 151 12345678, max@example.com",
            "+49 170 9876543, anna@example.com",
            "+49 89 123456, weber@klinik.de",
        };
        flux_ui_draw_contacts(&fb, cnames, cdetails, 3, 0);
    }
    save_png(&fb, outdir, "20_kontakte");
    flux_ui_set_accent(0x6366F1);  /* #8: Akzent zurücksetzen */

    /* 21 -- Fotogalerie: echtes 3-Spalten-Raster mit Thumbnails.
     * Wir erzeugen synthetische P6-.ppm-Testbilder in einem Temp-Ordner,
     * fuettern die Galerie damit und lassen sie ueber den Demo-Provider
     * dekodieren -> der Screenshot zeigt das ECHTE Raster. Eine .jpg-Datei
     * bleibt als ehrliche Platzhalter-Kachel (nicht dekodierbar) drin. */
    {
        mkdir(GAL_DEMO_DIR, 0755);
        /* Namen mit Aufnahmedatum im Dateinamen (IMG_YYYYMMDD_HHMMSS). */
        const char *gnames[] = {
            "IMG_20260618_143022.ppm", "IMG_20260617_091530.ppm",
            "IMG_20260615_180240.ppm", "IMG_20260612_120000.ppm",
            "IMG_20260610_084500.ppm", "IMG_20260605_201500.ppm",
            "IMG_20260528_110000.ppm", "IMG_20260520_154500.ppm",
            "IMG_20260514_093000.ppm", "IMG_20260509_171500.ppm",
            "IMG_20260427_134500.ppm", "IMG_20260612_223000.jpg", /* .jpg -> Platzhalter */
        };
        const char *gdates[] = {
            "18.06.2026", "17.06.2026", "15.06.2026", "12.06.2026",
            "10.06.2026", "05.06.2026", "28.05.2026", "20.05.2026",
            "14.05.2026", "09.05.2026", "27.04.2026", "12.06.2026",
        };
        int gn = (int)(sizeof(gnames) / sizeof(gnames[0]));
        for (int i = 0; i < gn; i++) {
            size_t L = strlen(gnames[i]);
            if (L > 4 && strcmp(gnames[i] + L - 4, ".ppm") == 0) {
                char p[512]; snprintf(p, sizeof(p), "%s/%s", GAL_DEMO_DIR, gnames[i]);
                demo_write_ppm(p, 200 + (i % 3) * 40, 160 + (i % 2) * 60, i);
            }
        }
        /* Filter "Alle": volles randloses Raster mit echten Thumbnails. */
        flux_ui_draw_gallery(&fb, gnames, gdates, gn, -1, 0,
                             FLUX_GAL_ALLE, 0, demo_thumb_provider, NULL);
        save_png(&fb, outdir, "21_fotogalerie");

        /* Temp-Dateien + Cache aufraeumen, damit nichts liegenbleibt. */
        for (int i = 0; i < g_demo_cache_n; i++) free(g_demo_cache[i].tile);
        g_demo_cache_n = 0;
        for (int i = 0; i < gn; i++) {
            char p[512]; snprintf(p, sizeof(p), "%s/%s", GAL_DEMO_DIR, gnames[i]);
            unlink(p);
        }
        rmdir(GAL_DEMO_DIR);
    }

    /* 22 -- Bild-Betrachter mit KI-Analyse (#11: .jpg) */
    {
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
            flux_ui_draw_image_viewer(&fb, "IMG_20260618_143022.jpg",
                test_img, IW, IH,
                "Das Bild zeigt einen klaren blauen Himmel\n"
                "mit grüner Wiese. Aufgenommen im Freien,\n"
                "vermutlich Mitteleuropa.",
                0);
            free(test_img);
        }
    }
    save_png(&fb, outdir, "22_bild_betrachter");

    /* 23 -- KI-Overlay über Datei-Betrachter */
    flux_ui_draw_file_viewer(&fb, "/home/user/mietvertrag.txt",
        "Mietvertrag\n\nParagraph 1: Mietbeginn 01.07.2026\n"
        "Paragraph 4: Kündigungsfrist 3 Monate\n"
        "Paragraph 8: Zutritt mit 24h Vorankündigung\n",
        0);
    flux_ui_draw_ai_overlay(&fb,
        "Datei: mietvertrag.txt",
        "Was ist ungewöhnlich an diesem Vertrag?",
        "Paragraph 8 enthält eine unübliche Klausel: "
        "Der Vermieter darf die Wohnung mit nur 24h Vorankündigung betreten. "
        "Üblich sind 48h. Rechtlich ist das in Deutschland grenzwertig.");
    save_png(&fb, outdir, "23_ki_overlay_datei");

    /* 24 -- KI-Overlay über Kalender (#2: aktueller Tag) */
    {
        const char *evs[] = {
            "2026-06-20 14:00 Arzttermin",
            "2026-06-12 19:00 Friedensfest",
        };
        flux_ui_draw_calendar(&fb, 2026, 6, today_day, 20, evs, 2);
    }
    flux_ui_draw_ai_overlay(&fb,
        "Kalender: Juni 2026",
        "Welche freien Tage habe ich diese Woche?",
        "");
    save_png(&fb, outdir, "24_ki_overlay_kalender");

    /* 25 -- Lockscreen mit proaktiver KI-Benachrichtigung (#3: konsistente Daten) */
    {
        FILE *pf = fopen("/tmp/flux_proactive.txt", "w");
        if (pf) {
            /* Lauras Geburtstag ist am 15. März -- kein Widerspruch mehr */
            fprintf(pf, "Übermorgen: Meeting mit Team um 09:00 – soll ich einen Reminder setzen?\n");
            fclose(pf);
        }
        flux_ui_set_accent(0x4FD1C5);
        flux_ui_draw_lock(&fb);
        save_png(&fb, outdir, "25_lockscreen_proaktiv");
        unlink("/tmp/flux_proactive.txt");
    }
    flux_ui_set_accent(0x6366F1);  /* #8: Akzent zurücksetzen */

    /* 26 -- Meeting-Mitschrift (Aufnahme läuft) */
    flux_ui_set_accent(0xF97316);
    flux_ui_draw_meeting(&fb, 1, 423,
        "Max: Wir müssen das Produkt bis Q3 fertig haben.\n"
        "Anna: Der Backend-Service braucht noch 3 Wochen.\n"
        "Max: Okay, dann priorisieren wir den MVP.",
        "Aufnahme läuft...");
    save_png(&fb, outdir, "26_meeting_aufnahme");
    flux_ui_set_accent(0x6366F1);  /* #8: Akzent zurücksetzen */

    /* 27 -- KI-Gedächtnis (#3: Lauras Geburtstag konsistent am 15. März) */
    flux_ui_set_accent(0x4FD1C5);
    {
        const char *mem_entries[] = {
            "[2026-06-15 10:22] Meine Frau heißt Laura und hat am 15. März Geburtstag",
            "[2026-06-16 14:05] Ich bin Softwareentwickler und arbeite bei Acme GmbH",
            "[2026-06-17 08:30] Ich trinke morgens keinen Kaffee, nur Tee",
            "[2026-06-18 19:47] Lieblingsrestaurant: Trattoria Bella Vista in der Hauptstraße",
            "[2026-06-19 11:13] Mein Auto ist ein blauer VW Golf, Kennzeichen M-AB 1234",
        };
        flux_ui_draw_memory(&fb, mem_entries, 5, 0);
    }
    save_png(&fb, outdir, "27_ki_gedaechtnis");

    /* 28 -- Semantische KI-Suche (#3: Lauras Geburtstag konsistent) */
    {
        const char *results[] = {
            "Gedächtnis: Meine Frau heißt Laura und hat am 15. März Geburtstag",
            "Kalender: 2026-03-15 Lauras Geburtstag",
            "Kontakte: Laura Mustermann, +49 151 12345678",
            "Notizen: Geschenkideen für Laura: Buch, Schmuck, Konzerttickets",
        };
        flux_ui_draw_search(&fb, "Laura", results, 4, 0);
    }
    save_png(&fb, outdir, "28_semantic_search");

    /* 29 -- Spracheingabe-Overlay (animiert, Aufnahme läuft 7 s) */
    flux_ui_draw_voice_overlay(&fb, 7, 6);
    save_png(&fb, outdir, "29_voice_overlay");
    flux_ui_set_accent(0x6366F1);  /* #8: Akzent zurücksetzen */

    /* 30 -- Ehrlicher Cloud-Hinweis (kein API-Key konfiguriert) */
    flux_ui_draw_assistant(&fb, "wer bist du?", "",
        "Kein Cloud-Zugang konfiguriert. Trage in den Einstellungen einen "
        "API-Key für den gewählten Anbieter ein (oder wähle einen anderen "
        "Anbieter).", 0);
    save_png(&fb, outdir, "30_cloud_fallback");

    /* 31 -- WLAN-Auswahl */
    {
        const char *wnames[] = { "FritzBox 7590", "Eduroam", "Cafe Free WiFi" };
        const char *wmetas[] = { "Signal 100% - gesichert", "Signal 90% - gesichert",
                                 "Signal 56% - offen" };
        flux_ui_draw_wifi(&fb, "FritzBox 7590", wnames, wmetas, 3, 0, 0);
    }
    save_png(&fb, outdir, "31_wlan");

    /* 32-34 -- animierte Aktions-Symbole (je ein Frame) */
    flux_ui_draw_action_anim(&fb, FLUX_ANIM_MAIL, 10, "max@example.com");
    save_png(&fb, outdir, "32_anim_mail");
    flux_ui_draw_action_anim(&fb, FLUX_ANIM_CALL, 6, "Stefan Schmied");
    save_png(&fb, outdir, "33_anim_anruf");
    flux_ui_draw_action_anim(&fb, FLUX_ANIM_SCAN, 7, "WLAN-Suche");
    save_png(&fb, outdir, "34_anim_wlan_suche");

    /* ---- Neue Features (Batch 5) ---------------------------------------- */

    /* 35 -- Lockscreen mit Notification-Badge + Alarm-Infochip */
    {
        /* Mock: 3 ungelesene Benachrichtigungen */
        FILE *nf = fopen("/tmp/flux_notifications.txt", "w");
        if (nf) {
            fprintf(nf, "COUNT:3\n");
            fprintf(nf, "[2026-06-23 08:45] Wecker: 07:30 Aufstehen\n");
            fprintf(nf, "[2026-06-23 08:00] Termin: Team-Meeting um 09:00\n");
            fprintf(nf, "[2026-06-23 07:55] Batterie bei 18%% -- bitte laden.\n");
            fclose(nf);
        }
        /* Mock: Alarm in ~1 Stunde */
        FILE *af = fopen("/tmp/flux_alarms.txt", "w");
        if (af) {
            time_t soon = time(NULL) + 3600;
            struct tm st; localtime_r(&soon, &st);
            fprintf(af, "%04d-%02d-%02d %02d:%02d Mittags-Pause\n",
                    st.tm_year+1900, st.tm_mon+1, st.tm_mday,
                    st.tm_hour, st.tm_min);
            fclose(af);
        }
        flux_ui_set_accent(0x6366F1);
        flux_ui_draw_lock(&fb);
        save_png(&fb, outdir, "35_lockscreen_badge_chip");
        unlink("/tmp/flux_notifications.txt");
        unlink("/tmp/flux_alarms.txt");
    }

    /* 36 -- Lockscreen mit Kalender-Infochip (kein Alarm, aber Termin heute) */
    {
        FILE *cf = fopen("/etc/flux/calendar.txt", "w");
        if (cf) {
            time_t soon = time(NULL) + 7200;
            struct tm st; localtime_r(&soon, &st);
            fprintf(cf, "%04d-%02d-%02d %02d:%02d Team-Retrospektive\n",
                    st.tm_year+1900, st.tm_mon+1, st.tm_mday,
                    st.tm_hour, st.tm_min);
            fclose(cf);
        }
        flux_ui_draw_lock(&fb);
        save_png(&fb, outdir, "36_lockscreen_kalender_chip");
        unlink("/etc/flux/calendar.txt");
    }

    /* 37 -- Vollbild-Wecker (Alarm klingelt) */
    flux_ui_draw_alarm(&fb, "07:30 Aufstehen");
    save_png(&fb, outdir, "37_alarm_klingelt");

    /* 38 -- Wecker mit langem Label (zweizeilig) */
    flux_ui_draw_alarm(&fb, "09:00 Team-Meeting mit Max, Anna und Stefan");
    save_png(&fb, outdir, "38_alarm_lang");

    /* 38b -- Vollbild-Anruf: annehmen (gruen) / auflegen (rot) */
    flux_ui_draw_call(&fb, "Stefan Schmied", "+49 151 12345678", 0, 0);
    save_png(&fb, outdir, "38b_anruf");

    /* 38c -- Anruf verbunden: KI nimmt auf (Timer + Rec-Indikator) */
    flux_ui_draw_call(&fb, "Stefan Schmied", "+49 151 12345678", 1, 125);
    save_png(&fb, outdir, "38c_anruf_verbunden");

    /* 39 -- Nutzungsgewohnheiten (mehrere Eintraege, reales Format aus habits.c) */
    {
        const char *habits[] = {
            "[2026-06-23 10:42] assistant | wie spät ist es?",
            "[2026-06-23 10:15] assistant | öffne den Kalender",
            "[2026-06-23 09:33] assistant | schreibe eine Mail an Max",
            "[2026-06-23 09:00] assistant | zeige Einstellungen",
            "[2026-06-23 08:47] assistant | stell einen Wecker auf 09:00",
            "[2026-06-22 22:15] assistant | gute Nacht, schlaf schön",
            "[2026-06-22 20:02] assistant | öffne meine Dateien",
            "[2026-06-22 19:30] assistant | was gibts heute Abend zum Essen?",
        };
        flux_ui_draw_habits(&fb, habits, 8, 0);
    }
    save_png(&fb, outdir, "39_gewohnheiten");

    /* 40 -- Gewohnheiten (leer -- erster Start) */
    flux_ui_draw_habits(&fb, NULL, 0, 0);
    save_png(&fb, outdir, "40_gewohnheiten_leer");

    flux_fb_close(&fb);
    printf("\nFertig! %d Screenshots in %s/\n", 40, outdir);
    return 0;
}
