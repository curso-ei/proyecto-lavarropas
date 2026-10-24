#!/bin/bash
# ============================================================
# guardar_diagnostico.sh
# ------------------------------------------------------------
# Guarda una foto completa del estado de red/RaspAP en una
# carpeta con timestamp. Solo LECTURA: no reinicia ni modifica
# ningún servicio ni configuración de red.
#
# Uso recomendado:
#   1) Aplicar AP + STA desde curso-menu y esperar a que termine
#      (ver /etc/curso-raspap/apsta/last-result.txt -> OK/ERROR).
#   2) Ejecutar:  sudo ./guardar_diagnostico.sh
#
# Opcional, como referencia previa:
#   sudo ./guardar_diagnostico.sh antes
#
# Salida: ~/diagnostico/<timestamp>[-antes]/  y un .tar.gz
# ============================================================

set -u

TAG="${1:-despues}"
STAMP="$(date '+%Y%m%d-%H%M%S')"
if [ "$TAG" = "antes" ]; then
    OUT_DIR="$HOME/diagnostico/${STAMP}-antes"
else
    OUT_DIR="$HOME/diagnostico/${STAMP}"
fi

# Si se ejecuta con sudo, HOME puede quedar en /root; preferir el usuario real.
if [ "$(id -u)" -eq 0 ] && [ -n "${SUDO_USER:-}" ]; then
    REAL_HOME="$(getent passwd "$SUDO_USER" | cut -d: -f6)"
    OUT_DIR="${REAL_HOME}/diagnostico/${STAMP}${TAG:+}" 
    [ "$TAG" = "antes" ] && OUT_DIR="${REAL_HOME}/diagnostico/${STAMP}-antes"
    [ "$TAG" != "antes" ] && OUT_DIR="${REAL_HOME}/diagnostico/${STAMP}"
fi

mkdir -p "$OUT_DIR"

run() {
    local name="$1"; shift
    "$@" > "$OUT_DIR/$name.txt" 2>&1 || true
}

echo "Guardando diagnóstico en:"
echo "  $OUT_DIR"
echo

run 00-fecha            date
run 00-hostname         hostnamectl
run 00-timedatectl      timedatectl

run 01-journal-boot     journalctl -b --no-pager
run 01-journal-servicios journalctl -u wpa_supplicant -u hostapd -u dhcpcd -u dnsmasq -b --no-pager
run 01-dmesg            dmesg

run 02-ip-br-addr       ip -br addr
run 02-ip-route         ip route
run 02-ip-route6        ip -6 route
run 02-ip-link          ip -s link

run 03-iw-dev           iw dev
run 03-iw-wlan0-info    iw dev wlan0 info
run 03-iw-wlan0-link    iw dev wlan0 link
run 03-iw-uap0-info     iw dev uap0 info
run 03-rfkill           rfkill list

run 04-wpa-status       sh -c 'wpa_cli -i wlan0 status 2>&1 || true'
run 04-wpa-networks     sh -c 'wpa_cli -i wlan0 list_networks 2>&1 || true'
run 04-ps-red           ps aux

run 05-servicios        sh -c 'for s in hostapd dnsmasq dhcpcd wpa_supplicant raspapd; do echo "$s: active=$(systemctl is-active $s 2>/dev/null) enabled=$(systemctl is-enabled $s 2>/dev/null)"; done'

run 06-iptables-nat     iptables -t nat -S
run 06-iptables-filter  iptables -S
run 06-ip-forward       sysctl net.ipv4.ip_forward

# Configuración relevante
for f in /etc/raspap/hostapd.ini /etc/hostapd/hostapd.conf \
         /etc/dhcpcd.conf /etc/dnsmasq.d/090_raspap.conf \
         /etc/dnsmasq.d/090_uap0.conf /etc/dnsmasq.d/090_wlan0.conf \
         /etc/dnsmasq.d/090_eth0.conf /etc/systemd/system/curso-raspap-default.service; do
    if [ -f "$f" ]; then
        base="$(basename "$f")"
        cp "$f" "$OUT_DIR/07-conf-${base}.txt" 2>/dev/null || true
    fi
done

# wpa_supplicant.conf sin exponer el psk
if [ -f /etc/wpa_supplicant/wpa_supplicant.conf ]; then
    sed -E 's/(psk=).*/\1***OCULTO***/' /etc/wpa_supplicant/wpa_supplicant.conf \
        > "$OUT_DIR/07-conf-wpa_supplicant.conf.txt" 2>/dev/null || true
fi

# Logs AP + STA (snapshot del momento, no se pisan en la carpeta destino)
[ -f /etc/curso-raspap/apsta/last.log ] && cp /etc/curso-raspap/apsta/last.log "$OUT_DIR/08-apsta-last.log"
[ -f /etc/curso-raspap/apsta/last-result.txt ] && cp /etc/curso-raspap/apsta/last-result.txt "$OUT_DIR/08-apsta-last-result.txt"
[ -f /etc/curso-raspap/apsta/last-client.log ] && cp /etc/curso-raspap/apsta/last-client.log "$OUT_DIR/08-apsta-last-client.log" 2>/dev/null || true
[ -f /etc/curso-raspap/apsta/last-client-result.txt ] && cp /etc/curso-raspap/apsta/last-client-result.txt "$OUT_DIR/08-apsta-last-client-result.txt" 2>/dev/null || true

# dnsmasq.log persistente
[ -f /var/log/dnsmasq.log ] && cp /var/log/dnsmasq.log "$OUT_DIR/08-dnsmasq.log" 2>/dev/null || true

# Empaquetar
TARBALL="${OUT_DIR}.tar.gz"
tar -czf "$TARBALL" -C "$(dirname "$OUT_DIR")" "$(basename "$OUT_DIR")" 2>/dev/null || true

echo
echo "Archivos guardados en: $OUT_DIR"
[ -f "$TARBALL" ] && echo "Empaquetado en:        $TARBALL"
echo
echo "Resumen rápido del resultado AP + STA:"
[ -f /etc/curso-raspap/apsta/last-result.txt ] && cat /etc/curso-raspap/apsta/last-result.txt || echo "(sin resultado)"
