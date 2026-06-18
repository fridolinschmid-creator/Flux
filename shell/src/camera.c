/* camera.c -- Kamera-Aufnahme via V4L2 mit Testmuster-Fallback.
 * Wird vom flux-shell aufgerufen, wenn der Nutzer ein Foto machen moechte.
 */
#include "camera.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <linux/videodev2.h>

/* Sicherer Wertebereich 0-255. */
static inline int clamp8(int v) { return v < 0 ? 0 : v > 255 ? 255 : v; }

/* Schreibt einen RGB-Pixel in den Ausgabe-PPM-Stream. */
static void write_rgb(FILE *f, int r, int g, int b) {
    fputc(clamp8(r), f);
    fputc(clamp8(g), f);
    fputc(clamp8(b), f);
}

/* Konvertiert einen YUYV-Puffer in PPM und schreibt ihn in f.
 * W/H muessen gerade sein (YUYV-Anforderung). */
static void yuyv_to_ppm(FILE *f, const unsigned char *yuyv, int W, int H) {
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    for (int row = 0; row < H; row++) {
        for (int col = 0; col < W; col += 2) {
            int base = (row * W + col) * 2;
            int Y0 = yuyv[base + 0];
            int Cb = yuyv[base + 1];
            int Y1 = yuyv[base + 2];
            int Cr = yuyv[base + 3];
            int c  = Cb - 128;
            int d  = Cr - 128;
            write_rgb(f, Y0 + ((1402 * d) / 1000),
                         Y0 - ((344  * c) / 1000) - ((714 * d) / 1000),
                         Y0 + ((1772 * c) / 1000));
            write_rgb(f, Y1 + ((1402 * d) / 1000),
                         Y1 - ((344  * c) / 1000) - ((714 * d) / 1000),
                         Y1 + ((1772 * c) / 1000));
        }
    }
}

/* Versucht ein Einzelbild von /dev/video0 per V4L2 zu erfassen und als PPM
 * unter path zu speichern. Gibt 0 bei Erfolg, -1 wenn kein Geraet verfuegbar. */
static int try_v4l2(const char *path) {
    int fd = open("/dev/video0", O_RDWR | O_NONBLOCK);
    if (fd < 0) return -1;

    struct v4l2_format fmt = {0};
    fmt.type                = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width       = 640;
    fmt.fmt.pix.height      = 480;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
    fmt.fmt.pix.field       = V4L2_FIELD_INTERLACED;
    if (ioctl(fd, VIDIOC_S_FMT, &fmt) < 0) { close(fd); return -1; }

    int W = (int)fmt.fmt.pix.width;
    int H = (int)fmt.fmt.pix.height;

    struct v4l2_requestbuffers reqbuf = {0};
    reqbuf.count  = 1;
    reqbuf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    reqbuf.memory = V4L2_MEMORY_MMAP;
    if (ioctl(fd, VIDIOC_REQBUFS, &reqbuf) < 0) { close(fd); return -1; }

    struct v4l2_buffer buf = {0};
    buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index  = 0;
    if (ioctl(fd, VIDIOC_QUERYBUF, &buf) < 0) { close(fd); return -1; }

    void  *mptr = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, buf.m.offset);
    size_t mlen = buf.length;
    if (mptr == MAP_FAILED) { close(fd); return -1; }

    if (ioctl(fd, VIDIOC_QBUF, &buf) < 0) { munmap(mptr, mlen); close(fd); return -1; }

    enum v4l2_buf_type btype = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(fd, VIDIOC_STREAMON, &btype) < 0) { munmap(mptr, mlen); close(fd); return -1; }

    /* Warten auf Frame (max 2 Sekunden). */
    fd_set fds; FD_ZERO(&fds); FD_SET(fd, &fds);
    struct timeval tv = { .tv_sec = 2, .tv_usec = 0 };
    int ret = select(fd + 1, &fds, NULL, NULL, &tv);
    if (ret <= 0) {
        ioctl(fd, VIDIOC_STREAMOFF, &btype);
        munmap(mptr, mlen); close(fd); return -1;
    }

    buf.index = 0;
    if (ioctl(fd, VIDIOC_DQBUF, &buf) < 0) {
        ioctl(fd, VIDIOC_STREAMOFF, &btype);
        munmap(mptr, mlen); close(fd); return -1;
    }

    FILE *f = fopen(path, "wb");
    if (f) {
        yuyv_to_ppm(f, mptr, W, H);
        fclose(f);
    }

    ioctl(fd, VIDIOC_STREAMOFF, &btype);
    munmap(mptr, mlen);
    close(fd);
    return f ? 0 : -1;
}

/* Erzeugt ein farbiges Testmuster (Himmel/Boden-Gradient mit Farbpalette) als PPM.
 * Sieht wie ein abstraktes Landschaftsfoto aus. */
static int generate_test_pattern(const char *path) {
    const int W = 480, H = 480;
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    fprintf(f, "P6\n%d %d\n255\n", W, H);

    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            int r, g, b;
            if (y < H * 2 / 5) {
                /* Himmel: hellblauer Gradient mit weichem Verlauf */
                int t = y * 100 / (H * 2 / 5);
                r = 80  + t * 60 / 100;
                g = 140 + t * 50 / 100;
                b = 220 + t * 20 / 100;
                /* Wolken-Simulation: hellere Streifen */
                if ((x + y * 3) % 60 < 8) {
                    r = clamp8(r + 40);
                    g = clamp8(g + 40);
                    b = clamp8(b + 30);
                }
            } else {
                /* Boden: gruener Verlauf mit Textur */
                int t = (y - H * 2 / 5) * 100 / (H * 3 / 5);
                r = 60  + t * 40 / 100;
                g = 110 - t * 20 / 100;
                b = 40  + t * 10 / 100;
                /* Textur-Rauschen */
                if ((x * 7 + y * 13) % 20 == 0) {
                    r = clamp8(r - 15);
                    g = clamp8(g - 15);
                    b = clamp8(b - 5);
                }
            }
            /* Vignette: Raender leicht abdunkeln */
            int dx = x - W/2, dy = y - H/2;
            int dist2 = (dx*dx + dy*dy) / 1000;
            int fade = dist2 > 80 ? (dist2 - 80) / 3 : 0;
            if (fade > 60) fade = 60;
            r = clamp8(r - fade);
            g = clamp8(g - fade);
            b = clamp8(b - fade);
            fputc((unsigned char)r, f);
            fputc((unsigned char)g, f);
            fputc((unsigned char)b, f);
        }
    }
    fclose(f);
    return 0;
}

/* Stellt sicher dass das Bilder-Verzeichnis existiert. */
static void ensure_pictures_dir(void) {
    mkdir(FLUX_PICTURES_DIR, 0755);
}

int flux_camera_capture(const char *path) {
    ensure_pictures_dir();
    /* Echte Kamera versuchen, bei Fehler Testmuster. */
    if (try_v4l2(path) == 0) return 0;
    return generate_test_pattern(path);
}
