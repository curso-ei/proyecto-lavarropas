# Tacómetro y control de motor TRIAC: circuito del profesor (UNO) y adaptación al Mega

Documento de referencia. **No modifica firmware ni circuitos**: solo deja
registrado el circuito y las indicaciones del profesor, lo que se verificó en
el código y las opciones para llevar el tacómetro al Arduino Mega.

---

## 1. Resumen ejecutivo

- El profesor diseñó el **circuito del tacómetro para el Arduino UNO**, usando
  el **comparador analógico interno** (AIN0 = D6, AIN1 = D7).
- Ese circuito **funciona con los dos firmwares standalone de motor**:
  - `lavarropas_motor_cruce_cero.ino` (el "antiguo")
  - `lavarropas_motor_cruce_cero_mejorado.ino` (PID del profesor)
- **Ambos firmwares standalone usan 115200 baudios**, no 9600 (verificado en el
  código con `Serial.begin(115200)`). El 9600 es de los firmwares de ciclo y
  del integrado del Mega.
- El circuito del UNO **no se puede conectar directo al Mega**, porque en el
  Mega el pin del comparador analógico (AIN0/PE2) **no sale a ningún conector**.
  El firmware del Mega espera el tacómetro en **D18 (INT3) con señal digital
  0–5 V limpia**.
- Para el Mega hay que **adaptar** la señal con un comparador externo (LM393 o
  similar) u otra placa que entregue pulsos digitales.

---

## 2. El circuito del profesor (para UNO)

### 2.1 Qué señal espera el UNO

El firmware standalone configura el comparador analógico interno del ATmega:

```cpp
// Comparador analógico: AIN0 (D6) vs AIN1 (D7)
DIDR1 = _BV(AIN1D) | _BV(AIN0D);
ACSR  = _BV(ACIE) | _BV(ACIS1) | _BV(ACIS0);
```

- **AIN0 (D6)**: entrada del tacómetro.
- **AIN1 (D7)**: referencia de **2,5 V** (divisor resistivo desde +5 V).
- El comparador dispara una interrupción (`ANALOG_COMP_vect`) cuando la señal
  del tacómetro cruza la referencia de 2,5 V. Cada cruce se cuenta como pulso
  del tacómetro.

### 2.2 Conexiones (según las indicaciones del profesor)

| Pin | Función |
|---|---|
| D2 | Detector de cruce por cero (PC817 + puente de diodos; también se probó H11AA1, que daba rebotes) |
| D3 | Disparo del MOC3021 hacia el gate del TRIAC Q6012LH5 |
| D6 (AIN0) | Entrada del comparador analógico, con R de 1 MΩ a GND para marcar 0 RPM con el motor parado |
| D7 (AIN1) | Entrada del comparador, con divisor de referencia de 2,5 V |
| USB/serie | Comandos `R<rpm>` (velocidad) y `P<0-100>` (potencia) |

### 2.3 Esquema del divisor y recorte (texto del profesor)

```
            +5 V
              │
             R divisor
              │
 D7 (AIN1) ◄──┼───────────── Tacómetro cable A
 (referencia  │
  2,5 V)     R divisor
              │
             GND

 Tacómetro cable B ──► R 10 kΩ ──┬──► D6 (AIN0)
                                 ├──► diodo a +5 V
                                 ├──► diodo a GND
                                 └──► R 1 MΩ a GND
```

### 2.4 Cómo funciona

1. **Un cable del tacómetro va a 2,5 V**, así la señal alterna del tacómetro
   oscila **alrededor de 2,5 V** (no alrededor de 0 V).
2. La **R de 10 kΩ + los dos diodos** recortan D6 entre **−0,6 V y +5,6 V**.
   Así los picos (el profesor menciona ±17 V) no llegan al pin del
   microcontrolador.
3. La **R de 1 MΩ a GND** deja D6 apenas **por debajo** de los 2,5 V de D7 con
   el motor parado. Por eso no cuenta ruido de red cuando el motor está quieto.

### 2.5 Orientación de los diodos (clave para que recorten)

- **Diodo a +5 V:** ánodo en **D6**, cátodo en **+5 V**.
- **Diodo a GND:** ánodo en **GND**, cátodo en **D6**.

