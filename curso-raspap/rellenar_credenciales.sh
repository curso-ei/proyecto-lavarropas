#!/bin/bash
# ============================================================================
# rellenar_credenciales.sh
# Genera las credenciales locales a partir de las plantillas sin claves.
#
# Los archivos con valores reales se escriben en: credenciales-local/
# (esa carpeta está ignorada por Git).
# Las plantillas y placeholders NO se tocan, así que siempre se pueden subir.
# ============================================================================

set -euo pipefail

BASE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TPL_DIR="$BASE_DIR/templates"
OUT_DIR="$BASE_DIR/credenciales-local"

mkdir -p "$OUT_DIR"
chmod 700 "$OUT_DIR"

echo "=================================================="
echo "  GENERAR CREDENCIALES LOCALES DEL CURSO-RASPAP"
echo "=================================================="
echo
echo "Los archivos se generarán en:"
echo "  $OUT_DIR"
echo
echo "Esa carpeta está ignorada por Git (no se sube)."
echo
echo "Seleccioná qué querés generar:"
echo "  1) WiFi cliente (wpa_supplicant.conf)"
echo "  2) Contraseña del AP (hostapd.conf)"
echo "  3) Admin de RaspAP (raspap.auth)"
echo "  4) API key de DeepSeek (bashrc.deepseek)"
echo "  5) Todo"
echo "  0) Salir"
echo

read -rp "Opción [0-5]: " opt

gen_wifi() {
    echo
    echo "--- WiFi cliente ---"
    read -rp "SSID de la red WiFi: " ssid
    read -rsp "Contraseña (psk) del WiFi: " psk
    echo
    [ -n "$ssid" ] || { echo "SSID vacío. Cancelado."; exit 1; }
    sed -e "s|__WIFI_SSID__|$ssid|" \
        -e "s|__WIFI_PSK__|$psk|" \
        "$TPL_DIR/wpa_supplicant.conf.tpl" \
        > "$OUT_DIR/wpa_supplicant.conf"
    chmod 600 "$OUT_DIR/wpa_supplicant.conf"
    echo "OK: $OUT_DIR/wpa_supplicant.conf"
}

gen_ap() {
    echo
    echo "--- Contraseña del AP (hostapd) ---"
    read -rsp "Contraseña del AP (wpa_passphrase): " appass
    echo
    [ -n "$appass" ] || { echo "Contraseña vacía. Cancelado."; exit 1; }
    sed -e "s|__AP_WPA_PASSPHRASE__|$appass|" \
        "$BASE_DIR/etc/hostapd/hostapd.conf" \
        > "$OUT_DIR/hostapd.conf"
    chmod 600 "$OUT_DIR/hostapd.conf"
    echo "OK: $OUT_DIR/hostapd.conf"
}

gen_admin() {
    echo
    echo "--- Admin de RaspAP (raspap.auth) ---"
    read -rsp "Contraseña del admin de RaspAP: " adminpass
    echo
    [ -n "$adminpass" ] || { echo "Contraseña vacía. Cancelado."; exit 1; }
    if command -v php >/dev/null 2>&1; then
        hash="$(php -r "echo password_hash('$adminpass', PASSWORD_BCRYPT);")"
    else
        echo "No se encontró php para generar el hash bcrypt."
        echo "Se deja el placeholder __RASPAP_ADMIN_HASH__."
        hash="__RASPAP_ADMIN_HASH__"
    fi
    sed -e "s|__RASPAP_ADMIN_HASH__|$hash|" \
        "$TPL_DIR/raspap.auth.tpl" \
        > "$OUT_DIR/raspap.auth"
    chmod 600 "$OUT_DIR/raspap.auth"
    echo "OK: $OUT_DIR/raspap.auth"
}

gen_ds() {
    echo
    echo "--- API key de DeepSeek ---"
    read -rsp "API key de DeepSeek: " ds
    echo
    [ -n "$ds" ] || { echo "API key vacía. Cancelado."; exit 1; }
    sed -e "s|__DEEPSEEK_API_KEY__|$ds|" \
        "$TPL_DIR/bashrc-deepseek.tpl" \
        > "$OUT_DIR/bashrc.deepseek"
    chmod 600 "$OUT_DIR/bashrc.deepseek"
    echo "OK: $OUT_DIR/bashrc.deepseek"
}

case "$opt" in
    1) gen_wifi ;;
    2) gen_ap ;;
    3) gen_admin ;;
    4) gen_ds ;;
    5)
        gen_wifi
        gen_ap
        gen_admin
        gen_ds
        ;;
    0)
        echo "Nada que hacer."
        exit 0
        ;;
    *)
        echo "Opción inválida."
        exit 1
        ;;
esac

echo
echo "Listo. Las credenciales quedaron en credenciales-local/ (ignoradas por Git)."
