/* contacts.h -- Kontaktverwaltung, ausschliesslich von fluxaid besessen
 * und beschrieben (flux-shell kennt nur das Protokoll, nicht den
 * Speicherort -- gleiche Trennung wie ueberall sonst in Flux: Shell ist
 * reine UI, fluxaid ist der einzige Besitzer von Geraete-/Systemzustand).
 */
#ifndef FLUX_CONTACTS_H
#define FLUX_CONTACTS_H

#include <stddef.h>

/* Legt einen Kontakt an (haengt an FLUX_CONTACTS_PATH an). Ein bereits
 * vorhandener Name gleichen Wortlauts wird durch die neue Nummer
 * ersetzt, statt einen Duplikat-Eintrag zu erzeugen. out enthaelt immer
 * eine lesbare Antwort. Gibt 1 bei Erfolg, 0 bei Fehler zurueck. */
int flux_contacts_add(const char *name, const char *phone, char *out, size_t out_cap);

/* Sucht einen Kontakt per (Teil-)Namensvergleich, gibt bei Treffer
 * "<name>\t<telefonnummer>" in out. Gibt 1 bei Treffer, 0 sonst. */
int flux_contacts_find(const char *name, char *out, size_t out_cap);

/* Formatiert alle gespeicherten Kontakte als lesbare Liste in out.
 * Gibt 1 zurueck, wenn mindestens ein Kontakt vorhanden ist. */
int flux_contacts_list(char *out, size_t out_cap);

#endif
