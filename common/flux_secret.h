/* flux_secret.h -- Verschluesselung sensibler Daten "at rest".
 *
 * Versiegelt/oeffnet Bytebloecke mit AES-256-GCM. Der Schluessel wird aus
 * einem geraetelokalen Device-Key (FLUX_CONFIG_DIR/.devkey, 0600) und der
 * /etc/machine-id abgeleitet.
 *
 * Schutzumfang (ehrlich): schuetzt gegen das versehentliche Ausleiten der
 * Konfigurationsdatei allein (Backups, Logs, der frueher moegliche
 * file_read-Exfil-Pfad). KEIN Schutz gegen Diebstahl des kompletten
 * Dateisystem-Images -- dafuer muss der Device-Key hardware-gestuetzt
 * abgelegt werden (TPM/Secure Element) oder aus einer Nutzer-Passphrase
 * abgeleitet werden. Siehe scripts/setup-users.sh.
 */
#ifndef FLUX_SECRET_H
#define FLUX_SECRET_H

#include <stddef.h>

/* 1, wenn Schreibvorgaenge verschluesselt werden sollen: entweder
 * FLUX_CONFIG_ENCRYPT=1 gesetzt oder bereits ein Device-Key vorhanden. */
int flux_secret_active(void);

/* 1, wenn buf mit dem Versiegelungs-Header beginnt. */
int flux_secret_is_sealed(const unsigned char *buf, size_t len);

/* Versiegelt pt[0..pt_len) nach out. Erzeugt den Device-Key bei Bedarf.
 * Gibt die Laenge der versiegelten Daten zurueck, -1 bei Fehler. */
int flux_secret_seal(const unsigned char *pt, size_t pt_len,
                     unsigned char *out, size_t out_cap);

/* Oeffnet einen versiegelten Block nach out (Klartext, NUL-terminiert nicht
 * garantiert -- Aufrufer nutzt den Rueckgabewert). Gibt die Klartextlaenge
 * zurueck, -1 bei Fehler/Manipulation oder wenn in nicht versiegelt ist. */
int flux_secret_open(const unsigned char *in, size_t in_len,
                     unsigned char *out, size_t out_cap);

#endif
