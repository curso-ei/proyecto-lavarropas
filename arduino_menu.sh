#!/bin/bash
# arduino_menu.sh
# Menú interactivo para las tareas más comunes con el Arduino y el dashboard.
#
# Requiere que check_arduino.sh esté en la misma carpeta que este script.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CHECK_SCRIPT="$SCRIPT_DIR/check_arduino.sh"
PROYECTO_DIR="$SCRIPT_DIR"
VENV_DIR="$PROYECTO_DIR/venv"

# Firmware del proyecto (versión actualizada con reporte JSON)
FIRMWARE_ORIGINAL="$PROYECTO_DIR/firmware/lavarropas_controller_display_error_buzzer"
FIRMWARE_2ENJUAGUES="$PROYECTO_DIR/firmware/lavarropas_controller_display_error_buzzer_2enjuagues"

PYTHON_BIN="python3"
if [ -f "$VENV_DIR/bin/python3" ]; then
    PYTHON_BIN="$VENV_DIR/bin/python3"
fi

# arduino-cli suele estar instalado en ~/bin, que puede no estar en el PATH
# segun como se lance el script (sudo, cron, otra sesion). Se resuelve la ruta
# una sola vez y se usa $ARDUINO_CLI en todos los comandos.
ARDUINO_CLI=""
if [ -x "$HOME/bin/arduino-cli" ]; then
    ARDUINO_CLI="$HOME/bin/arduino-cli"
elif command -v arduino-cli >/dev/null 2>&1; then
    ARDUINO_CLI="$(command -v arduino-cli)"
fi

# Intenta detectar el puerto del Arduino automáticamente (mismo criterio
# que main.py: VID 0x2341 = Arduino oficial, 0x1A86 = clones CH340).
detectar_puerto() {
    "$PYTHON_BIN" - << 'EOF' 2>/dev/null
try:
    import serial.tools.list_ports as list_ports
    ARDUINO_VIDS = (0x2341, 0x1A86)
    for p in list_ports.comports():
        if p.vid in ARDUINO_VIDS:
            print(p.device)
            break
except Exception:
    pass
EOF
}

pedir_puerto() {
    local puerto
    puerto=$(detectar_puerto)
    if [ -n "$puerto" ]; then
        echo "Puerto detectado automáticamente: $puerto" >&2
        read -rp "Presioná Enter para usarlo, o escribí otro puerto: " ingresado
        echo "${ingresado:-$puerto}"
    else
        echo "No se detectó ningún puerto automáticamente." >&2
        ls /dev/serial/by-id/ 2>/dev/null >&2
        read -rp "Escribí la ruta del puerto manualmente (ej. /dev/ttyACM0): " ingresado
        echo "$ingresado"
    fi
}

# Devuelve 0 si el puerto 8000 está libre, 1 si está ocupado.
puerto_8000_libre() {
    if command -v fuser >/dev/null 2>&1; then
        ! fuser -s 8000/tcp 2>/dev/null
    else
        ! ss -lnt 2>/dev/null | grep -q ':8000 '
    fi
}

# Mata TODOS los procesos relacionados con el dashboard y las pruebas:
# uvicorn (backend), simulador, socat y sesiones tmux de este proyecto.
matar_todo() {
    echo "--- Detener procesos del dashboard y pruebas ---"

    local algo=0

    # Sesiones tmux de pruebas (backend + simulador + socat)
    if command -v tmux >/dev/null 2>&1; then
        for s in "pruebas" "pruebas-socat"; do
            if tmux has-session -t "$s" 2>/dev/null; then
                tmux kill-session -t "$s" 2>/dev/null
                echo "  - sesión tmux '$s' cerrada"
                algo=1
            fi
        done
    fi

    # Procesos por nombre (solo los de este proyecto/venv)
    local patron='uvicorn main:app|python.*simulador\.py|socat .*pty'
    local pids
    pids=$(pgrep -f "$patron" 2>/dev/null)

    if [ -n "$pids" ]; then
        # No matar a este mismo menú (el patrón no debería coincidir, pero
        # se excluye explícitamente por seguridad).
        for pid in $pids; do
            [ "$pid" = "$$" ] && continue
            if kill "$pid" 2>/dev/null; then
                echo "  - proceso $pid terminado"
                algo=1
            fi
        done
        sleep 1
        pids=$(pgrep -f "$patron" 2>/dev/null)
        if [ -n "$pids" ]; then
            for pid in $pids; do
                [ "$pid" = "$$" ] && continue
                kill -9 "$pid" 2>/dev/null && echo "  - proceso $pid forzado (kill -9)"
            done
        fi
    fi

    # Puerto 8000 (por si algo quedó escuchando sin coincidir con el patrón)
    if command -v fuser >/dev/null 2>&1; then
        if fuser -s 8000/tcp 2>/dev/null; then
            fuser -k 8000/tcp 2>/dev/null
            echo "  - puerto 8000 liberado"
            algo=1
        fi
    fi

    if [ "$algo" -eq 0 ]; then
        echo "No se encontró nada corriendo. Todo limpio."
    fi

    echo "Listo."
}

