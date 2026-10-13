# Dashboard en macOS (sin Raspberry Pi)

Esta guía explica cómo correr el dashboard en una **Mac** (Apple Silicon o
Intel), usando el mismo `main.py` y `dashboard.html` que en la Raspberry.
No hace falta Linux: el backend es Python y el dashboard es una página web.

## Arquitectura en 30 segundos

```
   Arduino Uno                       Mac
  ┌────────────┐   cable USB      ┌──────────────────────┐
  │  firmware  │ ───(/dev/cu.*)─► │  dashboard (main.py) │
  │  (.ino)    │  líneas JSON     │  + navegador web      │
  └────────────┘                   └──────────────────────┘
   C++ / Arduino IDE                Python / pip
   librerías:                       librerías:
   ArduinoJson, LiquidCrystal       fastapi, uvicorn, pyserial
```

Son **dos programas independientes** que se comunican por el cable USB:

- El **Arduino** corre el ciclo de lavado y manda su estado como líneas JSON.
- La **Mac** recibe esas líneas y las muestra en el navegador (dashboard).

El dashboard es de **solo lectura**: no controla la máquina, solo refleja lo
que hace el Arduino. El ciclo se arranca con el botón físico START.

> ⚠ **Solo funciona con Arduino físico.** Esta versión para macOS **no corre
> simulación**: no trae `simulador.py` ni `socat` (son de Linux). Para ver el
> dashboard hay que conectar un Arduino Uno real por USB.

---

## 1. Requisitos

- **Python para macOS** (3.10 o superior):
  - Recomendado: https://www.python.org/downloads/
  - O con Homebrew: `brew install python`
  - Verificar abriendo *Terminal* y escribiendo:
    ```
    python3 --version
    ```
    Tiene que decir 3.10 o más.

- **Arduino IDE** (para subir el firmware): https://www.arduino.cc/en/software
  - Elegir la versión para macOS (Apple Silicon o Intel, según tu Mac).
  - Instalar la librería **ArduinoJson** desde
    *Herramientas → Administrar bibliotecas* (buscar "ArduinoJson").
    **LiquidCrystal** normalmente ya viene incluida con el IDE.

---

## 1.1. Dos programas, dos instalaciones (no confundir)

Este proyecto tiene **dos programas separados**, y cada uno necesita sus
propias librerías. Es la confusión más común:

| | **Arduino Uno** | **La Mac** |
|---|---|---|
| Qué programa | El **firmware** (`.ino`) | El **dashboard** (`main.py`) |
| Lenguaje | C++ | Python |
| Cómo se instala | Arduino IDE → *Administrar bibliotecas* | `pip install -r requirements.txt` |
| Librerías | **ArduinoJson**, LiquidCrystal | fastapi, uvicorn, pyserial, pydantic |
| Dónde viven | En el IDE, al compilar | En el `venv` de Python |

- **ArduinoJson** y **LiquidCrystal** son librerías de **C++ (Arduino)**. Se
  instalan desde el **Arduino IDE**, no con `pip`, y por eso **no** aparecen en
  `requirements.txt`.
- **`requirements.txt`** es solo para las librerías de **Python** de la Mac.
- Los dos programas **no se conectan por librerías, sino por el cable USB**: el
  Arduino manda líneas JSON por el puerto `/dev/cu.*`, y el dashboard (Python)
  las lee. Cada lado compila con lo suyo.

> Si al compilar el sketch aparece `fatal error: ArduinoJson.h: No such file
> or directory`, es que falta instalar **ArduinoJson** en el IDE (ver punto 4).

---

## 2. Copiar la carpeta del proyecto a la Mac

Copiar la carpeta `mac/` completa a la Mac, por ejemplo a:

```
~/proyecto-pi/mac/
```

Es importante que queden juntos `main.py`, `static/dashboard.html` y
`requirements.txt`.

---

## 3. Crear el entorno e instalar dependencias (una sola vez)

Abrir *Terminal* en la carpeta y ejecutar:

```bash
cd ~/proyecto-pi/mac
python3 -m venv venv
source venv/bin/activate
python3 -m pip install --upgrade pip
python3 -m pip install -r requirements.txt
```

> Esto crea una carpeta `venv/` propia de macOS (no sirve la de Linux ni la de
> Windows, hay que crearla de nuevo). Las dependencias son **fastapi, uvicorn,
> pyserial y pydantic**. Son librerías de **Python** (se instalan con `pip`);
> no tienen nada que ver con las librerías del Arduino (ver punto 1.1).

---

## 4. Subir el firmware con el Arduino IDE

1. Conectar el Arduino Uno por USB.
2. Abrir el sketch. **Elegir la variante que se quiera**:
   - Original (fiel al profesor):
     `firmware/lavarropas_controller_display_error_buzzer/`
   - Con dos enjuagues (enjuague final con agua + suavizante):
     `firmware/lavarropas_controller_display_error_buzzer_2enjuagues/`
3. En el IDE: *Herramientas → Placa → Arduino Uno*.
4. *Herramientas → Puerto*: elegir el puerto donde apareció el Arduino.
   En macOS se ve algo como `/dev/cu.usbmodem1101` o
   `/dev/cu.usbserial-XXXXXXXX` (ver punto 5).
