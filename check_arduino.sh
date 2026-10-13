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
echo " Diagnóstico terminado"
echo "======================================================"