> Nota textual del profesor: "d7 y tacómetro están a referencia 2,5 y d6 con
> diodos y R10k al tacómetro. Esto es en placa Arduino UNO: D2, D3, D6, D7, VCC
> y GND. El D3 viene del MOC pin 1 y el D2 del PC817 pin 4."

---

## 3. Firmwares standalone de motor (UNO) y qué datos usan

Ambos comparten pines, tacómetro (8 pulsos/vuelta) y disparo del TRIAC. Se
diferencian en el control PID.

| Parámetro | `lavarropas_motor_cruce_cero.ino` (antiguo) | `..._mejorado.ino` (PID profesor) |
|---|---|---|
| `Serial.begin` | **115200** | **115200** |
| Salida serie | texto plano (`SP/RPM/Pot`) | texto plano (`SP/RPM/Pot`) |
| Comandos | `R<rpm>`, `P<0-100>`, `R0` | `R<rpm>`, `P<0-100>`, `R0` |
| `Kp` | 0.001 | **0.0025** |
| `Ki` | 0.003 | **0.0005** |
| Filtro de RPM | `0.5*rpm + 0.5*rpmNueva` | **`0.85*rpm + 0.15*rpmNueva`** |
| Anti-windup | no | **sí** (`abs(error) < 2000`) |
| Rampa de potencia | no | **sí** (2 % / 8 % / 15 % por ciclo) |
| `POT_ARRANQUE` / `POT_MAX_PID` | 20 / 60 | 20 / 60 |
| `RETARDO_MIN` / `RETARDO_MAX` | 500 / 9300 µs | 500 / 9300 µs |
| `PULSOS_POR_VUELTA` | 8 | 8 |
| `DT_MS` | 100 | 100 |

**Datos que el profesor indicó y que funcionan con ambos:**

- Cruce por cero en **D2**, disparo MOC3021 en **D3**.
- Retardo de disparo: `RETARDO_MIN = 500 µs` (potencia máxima),
  `RETARDO_MAX = 9300 µs` (potencia mínima), pulso de 1000 µs.
- Tacómetro: intervalo mínimo 250 µs, bloqueo 400 µs, **8 pulsos por vuelta**.
- Seguridad: 20 ventanas de 100 ms sin pulsos = **2 s sin tacómetro → parar**.

> ⚠ Corrección importante: en esta sesión se dijo "el antiguo a 9600 baudios",
> pero **no es así**. Los dos firmwares standalone de motor están a **115200**.
> El 9600 corresponde al firmware integrado del Mega y a los de ciclo con JSON.

---

## 4. Por qué ese circuito NO se conecta directo al Mega

El firmware integrado del Mega
(`lavarropas_controller_2enjuagues_motor_triac_MEGA.ino`) **no** usa el
comparador analógico. Su propio encabezado lo dice:

> "El sketch del motor usaba el comparador analógico con AIN0 (D6) y AIN1 (D7)
> del Uno. En la placa Mega el pin AIN0 (PE2) NO sale a ningún conector, así
> que ese circuito no se puede conectar igual. Acá el tacómetro entra por una
> INTERRUPCIÓN EXTERNA (D18) y necesita una señal DIGITAL 0–5 V. Si tu tacómetro
> entrega una señal analógica/alterna, hace falta un comparador externo
> (LM393 o similar) entre el tacómetro y D18."

En concreto:

| Aspecto | UNO (profesor) | Mega (firmware integrado) |
|---|---|---|
| Entrada del tacómetro | D6 (AIN0) | **D18 (INT3)** |
| Referencia | D7 (AIN1) = 2,5 V (divisor) | no usa comparador interno |
| Tipo de señal | analógica/alterna alrededor de 2,5 V | **digital 0–5 V limpia** |
| Acondicionamiento | divisor + R10k + diodos + R1M | **comparador externo (LM393)** |
| ISR que cuenta | `ANALOG_COMP_vect` | `pulsoTaco()` por `attachInterrupt` |

**Razón de fondo:** en el UNO, el propio chip compara la señal con 2,5 V y
genera la interrupción. En el Mega, ese comparador existe por hardware pero su
entrada AIN0 (PE2) **no está disponible en los pines de la placa**. Por eso el
firmware del Mega optó por una interrupción externa que necesita la señal ya
**digitalizada**.

---

## 5. Alternativas para adaptar el tacómetro al Mega

### Opción A — Comparador externo LM393 (recomendada)

Convertir la señal del tacómetro en pulsos digitales 0–5 V hacia D18.

