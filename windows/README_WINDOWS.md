# Dashboard en Windows (sin Raspberry Pi)

Esta guía explica cómo correr el dashboard en una PC con **Windows**, usando
el mismo `main.py` y `dashboard.html` que en la Raspberry. No hace falta
Linux: el backend es Python y el dashboard es una página web.

## Arquitectura en 30 segundos

```
   Arduino Uno                       PC con Windows
  ┌────────────┐   cable USB      ┌──────────────────────┐
  │  firmware  │ ───(COMx)──────► │  dashboard (main.py) │
  │  (.ino)    │  líneas JSON     │  + navegador web      │
  └────────────┘                   └──────────────────────┘
   C++ / Arduino IDE                Python / pip
   librerías:                       librerías:
   ArduinoJson, LiquidCrystal       fastapi, uvicorn, pyserial
```

Son **dos programas independientes** que se comunican por el cable USB:

- El **Arduino** corre el ciclo de lavado y manda su estado como líneas JSON.
- La **PC** recibe esas líneas y las muestra en el navegador (dashboard).

El dashboard es de **solo lectura**: no controla la máquina, solo refleja lo
que hace el Arduino. El ciclo se arranca con el botón físico START.

> ⚠ **Solo funciona con Arduino físico.** Esta versión para Windows **no corre
> simulación**: el simulador necesita `socat`/`com0com` (ver sección 9.1). Para
> ver el dashboard hay que conectar un Arduino Uno real por USB.

---

## 1. Requisitos

- **Python para Windows** (3.10 o superior): https://www.python.org/downloads/
  - Al instalar, tildar **"Add python.exe to PATH"**. Es lo más importante.
  - Verificar abriendo *Símbolo del sistema* (cmd) y escribiendo:
    ```
    python --version
    ```
    Si dice `py` en lugar de `python`, usá `py` en los comandos de abajo.

- **Arduino IDE** (para subir el firmware): https://www.arduino.cc/en/software
  - Instalar la librería **ArduinoJson** desde
    *Herramientas → Administrar bibliotecas* (buscar "ArduinoJson").
    **LiquidCrystal** normalmente ya viene incluida con el IDE.

---

## 1.1. Dos programas, dos instalaciones (no confundir)

Este proyecto tiene **dos programas separados**, y cada uno necesita sus
propias librerías. Es la confusión más común:

| | **Arduino Uno** | **La PC** |
|---|---|---|
| Qué programa | El **firmware** (`.ino`) | El **dashboard** (`main.py`) |
| Lenguaje | C++ | Python |
| Cómo se instala | Arduino IDE → *Administrar bibliotecas* | `pip install -r requirements.txt` |
| Librerías | **ArduinoJson**, LiquidCrystal | fastapi, uvicorn, pyserial, pydantic |
| Dónde viven | En el IDE (PC), al compilar | En el `venv` de Python |

- **ArduinoJson** y **LiquidCrystal** son librerías de **C++ (Arduino)**. Se
  instalan desde el **Arduino IDE**, no con `pip`, y por eso **no** aparecen en
  `requirements.txt`.
- **`requirements.txt`** es solo para las librerías de **Python** de la PC.
- Los dos programas **no se conectan por librerías, sino por el cable USB**: el
  Arduino manda líneas JSON por el puerto `COMx`, y el dashboard (Python) las
  lee. Cada lado compila con lo suyo.

> Si al compilar el sketch aparece `fatal error: ArduinoJson.h: No such file
> or directory`, es que falta instalar **ArduinoJson** en el IDE (ver punto 4).

---

## 2. Copiar la carpeta del proyecto a Windows

Copiar la carpeta `windows/` completa a la PC, por ejemplo a:

```
C:\proyecto-pi\windows\
```

Es importante que queden juntos `main.py`, `static\dashboard.html` y
`requirements.txt`.

---

## 3. Crear el entorno e instalar dependencias (una sola vez)

Abrir *Símbolo del sistema* en la carpeta y ejecutar:

