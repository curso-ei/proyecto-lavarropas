# Requisitos del tacómetro para el firmware del Mega

Documento de especificación para el firmware de **referencia**
`lavarropas_controller_2enjuagues_motor_triac_MEGA.ino` y para un futuro
firmware específico. **No modifica código ni circuitos.**

> Texto sujeto a revisión del docente.

---

## 1. Qué es este firmware y qué no es

**Qué es:**

- Una integración de **referencia** que une el ciclo de 2 enjuagues con el
  control del motor por TRIAC (cruce por cero + tacómetro + PID), pensada sobre
  todo para **alimentar el dashboard nuevo** (rpm, objetivo, potencia, gráfico).
- Sirve como **especificación** para el firmware específico que se hará más
  adelante.

**Qué NO es:**

- **No está validado en hardware real** con motor y tacómetro: se compiló y se
  probó en simulación, no con el motor conectado.
- No es el firmware definitivo del lavarropas; es una variante derivada del
  proyecto (ver README, sección 4).

---

## 2. Interfaz con el tacómetro

Todas las referencias de línea corresponden a
`firmware/lavarropas_controller_2enjuagues_motor_triac_MEGA/lavarropas_controller_2enjuagues_motor_triac_MEGA.ino`.

| Aspecto | Valor | Línea |
|---|---|---|
| Pin del tacómetro | `PIN_TACO = 18` (INT3) | 54 |
| `pinMode` | `INPUT_PULLUP` | 599 |
| Interrupción | `attachInterrupt(... PIN_TACO, pulsoTaco, RISING)` | 618 |
| Pulsos por vuelta | `PULSOS_POR_VUELTA = 8` | 136 |
| Intervalo mínimo | `MIN_INTERVALO_US = 250` µs | 134 |
| Bloqueo tras disparo | `BLOQUEO_US = 400` µs | 135 |
| Período del PID | `DT_MS = 100` ms | 143 |
| Sin señal → falla | `T_SIN_TACO_MS = 2000` ms | 144 |
| Rango del PID | `RPM_MIN = 500`, `RPM_MAX = 12000` | 147-148 |
| Agitado | `RPM_AGITADO = 600` | 149 |
| Centrifugado | `RPM_CENTRIFUGADO = 6000` | 150 |
| Puerta segura | `RPM_PUERTA_SEGURA = 100` | 156 |

### 2.1 Señal esperada

- **Pulsos DIGITALES de 0–5 V**, con masa común con el Mega.
- La interrupción es por **flanco ascendente** (`RISING`).
- **No sirve** una señal senoidal/alterna centrada en ~2,5 V (el pin digital
  tiene una zona de indefinición entre los umbrales; dato general de la hoja de
  datos, a confirmar).
- **No sirve** una señal con amplitud mayor a 5 V o con excursión negativa sin
  acondicionar: riesgo para el pin.

### 2.2 Pulsos por vuelta

`PULSOS_POR_VUELTA = 8`. Si el sensor entrega otra cantidad, las RPM calculadas
salen mal por un **factor constante**; la corrección es ajustar esa constante
al valor real del sensor.

### 2.3 Frecuencias esperadas

| Régimen | RPM | Pulsos/s | Período entre pulsos |
|---|---|---|---|
| Mínimo del PID | 500 | 66,7 | 15 000 µs |
| Agitado | 600 | 80 | 12 500 µs |
| Centrifugado | 6000 | 800 | 1 250 µs |

Comparación con los filtros de la ISR:

- `MIN_INTERVALO_US = 250` µs → rechaza pulsos separados por menos de 250 µs
  (equivalente a 4000 pulsos/s = 30 000 RPM con 8 pulsos/vuelta). **No afecta**
  al rango de trabajo (máx. 800 pulsos/s).
- `BLOQUEO_US = 400` µs → ventana tras cada disparo del TRIAC en la que se
  descartan pulsos (líneas 224-225). Ver sección 2.4.

### 2.4 Posible subestimación a alta velocidad (estimación, a confirmar en banco)

