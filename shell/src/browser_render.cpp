/* browser_render.cpp -- siehe browser_render.h.
 *
 * FluxContainer implementiert litehtml::document_container: sie bekommt
 * von litehtml Zeichenbefehle (Text, Rechtecke, Bilder, Rahmen) und setzt
 * sie in Aufrufe der bestehenden Framebuffer-Primitive (fb.c) um. Kein
 * neues Grafiksystem -- derselbe Backbuffer, dieselben stb_truetype-
 * Glyphen wie ueberall sonst im Shell.
 *
 * Vereinfachungen (bewusst, fuer einen ersten funktionierenden Stand):
 *  - Rahmen werden als vier flache Rechtecke gezeichnet, keine Radien/
 *    Gehrungen an den Ecken.
 *  - Gradienten werden durch eine Flaechenfarbe angenaehert (erster
 *    Farb-Stop) statt echter Verlaeufe.
 *  - Hintergrundbilder werden nicht gekachelt (nur "no-repeat"-Fall).
 *  - Clipping ist ein einfacher rechteckiger Stack (keine Rundungen).
 * Alles andere (Text-Layout, Boxen/Flexbox, Farben, echte Fotos) ist
 * echtes litehtml-Rendering.
 */
#include "browser_render.h"
#include "litehtml/include/litehtml.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#include "stb_image.h"

#include <curl/curl.h>
#include <string>
#include <vector>
#include <map>
#include <cstring>
#include <cstdio>

using namespace litehtml;

/* ---- Netzwerk (eigener, von fluxaid unabhaengiger Pfad -- siehe .h) --- */

struct FetchBuf { std::string data; };

static size_t fetch_write_cb(void *ptr, size_t size, size_t nmemb, void *ud) {
    FetchBuf *b = (FetchBuf *)ud;
    b->data.append((const char *)ptr, size * nmemb);
    return size * nmemb;
}

static bool http_get(const std::string &url, std::string &out, std::string &effective_url) {
    CURL *curl = curl_easy_init();
    if (!curl) return false;
    FetchBuf buf;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "http,https");
    curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "http,https");
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(curl, CURLOPT_MAXFILESIZE_LARGE, (curl_off_t)(8 * 1024 * 1024));
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, fetch_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 12L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0 (compatible; FluxOS-Browser/1.0)");
    CURLcode res = curl_easy_perform(curl);
    long http = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http);
    char *eff = nullptr;
    curl_easy_getinfo(curl, CURLINFO_EFFECTIVE_URL, &eff);
    if (eff) effective_url = eff;
    curl_easy_cleanup(curl);
    if (res != CURLE_OK || http >= 400) return false;
    out = std::move(buf.data);
    return true;
}

/* Minimaler URL-Resolver (relativ -> absolut), analog zu
 * fluxai/src/browser.c resolve_href() -- eigenstaendige Kopie, da dieser
 * Renderpfad komplett getrennt von fluxaid laeuft (siehe .h). */
static std::string resolve_url(const std::string &base, const std::string &href) {
    if (href.empty() || href[0] == '#') return "";
    if (href.rfind("javascript:", 0) == 0 || href.rfind("mailto:", 0) == 0 ||
        href.rfind("tel:", 0) == 0 || href.rfind("data:", 0) == 0)
        return "";
    if (href.rfind("http://", 0) == 0 || href.rfind("https://", 0) == 0)
        return href;
    std::string scheme_host, dir;
    size_t p = base.find("://");
    if (p == std::string::npos) return "";
    size_t path_start = base.find('/', p + 3);
    scheme_host = (path_start == std::string::npos) ? base : base.substr(0, path_start);
    if (href.rfind("//", 0) == 0) {
        size_t se = base.find(':');
        return base.substr(0, se) + ":" + href;
    }
    if (href[0] == '/') {
        std::string url = scheme_host + href;
        size_t hash = url.find('#');
        return hash == std::string::npos ? url : url.substr(0, hash);
    }
    if (path_start == std::string::npos) dir = "/";
    else {
        std::string path = base.substr(path_start);
        size_t slash = path.find_last_of('/');
        dir = (slash == std::string::npos) ? "/" : path.substr(0, slash + 1);
    }
    std::string url = scheme_host + dir + href;
    size_t hash = url.find('#');
    return hash == std::string::npos ? url : url.substr(0, hash);
}

/* ---- Bild-Cache (Fetch + stb_image-Dekodierung) ----------------------- */

struct DecImage {
    std::vector<uint8_t> rgba; /* leer = fehlgeschlagen/noch nicht geladen */
    int w = 0, h = 0;
};

