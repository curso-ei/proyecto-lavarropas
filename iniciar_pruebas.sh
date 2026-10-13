#!/bin/bash
# iniciar_pruebas.sh
# Levanta las 3 partes (socat, simulador, backend) en una sesión tmux,
# usando los archivos de ESTA carpeta (~/proyecto-pi).
#
# Uso:
#   cd ~/proyecto-pi && ./iniciar_pruebas.sh
#
# Variables de entorno opcionales:
#   CICLO=original|dos_enjuagues   variante de ciclo del simulador
#   NO_ATTACH=1                    no entrar a la sesión tmux (queda en 2do plano)

set -e

SESION="pruebas"
PROYECTO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
VENV_DIR="$HOME/proyecto-pi/venv"
SOCAT_LOG="/tmp/socat_pty.log"
CICLO="${CICLO:-original}"
NO_ATTACH="${NO_ATTACH:-0}"

echo "=== Verificando tmux ==="
if ! command -v tmux &> /dev/null; then
    echo "tmux no está instalado, instalando..."
    sudo apt install -y tmux
fi

echo "=== Matando procesos anteriores (si existen) ==="
tmux kill-session -t "$SESION" 2>/dev/null && echo "  - sesión tmux '$SESION' anterior cerrada" || echo "  - no había sesión tmux previa"
pkill -f "socat -d -d pty" 2>/dev/null && echo "  - socat anterior detenido" || echo "  - no había socat corriendo"
pkill -f "simulador.py" 2>/dev/null && echo "  - simulador anterior detenido" || echo "  - no había simulador corriendo"
pkill -f "uvicorn main:app" 2>/dev/null && echo "  - uvicorn anterior detenido" || echo "  - no había uvicorn corriendo"
fuser -k 8000/tcp 2>/dev/null && echo "  - puerto 8000 liberado" || echo "  - puerto 8000 ya estaba libre"

sleep 1

echo "=== Iniciando socat ==="
rm -f "$SOCAT_LOG"
tmux new-session -d -s "$SESION-socat" "socat -d -d pty,raw,echo=0 pty,raw,echo=0 2>&1 | tee $SOCAT_LOG"

echo "Esperando a que socat cree los puertos virtuales..."
PTY1=""
PTY2=""
for i in $(seq 1 20); do
    sleep 0.5
    PTY1=$(grep "PTY is" "$SOCAT_LOG" 2>/dev/null | sed -n '1p' | awk '{print $NF}')
    PTY2=$(grep "PTY is" "$SOCAT_LOG" 2>/dev/null | sed -n '2p' | awk '{print $NF}')
    if [ -n "$PTY1" ] && [ -n "$PTY2" ]; then
        break
    fi
done

if [ -z "$PTY1" ] || [ -z "$PTY2" ]; then
    echo "ERROR: no se pudieron detectar los puertos de socat. Revisá $SOCAT_LOG"
    cat "$SOCAT_LOG"
    exit 1
fi

echo "Puerto para el backend (main.py):   $PTY1"
echo "Puerto para el simulador:            $PTY2"

echo "=== Creando sesión tmux '$SESION' con los 3 procesos ==="

tmux new-session -d -s "$SESION" -n dev

# Panel 1: log de socat
tmux send-keys -t "$SESION" "echo '--- socat (puentes: $PTY1 <-> $PTY2) ---'; tail -f $SOCAT_LOG" C-m

# Panel 2: simulador (de esta carpeta)
tmux split-window -h -t "$SESION"
tmux send-keys -t "$SESION" "cd $PROYECTO_DIR && source $VENV_DIR/bin/activate && CICLO=$CICLO python simulador.py $PTY2" C-m

# Panel 3: backend (main.py de esta carpeta) apuntando al otro extremo
tmux split-window -v -t "$SESION"
tmux send-keys -t "$SESION" "cd $PROYECTO_DIR && source $VENV_DIR/bin/activate && PUERTO_FORZADO=$PTY1 uvicorn main:app --host 0.0.0.0 --port 8000 --reload" C-m

tmux select-layout -t "$SESION" tiled

echo ""
echo "Listo. Variante de ciclo: $CICLO"
echo "(Para salir sin cortar los procesos: Ctrl+B, después D)"
echo "(Para volver a entrar después: tmux attach -t $SESION)"
sleep 2

if [ "$NO_ATTACH" = "1" ]; then
    echo "NO_ATTACH=1 -> la sesión '$SESION' queda corriendo en segundo plano."
    echo "Dashboard: http://localhost:8000"
else
    tmux attach -t "$SESION"
fi