La ISR descarta los pulsos que llegan a menos de `BLOQUEO_US = 400` µs de un
disparo del TRIAC. El disparo ocurre una vez por **semiciclo** (10 ms a 50 Hz).

- Fracción de tiempo "ciega" por semiciclo: `400 µs / 10000 µs = 4 %`.
- El cálculo de RPM usa `(c - 1)` pulsos sobre el tiempo entre el primero y el
  último (línea 325). Cada pulso descartado hace que la lectura quede por
  debajo del valor real.
- **Fracción promedio de pulsos perdidos ≈ 4 %** (400 µs de bloqueo por
  semiciclo de 10 ms), igual a cualquier velocidad si la fase es aleatoria.
- **Error relativo por cada pulso perdido**: resta 1 a `(c - 1)`. A 6000 RPM
  (`c ≈ 80`) es ≈ 1,3 % por pulso; a 600 RPM (`c ≈ 8`) es ≈ 14 % por pulso. A
  baja velocidad la lectura es más escalonada (el filtro 0,5/0,5 la suaviza).
- **Alineación**: a 6000 RPM los pulsos son 800 Hz = 8 × 100 Hz (frecuencia de
  disparo). Los pulsos pueden alinearse con el bloqueo y la pérdida alternar
  entre 0 y 1 pulso por disparo (≈ 12,5 %) mientras la velocidad deriva. Vale
  para múltiplos de 750 RPM.

> Esta es una **estimación**, no una medición; supone que el disparo se reparte
> al azar respecto de los pulsos. **A confirmar en banco.** No se cambia el
> firmware por esto.
> Texto sujeto a revisión del docente.

### 2.5 Cálculo de RPM

Líneas 315-327:

```cpp
float rpmNueva = 0;
unsigned long span = t2 - t1;
if (c >= 2 && span > 0) {
  rpmNueva = (c - 1) * 1000000.0 / (float)span * 60.0 / PULSOS_POR_VUELTA;
}
rpm = 0.5 * rpm + 0.5 * rpmNueva;
```

- Se necesitan **al menos 2 pulsos** en la ventana `DT_MS = 100` ms.
- Filtro exponencial 0,5/0,5.
- **Umbral de ~150 RPM:** con 8 pulsos/vuelta, 2 pulsos por ventana de 100 ms
  equivalen a `2 pulsos / 0,1 s × 60 s/min ÷ 8 pulsos/vuelta = 150 RPM`. Por
  debajo de eso la ventana puede traer un solo pulso → `c < 2` → la lectura cae
  a 0 RPM.

### 2.6 Protección por falta de señal

`T_SIN_TACO_MS = 2000`. Si no llegan pulsos durante 2 s **con el motor en
marcha** (`motorEstado == M_MARCHA`), se declara `ERROR_MOTOR` (líneas 331-335).
Con el motor parado no se acumula la falta de señal.

### 2.7 Señales relacionadas (comparten entorno de ruido)

| Señal | Pin | Línea |
|---|---|---|
| Cruce por cero | `PIN_ZC = 19` (INT2) | 53 |
| Disparo del TRIAC (MOC3021) | `PIN_MOC = 22` | 55 |

Estas señales conviven con el tacómetro y son fuente de ruido eléctrico; de ahí
los filtros de la ISR.

---

## 3. Medición previa necesaria (sin tensión de red)

Antes de conectar nada al Mega, con el motor **girado a mano** o con la potencia
desconectada, medir:

1. **Amplitud pico a pico** de la señal del tacómetro a baja y alta velocidad.
2. **Componente continua** (offset) de la señal.
3. **Frecuencia a una velocidad conocida**, para confirmar los 8 pulsos por
   vuelta.

> El circuito de acondicionamiento debe ser **revisado por el docente** antes de
> conectar nada al Mega.

---

## 4. Casos de adaptación del tacómetro

Resumen de los casos documentados en `docs/TACOMETRO_MOTOR_TRIAC.md` (secciones
2, 4, 5 y 6), evaluados contra el requisito del firmware actual (pulsos
digitales 0–5 V en D18, flanco ascendente).