opcion_check() {
    if [ -x "$CHECK_SCRIPT" ]; then
        "$CHECK_SCRIPT"
    else
        echo "No se encontró check_arduino.sh en $SCRIPT_DIR (o no es ejecutable)."
        echo "Asegurate de tenerlo en la misma carpeta que este script y darle chmod +x."
    fi
}

opcion_escuchar() {
    echo "--- Escuchar el puerto serie del Arduino (solo lectura) ---"
    echo "Esto solo ESCUCHA lo que el Arduino manda, no sube ni modifica nada."
    local puerto
    puerto=$(pedir_puerto)
    [ -z "$puerto" ] && { echo "No se especificó ningún puerto. Cancelado."; return; }

    read -rp "Baudrate (Enter para 9600): " baud
    baud=${baud:-9600}

    echo ""
    echo "Escuchando $puerto a $baud baudios."
    echo "IMPORTANTE: apretá Ctrl+C para salir de la escucha y volver al menú."
    echo ""
    [ -z "$ARDUINO_CLI" ] && { echo "No se encontró arduino-cli (ni en ~/bin ni en el PATH)."; return; }

    # Si el usuario corta con Ctrl+C (SIGINT), el monitor termina y volvemos
    # al menú en vez de salir del script. Usamos una variable para detectar el
    # corte y limpiar el trap SIEMPRE (aunque se interrumpa).
    local interrumpido=0
    trap 'interrumpido=1' INT
    "$ARDUINO_CLI" monitor -p "$puerto" -c "baudrate=$baud"
    trap - INT

    if [ "$interrumpido" -eq 1 ]; then
        echo ""
        echo "Escucha cortada con Ctrl+C."
    fi
    echo "Volviendo al menú..."
}

opcion_subir_firmware() {
    echo "--- Compilar y subir firmware del proyecto ---"
    echo "OJO: esto SOBRESCRIBE el programa que el Arduino tenga cargado."
    echo ""
    echo "Elegí la variante:"
    echo "  1) Original (fiel al profesor)"
    echo "  2) Dos enjuagues (enjuague final con agua + suavizante)"
    read -rp "Opción [1]: " variante
    case "${variante:-1}" in
        2) sketch="$FIRMWARE_2ENJUAGUES" ;;
        *) sketch="$FIRMWARE_ORIGINAL" ;;
    esac

    if [ ! -f "$sketch/$(basename "$sketch").ino" ]; then
        echo "No se encontró el sketch: $sketch"
        return
    fi

    if [ -z "$ARDUINO_CLI" ]; then
        echo "No se encontró arduino-cli (ni en ~/bin ni en el PATH)."
        return
    fi

    # ArduinoJson es necesaria para compilar el firmware
    if ! "$ARDUINO_CLI" lib list 2>/dev/null | grep -qi "arduinojson"; then
        echo "La librería ArduinoJson no está instalada. Instalando..."
        "$ARDUINO_CLI" lib install ArduinoJson
    fi

    local puerto
    puerto=$(pedir_puerto)
    [ -z "$puerto" ] && { echo "No se especificó ningún puerto. Cancelado."; return; }

    echo ""
    echo "Compilando $sketch ..."
    if ! "$ARDUINO_CLI" compile --fqbn arduino:avr:uno "$sketch"; then
        echo "Error de compilación. No se va a intentar subir nada."
        return
    fi

    echo ""
    read -rp "Compiló bien. ¿Subir ahora a $puerto? (s/n): " confirmar
    if [[ "$confirmar" == "s" || "$confirmar" == "S" ]]; then
        "$ARDUINO_CLI" upload -p "$puerto" --fqbn arduino:avr:uno "$sketch"
    else
        echo "No se subió nada. El binario compilado queda en la carpeta build/ del sketch."
    fi
}

