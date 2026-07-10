/* webview.c -- WPE WebKit-Anbindung. Siehe README.md: dieser Teil ist
 * in dieser Sitzung NICHT kompiliert/getestet (kein wpewebkit auf dem
 * Build-Host verfuegbar). Jede WPE/libwpe-spezifische Stelle hat einen
 * VERIFY-Kommentar -- vor dem ersten echten Build auf dem Pi 5
 * gegen die echten Header (`pkg-config --cflags wpe-webkit-2.0 wpe-2.0`)
 * gegenpruefen.
 *
 * Ohne FLUXWEB_USE_WPE (Standard, `make` ohne WPE=1) ist dies ein
 * ehrlicher Stub: er meldet "nicht verfuegbar" statt etwas vorzutaeuschen
 * -- passt zum Ehrlichkeitsprinzip des Projekts (siehe CLAUDE.md). Damit
 * ist wenigstens main.c (Socket-Protokoll) unabhaengig von WPE testbar.
 */
#include "webview.h"

#include <stdio.h>
#include <string.h>

#ifndef FLUXWEB_USE_WPE

static webview_frame_cb s_stub_cb;
static void *s_stub_user;

/* Meldet sofort einen Fehler statt den wartenden Client fuer immer
 * haengen zu lassen (main.c antwortet dem Client erst, wenn der
 * frame_cb feuert -- ohne diesen Aufruf wuerde im Stub-Build nie eine
 * Antwort kommen). Ehrlich fehlschlagen statt stillzuhalten. */
static void stub_fail(const char *why) {
    if (s_stub_cb) s_stub_cb(why, -1, -1, s_stub_user);
}

int webview_init(webview_frame_cb on_frame, void *user) {
    s_stub_cb = on_frame;
    s_stub_user = user;
    fprintf(stderr,
            "fluxweb: ohne WPE gebaut (kein WPE=1 beim make) -- "
            "kein echtes Rendering, nur Protokoll-/Prozessgeruest.\n");
    return 0;
}
void webview_set_size(int w, int h) { (void)w; (void)h; }
void webview_load(const char *url) { (void)url; stub_fail("fluxweb ohne WPE gebaut -- kein Rendering moeglich"); }
void webview_click(int x, int y) { (void)x; (void)y; stub_fail("fluxweb ohne WPE gebaut -- kein Rendering moeglich"); }
void webview_scroll(int dy) { (void)dy; stub_fail("fluxweb ohne WPE gebaut -- kein Rendering moeglich"); }

#else /* FLUXWEB_USE_WPE -- ab hier NICHT verifiziert, siehe Datei-Kopf */

#include <wpe/webkit.h>
#include <wpe/wpe.h>
#include <wpe/fdo.h>
#include <wpe/fdo-egl.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <stdlib.h>

#include "../../common/flux_webview_protocol.h"

static WebKitWebView *s_view;
static struct wpe_view_backend_exportable_fdo *s_exportable;
static webview_frame_cb s_frame_cb;
static void *s_frame_cb_user;
static int s_vw = 480, s_vh = 854;
static EGLDisplay s_egl_dpy;
static EGLContext s_egl_ctx;

/* Wird von libwpe aufgerufen, sobald ein fertiges EGL-Image (der
 * gerenderte Frame) bereitsteht. VERIFY: die genaue Signatur und
 * ob "egl_image" hier wirklich ein EGLImageKHR ist (statt z.B. schon
 * ein dma-buf fd), haengt von der wpebackend-fdo-Version ab -- in
 * neueren Versionen gibt es wpe_view_backend_exportable_fdo_egl_dispatch_frame_complete
 * mit leicht anderem Fluss (export_dmabuf statt export_egl_image). */
