# Plan de integración: ciclo de 2 enjuagues (Mega) + motor TRIAC mejorado (UNO)

Documento de diseño y decisión. **No modifica firmware ni código**: establece el
estado actual del proyecto, la arquitectura propuesta y los pasos a seguir.

---

## 1. Objetivo

Integrar dos firmwares del profesor que hoy funcionan por separado:

1. **Ciclo de lavado con 2 enjuagues**
   (`lavarropas_controller_display_error_buzzer_2enjuagues.ino`)
2. **Motor con TRIAC, cruce por cero y tacómetro — PID mejorado**
   (`lavarropas_motor_cruce_cero_mejorado.ino`)

Resultado buscado:

- **Arquitectura maestro-esclavo** (propuesta, ver sección 4):
  - **Arduino Mega** → ciclo de lavado completo (máquina de estados, relés,
    LCD, presostato, buzzer). Actúa como **maestro**.
  - **Arduino UNO** → control del motor (cruce por cero, TRIAC, tacómetro,
    PID mejorado). Actúa como **esclavo**.
- **Funcionamiento independiente**: Mega + UNO deben trabajar solos, **sin
  Raspberry Pi**. La Raspberry es **opcional** y solo aporta el dashboard
  (recibe la información por USB del Mega cuando está conectada).
- **Sin sobrescribir** los firmwares ni dashboards existentes: todo lo nuevo va en
  variantes con nombre propio.
- Respetar la lógica del profesor (máquina de estados, tiempos, PID).

---

## 2. Inventario de firmwares existentes

| Firmware | Placa | Estado real | Rol |
|---|---|---|---|
| `lavarropas_controller_display_error_buzzer_2enjuagues` | UNO y Mega | Probado; actualmente en el Mega con los componentes del lavarropas (sin motor ni tacómetro) | Ciclo completo, 2 enjuagues |
| `lavarropas_motor_cruce_cero` | UNO y Mega | Probado; controla solo el motor | Motor standalone, PID original |
| `lavarropas_motor_cruce_cero_mejorado` | UNO | Probado; controla solo el motor | Motor standalone, PID del profesor |
| `lavarropas_controller_2enjuagues_motor_triac_MEGA` | Mega | Solo probado en simulación | Integración de referencia (todo en una placa) |

### 2.1 Qué aporta cada pieza a la integración

- **Del ciclo 2 enjuagues (Mega)**: la máquina de estados completa (REPOSO →
  LLENADO → LAVADO → DRENAJE_1 → LLENADO_ENJUAGUE → ENJUAGUE → DRENAJE_2 →
  LLENADO_SUAVIZANTE → ENJUAGUE_SUAVIZANTE → DRENAJE_3 → CENTRIFUGADO → FIN →
  ERROR_AGUA), el LCD, el presostato, los relés y el reporte JSON base.
- **Del motor mejorado (UNO)**: el control de fase del TRIAC, el cruce por
  cero, el tacómetro (comparador analógico D6/D7) y el PID mejorado
  (`Kp=0.0025`, `Ki=0.0005`, filtro 0.85/0.15, anti-windup, rampa dinámica).
- **De la integración de referencia (Mega)**: ideas aprovechables, pero **no**
  es la base propuesta para esta arquitectura (esa integración concentra todo en
  el Mega). Se reutilizan: la secuencia segura de relés (sentido con TRIAC
  apagado), la fase `ERROR_MOTOR`, el objeto JSON `motor` y el dashboard de
  telemetría como referencia visual.

---

## 3. Por qué maestro-esclavo (y no todo en el Mega)

### 3.1 El circuito del profesor es para UNO

El tacómetro del profesor usa el **comparador analógico interno** del UNO
(AIN0 = D6, AIN1 = D7). En el Mega ese comparador existe por hardware, pero
AIN0 (PE2) **no sale a ningún conector**, por lo que el circuito no se puede
conectar igual. Detalle completo en
[`docs/TACOMETRO_MOTOR_TRIAC.md`](TACOMETRO_MOTOR_TRIAC.md).

### 3.2 El UNO no alcanza para ciclo + motor

Si se intenta unir ciclo + motor en UNO, los pines chocan:

| Pin UNO | Ciclo 2 enjuagues | Motor mejorado | Conflicto |
|---|---|---|---|
| D2 | `PIN_START` | `PIN_ZC` (cruce por cero) | sí |
| D3 | `PIN_PRESOSTATO` | `PIN_MOC` (disparo TRIAC) | sí |
| D6 | LCD (pin D6) | `AIN0` (tacómetro) | sí |
| D7 | `PIN_BUZZER` | `AIN1` (referencia 2,5 V) | sí |