opcion_backend() {
    echo "--- Cargar backend del dashboard (con Arduino real) ---"

    if [ ! -x "$VENV_DIR/bin/python3" ]; then
        echo "No se encontró $VENV_DIR/bin/python3. Revisá el entorno virtual."
        return
    fi

    if ! puerto_8000_libre; then
        echo ""
        echo "⚠️  El puerto 8000 ya está en uso (probablemente otro backend)."
        echo "Si viene de 'Pruebas sin Arduino', cerrá la sesión tmux con:"
        echo "    tmux kill-session -t pruebas"
        echo ""
        read -rp "¿Matar lo que ocupa el puerto 8000 y continuar? [s/N]: " respuesta
        if [[ "$respuesta" =~ ^[sS]$ ]]; then
            fuser -k 8000/tcp 2>/dev/null || true
            sleep 1
        else
            echo "Operación cancelada."
            return
        fi
    fi

    local puerto
    puerto=$(detectar_puerto)
    if [ -n "$puerto" ]; then
        echo "Arduino detectado en: $puerto (se fuerza ese puerto)."
    else
        echo "No se detectó Arduino ahora. El backend queda en modo 'sin datos' e"
        echo "intentará reconectar cada 2 segundos cuando conectes el Arduino."
        read -rp "¿Forzar un puerto manual? (ej. /dev/ttyACM0) o Enter para omitir: " puerto
    fi

    echo ""
    echo "Arrancando backend en http://localhost:8000"
    echo "Para detenerlo y volver a la terminal: Ctrl+C."
    echo "IMPORTANTE: antes de usar 'Escuchar puerto' (opción 2), cerrá el backend."
    echo ""
    cd "$PROYECTO_DIR"
    if [ -n "$puerto" ]; then
        PUERTO_FORZADO="$puerto" "$VENV_DIR/bin/python3" -m uvicorn main:app --host 0.0.0.0 --port 8000 --reload
    else
        "$VENV_DIR/bin/python3" -m uvicorn main:app --host 0.0.0.0 --port 8000 --reload
    fi

    echo ""
    echo "Backend detenido. Volviendo al menú..."
}

opcion_pruebas() {
    echo "--- Pruebas sin Arduino (socat + simulador + backend en tmux) ---"

    if ! command -v tmux >/dev/null 2>&1; then
        echo "tmux no está instalado. Se instala automáticamente al correr el script."
    fi

    if [ ! -x "$PROYECTO_DIR/iniciar_pruebas.sh" ]; then
        echo "No se encontró $PROYECTO_DIR/iniciar_pruebas.sh"
        return
    fi

    echo "Variante de ciclo: ${CICLO:-original} (podés cambiar con CICLO=dos_enjuagues)"
    echo ""
    echo "⚠️  Al cerrar esta opción, el backend y el simulador pueden quedar"
    echo "corriendo en una sesión tmux (segundo plano)."
    echo "Para frenarlos por completo después:  tmux kill-session -t pruebas"
    echo ""

    cd "$PROYECTO_DIR"
    ./iniciar_pruebas.sh

    echo ""
    echo "Volviendo al menú..."
}