5. *Herramientas → Administrar bibliotecas*: instalar **ArduinoJson**
   (buscar "ArduinoJson"). **LiquidCrystal** normalmente ya viene incluida.
6. Presionar **Subir** (flecha →).

> ⚠ El IDE exige que la carpeta tenga el **mismo nombre que el `.ino`**. Ya
> están así: no renombrar.

---

## 5. Averiguar el puerto del Arduino

El Arduino Uno se conecta por **USB** y aparece como un puerto serie. En macOS
el nombre es del estilo:

```
/dev/cu.usbmodem1101
/dev/cu.usbserial-XXXXXXXX
/dev/cu.wchusbserialXXXXXXXX
```

- El **mismo** puerto que se elige en el Arduino IDE para grabar es el que usa
  el dashboard para leer. No es un puerto nuevo ni distinto.
- El número **puede cambiar** según en qué USB se enchufe.

Opción A (lo más fácil): en el Arduino IDE, *Herramientas → Puerto* muestra el
nombre exacto.

Opción B (por Terminal, con el venv activado):

```bash
python3 -m serial.tools.list_ports
```

Va a mostrar una lista con los puertos y sus descripciones.

---

## 6. Correr el dashboard

Con el venv activado y **el Monitor Serie del IDE cerrado** (si está abierto,
ocupa el puerto y el dashboard no puede leer):

```bash
cd ~/proyecto-pi/mac
source venv/bin/activate
python3 -m uvicorn main:app --host 0.0.0.0 --port 8000
```

Abrir el navegador en:

```
http://localhost:8000
```

Debería verse el panel con "🟢 Arduino conectado".

### Si no detecta el Arduino

La detección es automática por VID. Si falla, se puede forzar el puerto con la
variable de entorno `PUERTO_FORZADO`:

```bash
PUERTO_FORZADO=/dev/cu.usbmodem1101 python3 -m uvicorn main:app --host 0.0.0.0 --port 8000
```

Reemplazar `/dev/cu.usbmodem1101` por el puerto real.

---

## 7. Lanzador rápido (opcional)

Hay un `iniciar_mac.sh` en esta carpeta que hace todo lo del punto 6. Se puede
usar así:

```bash
./iniciar_mac.sh
```

Si el puerto no es automático, se le pasa como argumento:

```bash
./iniciar_mac.sh /dev/cu.usbmodem1101
```

> Si no tiene permiso de ejecución, darle una vez con:
> `chmod +x iniciar_mac.sh`

---

## 8. Ver el dashboard desde el celular o la tablet

Si la Mac y el celular están en la misma red WiFi, averiguar la IP de la Mac:

```bash
ipconfig getifaddr en0
```

(o `ipconfig getifaddr en1` si `en0` no da nada).

Abrir desde el celular:

```
http://192.168.1.50:8000
```

(sustituyendo por la IP real). macOS puede pedir permiso de firewall la
primera vez: aceptar en **redes privadas**.

---

## 9. Notas y diferencias con la Raspberry / Windows

| Tema | Raspberry (Linux) | macOS |
|------|-------------------|-------|
| Puerto serie | `/dev/ttyACM0` | `/dev/cu.usbmodem*` |
| Activar venv | `source venv/bin/activate` | `source venv/bin/activate` (igual) |
| Forzar puerto | `export PUERTO_FORZADO=...` | `PUERTO_FORZADO=...` (misma sintaxis) |
| Scripts `.sh` | `iniciar_pruebas.sh` | `iniciar_mac.sh` |
| Detección Arduino | por VID | por VID (igual) |
| Dashboard / backend | idénticos | idénticos |

- El Monitor Serie del Arduino IDE y el dashboard **no pueden usar el puerto a
  la vez**. Si se está mirando el Monitor Serie, cerrarlo antes de levantar el
  dashboard (y viceversa).
- Si `uvicorn` no se reconoce, usar `python3 -m uvicorn main:app ...`.

---

## 9.1. Archivos que NO hacen falta con el Arduino real (opcional)

Si se usa el **Arduino Uno real** (que es el caso del profesor), estos
archivos son **opcionales** y se pueden ignorar sin problema:

| Archivo | Qué es | ¿Se necesita con Arduino real? |
|---------|--------|--------------------------------|
| `simulador.py` | Finge ser el Arduino, para probar sin hardware | **No** |
| `iniciar_pruebas.sh` | Script de Linux (Bash) que levanta socat + simulador + backend | **No** |
| `socat` | Crea puertos virtuales (solo Linux) | **No** |

> Nota: en esta carpeta `mac/` ni siquiera vienen `simulador.py` ni
> `iniciar_pruebas.sh`, porque no aplican al profesor en macOS.

### Camino simple (el que va a usar el profesor)

Con Arduino real **no hay simulación de por medio**. Solo dos pasos:

```
1) Arduino IDE  →  subir el firmware por USB (/dev/cu.*)
2) iniciar_mac.sh  →  leer ese mismo puerto y mostrar el dashboard
```

Sin `socat`, sin `tmux`, sin simulador. El resto de la carpeta
(`main.py`, `static/`, `requirements.txt`, `iniciar_mac.sh`) es lo único
que se usa.

---

## 10. Cómo cerrar

En la ventana del servidor, `Ctrl+C`. Para desactivar el entorno:

```bash
deactivate
```
