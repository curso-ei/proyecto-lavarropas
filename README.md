# Monitoreo y control didáctico de lavarropas industrial

Sistema integral de monitoreo en tiempo real, adquisición de datos y control para
maquetas didácticas de lavarropas. Combina automatización en microcontroladores
(Arduino UNO y Mega 2560), aislamiento del circuito de potencia (220 V) y una
arquitectura de software desacoplada (backend Python + frontend web) ejecutable
en Raspberry Pi, Windows, macOS y Android.

> **Registro de decisiones, problemas y soluciones:** [`DECISIONES.md`](DECISIONES.md)
> Ahí está el detalle de cada decisión (con evidencia de código), los problemas
> encontrados y cómo se resolvieron.

---

## 1. Objetivos del proyecto

- **Preservación y evolución modular del código:** mantener intactos los
  firmwares provistos por la cátedra (en `firmware/original_profesor/`) y
  desarrollar en paralelo un ecosistema de variantes modulares (ciclo de dos
  enjuagues, control de motor con TRIAC + PID, cruce por cero).
- **Telemetría y monitoreo multiplataforma:** capturar, procesar y graficar en
  tiempo real las variables del ciclo (fases, actuadores, RPM del motor y
  alarmas), sin depender de una única plataforma ni interferir con el lazo de
  control del microcontrolador.
- **Seguridad eléctrica y rigurosidad de ingeniería:** separar estrictamente el
  circuito de control de muy baja tensión (5 V) del circuito de potencia
  (220 V) mediante relés y optoacopladores.

## 2. Reglas de trabajo

- **Los firmwares originales del profesor no se modifican.** Viven en
  `firmware/original_profesor/`. Las variantes derivadas conviven en carpetas
  propias con el mismo nombre que su `.ino`.
- **El dashboard es de solo lectura:** no controla relés ni manda comandos de
  encendido/apagado. Solo muestra estado, componentes, alarmas y log.
- **Los firmwares de la categoría "Del profesor + JSON" conservan** pines,
  lógica, máquina de estados, LCD y buzzer. Solo agregan lo mínimo para
  reportar por Serial en JSON.
- **START / Reset remoto (comando serial):** solo tiene efecto en firmwares que
  efectivamente leen el puerto serie. En los firmwares de ciclo el parser está
  **comentado** (`Serial.read/available` deshabilitado); el firmware integrado
  del Mega (`..._motor_triac_MEGA`) **no lee** el puerto serie, por lo que
  START/Reset desde el backend no tienen efecto ahí. Los únicos que leen
  comandos son los standalone de motor (`R<rpm>`, `P<0-100>`, `R0`).
- **Los firmwares standalone de motor** (cruce por cero y mejorado) no emiten
  JSON: usan texto plano a 115200 baudios para afinar el PID por monitor serie
  (`R<rpm>`, `P<0-100>`, `R0`). No alimentan el dashboard.