### 3.3 Conclusión

- El **ciclo** ya está cableado y probado en el **Mega** (con LCD, presostato,
  relés; falta motor y tacómetro).
- El **motor + tacómetro** funcionan con el circuito del profesor en el **UNO**.
- La combinación natural es **dos placas**: Mega (ciclo, maestro) + UNO
  (motor, esclavo), comunicadas por UART.

---

## 4. Arquitectura propuesta: maestro-esclavo

```text
                    ┌─────────────────────────────┐
                    │      Arduino MEGA 2560      │
                    │  (maestro — ciclo lavado)   │
                    │  LCD, relés, presostato,    │
                    │  máquina de estados, JSON   │
                    └──────────────┬──────────────┘
                                   │ UART (TX1/RX1)
                                   │ órdenes y telemetría
                    ┌──────────────▼──────────────┐
                    │       Arduino UNO           │
                    │  (esclavo — motor TRIAC)    │
                    │  cruce por cero, MOC,       │
                    │  tacómetro D6/D7, PID       │
                    └─────────────────────────────┘
                                   │
                          circuito de potencia
                          y tacómetro (profesor)

   Raspberry Pi (OPCIONAL)  ──USB──►  Mega  →  solo dashboard
```

### 4.1 Roles

- **Mega (maestro)**: ejecuta el ciclo. Decide cuándo arrancar/parar el motor,
  en qué sentido y a qué RPM. Mantiene LCD, presostato, relés y el reporte
  JSON para el dashboard. Le envía órdenes al UNO por UART y recibe telemetría
  (RPM, estado, falla).
- **UNO (esclavo)**: recibe órdenes del Mega y ejecuta el lazo del motor
  (cruce por cero, disparo TRIAC, tacómetro, PID). **Debe poder detener el
  motor por sí solo** si pierde el enlace con el Mega (timeout de seguridad).
- **Raspberry Pi (opcional)**: se conecta por USB al Mega y recibe el JSON
  para el dashboard. **No es necesaria** para que el lavarropas funcione.

### 4.2 Conexión física Mega ↔ UNO

| Arduino Mega | Arduino UNO | Nota |
|---|---|---|
| TX1 (D18) | RX (D0) | TX de un extremo va a RX del otro |
| RX1 (D19) | TX (D1) | TX de un extremo va a RX del otro |
| GND | GND | referencia común obligatoria |

- Lógica de **5 V en ambas placas** → no hace falta conversor de niveles.
- El UNO se alimenta por USB o fuente regulada; **GND común siempre** y no
  unir fuentes sin verificar.

### 4.3 Conflicto de pines del Mega (liberados con esta arquitectura)

En la integración "todo en el Mega", D18/D19 estaban ocupados por tacómetro y
cruce por cero. En maestro-esclavo **esos pines quedan libres** en el Mega
porque el motor se muda al UNO, así que `Serial1` (D18/D19) está disponible
para el enlace UART.

---

## 5. Requisito: funcionamiento sin Raspberry Pi

- El ciclo (Mega) y el motor (UNO) se comunican **directamente por UART** y no
  dependen de la Raspberry.
- La Raspberry solo escucha el Serial del Mega por USB para alimentar el
  dashboard. Si no está conectada, **el lavarropas sigue funcionando** (sin
  dashboard, obviamente).
- Implicancia de diseño: el Mega **no debe esperar respuestas de la Raspberry**
  ni bloquearse si el puerto USB no está conectado. El JSON se emite siempre
  por Serial; si nadie lo lee, no pasa nada.

---

## 6. Qué hay que rehacer en cada firmware (variantes nuevas)

### 6.1 Mega (maestro) — variante nueva

Partir de `lavarropas_controller_display_error_buzzer_2enjuagues.ino`:

- **Quitar** los relés directos de motor (CW/CCW) en favor de **órdenes por
  UART** al UNO.
- **Mantener**: máquina de estados, LCD, presostato, buzzer, relés de agua,
  suavizante, centrífuga (si aplica), reporte JSON base.
