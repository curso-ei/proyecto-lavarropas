"""
Dashboard FastAPI para el ciclo de lavado (Raspberry + Arduino).

Protocolo esperado del Arduino (línea JSON por Serial, cada vez que cambia algo):
{
  "estado_ciclo": "inactivo" | "llenando" | "lavando" | "enjuagando"
                  | "desagotando" | "suavizando" | "centrifugando"
                  | "finalizado" | "error",
  "fase": "REPOSO" | "LLENADO" | ... (nombre crudo de la fase del firmware),
  "error": null | "texto del error",
  "giros": int | null,          # solo durante LAVADO/ENJUAGUE
  "max_giros": int | null,
  "lcd": ["linea1", "linea2"], # texto que muestra el LCD fisico
  "componentes": {
    "bomba_llenado": bool,
    "motor_sentido1": bool,
    "motor_sentido2": bool,
    "bomba_desagote": bool,
    "bloqueo_puerta": bool,   # PIN_TRABA del firmware = "En marcha"
    "suavizante": bool,
    "centrifuga": bool,
    "buzzer": bool            # alarma sonora (parpadea en error)
  },
  "presostato": bool,   # true = nivel de agua OK
  "pulsador": bool      # true = START presionado
}

Este dashboard es de SOLO LECTURA: no controla reles. La unica accion
prevista (y DESACTIVADA por defecto, ver Opcion 3.B en README) es
POST /accion/start y POST /accion/reset, que mandan CMD_START / CMD_RESET
para simular la pulsacion del boton fisico.

Corre con:
    source venv/bin/activate
    uvicorn main:app --host 0.0.0.0 --port 8000 --reload
"""

import asyncio
import json
import os
from collections import deque
from contextlib import asynccontextmanager
from datetime import datetime

import serial
import serial.tools.list_ports
from fastapi import FastAPI, HTTPException
from fastapi.responses import FileResponse
from fastapi.staticfiles import StaticFiles
from pydantic import BaseModel

ARDUINO_VIDS = (0x2341, 0x1A86)  # 2341 = Arduino oficial, 1a86 = clones CH340
BAUDRATE = 9600
POLL_INTERVAL = 2  # segundos
MAX_LOG = 30  # cuántos eventos guardar en el historial

# Componentes que reporta el firmware del profesor. Son TODOS de solo
# lectura: el dashboard no los controla.
COMPONENTES_VALIDOS = {
    "bomba_llenado",
    "motor_sentido1",
    "motor_sentido2",
    "bomba_desagote",
    "bloqueo_puerta",   # PIN_TRABA = "En marcha"
    "suavizante",
    "centrifuga",
    "buzzer",           # alarma sonora
}

ESTADO_DEFAULT = {
    "estado_ciclo": "sin_datos",
    "fase": None,
    "error": None,
    "giros": None,
    "max_giros": None,
    "lcd": ["", ""],
    "componentes": {c: False for c in COMPONENTES_VALIDOS},
    "presostato": False,
    "pulsador": False,
}


def encontrar_arduino() -> str | None:
    """Busca un dispositivo serie con VID conocido de Arduino.

    Si la variable de entorno PUERTO_FORZADO está seteada (útil para probar
    con el simulador de puerto virtual, que no tiene VID/PID real), se usa
    esa directamente sin buscar por VID.
    """
    forzado = os.environ.get("PUERTO_FORZADO")
    if forzado:
        return forzado
    for p in serial.tools.list_ports.comports():
        if p.vid in ARDUINO_VIDS:
            return p.device
    return None


