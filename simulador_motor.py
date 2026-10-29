"""
Simulador LOCAL del firmware unificado (2 enjuagues + motor TRIAC, Mega 2560).

Sirve para probar el dashboard de motor (static/dashboard_motor.html) sin
Arduino real. Emite por el puerto serie el MISMO contrato JSON que el firmware
lavarropas_controller_2enjuagues_motor_triac_MEGA.ino, incluyendo el objeto
"motor" con rpm / objetivo / potencia.

NO es el firmware real: es una imitación para ver el dashboard en local.

Uso:
  1) socat -d -d pty,raw,echo=0 pty,raw,echo=0
     -> imprime dos puertos, ej. /dev/pts/3 y /dev/pts/4
  2) python simulador_motor.py /dev/pts/4
  3) DASHBOARD=motor PUERTO_FORZADO=/dev/pts/3 uvicorn main:app \
       --host 0.0.0.0 --port 8000

O, más fácil, correr el script que levanta todo:
  ./iniciar_pruebas_motor.sh
"""

import json
import os
import sys
import threading
import time

import serial

PORT = sys.argv[1] if len(sys.argv) > 1 else "/dev/pts/4"
BAUD = 9600

# ---------------------------------------------------------------------------
# Parámetros que usa el firmware unificado (a los efectos de la simulación)
# ---------------------------------------------------------------------------
MAX_GIROS = 3
T_GIRO = 5.0           # s girando
T_PAUSA_GIRO = 2.0     # s de pausa en el vaivén
T_DRENAJE = 10.0       # s
T_CENTRIFUGADO = 15.0  # s

RPM_AGITADO = 600.0
RPM_CENTRIFUGADO = 6000.0
RAMPA_RPM_S = 1000.0   # subida del objetivo en RPM por segundo
POT_ARRANQUE = 20.0
POT_MAX_PID = 60.0
Kp = 0.001
Ki = 0.003

PULSOS_POR_VUELTA = 8

factor_velocidad = 1

FASES = [
    "REPOSO",
    "LLENADO",
    "LAVADO",
    "DRENAJE_1",
    "LLENADO_ENJUAGUE",
    "ENJUAGUE",
    "DRENAJE_2",
    "LLENADO_SUAVIZANTE",
    "ENJUAGUE_SUAVIZANTE",
    "DRENAJE_3",
    "CENTRIFUGADO",
    "FIN",
    "ERROR_AGUA",
    "ERROR_MOTOR",
]

ESTADO_CICLO = {
    "REPOSO": "inactivo",
    "LLENADO": "llenando",
    "LAVADO": "lavando",
    "DRENAJE_1": "desagotando",
    "LLENADO_ENJUAGUE": "llenando",
    "ENJUAGUE": "enjuagando",
    "DRENAJE_2": "desagotando",
    "LLENADO_SUAVIZANTE": "llenando",
    "ENJUAGUE_SUAVIZANTE": "enjuagando",
    "DRENAJE_3": "desagotando",
    "CENTRIFUGADO": "centrifugando",
    "FIN": "finalizado",
    "ERROR_AGUA": "error",
    "ERROR_MOTOR": "error",
}

FASE_COMPONENTES = {
    "REPOSO": {},
    "LLENADO": {"bloqueo_puerta": True, "bomba_llenado": True},
    "LAVADO": {"bloqueo_puerta": True},
    "DRENAJE_1": {"bloqueo_puerta": True, "bomba_desagote": True},
    "LLENADO_ENJUAGUE": {"bloqueo_puerta": True, "bomba_llenado": True},
    "ENJUAGUE": {"bloqueo_puerta": True},
    "DRENAJE_2": {"bloqueo_puerta": True, "bomba_desagote": True},
    "LLENADO_SUAVIZANTE": {
        "bloqueo_puerta": True,
        "bomba_llenado": True,
        "suavizante": True,
    },
    "ENJUAGUE_SUAVIZANTE": {"bloqueo_puerta": True},
    "DRENAJE_3": {"bloqueo_puerta": True, "bomba_desagote": True},
    "CENTRIFUGADO": {"bloqueo_puerta": True},
    "FIN": {},
    "ERROR_AGUA": {},
    "ERROR_MOTOR": {"bloqueo_puerta": True},
}