/* ---- document_container -----------------------------------------------
 * hdc ist immer ein (uint_ptr)(flux_fb_t*) -- ein einziges Zeichenziel,
 * kein Offscreen-Layering (litehtml uebernimmt die z-Reihenfolge selbst
 * durch die Aufrufreihenfolge). */
class FluxContainer : public document_container {
public:
    int m_vw = 480;
    int m_vh = 854; /* fuer vh-Einheiten/Media-Queries -- die tatsaechliche
                     * Dokumenthoehe ergibt sich unabhaengig davon aus dem
                     * Inhaltsfluss (document::height() nach render()). Ein
                     * zu grosser Platzhalterwert liess z.B. height:100%-
                     * Ketten auf absurde Pixelwerte explodieren. */
    std::string m_base_url;
    std::map<std::string, DecImage> m_images;
    std::vector<position> m_clip_stack;

    static flux_fb_t *fb_of(uint_ptr hdc) { return (flux_fb_t *)hdc; }

    bool clip_rect_ip(int &x, int &y, int &w, int &h) const {
        if (m_clip_stack.empty()) return w > 0 && h > 0;
        const position &c = m_clip_stack.back();
        int cx0 = (int)(float)c.x, cy0 = (int)(float)c.y;
        int cx1 = cx0 + (int)(float)c.width, cy1 = cy0 + (int)(float)c.height;
        int x1 = x + w, y1 = y + h;
        if (x < cx0) x = cx0;
        if (y < cy0) y = cy0;
        if (x1 > cx1) x1 = cx1;
        if (y1 > cy1) y1 = cy1;
        w = x1 - x; h = y1 - y;
        return w > 0 && h > 0;
    }

    bool y_visible(int y) const {
        if (m_clip_stack.empty()) return true;
        const position &c = m_clip_stack.back();
        return y >= (int)(float)c.y && y < (int)(float)c.y + (int)(float)c.height;
    }

    /* ---- Schrift ------------------------------------------------------- */
    uint_ptr create_font(const font_description &descr, const document *, font_metrics *fm) override {
        int px = (int)((float)descr.size + 0.5f);
        if (px < 6) px = 6;
        if (fm) {
            int asc, desc, gap;
            flux_fb_font_metrics_px(px, &asc, &desc, &gap);
            fm->font_size = (pixel_t)px;
            fm->ascent = (pixel_t)asc;
            fm->descent = (pixel_t)desc;
            fm->height = (pixel_t)(asc + desc);
            fm->x_height = (pixel_t)(px / 2);
            fm->ch_width = (pixel_t)flux_fb_text_width_px("0", px);
            fm->draw_spaces = true;
        }
        return (uint_ptr)(intptr_t)px;
    }
    void delete_font(uint_ptr) override {}
    pixel_t text_width(const char *text, uint_ptr hFont) override {
        return (pixel_t)flux_fb_text_width_px(text, (int)(intptr_t)hFont);
    }
    void draw_text(uint_ptr hdc, const char *text, uint_ptr hFont, web_color color,
                   const position &pos) override {
        flux_fb_t *fb = fb_of(hdc);
        int y = (int)(float)pos.y;
        if (!y_visible(y) && !y_visible(y + (int)(float)pos.height)) return;
        uint32_t rgb = ((uint32_t)color.red << 16) | ((uint32_t)color.green << 8) | color.blue;
        flux_fb_text_px(fb, (int)(float)pos.x, y, text, rgb, (int)(intptr_t)hFont);
    }
    pixel_t pt_to_px(float pt) const override { return (pixel_t)(pt * 96.0f / 72.0f); }
    pixel_t get_default_font_size() const override { return (pixel_t)16; }
    const char *get_default_font_name() const override { return "sans-serif"; }

