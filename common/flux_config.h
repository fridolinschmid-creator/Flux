/* flux_config.h -- gemeinsame Konfigurationsdatei fuer flux-shell und
 * fluxaid (PIN-Hash, SMTP-Zugangsdaten, Cloud-API-Key). Bewusst ein
 * simples key=value-Format statt JSON/INI-Bibliothek -- dieselbe
 * Haltung wie beim Mini-IPC-Protokoll (siehe flux_protocol.h).
 *
 * Sicherheits-Hinweis: liegt unverschluesselt unter FLUX_CONFIG_PATH,
 * nur fuer root lesbar (chmod 0600). Fuer den Dev-Build ausreichend
 * (siehe README "Sicherheits-Hinweis"), kein Ersatz fuer einen
 * echten Secret-Store auf einem produktiven Geraet.
 */
#ifndef FLUX_CONFIG_H
#define FLUX_CONFIG_H

#include <stddef.h>

#define FLUX_CONFIG_DIR  "/etc/flux"
#define FLUX_CONFIG_PATH "/etc/flux/flux.conf"

/* Konfigurationsverzeichnis: per Env FLUX_CONFIG_DIR ueberschreibbar
 * (Tests/abweichende Secret-Ablage), sonst FLUX_CONFIG_DIR. */
const char *flux_config_dir(void);

/* Gibt 1 zurueck und kopiert den Wert nach out, wenn key gesetzt ist.
 * Sonst 0 (Datei fehlt oder Key nicht vorhanden) -- out ist dann ein
 * leerer String. */
int flux_config_get(const char *key, char *out, size_t out_cap);

/* Setzt/ueberschreibt key=value (legt Verzeichnis/Datei bei Bedarf an).
 * Gibt 0 bei Erfolg zurueck, -1 bei Schreibfehler oder zu vielen Keys. */
int flux_config_set(const char *key, const char *value);

#endif