FASE_LCD = {
    "REPOSO": ["   LAVARROPAS   ", "Presione START  "],
    "LLENADO": ["CARGANDO AGUA   ", "Lavado principal"],
    "LAVADO": ["FASE: LAVADO    ", "Pausa de vaiven "],
    "DRENAJE_1": ["DESAGOTANDO...  ", "Fin de lavado   "],
    "LLENADO_ENJUAGUE": ["CARGANDO AGUA   ", "Fase: Enjuague  "],
    "ENJUAGUE": ["FASE: ENJUAGUE  ", "Pausa de vaiven "],
    "DRENAJE_2": ["DESAGOTANDO...  ", "Fin de enjuague "],
    "LLENADO_SUAVIZANTE": ["CARGANDO AGUA   ", "Enjuague+suav.  "],
    "ENJUAGUE_SUAVIZANTE": ["ENJUAGUE FINAL  ", "Pausa de vaiven "],
    "DRENAJE_3": ["DESAGOTANDO...  ", "Suavizante      "],
    "CENTRIFUGADO": ["CENTRIFUGANDO   ", "Alta velocidad  "],
    "FIN": ["CICLO TERMINADO ", "Retire la ropa  "],
    "ERROR_AGUA": ["  ! ALARMA !    ", " FALTA DE AGUA  "],
    "ERROR_MOTOR": ["  ! ALARMA !    ", " FALLA TACOMETRO"],
}

TITULO_AGITACION = {
    "LAVADO": "FASE: LAVADO    ",
    "ENJUAGUE": "FASE: ENJUAGUE  ",
    "ENJUAGUE_SUAVIZANTE": "ENJUAGUE FINAL  ",
}

FASE_SIGUIENTE = {
    "REPOSO": "LLENADO",
    "LLENADO": "LAVADO",
    "LAVADO": "DRENAJE_1",
    "DRENAJE_1": "LLENADO_ENJUAGUE",
    "LLENADO_ENJUAGUE": "ENJUAGUE",
    "ENJUAGUE": "DRENAJE_2",
    "DRENAJE_2": "LLENADO_SUAVIZANTE",
    "LLENADO_SUAVIZANTE": "ENJUAGUE_SUAVIZANTE",
    "ENJUAGUE_SUAVIZANTE": "DRENAJE_3",
    "DRENAJE_3": "CENTRIFUGADO",
    "CENTRIFUGADO": "FIN",
    "FIN": "REPOSO",
}

FASES_LLENADO = ("LLENADO", "LLENADO_ENJUAGUE", "LLENADO_SUAVIZANTE")
FASES_AGITACION = ("LAVADO", "ENJUAGUE", "ENJUAGUE_SUAVIZANTE")
FASES_DRENAJE = ("DRENAJE_1", "DRENAJE_2", "DRENAJE_3")

estado = {
    "estado_ciclo": "inactivo",
    "fase": "REPOSO",
    "error": None,
    "giros": None,
    "max_giros": None,
    "lcd": ["   LAVARROPAS   ", "Presione START  "],
    "componentes": {
        "bomba_llenado": False,
        "motor_sentido1": False,
        "motor_sentido2": False,
        "bomba_desagote": False,
        "bloqueo_puerta": False,
        "suavizante": False,
        "centrifuga": False,
        "buzzer": False,
    },
    "presostato": False,
    "pulsador": False,
    "motor": {"rpm": 0, "objetivo": 0, "potencia": 0},
}

lock = threading.Lock()

# --- Sub-estado del motor simulado ---
motor_activo = False
motor_sentido_cw = True
t_ultimo_giro = 0.0
t_inicio_fase = 0.0
fase_previa_error = "REPOSO"

rpm = 0.0
setpoint = 0.0
potencia = 0.0
integral = 0.0
t_motor = 0.0
falla_tacometro = False