    /* ---- Fuellungen/Rahmen --------------------------------------------- */
    void draw_solid_fill(uint_ptr hdc, const background_layer &layer, const web_color &color) override {
        if (color.alpha == 0) return;
        int x = (int)(float)layer.border_box.x, y = (int)(float)layer.border_box.y;
        int w = (int)(float)layer.border_box.width, h = (int)(float)layer.border_box.height;
        if (!clip_rect_ip(x, y, w, h)) return;
        uint32_t rgb = ((uint32_t)color.red << 16) | ((uint32_t)color.green << 8) | color.blue;
        if (color.alpha < 255) flux_fb_blend_rect(fb_of(hdc), x, y, w, h, rgb, color.alpha);
        else flux_fb_fill_rect(fb_of(hdc), x, y, w, h, rgb);
    }
    /* Gradienten: Naeherung durch den ersten Farb-Stop als Flaechenfarbe
     * statt eines echten Verlaufs (siehe Datei-Kommentar). */
    void draw_linear_gradient(uint_ptr hdc, const background_layer &layer,
                              const background_layer::linear_gradient &g) override {
        if (!g.color_points.empty()) draw_solid_fill(hdc, layer, g.color_points.front().color);
    }
    void draw_radial_gradient(uint_ptr hdc, const background_layer &layer,
                              const background_layer::radial_gradient &g) override {
        if (!g.color_points.empty()) draw_solid_fill(hdc, layer, g.color_points.front().color);
    }
    void draw_conic_gradient(uint_ptr hdc, const background_layer &layer,
                             const background_layer::conic_gradient &g) override {
        if (!g.color_points.empty()) draw_solid_fill(hdc, layer, g.color_points.front().color);
    }
    void draw_borders(uint_ptr hdc, const borders &b, const position &pos, bool) override {
        flux_fb_t *fb = fb_of(hdc);
        int x = (int)(float)pos.x, y = (int)(float)pos.y;
        int w = (int)(float)pos.width, h = (int)(float)pos.height;
        int tw = (int)(float)b.top.width, bw = (int)(float)b.bottom.width;
        int lw = (int)(float)b.left.width, rw = (int)(float)b.right.width;
        auto col = [](const web_color &c) {
            return ((uint32_t)c.red << 16) | ((uint32_t)c.green << 8) | c.blue;
        };
        int cx, cy, cw, ch;
        if (tw > 0 && b.top.style != border_style_none) {
            cx = x; cy = y; cw = w; ch = tw;
            if (clip_rect_ip(cx, cy, cw, ch)) flux_fb_fill_rect(fb, cx, cy, cw, ch, col(b.top.color));
        }
        if (bw > 0 && b.bottom.style != border_style_none) {
            cx = x; cy = y + h - bw; cw = w; ch = bw;
            if (clip_rect_ip(cx, cy, cw, ch)) flux_fb_fill_rect(fb, cx, cy, cw, ch, col(b.bottom.color));
        }
        if (lw > 0 && b.left.style != border_style_none) {
            cx = x; cy = y; cw = lw; ch = h;
            if (clip_rect_ip(cx, cy, cw, ch)) flux_fb_fill_rect(fb, cx, cy, cw, ch, col(b.left.color));
        }
        if (rw > 0 && b.right.style != border_style_none) {
            cx = x + w - rw; cy = y; cw = rw; ch = h;
            if (clip_rect_ip(cx, cy, cw, ch)) flux_fb_fill_rect(fb, cx, cy, cw, ch, col(b.right.color));
        }
    }
    void draw_list_marker(uint_ptr hdc, const list_marker &marker) override {
        flux_fb_t *fb = fb_of(hdc);
        int x = (int)(float)marker.pos.x, y = (int)(float)marker.pos.y;
        int w = (int)(float)marker.pos.width, h = (int)(float)marker.pos.height;
        uint32_t rgb = ((uint32_t)marker.color.red << 16) | ((uint32_t)marker.color.green << 8) | marker.color.blue;
        int r = (w < h ? w : h) / 2;
        if (r < 1) r = 1;
        if (!y_visible(y)) return;
        switch (marker.marker_type) {
        case list_style_type_disc:
            flux_fb_fill_circle(fb, x + w / 2, y + h / 2, r, rgb);
            break;
        case list_style_type_circle:
            flux_fb_draw_ring(fb, x + w / 2, y + h / 2, r, 1, rgb);
            break;
        case list_style_type_square:
            flux_fb_fill_rect(fb, x, y, w, h, rgb);
            break;
        default:
            break;
        }
    }