| Caso | Qué entrega a D18 | ¿Cumple el requisito? | Qué falta / condición | ¿Exige cambiar el firmware? | ¿Aislamiento galvánico? | Fuente |
|---|---|---|---|---|---|---|
| Circuito del profesor tal cual (UNO) | Señal centrada en 2,5 V (comparador) | No | No es digital; zona de indefinición | Sí | A confirmar con el docente | §2 (líneas 28-93) |
| A — LM393 | Pulsos digitales 0–5 V (con pull-up) | Condicional | Salida limpia 0–5 V | No | A confirmar con el docente | §5, Opción A |
| B — Módulo comparador | Pulsos TTL 0–5 V | Condicional | Verificar nivel y frecuencia | No | A confirmar con el docente | §5, Opción B |
| C — Profesor + comparador discreto | Según terminación | Condicional | Depende de cómo se cierra | Según terminación | A confirmar con el docente | §5, Opción C |
| D — UNO como acondicionador | Pulsos digitales desde un pin del UNO | Condicional | Sketch nuevo en UNO + masa común | No (en el Mega) | A confirmar con el docente | §5, Opción D |
| E — Maestro-esclavo Mega+UNO | — | No con este firmware | Rehacer ambos firmwares | Sí (ambos) | A confirmar con el docente | §6 (sección 6.4) |
| F — Pasivo directo a D18 | Pulsos digitales (si amplitud alcanza) | Condicional | Depende de amplitud en todo el rango y del ruido del TRIAC | Sí (`INPUT` en vez de `INPUT_PULLUP`) | A confirmar con el docente | §5, Opción F |
| G — Sensor Hall/óptico | Pulsos digitales directos | Sí (si son digitales) | Ajustar `PULSOS_POR_VUELTA` si no son 8 | Solo la constante | A confirmar con el docente | §5, Opción G |
| Ninguno / diseño propio | A definir | A confirmar | A definir | A confirmar | A confirmar con el docente | — |

---

## 5. Requisitos mínimos para un firmware futuro (alimentar el dashboard)

Verificado en `static/dashboard_motor.html` y `main.py`.

### 5.1 Campo obligatorio

- **`motor.rpm`** (número): sin él, el dashboard **oculta** el panel del motor y
  el gráfico (dashboard_motor.html, línea 526 y 657).

### 5.2 Campos opcionales y para qué se usan

| Campo | Uso en el dashboard |
|---|---|
| `motor.objetivo` | "Objetivo (rampa)" |
| `motor.objetivo_final` | "Objetivo final" y cálculo de la diferencia con el objetivo |
| `motor.estado` | Texto de estado (parado/arrancando/en marcha/parando) |
| `motor.potencia` | Barra de potencia y % |
| `motor.pot_max` | Leyenda de saturación y línea del gráfico |

### 5.3 Campos del JSON raíz

- `estado_ciclo` y `fase`: necesarios para el estado del ciclo y el gráfico.
- `componentes.*`, `presostato`, `pulsador`, `lcd`, `giros`, `max_giros`,
  `error`: alimentan el resto del dashboard.

### 5.4 Qué agrega la API (no lo manda el firmware)

`main.py` agrega a `/estado`: `arduino_conectado`, `log`, `ahora`,
`ultimo_json_ts`, `edad_ultimo_json`, `lineas_descartadas`, `seq`, `sesion`,
`t_servidor` y (con `?desde=<seq>`) `muestras`.

### 5.5 Baudios

- Firmware del Mega integrado: `Serial.begin(9600)` (línea 586 del `.ino`).
- Los standalone de motor (UNO) usan 115200 (texto plano), no JSON.

---

## 6. Limitación a dejar escrita

Si el tacómetro deja de dar pulsos **en `FIN`**, la lectura de RPM da 0 y la
traba se suelta con el tambor **posiblemente girando**. Un sensor roto (que no
entrega pulsos) **no se puede detectar por software** con la lógica actual
(`tamborParado = motorEstado == M_PARADO && rpm < 100`, línea 750).

---

> Texto sujeto a revisión del docente.