Lo mínimo a contemplar:

- **Divisor/referencia**: fijar el umbral de conmutación. Como la señal del
  tacómetro del profesor oscila alrededor de 2,5 V, el umbral natural también
  ronda 2,5 V.
- **Protección de entrada**: resistencias serie y diodos de recorte (como en el
  circuito del UNO) para que los picos no dañen el comparador.
- **Salida**: LM393 es de colector abierto; necesita **pull-up** (p. ej. 10 kΩ
  a 5 V) para dar un pulso limpio HIGH/LOW hacia D18.
- **Histéresis**: conviene una pequeña histéresis (realimentación positiva) para
  no contar rebotes cerca del umbral, especialmente con motor parado o ruido.
- **Alimentación**: 5 V (misma referencia lógica que el Mega).

### Opción B — Placa/módulo adicional

Usar un módulo detector/comparador ya armado que entregue salida digital TTL
0–5 V (o con salida open-collector + pull-up), si el tacómetro es del tipo que
el módulo soporta. Verificar siempre el nivel de salida y la frecuencia máxima.

### Opción C — Acondicionar con la misma idea del profesor + comparador discreto

Reutilizar el divisor de 2,5 V y el recorte de diodos del circuito del UNO,
pero alimentando con esa señal acondicionada la entrada de un LM393, y llevar
la salida del LM393 a D18. Es básicamente la opción A, conservando la topología
que el profesor ya validó en el UNO.

### Opción D — Usar el propio Arduino UNO como acondicionador (sin comprar nada)

Reutilizar el UNO que ya se tiene y el circuito del profesor (D6/D7 comparador
analógico) para que el UNO actúe como **conversor de señal**: recibe la señal
del tacómetro, la compara internamente y entrega por un pin digital un **pulso
limpio 0–5 V** hacia el D18 del Mega.

- No se toca el firmware del Mega: sigue usando D18 (tacómetro), D19 (cruce por
  cero) y D22 (TRIAC) exactamente igual que ahora.
- Firmware pequeño en el UNO: `ISR(ANALOG_COMP_vect)` → poner un pin en HIGH
  unos microsegundos → volver a LOW. Un cable de esa salida a D18 del Mega y
  **GND común**.
- Ventaja: cero compras, reusa el circuito ya validado y **respeta las
  restricciones** (el Mega queda intacto).
- Desventaja: agrega latencia de software (comparador → ISR → salida digital).
  Para el rango del tacómetro (8 pulsos/vuelta) no debería ser problema, pero
  hay que verificar en banco que los pulsos lleguen limpios y sin rebotes.

### Opción E — Arquitectura maestro-esclavo: Mega (ciclo) + UNO (motor)

Es una arquitectura distinta, no un simple adaptador. Se documenta en detalle
en la sección siguiente.

### Opción F — Acondicionador pasivo directo a D18 (reemplaza el circuito del profesor)

Variante sencilla y barata: **reemplazar** el circuito de referencia de 2,5 V
por un acondicionador pasivo para entrada digital, y conectar el tacómetro
directo a D18 del Mega.

```text
Tacómetro cable A ─────── GND

Tacómetro cable B ──► R 10 kΩ ──┬──► D18 (Mega)
                                ├──► diodo a +5 V
                                ├──► diodo a GND
                                └──► R pull-down a GND
```

- **Cable A → GND** (ya no a 2,5 V). La señal alterna queda centrada en 0 V.
- **R 10 kΩ en serie** limita la corriente de los picos.
- **Dos diodos de recorte** (1N4148/1N4007), misma orientación que el circuito
  del profesor:
  - diodo a +5 V: ánodo en D18, cátodo en +5 V.
  - diodo a GND: ánodo en GND, cátodo en D18.
- **R pull-down a GND** fija LOW firme con el motor parado.
- **Firmware**: `pinMode(PIN_TACO, INPUT);` (sin `INPUT_PULLUP`), para no
  arruinar el reposo.

**Cómo funciona:**

- **Motor parado (0 RPM):** el pull-down lleva D18 a 0 V → LOW sólido, sin
  pulsos fantasma (se elimina la zona indefinida de 1,5–3,0 V).
- **Motor girando:** el semiciclo positivo supera ~3,0 V → D18 marca HIGH y
  dispara la interrupción; el negativo baja de 1,5 V → LOW. Cada cruce
  ascendente cuenta un pulso.