static void on_export_egl_image(void *data, EGLImageKHR egl_image) {
    (void)data;
    /* VERIFY: EGL-Image -> CPU-lesbare RGBA-Bytes. Ohne einen
     * bestehenden GL-Kontext/FBO-Setup ist das der unsicherste Teil
     * dieser Datei -- ueblicher Weg: Image an eine Textur binden
     * (glEGLImageTargetTexture2DOES), in ein FBO rendern, mit
     * glReadPixels() auslesen. Hier nur als Platzhalter-Ablauf
     * skizziert, NICHT lauffaehig ohne echtes EGL/GL-Kontext-Setup
     * (eglCreateContext/eglMakeCurrent fehlen komplett). */
    size_t frame_bytes = (size_t)s_vw * s_vh * 4;
    unsigned char *pixels = malloc(frame_bytes);
    if (!pixels) {
        wpe_view_backend_exportable_fdo_egl_dispatch_frame_complete(s_exportable);
        return;
    }
    /* VERIFY: glReadPixels() setzt einen aktuellen GL-Kontext mit
     * dem Image als Renderziel voraus -- siehe Kommentar oben. */
    glReadPixels(0, 0, s_vw, s_vh, GL_RGBA, GL_UNSIGNED_BYTE, pixels);

    char path[512];
    snprintf(path, sizeof(path), "%s/frame.rgba", FLUX_WEBVIEW_FRAME_DIR);
    FILE *f = fopen(path, "wb");
    if (f) { fwrite(pixels, 1, frame_bytes, f); fclose(f); }
    free(pixels);

    if (s_frame_cb) s_frame_cb(path, s_vw, s_vh, s_frame_cb_user);

    /* VERIFY: erst NACH dem Auslesen freigeben, sonst rendert WPE
     * schon den naechsten Frame in denselben Speicher. */
    wpe_view_backend_exportable_fdo_egl_dispatch_frame_complete(s_exportable);
}

int webview_init(webview_frame_cb on_frame, void *user) {
    s_frame_cb = on_frame;
    s_frame_cb_user = user;

    /* VERIFY: EGL-Display/-Kontext-Aufbau ohne Fenstersystem.
     * Auf dem Pi 5 typischerweise ueber einen GBM-Device-Node
     * (/dev/dri/renderD128) + eglGetPlatformDisplay(EGL_PLATFORM_GBM_KHR, ...)
     * statt eglGetDisplay(EGL_DEFAULT_DISPLAY) -- Letzteres funktioniert
     * ohne X11/Wayland-Compositor vermutlich NICHT. Das ist der zweite
     * grosse Unsicherheitsblock neben on_export_egl_image(). */
    s_egl_dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (s_egl_dpy == EGL_NO_DISPLAY || !eglInitialize(s_egl_dpy, NULL, NULL)) {
        fprintf(stderr, "fluxweb: eglInitialize fehlgeschlagen (VERIFY: GBM-Platform noetig?)\n");
        return -1;
    }

    wpe_loader_init("libWPEBackend-fdo-1.0.so"); /* VERIFY: exakter Soname */

    s_exportable = wpe_view_backend_exportable_fdo_egl_create(
        &(struct wpe_view_backend_exportable_fdo_egl_client){
            .export_egl_image = on_export_egl_image,
        },
        NULL, s_vw, s_vh); /* VERIFY: Struct-/Callback-Feldname je nach Version */

    struct wpe_view_backend *backend =
        wpe_view_backend_exportable_fdo_get_view_backend(s_exportable);

    WebKitWebViewBackend *wk_backend =
        webkit_web_view_backend_new(backend, NULL, NULL); /* VERIFY */

    s_view = WEBKIT_WEB_VIEW(g_object_new(WEBKIT_TYPE_WEB_VIEW,
                                          "backend", wk_backend, NULL)); /* VERIFY */
    return 0;
}

void webview_set_size(int w, int h) {
    s_vw = w; s_vh = h;
    if (s_exportable)
        wpe_view_backend_exportable_fdo_egl_resize(s_exportable, w, h); /* VERIFY */
}

void webview_load(const char *url) {
    if (!s_view) return;
    webkit_web_view_load_uri(s_view, url); /* wahrscheinlich korrekt -- Standard-WebKit-API */
}

void webview_click(int x, int y) {
    /* VERIFY: Touch/Maus-Events fuer WPE laufen ueber
     * wpe_view_backend_dispatch_pointer_event/-touch_event mit einem
     * struct wpe_input_pointer_event -- exakte Struct-Felder je nach
     * libwpe-Version pruefen. Hier nur die Absicht dokumentiert.
     * Bis dahin sofort ehrlich fehlschlagen statt den Client haengen
     * zu lassen (kein frame_cb-Aufruf ohne diesen Fallback -- siehe
     * webview.h). */
    (void)x; (void)y;
    fprintf(stderr, "fluxweb: webview_click() VERIFY -- noch nicht implementiert\n");
    if (s_frame_cb) s_frame_cb("Klicken noch nicht implementiert (VERIFY in webview.c)", -1, -1, s_frame_cb_user);
}

void webview_scroll(int dy) {
    /* VERIFY: analog zu webview_click(), ueber ein Scroll-/Axis-Event. */
    (void)dy;
    fprintf(stderr, "fluxweb: webview_scroll() VERIFY -- noch nicht implementiert\n");
    if (s_frame_cb) s_frame_cb("Scrollen noch nicht implementiert (VERIFY in webview.c)", -1, -1, s_frame_cb_user);
}

#endif /* FLUXWEB_USE_WPE */