try:
    ser = serial.Serial(PORT, BAUD, timeout=0.5)
except serial.SerialException as e:
    print(f"No se pudo abrir {PORT}: {e}")
    sys.exit(1)

print(f"[simulador_motor] Escuchando en {PORT}")
print("[simulador_motor] Firmware simulado: 2 enjuagues + motor TRIAC (Mega)")


def duracion(segundos: float) -> float:
    return segundos / factor_velocidad


def actualizar_lcd_agitacion(fase: str):
    titulo = TITULO_AGITACION.get(fase, "FASE: ENJUAGUE  ")
    if motor_activo:
        sentido = ">>" if motor_sentido_cw else "<<"
        estado["lcd"] = [titulo, f"Giro {estado['giros'] + 1}/{MAX_GIROS} {sentido}"]
    else:
        estado["lcd"] = [titulo, "Pausa de vaiven "]


def aplicar_fase(nueva_fase: str, giros: int = 0):
    global motor_activo, motor_sentido_cw, t_ultimo_giro, t_inicio_fase
    global falla_tacometro, rpm, setpoint, potencia, integral

    t_inicio_fase = time.monotonic()

    for key in estado["componentes"]:
        estado["componentes"][key] = False
    for key, valor in FASE_COMPONENTES.get(nueva_fase, {}).items():
        estado["componentes"][key] = valor

    estado["fase"] = nueva_fase
    estado["estado_ciclo"] = ESTADO_CICLO.get(nueva_fase, "desconocido")

    if nueva_fase == "ERROR_AGUA":
        estado["error"] = "FALTA DE AGUA"
    elif nueva_fase == "ERROR_MOTOR":
        estado["error"] = "FALLA TACOMETRO"
    else:
        estado["error"] = None

    estado["lcd"] = list(FASE_LCD.get(nueva_fase, ["", ""]))

    if nueva_fase in FASES_AGITACION:
        motor_activo = False
        motor_sentido_cw = True
        t_ultimo_giro = time.monotonic()
        estado["giros"] = giros
        estado["max_giros"] = MAX_GIROS
        setpoint = RPM_AGITADO
        integral = POT_ARRANQUE
        actualizar_lcd_agitacion(nueva_fase)
    else:
        motor_activo = False
        estado["giros"] = None
        estado["max_giros"] = None
        setpoint = RPM_CENTRIFUGADO if nueva_fase == "CENTRIFUGADO" else 0.0
        integral = POT_ARRANQUE if nueva_fase == "CENTRIFUGADO" else 0.0

    falla_tacometro = False
    estado["motor"]["objetivo"] = int(setpoint)


def entrar_en_error(motivo: str):
    global fase_previa_error
    if estado["fase"] not in ("ERROR_AGUA", "ERROR_MOTOR"):
        fase_previa_error = estado["fase"]
    if motivo == "FALTA DE AGUA":
        aplicar_fase("ERROR_AGUA")
    else:
        aplicar_fase("ERROR_MOTOR")
    estado["componentes"]["buzzer"] = True
    print(f"[simulador_motor] ¡{motivo}!")


def evaluar_errores():
    if estado["fase"] in FASES_AGITACION and not estado["presostato"]:
        entrar_en_error("FALTA DE AGUA")
        return True
    return False


def actualizar_agitacion():
    global motor_activo, motor_sentido_cw, t_ultimo_giro
    fase = estado["fase"]

    if fase not in FASES_AGITACION:
        return
    if not estado["presostato"]:
        entrar_en_error("FALTA DE AGUA")
        return
    if estado["giros"] >= MAX_GIROS:
        pasar_siguiente()
        return

    ahora = time.monotonic()
    if motor_activo:
        if ahora - t_ultimo_giro >= duracion(T_GIRO):
            estado["componentes"]["motor_sentido1"] = False
            estado["componentes"]["motor_sentido2"] = False
            motor_activo = False
            t_ultimo_giro = ahora
            if not motor_sentido_cw:
                estado["giros"] += 1
            motor_sentido_cw = not motor_sentido_cw
            actualizar_lcd_agitacion(fase)
    else:
        if ahora - t_ultimo_giro >= duracion(T_PAUSA_GIRO):
            if motor_sentido_cw:
                estado["componentes"]["motor_sentido1"] = True
                estado["componentes"]["motor_sentido2"] = False
            else:
                estado["componentes"]["motor_sentido1"] = False
                estado["componentes"]["motor_sentido2"] = True
            motor_activo = True
            t_ultimo_giro = ahora
            actualizar_lcd_agitacion(fase)


