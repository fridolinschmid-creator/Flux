/* audit.h -- Aktions-Audit-Log (Ehrlichkeit/Nachvollziehbarkeit).
 *
 * Protokolliert JEDE von der KI ausgefuehrte X:-Aktion (Mail/SMS/Anruf/
 * Einstellung) nach Ausfuehrung in eine append-only Logdatei
 * /etc/flux/audit.txt -- damit der Nutzer nachvollziehen kann, was die
 * KI auf dem Geraet getan hat.
 *
 * WICHTIG: Es werden KEINE Geheimnisse geloggt (keine Passwoerter, kein
 * PIN, kein vollstaendiger sensibler Mailtext). Geloggt werden nur Typ,
 * Empfaenger/Key, eine Kurzbeschreibung und das EHRLICHE Ergebnis
 * (auch Fehlschlaege/Stubs).
 *
 * Format je Zeile (wie habits/memory):
 *   [YYYY-MM-DD HH:MM] TYP: Beschreibung -> Ergebnis
 */
#ifndef FLUX_AUDIT_H
#define FLUX_AUDIT_H

#include <stddef.h>

#define FLUX_AUDIT_PATH "/etc/flux/audit.txt"

/* Haengt einen Audit-Eintrag an audit.txt an (append-only).
 *   type:   kurzer Aktionstyp ("Mail", "SMS", "Anruf", "Einstellung")
 *   desc:   Kurzbeschreibung OHNE Geheimnisse (Empfaenger/Key + Betreff o.ae.)
 *   result: die ehrliche Ergebnismeldung der Aktion (OK oder Fehler/Stub)
 * Robust gegen NULL/leere Felder. Schreibt einzeilig (Newlines -> ' '). */
void flux_audit_log(const char *type, const char *desc, const char *result);

#endif /* FLUX_AUDIT_H */