opcion_pines() {
    cat << 'EOF'

--- Conexión de pines (firmware del profesor, sin cambios) ---

  Pin               Función
  ----------------  -----------------------------------------
  2                 START (entrada, INPUT_PULLUP)
  3                 Presostato (entrada, INPUT_PULLUP, LOW=lleno)
  4                 Traba de puerta / "En marcha" (salida relé)
  7                 Buzzer (salida)
  8                 Bomba de llenado (salida relé)
  9                 Motor sentido horario (salida relé)
  10                Motor sentido antihorario (salida relé)
  11                Bomba de desagote (salida relé)
  12                Suavizante (salida relé)
  13                Centrífuga (salida relé)
  A2, 6, A3, 5, A0, A1
                    LCD 16x2 (paralelo 4 bits)

  Los relés se activan en LOW (RELAY_ACTIVE_LOW = true).

--- ADVERTENCIA DE SEGURIDAD ---

  Los relés van a controlar cargas de 220V (bomba, motor, resistencia).
  El lado de baja tensión (Arduino, señal de control) y el lado de alta
  tensión (220V, contactos del relé) tienen que estar bien separados.
  Nunca toques ni recablees nada del lado de 220V con el equipo
  enchufado a la red. Si tenés dudas de una conexión, mejor consultarla
  con el profesor antes de energizar.

EOF
}

opcion_ayuda() {
    cat << 'EOF'

--- Comandos básicos que pueden servir ---

DETECCIÓN Y DIAGNÓSTICO
  ./check_arduino.sh                  Diagnóstico completo (USB, puerto, permisos)
  lsusb                               Lista dispositivos USB conectados
  ls /dev/serial/by-id/               Ruta estable del puerto serie
  arduino-cli board list              Detecta placas conectadas (según Arduino CLI)

ARDUINO CLI
  arduino-cli lib install ArduinoJson
                                      Instala ArduinoJson (necesaria para el firmware)
  arduino-cli compile --fqbn arduino:avr:uno firmware/lavarropas_controller_display_error_buzzer
                                      Compila sin subir (variante original)
  arduino-cli upload -p /dev/ttyACM0 --fqbn arduino:avr:uno firmware/lavarropas_controller_display_error_buzzer
                                      Sube el firmware original
  arduino-cli monitor -p /dev/ttyACM0 -c baudrate=9600
                                      Ver lo que manda el Arduino (Ctrl+C para salir)

DASHBOARD / BACKEND
  cd ~/proyecto-pi && source venv/bin/activate
  uvicorn main:app --host 0.0.0.0 --port 8000 --reload
                                      Backend real (con Arduino conectado)
  PUERTO_FORZADO=/dev/ttyACM0 uvicorn main:app --host 0.0.0.0 --port 8000 --reload
                                      Backend forzando el puerto
  ./iniciar_pruebas.sh                Backend + simulador (sin Arduino real)

GIT (respaldo del proyecto)
  git add -A && git commit -m "mensaje" && git push
  git log --oneline                   Ver historial de commits

PI CODING AGENT
  cd ~/proyecto-pi && pi              Entrar al agente
  /model                              Elegir modelo
  /effort off                         Apagar razonamiento (si instalaste pi-effort)

EOF
}

while true; do
    echo ""
    echo "======================================="
    echo "   Menú Arduino + Dashboard"
    echo "======================================="
    echo "1) Verificar detección (check_arduino.sh)"
    echo "2) Escuchar el puerto serie (solo lectura)"
    echo "3) Compilar y subir firmware del proyecto"
    echo "4) Cargar backend del dashboard (Arduino real)"
    echo "5) Pruebas sin Arduino (backend + simulador)"
    echo "6) Info de conexión de pines y seguridad"
    echo "7) Ayuda - comandos básicos"
    echo "8) Detener todo (backend, pruebas, simulador)"
    echo "0) Salir"
    echo "======================================="
    read -rp "Elegí una opción: " opcion

    case "$opcion" in
        1) opcion_check ;;
        2) opcion_escuchar ;;
        3) opcion_subir_firmware ;;
        4) opcion_backend ;;
        5) opcion_pruebas ;;
        6) opcion_pines ;;
        7) opcion_ayuda ;;
        8) matar_todo ;;
        0) echo "Chau."; exit 0 ;;
        *) echo "Opción inválida." ;;
    esac
done
