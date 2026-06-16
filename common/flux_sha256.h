/* flux_sha256.h -- kompakte SHA-256-Implementierung (FIPS-180-4),
 * bewusst ohne externe Krypto-Bibliothek fuer eine einzelne
 * Hash-Funktion. Wird fuer den PIN-Hash der Lockscreen-Sperre
 * gebraucht (siehe shell/src/ui.c, FLUX_SCREEN_PIN).
 */
#ifndef FLUX_SHA256_H
#define FLUX_SHA256_H

/* out_hex muss mindestens 65 Byte gross sein (64 Hex-Zeichen + '\0'). */
void flux_sha256_hex(const char *input, char out_hex[65]);

#endif
