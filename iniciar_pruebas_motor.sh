#!/bin/bash
# iniciar_pruebas_motor.sh
# Levanta socat + simulador_motor.py + backend con el dashboard de MOTOR.
# SOLO LOCAL: no se sube a GitHub.
#
# Uso:
#   ./iniciar_pruebas_motor.sh
#
# Variables opcionales:
#   NO_ATTACH=1   no entrar a la sesión tmux (queda en segundo plano)

set -e

SESION="pruebas-motor"
PROYECTO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
VENV_DIR="$HOME/proyecto-pi/venv"
SOCAT_LOG="/tmp/socat_motor_pty.log"
NO_ATTACH="${NO_ATTACH:-0}"

echo "=== Verificando tmux ==="
if ! command -v tmux &> /dev/null; then
    echo "tmux no está instalado, instalando..."
    sudo apt install -y tmux
fi

echo "=== Matando procesos anteriores ==="
tmux kill-session -t "$SESION" 2>/dev/null || true
pkill -f "socat -d -d pty" 2>/dev/null || true
pkill -f "simulador_motor.py" 2>/dev/null || true
pkill -f "uvicorn main:app" 2>/dev/null || true
fuser -k 8000/tcp 2>/dev/null || true
sleep 1

echo "=== Iniciando socat ==="
rm -f "$SOCAT_LOG"
tmux new-session -d -s "$SESION-socat" "socat -d -d pty,raw,echo=0 pty,raw,echo=0 2>&1 | tee $SOCAT_LOG"

echo "Esperando puertos virtuales..."
PTY1=""
PTY2=""
for i in $(seq 1 20); do
    sleep 0.5
    PTY1=$(grep "PTY is" "$SOCAT_LOG" 2>/dev/null | sed -n '1p' | awk '{print $NF}')
    PTY2=$(grep "PTY is" "$SOCAT_LOG" 2>/dev/null | sed -n '2p' | awk '{print $NF}')
    [ -n "$PTY1" ] && [ -n "$PTY2" ] && break
done

if [ -z "$PTY1" ] || [ -z "$PTY2" ]; then
    echo "ERROR: no se detectaron los puertos. Revisá $SOCAT_LOG"
    cat "$SOCAT_LOG"
    exit 1
fi

echo "Backend  -> $PTY1"
echo "Simulador-> $PTY2"

tmux new-session -d -s "$SESION" -n dev

# Panel 1: log socat
tmux send-keys -t "$SESION" "echo '--- socat ($PTY1 <-> $PTY2) ---'; tail -f $SOCAT_LOG" C-m

# Panel 2: simulador_motor
tmux split-window -h -t "$SESION"
tmux send-keys -t "$SESION" "cd $PROYECTO_DIR && source $VENV_DIR/bin/activate && python simulador_motor.py $PTY2" C-m

# Panel 3: backend con dashboard de motor
tmux split-window -v -t "$SESION"
tmux send-keys -t "$SESION" "cd $PROYECTO_DIR && source $VENV_DIR/bin/activate && DASHBOARD=motor PUERTO_FORZADO=$PTY1 uvicorn main:app --host 0.0.0.0 --port 8000 --reload" C-m

tmux select-layout -t "$SESION" tiled

echo ""
echo "Listo. Dashboard de motor: http://localhost:8000"
echo "(salir sin cortar: Ctrl+B, D; volver: tmux attach -t $SESION)"
sleep 2

if [ "$NO_ATTACH" = "1" ]; then
    echo "NO_ATTACH=1 -> sesión en segundo plano."
else
    tmux attach -t "$SESION"
fi
