/* flux_privdrop.h -- dauerhaftes Ablegen von root-Rechten (opt-in).
 *
 * Flux laeuft im Standard-Dev-Build weiterhin als root. Auf einem
 * gehaerteten Geraet sollen Daemon und Shell jeweils als eigener,
 * unprivilegierter Nutzer laufen. Dieser Wechsel ist OPT-IN: er passiert
 * nur, wenn ein Zielnutzer konfiguriert ist (Config-Key oder Env-Var).
 * Siehe scripts/setup-users.sh fuer die Einrichtung der Nutzer/Gruppen.
 */
#ifndef FLUX_PRIVDROP_H
#define FLUX_PRIVDROP_H

/* Wechselt dauerhaft auf den konfigurierten Zielnutzer (inkl.
 * Supplementary-Groups), wenn der Prozess als root laeuft UND ein Nutzer
 * konfiguriert ist. Reihenfolge beim Aufrufer: erst privilegierte
 * Initialisierung (Socket binden / Geraete oeffnen), dann diesen Aufruf.
 *
 * cfg_key:  Config-Schluessel mit dem Nutzernamen (z.B. "service_user").
 * env_var:  Name einer Env-Var, die den Config-Wert ueberschreibt
 *           (z.B. "FLUX_SERVICE_USER"); NULL = ignorieren.
 *
 * Rueckgabe: 0, wenn gewechselt wurde ODER nichts zu tun war (kein root /
 * nicht konfiguriert). -1, wenn ein konfigurierter Wechsel fehlschlug --
 * der Aufrufer MUSS dann abbrechen, statt unbeabsichtigt als root
 * weiterzulaufen. */
int flux_privdrop(const char *cfg_key, const char *env_var);

#endif
