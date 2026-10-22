#!/bin/bash
# check_arduino.sh
# Diagnóstico completo de detección del Arduino: USB, puerto serie,
# permisos, y si la lógica de encontrar_arduino() de main.py lo va a
# detectar correctamente.

echo "======================================================"
echo " 1) Dispositivos USB conectados"
echo "======================================================"
lsusb
echo ""
echo "--- Buscando específicamente Arduino/CH340/FTDI conocidos ---"
lsusb | grep -iE "arduino|2341|1a86|ch340|ch341|ftdi|0403" \
    && echo "-> Encontrado algo que parece un Arduino/adaptador serie." \
    || echo "-> No se detectó ningún VID conocido de Arduino en lsusb."
echo ""

echo "======================================================"
echo " 2) Puertos serie disponibles (rutas estables)"
echo "======================================================"
if [ -d /dev/serial/by-id ] && [ "$(ls -A /dev/serial/by-id 2>/dev/null)" ]; then
    ls -la /dev/serial/by-id/
else
    echo "No hay nada en /dev/serial/by-id/ -- ningún dispositivo serie detectado."
fi
echo ""

echo "======================================================"
echo " 3) Todos los /dev/ttyACM* y /dev/ttyUSB*"
echo "======================================================"
ls -la /dev/ttyACM* /dev/ttyUSB* 2>/dev/null \
    || echo "No hay ningún /dev/ttyACM* ni /dev/ttyUSB* presente."
echo ""

echo "======================================================"
echo " 4) Detalle de VID/PID por cada puerto encontrado (udevadm)"
echo "======================================================"
for dev in /dev/ttyACM* /dev/ttyUSB*; do
    [ -e "$dev" ] || continue
    echo "--- $dev ---"
    udevadm info -a -n "$dev" 2>/dev/null | grep -E "idVendor|idProduct|MANUFACTURER|PRODUCT=" | head -6
    echo ""
done

echo "======================================================"
echo " 5) Permisos: ¿tu usuario está en el grupo dialout?"
echo "======================================================"
if groups | grep -q dialout; then
    echo "OK: $(whoami) pertenece al grupo dialout."
else
    echo "FALTA: $(whoami) NO está en el grupo dialout."
    echo "  Arreglar con: sudo usermod -a -G dialout \$USER"
    echo "  (después hace falta cerrar sesión y volver a entrar)"
fi
echo ""

echo "======================================================"
echo " 6) Últimos eventos del kernel sobre USB/tty (dmesg)"
echo "======================================================"
sudo dmesg | grep -iE "tty|usb" | tail -15
echo ""

echo "======================================================"
echo " 7) Lo que vería main.py (detección por VID/PID con pyserial)"
echo "======================================================"
PYTHON_BIN="python3"
if [ -f "$HOME/proyecto-pi/venv/bin/python3" ]; then
    PYTHON_BIN="$HOME/proyecto-pi/venv/bin/python3"
fi

"$PYTHON_BIN" - << 'EOF'
try:
    import serial.tools.list_ports as list_ports
except ImportError:
    print("pyserial no está instalado en este intérprete de Python.")
    raise SystemExit

ARDUINO_VIDS = (0x2341, 0x1A86)  # igual que en main.py

puertos = list(list_ports.comports())
if not puertos:
    print("pyserial no ve ningún puerto serie en absoluto.")
else:
    for p in puertos:
        vid_hex = f"0x{p.vid:04X}" if p.vid else "N/A"
        marca = "  <-- este lo tomaría main.py" if p.vid in ARDUINO_VIDS else ""
        print(f"  {p.device}  VID={vid_hex}  descripcion='{p.description}'{marca}")

    encontrados = [p.device for p in puertos if p.vid in ARDUINO_VIDS]
    print("")
    if encontrados:
        print(f"RESULTADO: main.py detectaría el Arduino en: {encontrados[0]}")
    else:
        print("RESULTADO: main.py NO detectaría ningún Arduino con los VID conocidos (0x2341, 0x1A86).")
        print("Si el puerto aparece arriba pero con otro VID, avisar para agregarlo a ARDUINO_VIDS en main.py.")
EOF

echo ""
echo "======================================================"
echo " 8) Tipo de placa real y firmware en ejecución"
echo "======================================================"
# Compilar con el FQBN equivocado es la causa #1 de 'el Arduino está
# conectado pero el dashboard no muestra datos' (ver DECISIONES.md P12).
ARDUINO_CLI=""
if [ -x "$HOME/bin/arduino-cli" ]; then
    ARDUINO_CLI="$HOME/bin/arduino-cli"
elif command -v arduino-cli >/dev/null 2>&1; then
    ARDUINO_CLI="$(command -v arduino-cli)"
fi

if [ -z "$ARDUINO_CLI" ]; then
    echo "arduino-cli no encontrado; no se puede determinar el FQBN."
else
    echo "--- Placas detectadas (arduino-cli board list) ---"
    "$ARDUINO_CLI" board list 2>/dev/null
    echo ""
    echo "--- ¿El puerto entrega JSON del proyecto? ---"
    PUERTO_JSON=$("$PYTHON_BIN" - << 'EOF' 2>/dev/null
import serial.tools.list_ports as list_ports
for p in list_ports.comports():
    if p.vid in (0x2341, 0x1A86):
        print(p.device)
        break
EOF
)
    if [ -n "$PUERTO_JSON" ]; then
        "$PYTHON_BIN" - "$PUERTO_JSON" << 'EOF'
import sys, time
try:
    import serial
except ImportError:
    print("pyserial no disponible.")
    raise SystemExit
puerto = sys.argv[1]
try:
    s = serial.Serial(puerto, 9600, timeout=2)
    time.sleep(1.5)
    s.reset_input_buffer()
except Exception as e:
    print(f"No se pudo abrir {puerto}: {e}")
    raise SystemExit
buf = b""
fin = time.time() + 4
while time.time() < fin and len(buf) < 400:
    buf += s.read(200)
s.close()
if b"estado_ciclo" in buf:
    print(f"OK: {puerto} responde JSON del proyecto a 9600 baudios.")
elif buf.strip():
    print(f"PROBLEMA: {puerto} manda datos que NO son JSON del proyecto.")
    print(f"  Primeros bytes: {buf[:60]!r}")
    print("  Causa probable: firmware compilado/subido con FQBN equivocado")
    print("  (p. ej. un .hex de UNO en un Mega). Volver a subirlo con la")
    print("  opción 3 del menú: ahora detecta el FQBN solo.")
else:
    print(f"PROBLEMA: {puerto} no manda nada. ¿Firmware sin reporte JSON?")
EOF
    else
        echo "No se detectó un Arduino para probar el puerto."
    fi
fi

echo ""
echo "======================================================"
echo " Diagnóstico terminado"
echo "======================================================"
