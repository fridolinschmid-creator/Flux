#!/bin/sh
# setup-users.sh -- richtet die Privilege-Separation fuer Flux ein (opt-in).
#
# Auf dem Dev-Build laeuft alles als root. Dieses Skript haertet ein echtes
# Geraet: es legt eigene, unprivilegierte Nutzer fuer Daemon und Shell an,
# uebergibt der Shell die Geraetegruppen (video/input), uebereignet den
# Zustand dem Dienstnutzer und aktiviert die Verschluesselung der Secrets.
#
# Nach dem Lauf lesen fluxaid/flux-shell die Config-Keys
#   service_user=flux-ai
#   service_user_ui=flux-ui
# und legen beim Start ihre root-Rechte ab (siehe common/flux_privdrop.c).
#
# Als root auf dem Zielgeraet ausfuehren:  sh scripts/setup-users.sh
#
# WICHTIG -- vorher pruefen/testen, da ein falsch gesetzter Besitzer den
# Daemon am Schreiben des Zustands (oder die Shell am Zugriff auf den
# Framebuffer) hindern und das Geraet unbrauchbar machen kann.
set -eu

CONFIG_DIR="${FLUX_CONFIG_DIR:-/etc/flux}"
AI_USER="flux-ai"
UI_USER="flux-ui"

if [ "$(id -u)" -ne 0 ]; then
    echo "Bitte als root ausfuehren." >&2
    exit 1
fi

# --- Nutzer anlegen (idempotent) -------------------------------------------
add_user() {
    user="$1"
    if id "$user" >/dev/null 2>&1; then
        echo "Nutzer $user existiert bereits."
    else
        # BusyBox adduser; Fallback auf useradd auf groesseren Systemen.
        if command -v adduser >/dev/null 2>&1; then
            adduser -D -H -s /sbin/nologin "$user" 2>/dev/null || adduser --system --no-create-home "$user"
        else
            useradd -r -s /sbin/nologin "$user"
        fi
        echo "Nutzer $user angelegt."
    fi
}

add_user "$AI_USER"
add_user "$UI_USER"

# --- Shell braucht Zugriff auf Framebuffer und Eingabegeraete ---------------
for grp in video input tty; do
    if getent group "$grp" >/dev/null 2>&1 || grep -q "^$grp:" /etc/group 2>/dev/null; then
        if command -v adduser >/dev/null 2>&1; then
            adduser "$UI_USER" "$grp" 2>/dev/null || true
        else
            usermod -aG "$grp" "$UI_USER" 2>/dev/null || true
        fi
        echo "$UI_USER zur Gruppe $grp hinzugefuegt."
    else
        echo "WARNUNG: Gruppe $grp fehlt -- die Shell kann ggf. nicht auf /dev/fb0 bzw. /dev/input zugreifen." >&2
    fi
done

# --- Zustand/Secrets dem Dienstnutzer uebereignen --------------------------
# fluxaid schreibt Speicher/Notizen/Memory unter CONFIG_DIR und muss das nach
# dem Privilege-Drop weiterhin koennen.
mkdir -p "$CONFIG_DIR"
chown -R "$AI_USER" "$CONFIG_DIR"
chmod 700 "$CONFIG_DIR"
[ -f "$CONFIG_DIR/flux.conf" ] && chmod 600 "$CONFIG_DIR/flux.conf"
[ -f "$CONFIG_DIR/.devkey" ]  && chmod 600 "$CONFIG_DIR/.devkey"

# Die Shell muss den PIN-Hash aus flux.conf lesen koennen. Einfachste Loesung
# fuer den Prototyp: gemeinsame Gruppe mit Lesezugriff auf die Config.
# (Sauberer waere, den PIN-Hash in eine separate, der UI gehoerende Datei zu
#  trennen -- als Folgeschritt vermerkt.)
if command -v groupadd >/dev/null 2>&1; then groupadd -f flux 2>/dev/null || true; fi
chgrp -R flux "$CONFIG_DIR" 2>/dev/null || true
chmod 640 "$CONFIG_DIR/flux.conf" 2>/dev/null || true
if command -v adduser >/dev/null 2>&1; then
    adduser "$UI_USER" flux 2>/dev/null || true
    adduser "$AI_USER" flux 2>/dev/null || true
else
    usermod -aG flux "$UI_USER" 2>/dev/null || true
    usermod -aG flux "$AI_USER" 2>/dev/null || true
fi

# --- Config-Keys setzen, die den Privilege-Drop aktivieren ------------------
# (key=value; flux_config respektiert FLUX_CONFIG_DIR.)
set_key() {
    key="$1"; val="$2"; conf="$CONFIG_DIR/flux.conf"
    if [ -f "$conf" ] && grep -q "^$key=" "$conf" 2>/dev/null; then
        sed -i "s|^$key=.*|$key=$val|" "$conf"
    else
        printf '%s=%s\n' "$key" "$val" >> "$conf"
    fi
}
# Hinweis: wenn flux.conf bereits verschluesselt ist, NICHT direkt editieren --
# dann diese Keys ueber die Einstellungen/Tools setzen lassen.
if [ -f "$CONFIG_DIR/flux.conf" ] && head -c 8 "$CONFIG_DIR/flux.conf" 2>/dev/null | grep -q "FLUXSEC1"; then
    echo "flux.conf ist verschluesselt -- service_user-Keys bitte ueber die App setzen."
else
    set_key service_user    "$AI_USER"
    set_key service_user_ui "$UI_USER"
    chmod 640 "$CONFIG_DIR/flux.conf"
fi

echo
echo "Fertig. Naechster Neustart laesst fluxaid als '$AI_USER' und"
echo "flux-shell als '$UI_USER' laufen. Secrets werden ab dem naechsten"
echo "Schreibvorgang verschluesselt (Device-Key: $CONFIG_DIR/.devkey)."
echo
echo "Vor dem Ausrollen auf einem produktiven Geraet den Device-Key"
echo "hardware-gestuetzt ablegen (TPM/Secure Element) -- siehe common/flux_secret.h."
