# Integración del controlador del profesor con el dashboard

> **Documento de decisiones, problemas y soluciones: [`DECISIONES.md`](DECISIONES.md)**
> Ahí está el detalle de cada decisión (con la evidencia del código), los
> problemas encontrados y cómo se resolvieron.
>
> **¿Usás Windows (PC con Arduino IDE) en vez de la Raspberry?** Ver
> [`windows/README_WINDOWS.md`](windows/README_WINDOWS.md): pasos completos y
> lanzador `windows/iniciar_windows.bat`. El backend y el dashboard son los
> mismos.
>
> **¿Usás macOS (Mac con Arduino IDE)?** Ver
> [`mac/README_MAC.md`](mac/README_MAC.md): pasos completos y lanzador
> `mac/iniciar_mac.sh`. El backend y el dashboard son los mismos.
>
> **¿Querés verlo en un celular Android sin PC ni Raspberry?** Ver
> [`android/README_ANDROID.md`](android/README_ANDROID.md): el navegador lee
> el Arduino directo por USB OTG (Web Serial), sin backend y sin root.

## Documentación de red y menú del curso (curso-menu)

El menú de red de la Raspberry (`curso-menu`) y sus modos (AP, AP + STA,
Solo Cliente WiFi) están documentados por separado:

- Manual de uso del menú: [`curso-raspap/CURSO_MENU.md`](curso-raspap/CURSO_MENU.md)
- Modos de red: [`curso-raspap/MODOS_AP_APSTA.md`](curso-raspap/MODOS_AP_APSTA.md)
- Instalación: [`curso-raspap/INSTALACION.md`](curso-raspap/INSTALACION.md)
- Diagnóstico AP + STA: [`scripts/DIAGNOSTICO_AP_STA.md`](scripts/DIAGNOSTICO_AP_STA.md)

## Objetivo

Conservar **casi sin cambios** el firmware del profesor
(`lavarropas_controller_display_error_buzzer.ino`) y adaptar la capa de
software (backend + dashboard) para que **refleje** lo que el Arduino hace.

Reglas de esta carpeta de trabajo:

- Esta carpeta (`~/proyecto-pi`) es ahora la **versión canónica para la
  Raspberry**. El `.ino` original del profesor está en
  `firmware/original_profesor/`.
- El **dashboard es de solo lectura**: no controla relés, no manda comandos
  de encendido/apagado. Solo muestra estado, componentes, alarma y log.
- El firmware conserva **pines, lógica, máquina de estados, LCD y buzzer**.
  Solo se le agrega lo mínimo para reportar por Serial en JSON.
- El único control previsto desde el dashboard es **START / Reset de error**
  (lo equivalente a pulsar el botón físico), porque es parte natural del
  ciclo. Se implementa como comando serial y **no reemplaza** al pulsador.

## Archivos en esta carpeta

```
proyecto-pi/
├── README.md                      ← este documento
├── DECISIONES.md                  ← decisiones, problemas y propuesta del ciclo
├── main.py                        ← backend (espejo de solo lectura)
├── simulador.py                   ← simulador fiel (variantes CICLO=...)
├── requirements.txt               ← dependencias Python (fastapi, uvicorn, pyserial, pydantic)
├── iniciar_pruebas.sh             ← (Linux) socat+simulador+backend en tmux
├── arduino_menu.sh                ← menú interactivo (check, subir firmware, backend)
├── check_arduino.sh               ← diagnóstico de detección del Arduino
├── static/
│   └── dashboard.html             ← dashboard (solo lectura)
├── firmware/
│   ├── lavarropas_controller_display_error_buzzer/
│   │   └── lavarropas_controller_display_error_buzzer.ino          ← original + JSON (default)
│   ├── lavarropas_controller_display_error_buzzer_2enjuagues/
│   │   └── lavarropas_controller_display_error_buzzer_2enjuagues.ino  ← VARIANTE (2 enjuagues)
│   ├── lavarropas_motor_cruce_cero/
│   │   └── lavarropas_motor_cruce_cero.ino  ← VARIANTE (control de motor con cruce por cero, PID + TRIAC)
│   ├── lavarropas_controller_2enjuagues_motor_triac_MEGA/
│   │   └── lavarropas_controller_2enjuagues_motor_triac_MEGA.ino  ← 2 enjuagues + motor TRIAC (Mega 2560)
│   └── original_profesor/
│       └── lavarropas_controller_display_error_buzzer.ino          ← original SIN modificar (referencia)
├── windows/                       ← carpeta autocontenida para el profe en Windows
│   ├── README_WINDOWS.md
│   ├── iniciar_windows.bat
│   ├── main.py
│   ├── requirements.txt
│   ├── static/
│   └── firmware/
├── mac/                           ← carpeta autocontenida para el profe en macOS
│   ├── README_MAC.md
│   ├── iniciar_mac.sh
│   ├── main.py
│   ├── requirements.txt
│   ├── static/
│   └── firmware/
├── android/                       ← dashboard PWA para celular (Web Serial, sin backend)
│   ├── README_ANDROID.md
│   ├── dashboard.html
│   ├── manifest.webmanifest
│   ├── sw.js
│   ├── icon-192.png
│   └── icon-512.png
```

