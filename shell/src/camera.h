/* camera.h -- Kamera-Modul fuer Flux.
 * Nimmt Fotos auf via Linux V4L2 (Video4Linux2).
 * Fallback: farbiges Testmuster, wenn kein Kamerageraet verfuegbar.
 */
#ifndef FLUX_CAMERA_H
#define FLUX_CAMERA_H

/* Verzeichnis fuer gespeicherte Fotos. */
#define FLUX_PICTURES_DIR "/home/user/Pictures"

/* Nimmt ein Foto auf und speichert es als PPM-Datei unter path.
 * Nutzt /dev/video0 per V4L2 wenn vorhanden, sonst generiert ein Testmuster.
 * Gibt 0 bei Erfolg, -1 bei Fehler. */
int flux_camera_capture(const char *path);

#endif