- El **Schmitt trigger** interno del pin filtra rebotes pequeños.

**Valor del pull-down (regla práctica):**

- **Arrancar con 10 kΩ** si el tacómetro es un dinamo/generador robusto que
  entrega varios voltios (>10 V pico): da más inmunidad al ruido. Ojo: forma
  un divisor ~2:1 con la R10k en serie (la señal llega a la mitad).
- **Si a RPM bajas pierde lectura o marca 0 RPM** (y la señal no alcanza los
  3 V), subir a **100 kΩ** (atenuación ~1,1:1, llega casi toda la amplitud).
- Idealmente **medir la amplitud real** del tacómetro antes de fijar el valor.

**Advertencia (distinguir la causa de “0 RPM a bajas RPM”):**

- Si marca 0 RPM **solo a RPM muy bajas** y recupera al acelerar → es amplitud
  insuficiente: subir el pull-down (100 kΩ).
- Si marca 0 RPM **siempre**, incluso en alta → no es el pull-down: revisar
  sensor/firmware.

**Veredicto:** factible y la opción pasiva más simple, sin LM393 ni UNO extra.
Requiere validar en banco la amplitud real de la señal y el valor del pull-down.

### Opción G — Cambiar a sensor Hall u óptico

Reemplazar el tacómetro original por un **sensor Hall o un encoder óptico** que
entregue pulsos digitales directos a D18 (respetando niveles 0–5 V).

- **Ventaja**: señal digital limpia, sin LM393 ni UNO adicional; procesamiento
  más simple y robusto.
- **Desventajas**:
  - Requiere **comprar el sensor** y **adaptarlo mecánicamente** al motor.
  - Hay que verificar que la medición represente correctamente las RPM del
    rotor (ubicación, cantidad de pulsos por vuelta y compatibilidad con
    `PULSOS_POR_VUELTA = 8`).

**Factible** y eléctricamente la más limpia, pero va en contra del objetivo de
“no comprar nada más”. A evaluar si se acepta comprar el sensor.

### Qué tener en cuenta antes de elegir

- El firmware del Mega declara falla si no llegan pulsos en **2 s**
  (`T_SIN_TACO_MS = 2000`) y usa el tacómetro para liberar la puerta solo con
  el tambor parado (`RPM_PUERTA_SEGURA = 100`).
- Limitación ya documentada en `docs/FIRMWARE_MEGA_CICLO_MOTOR_TRIAC.md`: por
  debajo de ~150 RPM hay menos de 2 pulsos por ventana de 100 ms y el sistema
  lee 0 RPM. A velocidades muy bajas, la lectura de "parado" tiene ese margen.
- La frecuencia de pulsos debe ser compatible con `PULSOS_POR_VUELTA = 8` y la
  interrupción de D18.

---

## 6. Arquitectura maestro-esclavo: Mega (ciclo) + UNO (motor)

Alternativa documentada para evaluar a futuro. Es **factible**, reusa el UNO y
evita comprar un comparador externo, pero implica **rediseñar dos firmwares**,
no es un simple cableado.

### 6.1 Idea general

```text
Raspberry Pi
  (dashboard y reportes)
       │ USB
Arduino Mega 2560
  (control general del lavarropas: máquina de estados, relés, LCD, presostato)
       │ UART TX/RX
Arduino UNO
  (control del motor y tacómetro: cruce por cero, TRIAC, PID)
```

- La Raspberry sigue hablando con el Mega por USB (JSON del dashboard).
- El Mega le envía órdenes al UNO por UART (sentido, setpoint, arrancar/parar)
  y el UNO le devuelve telemetría (RPM, falla de tacómetro, estado).
- El UNO se conecta al circuito de potencia y al tacómetro (el circuito del
  profesor que ya está validado para UNO).

### 6.2 Conexión física Mega ↔ UNO

| Arduino Mega | Arduino UNO | Nota |
|---|---|---|
| TX1 (D18) | RX (D0) | TX de uno va a RX del otro |
| RX1 (D19) | TX (D1) | TX de uno va a RX del otro |
| GND | GND | referencia común obligatoria |

- Los pines TX/RX se cruzan: TX de un extremo va a RX del otro.
- Ambos trabajan con lógica de **5 V**, así que **no hace falta conversor de
  niveles** para esta UART.