> Cada carpeta de sketch tiene el **mismo nombre que su `.ino`**, que es lo que
> exige el arduino-cli / IDE para compilar.
>
> **El `.ino` original del profesor no se tocó.** La carpeta
> `..._2enjuagues` es una **variante derivada**. Ver
> [`DECISIONES.md` sección 8](DECISIONES.md).

> ⚠ **Observación sobre el firmware original:** la fase `SUAVIZANTE` solo
> abre el relé de suavizante durante 10 s: **no carga agua ni hace girar el
> motor**. El detalle y el contexto están en
> [`DECISIONES.md`, sección 9 (O1)](DECISIONES.md#o1--la-fase-suavizante-no-llena-agua-ni-hace-girar-el-motor).

## Estado actual (qué NO se toca del firmware)

El `.ino` del profesor ya trae, y se conserva tal cual:

- Máquina de estados no bloqueante con `millis()`:
  `REPOSO → LLENADO → LAVADO → DRENAJE_1 → LLENADO_ENJUAGUE → ENJUAGUE →
  DRENAJE_2 → SUAVIZANTE → DRENAJE_3 → CENTRIFUGADO → FIN`, más `ERROR_AGUA`.
- Pines (sin cambios):
  - Entradas: `PIN_START = 2`, `PIN_PRESOSTATO = 3`.
  - Salidas: `PIN_TRABA = 4`, `PIN_BUZZER = 7`, `PIN_LLENADO = 8`,
    `PIN_MOTOR_CW = 9`, `PIN_MOTOR_CCW = 10`, `PIN_DRENAJE = 11`,
    `PIN_SUAVIZANTE = 12`, `PIN_CENTRIFUGA = 13`.
  - LCD 16x2: `LiquidCrystal lcd(A2, 6, A3, 5, A0, A1)`.
- Relés activos en LOW (`RELAY_ACTIVE_LOW = true`), antirrebote del START,
  presostato `LOW = lleno`, buzzer con `tone(2500)/noTone()`, parpadeo del
  error cada 500 ms y reanudación del ciclo al resolver el error.

## Lo mínimo a agregar al firmware

1. **Salida JSON periódica** (cada ~500 ms–1 s) con el mismo formato que ya
   conoce `main.py`:

   ```json
   {
     "estado_ciclo": "lavando",
     "error": null,
     "fase": "LAVADO",
     "giros": 1,
     "max_giros": 3,
     "lcd": ["FASE: LAVADO", "Giro 2/3 >>"],
     "componentes": {
       "bomba_llenado": false,
       "motor_sentido1": true,
       "motor_sentido2": false,
       "bomba_desagote": false,
       "bloqueo_puerta": true,
       "suavizante": false,
       "centrifuga": false,
       "buzzer": false
     },
     "presostato": true,
     "pulsador": false
   }
   ```

   - Se usa `ArduinoJson` (`StaticJsonDocument<384>`), sin concatenar
     `String`, para no fragmentar el heap del Uno.
   - `error` se setea cuando `faseActual == ERROR_AGUA` (`"FALTA DE AGUA"`).
   - `pulsador` = estado del START ya antirrebotado.
   - `componentes` sale de leer el estado real de los pines con
     `digitalRead` (o de un espejo actualizado en `setRelay`), no de
     suposiciones.

2. **Parseo de un comando serial mínimo** (no invasivo):
   - `CMD_START` → dispara la misma acción que el botón físico.
   - `CMD_RESET` → reintenta salir de `ERROR_AGUA` (equivale a START con
     presostato lleno).

   La lógica del ciclo no cambia: el comando solo simula la pulsación.

3. **Quitar/limitar el `Serial.print("[LCD] ...")`**: hoy `mostrarLCD()`
   escribe texto por Serial cada vez que cambia el LCD. Si se deja, el backend
   lo ignora (`JSONDecodeError` contemplado), pero ensucia el monitor serie.
   Recomendado: que el LCD físico reciba el texto y que el JSON sea el único
   canal de estado. El texto del LCD igual viaja dentro del campo `"lcd"`.

## Mapa de componentes dashboard ↔ firmware

| Dashboard (JSON)   | Firmware                      | Pin | Estado |
|--------------------|-------------------------------|-----|--------|
| `bomba_llenado`    | `PIN_LLENADO`                 | 8   | coincide |
| `motor_sentido1`   | `PIN_MOTOR_CW`                | 9   | coincide |
| `motor_sentido2`   | `PIN_MOTOR_CCW`               | 10  | coincide |
| `bomba_desagote`   | `PIN_DRENAJE`                 | 11  | coincide |
| `bloqueo_puerta`   | `PIN_TRABA` (marcha/traba)    | 4   | reinterpretar etiqueta |
| `suavizante`       | `PIN_SUAVIZANTE`              | 12  | **nuevo en dashboard** |
| `centrifuga`       | `PIN_CENTRIFUGA`              | 13  | **nuevo en dashboard** |
| `buzzer`           | `PIN_BUZZER`                  | 7   | **nuevo (alarma)** |
| `presostato`       | `PIN_PRESOSTATO`              | 3   | coincide (`LOW=lleno`) |
| `pulsador`         | `PIN_START`                   | 2   | coincide |
| —                  | LCD                           | A2,6,A3,5,A0,A1 | se refleja en `"lcd"` |

`resistencia` **desaparece**: no existe en el firmware del profesor.

## Mapa de estados del ciclo

| Firmware (`Fase`)   | Dashboard (`estado_ciclo`) |
|---------------------|----------------------------|
| `REPOSO`            | `inactivo`                 |
| `LLENADO`           | `llenando`                 |
| `LLENADO_ENJUAGUE`  | `llenando` (enjuague)      |
| `LAVADO`            | `lavando`                  |
| `ENJUAGUE`          | `enjuagando`               |
| `DRENAJE_1/2/3`     | `desagotando`              |
| `SUAVIZANTE`        | `suavizando`               |
| `CENTRIFUGADO`      | `centrifugando`            |
| `FIN`               | `finalizado`               |
| `ERROR_AGUA`        | `error` + `error:"FALTA DE AGUA"` |

Se conserva además el nombre crudo de la fase en `"fase"` (ej. `"LAVADO"`)
para no perder información.

## Cambios por archivo (resumen)

### `firmware/.../*.ino`
- Agregar `#include <ArduinoJson.h>`.
- Agregar `enviarEstadoJSON()` y llamarla por intervalo en `loop()`.
- Agregar `leerComandosSerial()` (`CMD_START`, `CMD_RESET`).
- Exponer `pulsador` y `error` en el JSON.
- **No** cambiar pines, tiempos, fases ni la lógica de `manejar*`.

### `main.py`
- `COMPONENTES_VALIDOS` → claves reales: `bomba_llenado`, `motor_sentido1`,
  `motor_sentido2`, `bomba_desagote`, `bloqueo_puerta`, `suavizante`,
  `centrifuga`, `buzzer`. Sale `resistencia`.
- `ESTADO_DEFAULT` con las claves nuevas y con `fase`, `giros`, `lcd`.
- Nuevo endpoint de acciones (solo START/RESET), por ejemplo
  `POST /accion/{nombre}` con `start` / `reset`, que manda `CMD_START` /
  `CMD_RESET`. Se **elimina/ignora** el `/control/{componente}` de relés.
- Mantener el manejo tolerante de líneas no-JSON.

### `static/dashboard.html`
- Quitar todos los botones "Encender/Apagar" (no se controlan relés).
- Mostrar:
  - Conexión, estado del ciclo (traducido) y cronología de fases.
  - Grilla de **salidas** como indicadores (luz on/off) de los 8 componentes.
  - Grilla de **entradas** (presostato, pulsador).
  - Panel del **LCD** (dos líneas, reflejando `"lcd"`).
  - Indicador de **ALARMA** visible cuando `estado_ciclo == "error"`.
  - Contador de giros (`giros` / `max_giros`) si aplica.
  - Log de eventos.
- Botones solo de **START / Reset error**.

### `simulador.py`
- Alinear `FASES` y `FASE_COMPONENTES` con el ciclo real del profesor
  (incluye `suavizando`, `centrifuga`, `buzzer`).
- Debe emitir el mismo JSON ampliado (con `fase`, `lcd`, etc.).
- **Reproduce la lógica del firmware**: el presostato condiciona el avance
  (`LLENADO` avanza al marcar nivel), dispara `ERROR_AGUA` en lavado/enjuague
  y se vacía al terminar un drenaje. El vaivén del motor (5 s giro / 2 s
  pausa, 3 giros) y las fases con temporizador (drenaje, suavizante,
  centrifugado) avanzan solos por tiempo, como en el Arduino. La opción 8
  cambia la velocidad de simulación (1x/2x/5x/10x) y la 7 fuerza el avance.
  Detalle en [`DECISIONES.md` P9, P10 y P11](DECISIONES.md).

## Pasos de implementación

1. Firmware: `enviarEstadoJSON()` + parser de comandos + `ArduinoJson`.
2. Backend: claves y endpoint de acciones.
3. Dashboard: rehacer como espejo + START/Reset.
4. Simulador: alinear con el nuevo contrato.
5. Prueba end-to-end con `socat` (sin hardware) y luego con el Arduino real.

## ⚠ El modelo de placa no está confirmado: no fijar el FQBN

El proyecto se documentó pensando en un **Arduino UNO**. En una sesión real, el
menú (que compilaba siempre con `--fqbn arduino:avr:uno`) produjo un caso en el
que el dashboard decía *Arduino conectado* pero no mostraba datos.

La evidencia recogida en esa sesión (`lsusb`, `dmesg`, `/dev/serial/by-id/`,
`arduino-cli board list`) apunta a un **Mega 2560 R3**, y la subida funcionó con
`arduino:avr:mega`. **Eso no alcanza para afirmar el modelo**: el VID/PID lo
declara el chip USB-serial, y no se verificó la firma del microcontrolador ni
la serigrafía. Puede tratarse simplemente de un acierto de detección.

Lo que sí quedó resuelto: **no se asume el modelo**. La opción 3 del menú
consulta la placa y usa el FQBN que reporte la herramienta.

```bash
# Ver qué placa detecta la herramienta y con qué FQBN
arduino-cli board list
```

Reporte de la falla, evidencia y solución adoptada:
[`DECISIONES.md` P12 a P14](DECISIONES.md).

## Pendientes / a decidir

- ¿`bloqueo_puerta` se muestra como "En marcha" o "Traba de puerta"?
  (El firmware lo prende al iniciar el ciclo y lo apaga en `FIN`.)
- ¿La alarma/buzzer se muestra como componente o solo como banner de error?
- ¿El botón START del dashboard es imprescindible o alcanza con mostrar
  solo estado (cero control)?
- ¿Se conserva `Serial.print("[LCD] ...")` para depurar, o se elimina?
- Instalar dependencia: `arduino-cli lib install ArduinoJson`.
- ~~`arduino_menu.sh` no revisa el resultado de `arduino-cli upload`~~ →
  **resuelto**: ahora comprueba el código de salida y avisa si la subida falló.
  También detecta si el puerto serie está ocupado y ofrece liberarlo.
  Ver [`DECISIONES.md` P15](DECISIONES.md).

## Seguridad

- Firmware, backend, dashboard y simulador se prueban **primero con socat**
  (sin Arduino). Recién después con hardware.
- El lado de 220 V del relé no se toca. No recablear con el equipo enchufado.
