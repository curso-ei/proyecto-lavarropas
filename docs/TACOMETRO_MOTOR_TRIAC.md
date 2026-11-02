# Tacómetro y control de motor TRIAC: circuito del profesor (UNO) y adaptación al Mega

Documento de referencia. **No modifica firmware ni circuitos**: solo deja
registrado lo que explicó el profesor en `respuesta.txt`, lo que se verificó en
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

### 2.2 Conexiones (según `respuesta.txt`)

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

## 6. Comparación completa UNO standalone vs Mega integrado (motor)

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

## 7. ¿Conviene portar el PID del profesor al Mega?

**No conviene hacerlo "a lo bruto".** Razones:

1. **Son estrategias distintas para el mismo problema** (aceleración suave):
   - UNO mejorado: setpoint directo + **rampa de potencia**.
   - Mega: **rampa de setpoint** (`RAMPA_RPM_S = 1000 RPM/s`).
   Si se ponen las dos a la vez, se limitan mutuamente y el comportamiento deja
   de ser predecible.

2. **Los `Kp`/`Ki` son empíricos del lazo del UNO** (tacómetro por comparador,
   disparo en `loop()`, setpoint directo). El Mega tiene tacómetro digital por
   INT3, disparo por Timer1 y rampa de setpoint: el error que ve el PID es
   distinto, así que la sintonía **no se transfiere automáticamente**.

3. **Restricción del proyecto**: `tarea.txt` marca como no negociable **no
   cambiar `Kp`, `Ki`, `POT_MAX_PID` ni la máquina de estados del firmware del
   Mega**.

4. Lo único que podría evaluarse aparte (con permiso y pruebas sin 220 V) es el
   **anti-windup**, por ser una mejora de robustez independiente de los valores.

Si algún día se experimenta, lo correcto es **una variante nueva del Mega**
(por ejemplo `..._pid_profesor`) o un `#define`, dejando intacto el firmware
que ya anda.

---

## 8. Qué se hizo en esta sesión (registro)

1. Se comparó el código de `respuesta.txt` con los firmwares existentes:
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
5. Se agregó `respuesta.txt` y `tarea.txt` a `.gitignore` (no se suben a Git).
6. Commit `b8373cf` y push a `origin/main`.

---

## 9. Pendientes / a decidir

- Definir cuál de las alternativas de adaptación al Mega se implementa
  (LM393 recomendado).
- Confirmar el tipo exacto de tacómetro (analógico/alterno) y su nivel pico a
  pico para dimensionar divisor y recorte del LM393.
- Validar en banco (sin 220 V) la salida digital hacia D18 antes de probar en
  la máquina.
- Si se quisiera, evaluar el anti-windup como mejora aislada del Mega en una
  variante nueva (nunca tocando el firmware actual directamente).