def actualizar_motor():
    """Aproxima el lazo del firmware: rampa + PID simple + decaimiento al parar."""
    global rpm, setpoint, potencia, integral, t_motor, falla_tacometro

    fase = estado["fase"]
    ahora = time.monotonic()
    dt = ahora - t_motor
    t_motor = ahora

    if fase in FASES_AGITACION and motor_activo:
        objetivo = RPM_AGITADO
    elif fase == "CENTRIFUGADO":
        objetivo = RPM_CENTRIFUGADO
        estado["componentes"]["centrifuga"] = True
        estado["componentes"]["motor_sentido1"] = True
        estado["componentes"]["motor_sentido2"] = False
    else:
        objetivo = 0.0

    # Rampa del setpoint (solo sube; para bajar se usa el decaimiento de rpm)
    if objetivo > setpoint:
        setpoint = min(setpoint + RAMPA_RPM_S * dt, objetivo)
    elif objetivo == 0.0:
        setpoint = 0.0

    # PID simplificado para la simulación
    error = setpoint - rpm
    if objetivo > 0:
        integral = max(0.0, integral + Ki * error * dt)
        salida = Kp * error + integral
        potencia = max(0.0, min(POT_MAX_PID, salida))
    else:
        integral = 0.0
        potencia = 0.0

    # Decaimiento simple del tambor/motor
    if objetivo > 0:
        rpm += (objetivo - rpm) * min(1.0, dt * 2.0)
    else:
        rpm *= max(0.0, 1.0 - dt * 0.5)
        if rpm < 1.0:
            rpm = 0.0

    # Falla de tacómetro simulada (se activa a mano con la opción 5)
    if falla_tacometro and fase not in ("ERROR_AGUA", "ERROR_MOTOR"):
        rpm = 0.0
        entrar_en_error("FALLA TACOMETRO")
        return

    estado["motor"]["rpm"] = int(rpm)
    estado["motor"]["objetivo"] = int(setpoint)
    estado["motor"]["potencia"] = int(potencia)


def pasar_siguiente():
    fase = estado["fase"]
    siguiente = FASE_SIGUIENTE.get(fase)
    if siguiente is None:
        return
    giros = (estado["giros"] + 1) if estado["giros"] is not None else 0
    if fase in FASES_DRENAJE:
        estado["presostato"] = False
    aplicar_fase(siguiente, giros)
    print(f"[simulador_motor] Nueva fase: {siguiente} ({estado['estado_ciclo']})")


def avanzar_automatico():
    fase = estado["fase"]
    transcurrido = time.monotonic() - t_inicio_fase

    if fase in FASES_AGITACION:
        actualizar_agitacion()
    elif fase in FASES_LLENADO:
        if estado["presostato"]:
            pasar_siguiente()
    elif fase in FASES_DRENAJE:
        if transcurrido >= duracion(T_DRENAJE):
            pasar_siguiente()
    elif fase == "CENTRIFUGADO":
        if transcurrido >= duracion(T_CENTRIFUGADO):
            pasar_siguiente()


def avanzar_fase(respetar_condiciones: bool = True):
    fase = estado["fase"]

    if fase in ("ERROR_AGUA", "ERROR_MOTOR"):
        print("[simulador_motor] Estás en error: usá la opción 5 para resolverlo.")
        return

    if respetar_condiciones:
        if fase in FASES_AGITACION and not estado["presostato"]:
            entrar_en_error("FALTA DE AGUA")
            return
        if fase in FASES_LLENADO and not estado["presostato"]:
            print("[simulador_motor] Esperando presostato (opción 2).")
            return
        if fase in FASES_AGITACION:
            print("[simulador_motor] La agitación avanza sola (opción 7 para forzar).")
            return

    pasar_siguiente()