- **Documentación técnica** de motor, tacómetro e integración: ver
  [sección 6](#6-documentación-técnica-de-referencia).

---

## 3. Arquitectura general

El sistema se organiza en cuatro capas:

1. **Capa física (potencia y control):** presostato de nivel, pulsador START,
   actuadores de 220 V (bombas, motor, válvulas) y módulos de relés /
   optoacopladores.
2. **Capa de control (firmware):** Arduino UNO / Mega 2560 con máquina de
   estados finitos no bloqueante (`millis()`) o lazo PID de velocidad.
3. **Capa de servicios (backend):** Python (FastAPI/Uvicorn) que gestiona el
   enlace serie, una cola circular de muestras y la interfaz REST.
4. **Capa de presentación (frontend):** dashboard HTML5/JavaScript puro
   (polling eficiente y gráficos sobre `<canvas>`), de solo lectura.

---

## 4. Estructura y clasificación de firmwares

Los firmwares se clasifican en cuatro grupos, según su origen y si se
modifican o no:

| Categoría | Firmwares | ¿Se modifica? |
|---|---|---|
| **Referencia pura (cátedra)** | `firmware/original_profesor/` | No, nunca |
| **Del profesor (mantenido tal cual)** | `firmware/lavarropas_motor_cruce_cero/`, `firmware/lavarropas_motor_cruce_cero_mejorado/` | No |
| **Del profesor + JSON (lógica intacta)** | `firmware/lavarropas_controller_display_error_buzzer/` | Solo se agregó reporte JSON; la lógica no se cambia |
| **Variantes derivadas** | `firmware/lavarropas_controller_display_error_buzzer_2enjuagues/`, `firmware/lavarropas_controller_2enjuagues_motor_triac_MEGA/` | Sí, son variantes del proyecto |

### Detalle por firmware

| Ruta | Descripción |
|---|---|
| `firmware/original_profesor/` | Firmware original del controlador de ciclo con LCD y alarmas (sin JSON). Copia fiel de referencia. |
| `firmware/lavarropas_controller_display_error_buzzer/` | Base del profesor + reporte JSON para el dashboard (lógica intacta). |
| `firmware/lavarropas_motor_cruce_cero/` | Motor standalone con TRIAC y cruce por cero (115200, texto plano). |
| `firmware/lavarropas_motor_cruce_cero_mejorado/` | Motor standalone con PID tuneado por la cátedra (Kp/Ki, filtro y rampa dinámica). |
| `firmware/lavarropas_controller_display_error_buzzer_2enjuagues/` | Variante derivada: ciclo completo con segundo enjuague. |
| `firmware/lavarropas_controller_2enjuagues_motor_triac_MEGA/` | Variante derivada: integración de referencia (ciclo 2 enjuagues + motor TRIAC + telemetría). |

---

## 5. Matriz multiplataforma

| Entorno | Conexión | Detalle |
|---|---|---|
| Raspberry Pi OS | USB (`/dev/ttyACM0`) / red local (RaspAP) | [`curso-raspap/`](curso-raspap/README.md) |
| Windows | USB (`COMx`) | [`windows/README_WINDOWS.md`](windows/README_WINDOWS.md) |
| macOS | USB (`/dev/cu.*`) | [`mac/README_MAC.md`](mac/README_MAC.md) |
| Android | USB OTG (Web Serial) | [`android/README_ANDROID.md`](android/README_ANDROID.md) |

---

## 6. Documentación técnica de referencia

| Documento | Contenido |
|---|---|
| [`DECISIONES.md`](DECISIONES.md) | Registro histórico de problemas técnicos, evidencia y soluciones. |
| [`docs/ARQUITECTURA_SEGURIDAD_LIMITACIONES.md`](docs/ARQUITECTURA_SEGURIDAD_LIMITACIONES.md) | Riesgos eléctricos, límites de hardware y análisis de ingeniería. |
| [`docs/GUIA_ALUMNOS_ELECTRICIDAD.md`](docs/GUIA_ALUMNOS_ELECTRICIDAD.md) | Manual de práctica para taller (electricidad industrial, relés). |
| [`docs/TACOMETRO_MOTOR_TRIAC.md`](docs/TACOMETRO_MOTOR_TRIAC.md) | Análisis de la señal del tacómetro y alternativas de adaptación. |
| [`docs/INTEGRACION_CICLO_MOTOR_MEJORADO.md`](docs/INTEGRACION_CICLO_MOTOR_MEJORADO.md) | Propuesta de integración: Mega (ciclo, maestro) + UNO (motor, esclavo). |
| [`docs/FIRMWARE_MEGA_CICLO_MOTOR_TRIAC.md`](docs/FIRMWARE_MEGA_CICLO_MOTOR_TRIAC.md) | Integración de referencia todo-en-el-Mega. |

---

## 7. Protocolo de comunicación (trama JSON)

En las variantes de ciclo con monitoreo, el microcontrolador transmite
periódicamente una línea JSON como esta:

```json
{
  "estado_ciclo": "lavando",
  "fase": "LAVADO",
  "error": null,
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
  "pulsador": false,
  "motor": {
    "rpm": 600,
    "objetivo": 600,
    "objetivo_final": 600,
    "estado": 2,
    "potencia": 25,
    "pot_max": 60
  }
}
```

- El objeto `motor` solo está presente en las variantes con control TRIAC; los
  dashboards y el backend lo tratan como opcional.
- Los firmwares standalone de motor **no** envían JSON (texto plano, ver
  sección 2).

### Campos del objeto `motor`

| Campo | Descripción |
|---|---|
| `rpm` | RPM medidas (filtradas) |
| `objetivo` | Valor instantáneo de la rampa de setpoint |
| `objetivo_final` | Objetivo de RPM de la fase |
| `estado` | 0 parado, 1 arrancando, 2 en marcha, 3 parando (con estado 0 el tambor puede seguir girando por inercia) |
| `potencia` | % de potencia aplicada al TRIAC |
| `pot_max` | Tope del PID en lazo cerrado |

### Respuesta de `GET /estado`

El backend expone el estado en `GET /estado` (y `GET /estado?desde=<seq>` para
la cola de muestras del gráfico):

| Campo | Descripción |
|---|---|
| `arduino_conectado` | true si hay puerto serie abierto |
| `datos` | último JSON válido recibido del firmware |
| `log` | últimos eventos registrados por el backend |
| `ahora` | epoch (s) del servidor |
| `ultimo_json_ts` | epoch (s) del último JSON válido |
| `edad_ultimo_json` | segundos desde el último JSON válido (reloj monotónico; recomendado para saber si los datos están frescos) |
| `lineas_descartadas` | contador de líneas que fallaron `json.loads` |
| `seq` | secuencia creciente de muestras válidas |
| `sesion` | UUID del proceso backend (cambia al reiniciar) |
| `t_servidor` | reloj monotónico del backend (para el gráfico) |
| `muestras` | solo con `?desde=<seq>`: hasta 800 muestras livianas (`seq`, `t`, `fase`, `rpm`, `objetivo`, `objetivo_final`, `estado`, `potencia`) |

---

## 8. Inicio rápido (Raspberry / Linux)

```bash
git clone https://github.com/curso-ei/proyecto-lavarropas.git
cd proyecto-lavarropas

python3 -m venv venv
source venv/bin/activate
pip install -r requirements.txt

chmod +x arduino_menu.sh
./arduino_menu.sh
```

El menú detecta la placa conectada (FQBN), verifica la disponibilidad del
puerto serie y permite compilar, cargar el firmware o iniciar el backend de
supervisión.

### Variables de entorno del backend

| Variable | Valores | Descripción |
|---|---|---|
| `DASHBOARD` | `relay` (default) / `motor` | elige `dashboard.html` o `dashboard_motor.html` |
| `BAUDRATE` | entero (default 9600; el motor pasará a 115200 en la fase 4b) | prioridad sobre el baudrate de la variante |
| `GZIP` | `1` (opcional) | habilita compresión GZip en las respuestas |
| `PUERTO_FORZADO` | ruta de puerto (ej. `/dev/ttyACM0`) | fuerza el puerto serie (útil con socat/simulador) |

---

## 9. Archivos principales

```
proyecto-pi/
├── README.md                      ← este documento
├── DECISIONES.md                  ← decisiones, problemas y soluciones
├── main.py                        ← backend (espejo de solo lectura)
├── simulador.py                   ← simulador fiel del ciclo (relés)
├── simulador_motor.py             ← simulador del ciclo + motor TRIAC (telemetría)
├── requirements.txt               ← dependencias Python
├── iniciar_pruebas.sh             ← pruebas sin Arduino (relés: socat + simulador.py + backend)
├── iniciar_pruebas_motor.sh       ← pruebas sin Arduino (motor: socat + simulador_motor.py + backend)
├── arduino_menu.sh                ← menú interactivo
├── check_arduino.sh               ← diagnóstico de detección del Arduino
├── static/
│   ├── dashboard.html             ← dashboard de relés
│   └── dashboard_motor.html       ← dashboard de motor TRIAC
├── firmware/
│   ├── lavarropas_controller_display_error_buzzer/
│   ├── lavarropas_controller_display_error_buzzer_2enjuagues/
│   ├── lavarropas_motor_cruce_cero/
│   ├── lavarropas_motor_cruce_cero_mejorado/
│   ├── lavarropas_controller_2enjuagues_motor_triac_MEGA/
│   └── original_profesor/
├── docs/                          ← documentación técnica
├── windows/                       ← versión autocontenida para Windows
├── mac/                           ← versión autocontenida para macOS
├── android/                       ← dashboard PWA (Web Serial)
└── curso-raspap/                  ← menú de red y RaspAP
```

> Cada carpeta de sketch lleva el **mismo nombre que su `.ino`**, que es lo que
> exige arduino-cli / IDE para compilar.

### Simuladores y dashboards: cuál usar

| Escenario | Simulador | Dashboard | Cómo levantar |
|---|---|---|---|
| Ciclo con relés (sin motor) | `simulador.py` | `dashboard.html` | `./iniciar_pruebas.sh` |
| Ciclo + motor TRIAC (telemetría) | `simulador_motor.py` | `dashboard_motor.html` | `./iniciar_pruebas_motor.sh` |

El menú del simulador de motor ofrece: `P` pulsar START, `2` presostato,
`4` error de agua, `5` falla de tacómetro, `6` reparar tacómetro,
`7` ver JSON, `8` forzar avance, `9` velocidad, `L` línea no-JSON y
`T` intervalo de envío (1 s / 250 ms).

---

## 10. Menú Arduino (`arduino_menu.sh`)

### Variantes de firmware (opción 3)

| # | Variante | Placa | Salida serie |
|---|----------|-------|--------------|
| 1 | Base del profesor + JSON | UNO y Mega | JSON 9600 |
| 2 | Dos enjuagues (agua + suavizante) | UNO y Mega | JSON 9600 |
| 3 | Motor cruce por cero (PID + TRIAC) | solo UNO | texto 115200 |
| 4 | Dos enjuagues + motor TRIAC | solo Mega | JSON 9600 |
| 5 | Motor cruce por cero MEJORADO (PID profesor) | solo UNO | texto 115200 |

- La variante **5** es el código del profesor tal cual: `Kp=0.0025`,
  `Ki=0.0005`, filtro `0.85/0.15`, anti-windup y rampa dinámica de potencia.
- Las variantes **3** y **5** no emiten JSON (texto plano para afinar el PID).

### Escuchar el puerto serie (opción 2)

```
1) 9600   (ciclo relés / Mega motor TRIAC)
2) 115200 (motor cruce por cero UNO: original y mejorado)
Enter=9600, 1, 2, u otro valor numérico manual
```

---

## 11. Seguridad

- El proyecto maneja **tensión de red (220 V)**. El software (firmware,
  backend y dashboard) es una ayuda didáctica y **no reemplaza** las
  protecciones del tablero (térmica, diferencial) ni una **parada de emergencia
  física** independiente del Arduino y de la Raspberry.
- **Orden de pruebas:**
  1. Sin hardware (`socat` + simulador).
  2. Arduino conectado a la Raspberry con la parte de potencia (220 V)
     **desconectada**.
  3. Potencia con una **carga resistiva** (lámpara) en lugar del motor.
  4. Recién después con el **motor**, siempre con supervisión del docente.
- **El lado de 220 V no se toca con el equipo enchufado.** Mantener separados
  el circuito de 5 V (control) y el de potencia; relés y optoacopladores como
  aislamiento.
- El dashboard es de **solo lectura** (`PERMITIR_ACCIONES = false`). El backend
  escucha en `0.0.0.0` **sin autenticación**: cualquiera en la misma red puede
  ver el estado. **No exponerlo a internet.** Si algún día se habilitan
  comandos, hace falta autenticación y confirmación.
- **Traba de puerta** (verificado en el `.ino` del Mega):
  - se activa al presionar START (`setRelay(PIN_TRABA, true)`, línea 659);
  - queda trabada durante los errores (`ERROR_AGUA` / `ERROR_MOTOR` no la
    sueltan);
  - se suelta solo en `FIN` con el tambor parado:
    `tamborParado = (motorEstado == M_PARADO) && (rpm < RPM_PUERTA_SEGURA)`
    (línea 750), con `RPM_PUERTA_SEGURA = 100`.
  - Si el tacómetro falla de forma permanente, la puerta solo se libera
    cortando la alimentación. **A DEFINIR con el docente** un mecanismo manual
    de liberación.
- **Latido del firmware** (verificado): `T_LATIDO_MAX_MS = 800`. Si `loop()` no
  corre en más de 800 ms, el cruce por cero deja de disparar el TRIAC
  (protección si el programa se cuelga).
- El aviso del dashboard **"Tambor girando: no abrir la puerta"** es
  informativo; **no es una protección**.

> Texto sujeto a revisión del docente.