```bat
cd C:\proyecto-pi\windows
python -m venv venv
venv\Scripts\activate
python -m pip install --upgrade pip
python -m pip install -r requirements.txt
```

> Esto crea una carpeta `venv\` propia de Windows (no sirve la de Linux, hay
> que crearla de nuevo en Windows). Las dependencias son **fastapi, uvicorn,
> pyserial y pydantic**. Son librerías de **Python** (se instalan con `pip`);
> no tienen nada que ver con las librerías del Arduino (ver punto 1.1).

---

## 4. Subir el firmware con el Arduino IDE

1. Conectar el Arduino Uno por USB.
2. Abrir el sketch. **Elegir la variante que se quiera**:
   - Original (fiel al profesor):
     `firmware\lavarropas_controller_display_error_buzzer\`
   - Con dos enjuagues (enjuague final con agua + suavizante):
     `firmware\lavarropas_controller_display_error_buzzer_2enjuagues\`
3. En el IDE: *Herramientas → Placa → Arduino Uno*.
4. *Herramientas → Puerto*: elegir el `COMx` donde apareció el Arduino
   (¿qué es `COMx`? ver punto 5).
5. *Herramientas → Administrar bibliotecas*: instalar **ArduinoJson**
   (buscar "ArduinoJson"). **LiquidCrystal** normalmente ya viene incluida.
   Recordar: son librerías de **Arduino (C++)**, no de Python (ver punto 1.1).
6. Presionar **Subir** (flecha →).

> ⚠ El IDE exige que la carpeta tenga el **mismo nombre que el `.ino`**. Ya
> están así: no renombrar.

### ¿Y si el Arduino no tiene LCD conectado?

**No afecta en nada, y el dashboard sigue igual.**

- El sketch usa el LCD con los pines **A0, A1, A2, A3, 5 y 6**, pero si no
  hay LCD físico, esas llamadas simplemente escriben en pines vacíos. **No da
  error ni rompe nada.**
- El **dashboard sigue mostrando el panel LCD** con los mensajes
  (`LAVARROPAS`, `CARGANDO AGUA`, `FASE: LAVADO`, ...). ¿Por qué? Porque no lee
  el LCD físico: el Arduino le manda por USB las dos líneas de texto que ya
  tiene guardadas en memoria. O sea, el dashboard "ve" el texto aunque no
  exista pantalla.
- **LiquidCrystal igual hay que tenerla** para que el sketch compile, aunque
  no haya LCD. En el IDE suele venir de fábrica.
- Único recaudo: si no hay LCD, esos 6 pines deberían quedar **libres** (sin
  otra cosa cableada), para no chocar con lo que el sketch escribe ahí.

---

## 5. Averiguar el puerto COM (¿qué es `COMx`?)

El Arduino Uno se conecta por **USB**, pero adentro tiene un chip que convierte
USB a **puerto serie**. Windows le da a ese puerto un nombre del estilo
`COM3`, `COM4`, etc. (`COMx` = "cualquier COM con número x").

- El **mismo** `COMx` que se elige en el Arduino IDE para grabar es el que usa
  el dashboard para leer. No es un puerto nuevo ni distinto.
- El número **puede cambiar** según en qué USB se enchufe y cuántos puertos
  tenga la PC. Por eso hay que revisarlo cada vez.

Opción A (lo más fácil): *Administrador de dispositivos → Puertos (COM y LPT)*.
El Arduino figura como `Arduino Uno (COM3)` o `USB-SERIAL CH340 (COM4)`.

Opción B (por consola, con el venv activado):

```bat
python -m serial.tools.list_ports
```

---

## 6. Correr el dashboard

Con el venv activado y **el Monitor Serie del IDE cerrado** (si está abierto,
ocupa el puerto y el dashboard no puede leer):

```bat
cd C:\proyecto-pi\windows
venv\Scripts\activate
uvicorn main:app --host 0.0.0.0 --port 8000
```

Abrir el navegador en:

```
http://localhost:8000
```

Debería verse el panel con "🟢 Arduino conectado".

### Si no detecta el Arduino

La detección es automática por VID. Si falla, se puede forzar el puerto con
la variable de entorno `PUERTO_FORZADO` (en Windows se usa `set`):

```bat
set PUERTO_FORZADO=COM3
uvicorn main:app --host 0.0.0.0 --port 8000
```

Reemplazar `COM3` por el puerto real.

---

## 7. Lanzador rápido (opcional)

Hay un `iniciar_windows.bat` en esta carpeta que hace todo lo del punto 6.
Se puede usar doble clic. Si el puerto no es automático, se le pasa como
argumento:

```bat
iniciar_windows.bat COM3
```

---

## 8. Ver el dashboard desde el celular o la tablet

Si la PC y el celular están en la misma red WiFi, averiguar la IP de la PC:

```bat
ipconfig
```

Buscar la "Dirección IPv4" (algo como `192.168.1.50`) y abrir desde el
celular:

```
http://192.168.1.50:8000
```

Windows puede pedir permiso de firewall la primera vez: aceptar en **redes
privadas**.

---

## 9. Notas y diferencias con la Raspberry

| Tema | Raspberry (Linux) | Windows |
|------|-------------------|---------|
| Puerto serie | `/dev/ttyACM0` | `COM3`, `COM4`, ... |
| Activar venv | `source venv/bin/activate` | `venv\Scripts\activate` |
| Forzar puerto | `export PUERTO_FORZADO=...` | `set PUERTO_FORZADO=...` |
| Scripts `.sh` | `iniciar_pruebas.sh` | no aplica (usar `iniciar_windows.bat`; ver 9.1) |
| Detección Arduino | por VID | por VID (igual) |
| Dashboard / backend | idénticos | idénticos |

- El Monitor Serie del Arduino IDE y el dashboard **no pueden usar el puerto a
  la vez**. Si se está mirando el Monitor Serie, cerrarlo antes de levantar el
  dashboard (y viceversa).

- Si `uvicorn` no se reconoce, usar `python -m uvicorn main:app ...`.

---

## 9.1. Archivos que NO hacen falta con el Arduino real (opcional)

Si se usa el **Arduino Uno real** (que es el caso del profesor), estos
archivos son **opcionales** y se pueden ignorar sin problema:

| Archivo | Qué es | ¿Se necesita con Arduino real? |
|---------|--------|--------------------------------|
| `simulador.py` | Finge ser el Arduino, para probar sin hardware | **No** |
| `iniciar_pruebas.sh` | Script de Linux (Bash) que levanta socat + simulador + backend | **No** |
| `socat` | Crea puertos virtuales (solo Linux) | **No** |

Son **herramientas de desarrollo** para probar el dashboard sin conectar el
Arduino. Existían antes de tener el hardware. Con el Arduino real **no se usan
para nada**.

### Por qué no corren en Windows

- **`iniciar_pruebas.sh`**: es un script de **Bash**. Windows no trae Bash (solo
  con WSL o Git Bash) y además usa `socat` y `tmux`, que no son de Windows.
  **No aplica.**
- **`simulador.py`**: el script en sí es Python y podría correr, pero necesita
  un **par de puertos virtuales**. En Linux se crean con `socat`; en Windows
  habría que instalar **com0com**. Es un lío innecesario si ya se tiene el
  Arduino.

### Camino simple (el que va a usar el profesor)

Con Arduino real **no hay simulación de por medio**. Solo dos pasos:

```
1) Arduino IDE  →  subir el firmware por USB (COMx)
2) iniciar_windows.bat  →  leer ese mismo COMx y mostrar el dashboard
```

Sin `socat`, sin `tmux`, sin simulador. El resto de la carpeta
(`main.py`, `static/`, `requirements.txt`, `iniciar_windows.bat`) es lo único
que se usa.

---

## 10. Cómo cerrar

En la ventana del servidor, `Ctrl+C`. Para desactivar el entorno:

```bat
deactivate
```