def reset_error():
    global falla_tacometro
    if estado["fase"] not in ("ERROR_AGUA", "ERROR_MOTOR"):
        print("[simulador_motor] No hay error activo.")
        return
    if estado["fase"] == "ERROR_AGUA" and not estado["presostato"]:
        print("[simulador_motor] Reset rechazado: presostato vacío.")
        return
    falla_tacometro = False
    destino = fase_previa_error if fase_previa_error in FASES else "LAVADO"
    aplicar_fase(destino)
    print(f"[simulador_motor] Error resuelto -> {destino}")


def procesar_comando(linea: str):
    linea = linea.strip()
    if linea not in ("CMD_START", "CMD_RESET"):
        return
    with lock:
        fase = estado["fase"]
        if linea == "CMD_RESET" and fase in ("ERROR_AGUA", "ERROR_MOTOR"):
            reset_error()
        elif fase == "REPOSO":
            aplicar_fase("LLENADO")
        elif fase in ("ERROR_AGUA", "ERROR_MOTOR"):
            reset_error()
        elif fase == "FIN":
            aplicar_fase("REPOSO")


def hilo_lector():
    buffer = ""
    while True:
        try:
            data = ser.read(200).decode(errors="ignore")
        except serial.SerialException:
            break
        if data:
            buffer += data
            while "\n" in buffer:
                linea, buffer = buffer.split("\n", 1)
                if linea.strip():
                    procesar_comando(linea.strip())


def hilo_escritor():
    while True:
        with lock:
            payload = json.dumps(estado)
        try:
            ser.write((payload + "\n").encode())
        except serial.SerialException:
            break
        time.sleep(1)


def hilo_tiempos():
    while True:
        with lock:
            if estado["fase"] not in ("REPOSO", "FIN", "ERROR_AGUA", "ERROR_MOTOR"):
                avanzar_automatico()
            actualizar_motor()
        time.sleep(0.2)


threading.Thread(target=hilo_lector, daemon=True).start()
threading.Thread(target=hilo_escritor, daemon=True).start()
threading.Thread(target=hilo_tiempos, daemon=True).start()

MENU = """
--- Simulador firmware 2 enjuagues + motor TRIAC (Mega) ---
1) Avanzar a la siguiente fase (respeta el presostato)
2) Togglear presostato
3) Togglear pulsador
4) Forzar error de falta de agua
5) Forzar error de tacómetro (motor)
6) Resolver error
7) Ver estado actual (JSON)
8) Forzar avance de fase (modo libre)
9) Cambiar velocidad de simulación
0) Salir
> """

while True:
    opcion = input(MENU).strip()
    with lock:
        if opcion == "1":
            avanzar_fase(respetar_condiciones=True)
        elif opcion == "2":
            estado["presostato"] = not estado["presostato"]
            print(f"[simulador_motor] Presostato: {estado['presostato']}")
            evaluar_errores()
        elif opcion == "3":
            estado["pulsador"] = not estado["pulsador"]
            print(f"[simulador_motor] Pulsador: {estado['pulsador']}")
        elif opcion == "4":
            entrar_en_error("FALTA DE AGUA")
        elif opcion == "5":
            falla_tacometro = True
            print("[simulador_motor] Falla de tacómetro forzada (en el próximo tick).")
        elif opcion == "6":
            reset_error()
        elif opcion == "7":
            print(json.dumps(estado, indent=2, ensure_ascii=False))
        elif opcion == "8":
            avanzar_fase(respetar_condiciones=False)
        elif opcion == "9":
            ciclo = [1, 2, 5, 10]
            idx = ciclo.index(factor_velocidad) if factor_velocidad in ciclo else 0
            factor_velocidad = ciclo[(idx + 1) % len(ciclo)]
            print(f"[simulador_motor] Velocidad de simulación: {factor_velocidad}x")
        elif opcion == "0":
            break
        else:
            print("Opción inválida")
