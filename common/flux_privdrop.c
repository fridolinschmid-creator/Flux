#include "flux_privdrop.h"
#include "flux_config.h"

#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>

int flux_privdrop(const char *cfg_key, const char *env_var) {
    if (getuid() != 0)
        return 0; /* schon unprivilegiert -- nichts zu tun */

    char user[128] = {0};
    const char *e = env_var ? getenv(env_var) : NULL;
    if (e && *e)
        snprintf(user, sizeof(user), "%s", e);
    else if (!flux_config_get(cfg_key, user, sizeof(user)) || !user[0])
        return 0; /* opt-in: kein Zielnutzer konfiguriert */

    struct passwd *pw = getpwnam(user);
    if (!pw) {
        fprintf(stderr, "flux_privdrop: Nutzer '%s' nicht gefunden -- breche ab "
                        "(laufe nicht ungewollt als root weiter)\n", user);
        return -1;
    }

    /* Supplementary-Groups (z.B. video/input fuer die Shell) UEBERNEHMEN,
     * dann gid/uid -- Reihenfolge ist sicherheitskritisch: setgid vor
     * setuid, sonst fehlen die Rechte zum Gruppenwechsel. */
    if (initgroups(user, pw->pw_gid) != 0) { perror("flux_privdrop: initgroups"); return -1; }
    if (setgid(pw->pw_gid) != 0)           { perror("flux_privdrop: setgid");    return -1; }
    if (setuid(pw->pw_uid) != 0)           { perror("flux_privdrop: setuid");    return -1; }

    /* Gegenprobe: root darf nicht wiedererlangbar sein. */
    if (pw->pw_uid != 0 && setuid(0) == 0) {
        fprintf(stderr, "flux_privdrop: Drop unvollstaendig -- root wieder erlangbar, breche ab\n");
        return -1;
    }

    fprintf(stderr, "flux_privdrop: laufe jetzt als '%s' (uid=%d, gid=%d)\n",
            user, (int)pw->pw_uid, (int)pw->pw_gid);
    return 0;
}