    /* ---- Bilder ---------------------------------------------------------
     * Kein Kacheln (background_repeat wird ignoriert, immer als
     * "no-repeat" behandelt) -- deckt <img> und die meisten einfachen
     * CSS-Hintergrundbilder ab, keine gekachelten Texturen. */
    std::string make_url(const char *src, const char *baseurl) {
        std::string base = (baseurl && *baseurl) ? baseurl : m_base_url;
        return resolve_url(base, src);
    }
    void load_image(const char *src, const char *baseurl, bool) override {
        std::string url = make_url(src, baseurl);
        if (url.empty() || m_images.count(url)) return;
        DecImage img; /* leer bleibt = Fehler, aber Eintrag verhindert Mehrfach-Fetch */
        std::string bytes, eff;
        if (http_get(url, bytes, eff)) {
            int w, h, comp;
            unsigned char *px = stbi_load_from_memory(
                (const unsigned char *)bytes.data(), (int)bytes.size(), &w, &h, &comp, 4);
            if (px) {
                img.w = w; img.h = h;
                img.rgba.assign(px, px + (size_t)w * h * 4);
                stbi_image_free(px);
            }
        }
        m_images[url] = std::move(img);
    }
    void get_image_size(const char *src, const char *baseurl, size &sz) override {
        std::string url = make_url(src, baseurl);
        auto it = m_images.find(url);
        if (it != m_images.end()) { sz.width = (pixel_t)it->second.w; sz.height = (pixel_t)it->second.h; }
        else { sz.width = 0; sz.height = 0; }
    }
    void draw_image(uint_ptr hdc, const background_layer &layer, const std::string &url_in,
                    const std::string &base_url) override {
        std::string url = make_url(url_in.c_str(), base_url.c_str());
        auto it = m_images.find(url);
        if (it == m_images.end() || it->second.w == 0) return;
        DecImage &img = it->second;

        int dx = (int)(float)layer.origin_box.x, dy = (int)(float)layer.origin_box.y;
        int dw = (int)(float)layer.origin_box.width, dh = (int)(float)layer.origin_box.height;
        if (dw <= 0 || dh <= 0) return;
        if (!y_visible(dy) && !y_visible(dy + dh)) return;

        /* Einfache Nearest-Neighbour-Skalierung auf einen Scratch-Puffer in
         * Zielgroesse, dann als RGBA blitten (fb.c uebernimmt Clipping an
         * den physischen Bildschirmgrenzen bereits selbst). */
        std::vector<uint8_t> scaled((size_t)dw * dh * 4);
        for (int yy = 0; yy < dh; yy++) {
            int sy = yy * img.h / dh;
            for (int xx = 0; xx < dw; xx++) {
                int sx = xx * img.w / dw;
                const uint8_t *sp = &img.rgba[((size_t)sy * img.w + sx) * 4];
                uint8_t *dp = &scaled[((size_t)yy * dw + xx) * 4];
                dp[0] = sp[0]; dp[1] = sp[1]; dp[2] = sp[2]; dp[3] = sp[3];
            }
        }
        int cx = dx, cy = dy, cw = dw, ch = dh;
        if (!clip_rect_ip(cx, cy, cw, ch)) return;
        /* clip_rect_ip kann verkleinern -- fb_blit_rgba erwartet aber den
         * kompletten Quellpuffer mit Ursprung (dx,dy); Verschiebung durch
         * erneutes Blitten am unverkuerzten Ursprung, fb.c faengt
         * Bildschirm-Out-of-Bounds selbst ab. Bei aktivem engeren Clip
         * (z.B. overflow:hidden) kann es daher leicht ueber den Clip
         * hinaus zeichnen -- akzeptierte Vereinfachung. */
        flux_fb_blit_rgba(fb_of(hdc), dx, dy, dw, dh, scaled.data());
    }

    /* ---- Sonstiges (Defaults, wie test_container) ----------------------- */
    std::string m_caption;
    void set_caption(const char *caption) override { m_caption = caption ? caption : ""; }
    void set_base_url(const char *base_url) override { if (base_url) m_base_url = base_url; }
    void link(const std::shared_ptr<document> &, const element::ptr &) override {}
    void on_anchor_click(const char *, const element::ptr &) override {}
    void on_mouse_event(const element::ptr &, mouse_event) override {}
    void set_cursor(const char *) override {}
    void transform_text(std::string &, text_transform) override {}
    void import_css(std::string &text, const std::string &url, std::string &baseurl) override {
        std::string full = resolve_url(baseurl.empty() ? m_base_url : baseurl, url);
        if (full.empty()) return;
        std::string eff;
        http_get(full, text, eff);
        baseurl = full;
    }
    void set_clip(const position &pos, const border_radiuses &) override {
        position np = pos;
        if (!m_clip_stack.empty()) {
            const position &top = m_clip_stack.back();
            pixel_t x0 = std::max(pos.x, top.x), y0 = std::max(pos.y, top.y);
            pixel_t x1 = std::min(pos.right(), top.right()), y1 = std::min(pos.bottom(), top.bottom());
            np = position(x0, y0, x1 - x0, y1 - y0);
        }
        m_clip_stack.push_back(np);
    }
    void del_clip() override { if (!m_clip_stack.empty()) m_clip_stack.pop_back(); }
    void get_viewport(position &client) const override { client = position(0, 0, (pixel_t)m_vw, (pixel_t)m_vh); }
    element::ptr create_element(const char *, const string_map &, const std::shared_ptr<document> &) override {
        return nullptr;
    }
    void get_media_features(media_features &media) const override {
        position client; get_viewport(client);
        media.type = media_type_screen;
        media.width = client.width; media.height = client.height;
        media.color = 8; media.monochrome = 0; media.color_index = 0; media.resolution = 96;
    }
    void get_language(std::string &language, std::string &culture) const override {
        language = "de"; culture = "de-DE";
    }
};