class SerialManager:
    def __init__(self):
        self.conn: serial.Serial | None = None
        self.estado: dict = dict(ESTADO_DEFAULT)
        self.conectado = False
        self.log: deque = deque(maxlen=MAX_LOG)

    def _registrar_evento(self, mensaje: str):
        hora = datetime.now().strftime("%H:%M:%S")
        self.log.appendleft(f"{hora} - {mensaje}")

    def _intentar_conectar(self):
        puerto = encontrar_arduino()
        if puerto is None:
            self.conectado = False
            return
        try:
            self.conn = serial.Serial(puerto, BAUDRATE, timeout=1)
            self.conectado = True
            self._registrar_evento(f"Arduino conectado en {puerto}")
        except serial.SerialException as e:
            self.conectado = False
            self._registrar_evento(f"Error al conectar: {e}")

    def _leer_estado(self):
        """Lee TODAS las líneas pendientes en el buffer, no solo una.

        Si no se hace así, cuando el emisor manda datos más rápido de lo que
        acá se lee, el buffer se acumula y el estado mostrado queda cada vez
        más desfasado en el tiempo.
        """
        try:
            while self.conn and self.conn.in_waiting:
                linea = self.conn.readline().decode(errors="ignore").strip()
                if not linea:
                    continue
                nuevo = json.loads(linea)

                if nuevo.get("estado_ciclo") != self.estado.get("estado_ciclo"):
                    self._registrar_evento(f"Estado del ciclo: {nuevo.get('estado_ciclo')}")
                if nuevo.get("error") and nuevo.get("error") != self.estado.get("error"):
                    self._registrar_evento(f"ERROR: {nuevo.get('error')}")

                self.estado = nuevo
        except (serial.SerialException, OSError):
            self.conectado = False
            self.conn = None
            self._registrar_evento("Se perdió la conexión con el Arduino")
        except json.JSONDecodeError:
            pass  # línea corrupta o parcial, se ignora y se sigue con las demás

    def enviar_comando(self, comando: str) -> bool:
        if not (self.conectado and self.conn):
            return False
        try:
            self.conn.write((comando + "\n").encode())
            return True
        except (serial.SerialException, OSError):
            self.conectado = False
            self.conn = None
            return False

    async def loop_background(self):
        while True:
            if not self.conectado:
                self._intentar_conectar()
                await asyncio.sleep(POLL_INTERVAL)
            else:
                self._leer_estado()
                await asyncio.sleep(0.3)  # más seguido mientras está conectado


serial_manager = SerialManager()


@asynccontextmanager
async def lifespan(app: FastAPI):
    task = asyncio.create_task(serial_manager.loop_background())
    yield
    task.cancel()


app = FastAPI(lifespan=lifespan)


class ComandoControl(BaseModel):
    encendido: bool


# Acciones validas de la Opcion 3.B (arranque/reset remoto). En la Opcion
# 3.A el firmware NO lee comandos, asi que estos endpoints quedan listos
# pero no tienen efecto hasta habilitar el bloque correspondiente en el .ino.
ACCIONES_VALIDAS = {
    "start": "CMD_START",
    "reset": "CMD_RESET",
}


@app.get("/estado")
def get_estado():
    return {
        "arduino_conectado": serial_manager.conectado,
        "datos": serial_manager.estado,
        "log": list(serial_manager.log),
    }


# ---------------------------------------------------------------------------
# Control de reles: ELIMINADO a proposito.
# El dashboard es un espejo de solo lectura. El endpoint POST /control/{...}
# del ejemplo original ya no existe.
# ---------------------------------------------------------------------------


@app.post("/accion/{nombre}")
def accion(nombre: str):
    """Arranque / reset remoto (Opcion 3.B, DESACTIVADA por defecto).

    Manda CMD_START o CMD_RESET por Serial. Mientras el firmware no tenga
    habilitado su parser de comandos, el Arduino los ignora y no pasa nada.
    """
    if nombre not in ACCIONES_VALIDAS:
        raise HTTPException(400, f"Accion desconocida: {nombre}")
    ok = serial_manager.enviar_comando(ACCIONES_VALIDAS[nombre])
    return {"enviado": ok, "accion": nombre}


# Sirve el dashboard estático (dashboard.html en la misma carpeta)
app.mount("/static", StaticFiles(directory="static"), name="static")


@app.get("/")
def index():
    return FileResponse("static/dashboard.html")