- El UNO puede alimentarse por USB o por una fuente regulada. Si ambas placas
  comparten alimentación, hay que diseñar con cuidado la distribución de 5 V y
  GND; **no unir fuentes sin verificar**. En todos los casos, GND común.

### 6.3 Conflicto de pines que hay que resolver sí o sí

En el firmware integrado actual del Mega, D18 y D19 ya están ocupados:

| Pin Mega | Uso en el firmware actual | Uso en maestro-esclavo |
|---|---|---|
| D18 | Tacómetro (INT3) | TX1 → RX del UNO |
| D19 | Cruce por cero (INT2) | RX1 → TX del UNO |

Es decir: **con el firmware actual del Mega esta arquitectura no funciona**,
porque `Serial1` compite con el tacómetro y el cruce por cero. Para que sea
viable, el bloque de motor tiene que **salir del Mega y mudarse al UNO**,
liberando D18/D19 (y también D22, que quedaría sin uso en el Mega).

### 6.4 Qué hay que rehacer en cada firmware

**Mega (maestro):**
- Quitar el bloque de motor: cruce por cero (D19), tacómetro (D18), disparo
  TRIAC (D22), Timer1, PID, rampa de setpoint y las protecciones asociadas.
- Mantener la máquina de estados del ciclo, relés, LCD, presostato y JSON.
- Agregar un protocolo por `Serial1`: arrancar en un sentido, setpoint de RPM,
  parar, y recibir RPM/estado/falla.
- La traba de puerta deja de leer RPM local y pasa a depender del estado que
  informe el UNO → **rediseñar esa seguridad** (timeout, estado "tambor parado",
  falla de enlace).

**UNO (esclavo):**
- Recibir órdenes por `Serial` (D0/D1) en vez de comandos por monitor serie.
- Mantener el control del motor (cruce por cero, TRIAC, tacómetro, PID).
- **Seguridad obligatoria**: el UNO debe poder detener el motor por sí solo si
  el Mega se bloquea o se desconecta. No debe depender de recibir órdenes
  permanentes para mantenerse seguro (timeout de enlace → cortar potencia).

### 6.5 Precauciones adicionales

- **Programar el UNO**: si D0/D1 quedan cableados al Mega, el USB del UNO
  compite con el enlace UART. Conviene programar el UNO **desconectado del
  Mega**, o prever un método de carga sin conflicto en esos pines.
- **Latido/seguridad**: igual que el Mega integrado usa `T_LATIDO_MAX_MS` para
  el TRIAC, el enlace maestro-esclavo necesita su propio timeout de seguridad.
- **Baudrate y framing**: definir bien velocidad y formato del protocolo (y un
  checksum si se quiere robustez) para no mezclar los datos del dashboard (USB)
  con las órdenes del motor (UART1), que van por canales separados.
- **Ventaja real**: separa el ciclo (Mega) del lazo rápido del motor (UNO), y
  el UNO puede usar el circuito del profesor sin adaptaciones.
- **Costo**: es un **proyecto nuevo** (dos firmwares + protocolo + seguridad),
  no un cambio menor, y contradice la restricción actual de no tocar pines ni
  máquina de estados del firmware del Mega. Por eso se plantea como variante a
  evaluar, no como ajuste inmediato.

### 6.6 Comparación rápida de alternativas sin comprar nada

| Alternativa | Toca el firmware del Mega | Complejidad | Riesgo |
|---|---|---|---|
| Opción D (UNO como acondicionador) | No | Baja (firmware chico en UNO) | Bajo |
| Opción E (maestro-esclavo) | Sí, rediseño | Alta (2 firmwares + protocolo) | Medio/alto |

Para el objetivo inmediato (conectar el tacómetro del profesor al Mega **sin
comprar nada y sin romper lo que funciona**), la **Opción D** es la más
segura. La **Opción E** queda como mejora de arquitectura a evaluar a futuro.

---

## 7. Comparación completa UNO standalone vs Mega integrado (motor)