/* ---- Globaler Zustand (ein Prozess, ein sichtbarer Browser-Screen) ---- */

struct LinkInfo { std::string href; position pos; };

static FluxContainer          g_container;
static std::shared_ptr<document> g_doc;
static std::vector<LinkInfo>  g_links;

static void collect_links(const element::ptr &root, const std::string &base) {
    g_links.clear();
    if (!root) return;
    elements_list anchors = root->select_all("a");
    for (auto &a : anchors) {
        const char *href = a->get_attr("href");
        if (!href || !*href) continue;
        std::string resolved = resolve_url(base, href);
        if (resolved.empty()) continue;
        LinkInfo li; li.href = resolved; li.pos = a->get_placement();
        g_links.push_back(std::move(li));
    }
}

static int render_url(const std::string &url, int viewport_w,
                      char *resolved_url, size_t url_cap,
                      char *title, size_t title_cap,
                      char *err, size_t err_cap) {
    std::string full = url;
    if (full.rfind("http://", 0) != 0 && full.rfind("https://", 0) != 0)
        full = "https://" + full;

    std::string html, eff;
    if (!http_get(full, html, eff)) {
        snprintf(err, err_cap, "Seite nicht erreichbar.");
        return -1;
    }
    std::string base = eff.empty() ? full : eff;

    g_container.m_vw = viewport_w;
    g_container.m_base_url = base;
    g_container.m_clip_stack.clear();
    g_container.m_caption.clear();

    document::ptr doc = document::createFromString(html.c_str(), &g_container);
    if (!doc) { snprintf(err, err_cap, "Seite konnte nicht geparst werden."); return -1; }
    doc->render((pixel_t)viewport_w);

    g_doc = doc;
    collect_links(doc->root(), base);

    snprintf(resolved_url, url_cap, "%s", base.c_str());
    snprintf(title, title_cap, "%s", g_container.m_caption.c_str());
    return 0;
}

extern "C" int flux_browser_render_open(const char *url, int viewport_w,
                                        char *resolved_url, size_t url_cap,
                                        char *title, size_t title_cap,
                                        char *err, size_t err_cap) {
    if (!url || !*url) { snprintf(err, err_cap, "Keine Adresse angegeben."); return -1; }
    return render_url(url, viewport_w, resolved_url, url_cap, title, title_cap, err, err_cap);
}

extern "C" int flux_browser_render_click(int idx, int viewport_w,
                                         char *resolved_url, size_t url_cap,
                                         char *title, size_t title_cap,
                                         char *err, size_t err_cap) {
    if (idx < 0 || idx >= (int)g_links.size()) {
        snprintf(err, err_cap, "Kein Link mit dieser Nummer.");
        return -1;
    }
    return render_url(g_links[idx].href, viewport_w, resolved_url, url_cap, title, title_cap, err, err_cap);
}

extern "C" int flux_browser_render_height(void) {
    if (!g_doc) return 0;
    return (int)(float)g_doc->height();
}

extern "C" void flux_browser_render_draw(flux_fb_t *fb, int top, int bottom, int scroll_y) {
    if (!g_doc) return;
    g_container.m_clip_stack.clear();
    g_container.m_clip_stack.push_back(position(0, (pixel_t)top, (pixel_t)g_container.m_vw,
                                                (pixel_t)(bottom - top)));
    g_doc->draw((uint_ptr)fb, 0, (pixel_t)(top - scroll_y), &g_container.m_clip_stack.back());
}

extern "C" int flux_browser_render_link_at(int x, int y, int top, int scroll_y) {
    int doc_y = y - top + scroll_y;
    for (size_t i = 0; i < g_links.size(); i++) {
        const position &p = g_links[i].pos;
        int px0 = (int)(float)p.x, py0 = (int)(float)p.y;
        int px1 = px0 + (int)(float)p.width, py1 = py0 + (int)(float)p.height;
        if (x >= px0 && x < px1 && doc_y >= py0 && doc_y < py1) return (int)i;
    }
    return -1;
}
