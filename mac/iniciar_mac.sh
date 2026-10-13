#!/bin/bash
# ============================================================
#  iniciar_mac.sh
#  Levanta el dashboard (backend FastAPI) en macOS.
#
#  Uso:
#    ./iniciar_mac.sh                -> deteccion automatica del puerto
#    ./iniciar_mac.sh /dev/cu.usbmodem1101  -> fuerza un puerto
#
#  Requiere haber creado el venv y las dependencias una vez:
#    python3 -m venv venv
#    source venv/bin/activate
#    python3 -m pip install -r requirements.txt
# ============================================================

set -e

# Ir a la carpeta donde está este script
cd "$(dirname "$0")"

if [ ! -x "venv/bin/python3" ]; then
    echo "[ERROR] No existe el entorno virtual 'venv'."
    echo "Crearlo una sola vez con:"
    echo "    python3 -m venv venv"
    echo "    source venv/bin/activate"
    echo "    python3 -m pip install -r requirements.txt"
    exit 1
fi

if [ -n "$1" ]; then
    echo "[info] Puerto forzado: $1"
    PUERTO_FORZADO="$1"
else
    echo "[info] Deteccion automatica del Arduino por VID."
fi

echo
echo "[info] Abriendo el dashboard en http://localhost:8000"
echo "[info] Para detenerlo: Ctrl+C"
echo

# Abrir el navegador un instante después de que arranque el server
( sleep 3; open "http://localhost:8000" >/dev/null 2>&1 || true ) &

if [ -n "${PUERTO_FORZADO:-}" ]; then
    PUERTO_FORZADO="$PUERTO_FORZADO" ./venv/bin/python3 -m uvicorn main:app --host 0.0.0.0 --port 8000
else
    ./venv/bin/python3 -m uvicorn main:app --host 0.0.0.0 --port 8000
fi