| Aspecto | UNO standalone (antiguo y mejorado) | Mega integrado (ciclo + TRIAC) |
|---|---|---|
| Qué hace | solo motor (PID + TRIAC) | ciclo completo de lavado + motor |
| Cruce por cero | D2 (INT0) | D19 (INT2) |
| Tacómetro | D6/D7 comparador analógico | D18 (INT3), digital |
| Disparo TRIAC | D3, en `loop()` con `delayMicroseconds` | D22, con Timer1 + latido |
| Baudios | **115200** | **9600** |
| Salida | texto plano | JSON (con objeto `motor`) |
| `Kp` / `Ki` | 0.001/0.003 (antiguo) · 0.0025/0.0005 (mejorado) | 0.001/0.003 |
| Filtro RPM | 0.5/0.5 (antiguo) · 0.85/0.15 (mejorado) | 0.5/0.5 |
| Anti-windup | solo el mejorado | no |
| Rampa de potencia | solo el mejorado (2/8/15 %) | no |
| Rampa de setpoint | no | sí (`RAMPA_RPM_S = 1000`) |
| Protección sin tacómetro | 20 × 100 ms = 2 s → parar | `T_SIN_TACO_MS = 2000` → `ERROR_MOTOR` |

---

## 8. ¿Conviene portar el PID del profesor al Mega?

**No conviene hacerlo de forma directa.** Razones:

1. **Son estrategias distintas para el mismo problema** (aceleración suave):
   - UNO mejorado: setpoint directo + **rampa de potencia**.
   - Mega: **rampa de setpoint** (`RAMPA_RPM_S = 1000 RPM/s`).
   Si se ponen las dos a la vez, se limitan mutuamente y el comportamiento deja
   de ser predecible.

2. **Los `Kp`/`Ki` son empíricos del lazo del UNO** (tacómetro por comparador,
   disparo en `loop()`, setpoint directo). El Mega tiene tacómetro digital por
   INT3, disparo por Timer1 y rampa de setpoint: el error que ve el PID es
   distinto, así que la sintonía **no se transfiere automáticamente**.

3. **Restricción del proyecto**: la tarea de integración marca como no
   negociable **no cambiar `Kp`, `Ki`, `POT_MAX_PID` ni la máquina de estados
   del firmware del Mega**.

4. Lo único que podría evaluarse aparte (con permiso y pruebas sin 220 V) es el
   **anti-windup**, por ser una mejora de robustez independiente de los valores.

Si algún día se experimenta, lo correcto es **una variante nueva del Mega**
(por ejemplo `..._pid_profesor`) o un `#define`, dejando intacto el firmware
que ya funciona.

---

## 9. Qué se hizo en esta sesión (registro)

1. Se comparó el código entregado por el profesor con los firmwares existentes:
   - No coincide con `lavarropas_motor_cruce_cero.ino` (el profesor tiene otro
     PID: Kp/Ki, filtro, anti-windup y rampa dinámica).
   - Tampoco con el integrado del Mega (arquitectura distinta).
2. Se creó `firmware/lavarropas_motor_cruce_cero_mejorado/` con el código del
   profesor para UNO (sin JSON, texto plano a 115200). Compila OK para
   `arduino:avr:uno` (6636 bytes flash / 419 bytes RAM).
3. Se agregó al menú (`arduino_menu.sh`):
   - Variante **5** en la opción 3: "Motor cruce por cero MEJORADO (PID
     profesor) [solo UNO]".
   - Menú de baudrate en la opción 2: 9600 / 115200 / manual.
   - Documentación de pines del motor UNO en la opción 6.
4. Se documentó en `README.md` (variantes y selector de baudrate).
5. Se ignoraron las notas de trabajo locales (no se suben a Git).
6. Commit `b8373cf` y push a `origin/main`.

---

## 10. Pendientes / a decidir

- Definir cuál de las alternativas de adaptación al Mega se implementa:
  - **LM393** (opción A), recomendada.
  - **UNO como acondicionador** (opción D), sin comprar nada.
  - **Maestro-esclavo** (opción E), arquitectura propuesta para la integración
    ciclo + motor.
  - **Acondicionador pasivo a D18** (opción F): reemplaza el circuito del
    profesor (referencia a GND + clamp + pull-down), sin integrados extra.
  - **Sensor Hall/óptico** (opción G), solo si se acepta comprar/adaptar sensor.
- Confirmar el tipo exacto de tacómetro (analógico/alterno) y su nivel pico a
  pico para dimensionar divisor y recorte del LM393.
- Validar en banco (sin 220 V) la salida digital hacia D18 antes de probar en
  la máquina.
- Si se quisiera, evaluar el anti-windup como mejora aislada del Mega en una
  variante nueva (nunca tocando el firmware actual directamente).