- **Agregar**:
  - `Serial1` (D18/D19) para hablar con el UNO.
  - Protocolo de órdenes: arrancar(sentido, rpm), parar, y recepción de
    telemetría (rpm, estado, falla).
  - `ERROR_MOTOR` si el UNO reporta falla de tacómetro o no responde.
  - Objeto `motor` en el JSON (reutilizar el contrato de la integración de
    referencia) para el dashboard.

### 6.2 UNO (esclavo) — variante nueva

Partir de `lavarropas_motor_cruce_cero_mejorado.ino`:

- **Cambiar** la entrada de comandos: de `R<rpm>`/`P<0-100>` por monitor serie
  a **órdenes por `Serial` (D0/D1)** desde el Mega.
- **Mantener**: cruce por cero (D2), MOC (D3), tacómetro (D6/D7), PID mejorado
  completo.
- **Agregar**:
  - Timeout de enlace: si no recibe órdenes válidas del Mega durante un
    tiempo, **detener el motor** (seguridad, no depender del maestro).
  - Respuesta de telemetría al Mega (rpm, estado, falla).

### 6.3 Raspberry / backend / dashboard (opcional)

- El backend (`main.py`) ya lee JSON del Mega por USB; no cambia en lo
  esencial.
- **Dashboard nuevo**: variante basada en `dashboard_motor.html` (que ya
  muestra ciclo + motor), con nombre propio, p. ej.
  `dashboard_motor_maestro_esclavo.html`, servida sin sobrescribir los existentes.
- Si la Raspberry no está, simplemente no hay dashboard; el resto funciona.

---

## 7. Decisión de control: rampa de setpoint vs rampa de potencia

En esta arquitectura, el lazo del motor vive **completo en el UNO** con el
firmware del profesor, así que **se conserva la estrategia del profesor**: la
**rampa dinámica de potencia** (2/8/15 % por ciclo) y el setpoint directo.

- No hay conflicto con la rampa de setpoint del Mega integrado, porque esa
  integración **no es la base** de esta arquitectura.
- Si en el futuro se quisiera la rampa de setpoint, habría que agregarla en el
  UNO (no en el Mega), y decidir si convive con la rampa de potencia.

---

## 8. Qué se puede hacer ya y qué requiere decisión

### Se puede hacer ya (sin hardware)

- Crear las **dos variantes nuevas** de firmware (Mega maestro + UNO esclavo)
  y compilarlas (`arduino:avr:mega` y `arduino:avr:uno`).
- Definir el **protocolo UART** (tramas de órdenes y telemetría).
- Crear el **dashboard nuevo**.
- Extender el **simulador** para representar el enlace maestro-esclavo.

### Requiere decisión / hardware

- Definir el **formato exacto del protocolo UART** (comandos, delimitadores,
  timeout, checksum opcional).
- Confirmar el **baudrate** del enlace Mega↔UNO (recomendado 9600 o 115200).
- Definir los **nombres finales** de las variantes nuevas y del dashboard.
- Validar en banco sin 220 V antes de probar en la máquina.

---

## 9. Resumen de la sesión (lo analizado)

1. El profesor entregó el **motor mejorado** (PID tuneado) para UNO, no para
   Mega.
2. El circuito del tacómetro del profesor funciona con los dos firmwares
   standalone de motor (ambos a 115200, texto plano).
3. Ese circuito no va directo al Mega (AIN0 no disponible).
4. La integración de referencia (`..._motor_triac_MEGA`) es una aproximación
   "todo en el Mega", probada solo en simulación.
5. El objetivo real es **integrar ciclo 2 enjuagues + motor mejorado**, sin
   sobrescribir nada.
6. **Propuesta de decisión**: arquitectura **maestro-esclavo** (Mega ciclo + UNO
   motor), porque:
   - el ciclo ya está cableado y probado en el Mega;
   - el motor + tacómetro funcionan en el UNO con el circuito del profesor;
   - hay UNO y Mega disponibles en el curso.
7. **Requisito**: Mega + UNO funcionan de forma independiente (sin Raspberry);
   la Raspberry es opcional y solo aporta el dashboard.

---

## 10. Pendientes / a decidir

- [ ] Definir el formato del protocolo UART Mega↔UNO (tramas y timeout).
- [ ] Confirmar el baudrate del enlace Mega↔UNO.
- [ ] Definir nombres finales de las variantes nuevas y del dashboard.
- [ ] Decidir si el timeout de seguridad del UNO corta solo el TRIAC o también
      libera los relés de sentido.
- [ ] Validar en simulador y luego en banco (sin 220 V).
