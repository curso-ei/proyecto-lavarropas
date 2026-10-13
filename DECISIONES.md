# Documento de decisiones, problemas y soluciones

Proyecto: controlador de lavarropas (Arduino UNO) + dashboard de monitoreo
(Raspberry Pi). Este documento registra **qué se decidió, por qué, qué
problemas aparecieron y cómo se resolvieron**.

Carpeta de trabajo: `~/proyecto-pi/`
Los originales de `~/proyecto-pi` **no se tocaron**.

---

## 1. Contexto de partida

- El profesor entregó `lavarropas_controller_display_error_buzzer.ino`: un
  **controlador completo y autónomo** con máquina de estados no bloqueante,
  LCD 16x2 y alarma sonora.
- El proyecto ya tenía un dashboard (`main.py` + `simulador.py` +
  `static/dashboard.html`) pensado para un firmware distinto
  (`sketch_base.ino`), con control manual de relés por comandos `SET_...`.
- Los dos mundos **no hablaban el mismo idioma**: el `.ino` del profesor
  enviaba por Serial solo texto de depuración `[LCD] ...` y no leía comandos;
  el backend esperaba JSON y mandaba `SET_...`.

## 2. Objetivo acordado

1. **Conservar casi sin cambios** el firmware del profesor (pines, lógica,
   fases, LCD, buzzer).
2. El dashboard es un **espejo de solo lectura**: refleja lo que hace el
   Arduino, sin controlar relés.
3. Agregar al firmware **lo mínimo** para reportar estado por JSON.
4. Documentar todo.

---

## 3. Decisiones tomadas

### D1 — Arquitectura: Arduino maestro, dashboard observa

**Decisión:** el Arduino corre el ciclo; la Raspberry solo lee y muestra.

**Por qué:** el `.ino` del profesor ya es un controlador autónomo probado.
Invertir la arquitectura (Pi maestra) habría implicado portar el ciclo al
`sketch_base.ino` y duplicar lógica, además de arriesgar el trabajo del
profesor. La opción elegida es la de menor intervención y menor riesgo.

### D2 — El dashboard no controla relés

**Decisión:** se elimina el control por relé (`POST /control/{componente}`).

**Por qué:** el usuario aclaró que la versión con botones era solo un
**ejemplo** de dashboard. Además, si el dashboard mandara `SET_...` mientras
el ciclo corre, el firmware lo sobrescribiría en cada `loop()` (porque las
fases reconfiguran los relés), generando un conflicto de lógica.

### D3 — Etiqueta de `PIN_TRABA`: "En marcha"

**Decisión:** la clave JSON `bloqueo_puerta` mapea a `PIN_TRABA`, y el
dashboard lo etiqueta **"En marcha"**.

**Evidencia del firmware:**
- `setup()`: `setRelay(PIN_TRABA, false)`
- `REPOSO` + START: `setRelay(PIN_TRABA, true)`
- `FIN`: `setRelay(PIN_TRABA, false)`
- `apagarTodo()` **no** incluye `PIN_TRABA`, así que queda encendido durante
  todo el ciclo.

Es decir, se comporta como un indicador de "ciclo en marcha", no como una
traba de puerta. Se conserva la clave `bloqueo_puerta` en el JSON para
mantener la compatibilidad de nombres, pero se muestra como "En marcha". Si en la
maqueta el pin 4 está cableado a una traba física real, alcanza con cambiar
la etiqueta.

### D4 — Buzzer: banner de error + indicador discreto

**Decisión:** el buzzer **no** se muestra como una caja más de la grilla de
salidas. En su lugar:
- cuando hay error aparece un **banner rojo** con "⚠ FALTA DE AGUA";
- junto al banner hay un **indicador discreto** "🔔 alarma sonora" que se
  enciende cuando el pin físico del buzzer está sonando.

**Por qué:** el firmware hace `tone(2500)` / `noTone()` alternando cada
500 ms, o sea el pin **parpadea ~2 veces por segundo**. Dibujarlo como una
luz en la grilla produciría un parpadeo molesto y lo pondría al mismo nivel
que bombas/motor, cuando en realidad es una alarma. Igual se expone
`"buzzer": true/false` en el JSON, así el indicador refleja el parpadeo real.

### D5 — START: solo lectura (Opción 3.A), con 3.B preparada

**Decisión:** el dashboard **no** manda comandos. El arranque y el reset de
error se hacen con el **botón físico** de la máquina.

**Por qué:** máxima fidelidad al trabajo del profesor y mínimo código nuevo.
Además, el firmware no lee Serial, así que respetar eso evita modificar su
lógica.

**Pendiente preparado (Opción 3.B):** si más adelante se quiere arranque
remoto, ya quedó listo el camino:
- En el firmware: el parser de `CMD_START` / `CMD_RESET` está **escrito y
  comentado** en `lavarropas_controller_display_error_buzzer.ino`.
- En el backend: existen `POST /accion/start` y `POST /accion/reset`
  (`main.py`), que mandan esos comandos.
- En el dashboard: existe la constante `PERMITIR_ACCIONES` y los botones
  (ocultos). Poniéndola en `true` y descomentando el firmware, se habilita.

### D6 — `DEBUG_LCD_SERIAL`: apagado, detrás de un `#define`

**Decisión:** el `Serial.print("[LCD] ...")` original **no se borra**: se
encierra en `#ifdef DEBUG_LCD_SERIAL` y por defecto queda **apagado**.

**Por qué:** por defecto el canal Serial queda limpio (solo JSON). Pero se
conserva el código del profesor y se puede reactivar descomentando una línea
para depurar con `arduino-cli monitor`.

### D7 — El texto del LCD se refleja en el dashboard

**Decisión:** el firmware envía `"lcd": ["linea1", "linea2"]` y el dashboard
lo dibuja en un panel estilo LCD verde.

**Por qué:** pedido explícito del usuario: que el dashboard muestre lo del
LCD. Además así la info no se pierde al apagar `DEBUG_LCD_SERIAL`.

### D8 — Mapa de fases y estados

| Fase del firmware    | `estado_ciclo` (dashboard) |
|----------------------|----------------------------|
| `REPOSO`             | `inactivo`                 |
| `LLENADO`            | `llenando`                 |
| `LLENADO_ENJUAGUE`   | `llenando`                 |
| `LAVADO`             | `lavando`                  |
| `ENJUAGUE`           | `enjuagando`               |
| `DRENAJE_1/2/3`      | `desagotando`              |
| `SUAVIZANTE`         | `suavizando`               |
| `CENTRIFUGADO`       | `centrifugando`            |
| `FIN`                | `finalizado`               |
| `ERROR_AGUA`         | `error` (+ `error:"FALTA DE AGUA"`) |

Se conserva además el nombre crudo de la fase en el campo `"fase"` para no
perder información y poder depurar.

### D9 — Mapa de componentes

| Dashboard          | Firmware            | Pin |
|--------------------|---------------------|-----|
| `bloqueo_puerta`   | `PIN_TRABA`         | 4   |
| `buzzer`           | `PIN_BUZZER`        | 7   |
| `bomba_llenado`    | `PIN_LLENADO`       | 8   |
| `motor_sentido1`   | `PIN_MOTOR_CW`      | 9   |
| `motor_sentido2`   | `PIN_MOTOR_CCW`     | 10  |
| `bomba_desagote`   | `PIN_DRENAJE`       | 11  |
| `suavizante`       | `PIN_SUAVIZANTE`    | 12  |
| `centrifuga`       | `PIN_CENTRIFUGA`    | 13  |
| `presostato`       | `PIN_PRESOSTATO`    | 3   |
| `pulsador`         | `PIN_START`         | 2   |

`resistencia` **desaparece**: no existe en el firmware del profesor (venía
del `sketch_base.ino`).

### D10 — Pista visual "Esperando nivel de agua"

**Decisión:** el dashboard muestra un aviso discreto
**"⏳ Esperando nivel de agua…"** cuando `estado_ciclo == "llenando"` **y**
`presostato == false`.

**Por qué:** el firmware no dispara `ERROR_AGUA` durante el llenado; se queda
llenando indefinidamente hasta que el presostato marque nivel (ver O1 y P9).
Sin esta pista, el dashboard se veía "congelado" en *Llenando* sin explicar
por qué. El aviso es una **espera**, no una alarma: se muestra en amarillo,
separado del banner rojo de error, para no confundirlos.

**Regla exacta:** `llenando && !presostato`.

**No requiere** cambios en firmware ni backend: se deriva de datos que el
dashboard ya recibe. Es a nivel de JavaScript/HTML, en `dashboard.html`.

**Ojo:** esto **no** es parte del protocolo ni del firmware. Si se decide que
el Arduino debería dar un error por llenado lento, eso sería una decisión
distinta y requeriría modificar el firmware.

---

## 4. Problemas encontrados y soluciones

### P1 — El firmware no emitía JSON

**Problema:** `main.py` busca líneas JSON; el `.ino` solo emitía `[LCD] ...`.
Resultado: dashboard siempre en `sin_datos`.

**Solución:** se agregó `enviarEstadoJSON()` con `ArduinoJson`, llamada por
intervalo (`INTERVALO_ENVIO_MS = 1000`) tanto en `REPOSO` como al final de
cada `loop()`. No se modificó la lógica del ciclo.

### P2 — RAM del Arduino UNO ajustada

**Problema:** el primer intento usó `StaticJsonDocument<512>`, que reserva los
bytes en el **stack**. El Uno tiene solo 2048 bytes de RAM y el sketch ya
usaba ~1269 en globales. Riesgo de cuelgue por desborde de pila.

**Solución:** cambiar a `JsonDocument` (ArduinoJson v7), que dimensiona el
documento en el **heap** y lo libera al salir de la función. Se eliminaron
además copias temporales de `String`. Resultado de compilación:
`ram: [=====     ] 61% (779 bytes libres)` y `flash: 48%`.

### P3 — ArduinoJson no estaba instalada

**Problema:** `fatal error: ArduinoJson.h: No such file or directory`.

**Solución:** `arduino-cli lib install ArduinoJson` (quedó la v7.4.3).
También faltaba `LiquidCrystal`, se instaló (`LiquidCrystal@1.0.7`).

### P4 — `tone()` comparte Timer2

**Problema (detectado, no bloqueante):** `tone()` usa el Timer2 del AVR, que
afecta el PWM de los pines 3 y 11.

**Análisis:** en este diseño el pin 3 es `PIN_PRESOSTATO` (entrada digital) y
el pin 11 es `PIN_DRENAJE` (salida digital, no PWM). Por eso **no hay
conflicto funcional**. Se documenta para que quede constancia.

### P5 — Cambiar el nombre de la carpeta del sketch

**Problema:** arduino-cli y el IDE exigen que el archivo `.ino` esté dentro
de una carpeta con el **mismo nombre que el `.ino`**.

**Solución:** la copia se ubicó en
`firmware/lavarropas_controller_display_error_buzzer/lavarropas_controller_display_error_buzzer.ino`.

### P6 — El dashboard tenía botones de control que ya no aplican

**Problema:** el dashboard original tenía botones "Encender/Apagar" por relé
y el backend un endpoint `/control/{componente}`.

**Solución:** se rehízo el dashboard (solo lectura) y se eliminó el endpoint.
Se verificó que `POST /control/...` ahora devuelve `404`.

### P7 — El simulador no representaba el ciclo real

**Problema:** `simulador.py` usaba fases inventadas (`bloqueando_puerta`,
`centrifugado`) y componentes viejos (`resistencia`), desalineados con el
firmware del profesor.

**Solución:** se reescribió para emitir el mismo JSON ampliado (con `fase`,
`lcd`, `giros`, `buzzer`) y las fases reales
(`REPOSO...FIN, ERROR_AGUA`). También acepta `CMD_START`/`CMD_RESET` para
cuando se habilite la Opción 3.B.

### P9 — El simulador dejaba avanzar el ciclo sin agua

**Problema:** la opción "Avanzar fase" pasaba de fase sin mirar el
presostato, así que se podía recorrer todo el ciclo con el presostato en
`false`. Eso **no coincide** con el firmware real, donde el presostato
gobierna el llenado (ver P2/O1): con nivel `false`, `LLENADO` nunca avanza.
El simulador daba una idea equivocada de cómo se comportaría el Arduino.

**Solución:** el simulador ahora replica la lógica real:
- Arrancar desde `REPOSO` entra a `LLENADO` **igual** (no mira el presostato),
  como el firmware.
- `LLENADO` / `LLENADO_ENJUAGUE` **no avanzan** sin presostato; avisa por
  consola: `Esperando presostato: LLENADO no avanza hasta que marque nivel`.
- `LAVADO` / `ENJUAGUE` pasan a `ERROR_AGUA` (buzzer) si el presostato cae,
  igual que la línea 244 del firmware.
- Al completar un `DRENAJE`, el presostato **se apaga solo** (simula que el
  tambor se vació).
- `reset_error()` vuelve a la **fase previa** al error (no a `REPOSO`), y solo
  si hay nivel — como el botón físico.
- Se agregó la opción **7) Forzar avance de fase (modo libre)**, que ignora
  todas esas condiciones, como atajo para mirar pantallas.

**Importante:** esto es solo `simulador.py`. Es un programa aparte que corre
en la Pi; **no puede afectar al Arduino real**, que no lo ejecuta ni lo conoce.
El firmware y el dashboard no se tocaron.

### P10 — El simulador saltaba el vaivén del motor en LAVADO/ENJUAGUE

**Problema:** al avanzar desde `LAVADO`, el simulador pasaba directamente a
`DRENAJE_1`, mostrando el motor encendido en un solo sentido y de golpe la
bomba de desagote. No reproducía el **vaivén** real: en el firmware, dentro
de `LAVADO`/`ENJUAGUE` el motor alterna sentido con pausas
(`T_GIRO = 5 s` girando, `T_PAUSA_GIRO = 2 s` de pausa) hasta completar
`MAX_GIROS = 3`, y **recién ahí** pasa al drenaje. Y todo eso ocurre **solo,
por tiempo**, no requiere apretar nada.

**Solución:** se agregó un sub-estado de agitación y un **hilo**
(`hilo_agitacion`) que avanza el vaivén por tiempo, igual que el firmware:
- Al entrar a `LAVADO`/`ENJUAGUE` arranca en **pausa** (como el firmware) y
  luego empieza a girar.
- Alterna `motor_sentido1` (>>) y `motor_sentido2` (<<) cada 5 s, con 2 s de
  pausa entre giros.
- Cuenta el giro al terminar el sentido inverso (CCW), como
  `manejarAgitadoPorCiclos()`.
- Completados 3 giros, pasa solo a `DRENAJE_1` / `DRENAJE_2`.
- El LCD refleja `Giro x/3 >>`, `Giro x/3 <<` o `Pausa de vaiven`.

La opción 1 durante el vaivén avisa que la agitación avanza sola (y recuerda
la opción 7 para forzar). El avance libre (opción 7) sí salta el vaivén, para
inspeccionar pantallas.

**Nota de fidelidad de tiempo:** el simulador usa los mismos tiempos que el
firmware (5 s / 2 s). Un lavado completo de 3 giros tarda entonces
3×(5+2) = 21 s de agitación, más los drenajes de 10 s. Es lento a propósito,
porque replica la máquina real.

### P11 — El simulador había que avanzarlo a mano en cada fase

**Problema:** además del vaivén (P10), las fases con temporizador
(`DRENAJE`, `SUAVIZANTE`, `CENTRIFUGADO`) y el `LLENADO` seguían requiriendo
apretar la opción 1 para pasar de una a otra. Eso no refleja al firmware, que
avanza **solo por tiempo** (o por presostato, en el llenado). Además, con los
tiempos reales (10 s drenaje, 15 s centrifugado), probar el ciclo completo a
mano era tedioso.

**Solución:** el hilo de tiempos ahora replica toda la lógica temporal del
firmware:
- `LLENADO` / `LLENADO_ENJUAGUE`: avanzan solos cuando el presostato marca
  nivel.
- `DRENAJE_1/2/3`: avanzan solos a los 10 s.
- `SUAVIZANTE`: avanza solo a los 10 s.
- `CENTRIFUGADO`: avanza solo a los 15 s.
- `LAVADO` / `ENJUAGUE`: vaivén automático (P10).

Con esto, una vez que arrancás (START/opción 1) y el presostato da nivel, el
ciclo **se completa solo**, igual que el Arduino.

**Factor de velocidad (opción 8):** para no esperar los tiempos reales, se
agregó un factor `1x / 2x / 5x / 10x` que divide todos los tiempos
(`duracion()`). Por defecto es `1x` (idéntico al firmware). Es **solo del
simulador**: no existe en el Arduino ni afecta al dashboard.

### P8 — Prueba end-to-end sin hardware

**Solución:** se probó con `socat` (par de PTYs) + backend + inyección del
JSON. Se verificó que:
- `/estado` refleja correctamente fases, componentes, LCD y error.
- El log registra `Estado del ciclo: ...` y `ERROR: FALTA DE AGUA`.
- `/accion/start` responde `{"enviado":true,"accion":"start"}`.
- `/control/...` devuelve `404`.
El script `iniciar_pruebas.sh` automatiza socat+simulador+backend
en una sesión tmux propia (`pruebas`), sin chocar con la original.

---

## 5. Cambios concretos por archivo

### `firmware/lavarropas_controller_display_error_buzzer/*.ino`
- `#include <ArduinoJson.h>`.
- `#define DEBUG_LCD_SERIAL` (comentado).
- Variables `tUltimoEnvioEstado`, `INTERVALO_ENVIO_MS`, `pulsadorActivo`.
- `nombreFase()`, `estadoCiclo()`, `releEncendido()`, `enviarEstadoJSON()`,
  `actualizarEnvioEstado()`.
- `pulsadorActivo` se actualiza en `leerStartPresionado()`.
- Bloque **comentado** con el parser `CMD_START`/`CMD_RESET` (Opción 3.B).
- `mostrarLCD()` ahora usa `#ifdef DEBUG_LCD_SERIAL`.
- `loop()` llama a `actualizarEnvioEstado()` en `REPOSO` y al final.
- **Sin cambios** en: pines, tiempos, máquina de estados, `manejar*`,
  antirrebote, LCD, buzzer.

### `main.py`
- Docstring del protocolo actualizado.
- `COMPONENTES_VALIDOS` con las 8 claves reales; sale `resistencia`.
- `ESTADO_DEFAULT` con `fase`, `giros`, `max_giros`, `lcd`.
- Eliminado `POST /control/{componente}` y el interlock asociado.
- Agregado `POST /accion/{start|reset}` (`ACCIONES_VALIDAS`).

### `static/dashboard.html`
- Rehecho como espejo de solo lectura.
- Panel de LCD, banner de alarma, indicador discreto de alarma sonora,
  barra de progreso de giros, grilla de salidas/entradas, log.
- `PERMITIR_ACCIONES = false` y función `accion()` para la Opción 3.B.
- **Pista visual "⏳ Esperando nivel de agua"**: aparece cuando
  `estado_ciclo == "llenando"` y `presostato == false`. No es un error (el
  firmware no da alarma en llenado): es una espera. Ver D10.

### `simulador.py`
- Fases y componentes reales, JSON ampliado, menú adaptado, parser de
  comandos.
- Lógica fiel al firmware (P9): el presostato condiciona el avance, dispara
  `ERROR_AGUA` en lavado/enjuague y se vacía al terminar un drenaje.
- Vaivén automático del motor (P10): hilo que alterna sentido por tiempo
  (5 s giro / 2 s pausa, 3 giros) y recién luego pasa al drenaje.
- Avance automático de fases por tiempo (P11): drenajes, suavizante y
  centrifugado avanzan solos; el llenado avanza con el presostato.
- Opción 8: factor de velocidad de simulación (1x/2x/5x/10x).
- Opción 7: avance libre para pruebas.
- Menú final:
  ```
  1) Avanzar a la siguiente fase (respeta el presostato)
  2) Togglear presostato
  3) Togglear pulsador
  4) Forzar un error de falta de agua
  5) Resolver error (vuelve a la fase previa si hay nivel)
  6) Ver estado actual
  7) Forzar avance de fase (modo libre, ignora condiciones)
  8) Cambiar velocidad de simulación
  0) Salir
  ```

### `README.md`
- Plan, contratos y pendientes (este documento es el detalle de decisiones).

### `iniciar_pruebas.sh` (nuevo)
- Levanta socat + simulador + backend desde `~/proyecto-pi`, en la sesión tmux
  `pruebas`.

---

## 6. Verificación realizada

| Prueba | Resultado |
|--------|-----------|
| `python -m py_compile main.py simulador.py` | OK |
| `arduino-cli compile --fqbn arduino:avr:uno` | OK (flash 48%, RAM 61%) |
| `/estado` con JSON del firmware | OK |
| Fase `ERROR_AGUA` + buzzer + LCD | OK |
| `POST /accion/start` | OK |
| `POST /accion/foo` | 400 |
| `POST /control/...` | 404 (eliminado) |
| `/` sirve el dashboard | OK |
| Aviso "Esperando nivel de agua" (6 casos) | OK |

---

## 7. Pendientes / a futuro

1. **Probar con el Arduino real** (no solo simulador): verificar que el JSON
   se lea estable a 9600 baudios.
2. **Decidir Opción 3.B**: habilitar arranque/resetremoto o dejarlo así.
3. **Confirmar cableado real del pin 4**: si es traba física, cambiar la
   etiqueta "En marcha" por "Bloqueo puerta".
4. **Alinear `arduino_menu.sh`**: la opción "Info de pines" y la plantilla
   siguen mostrando el pinout viejo (pines 2..9, sin LCD ni buzzer).
5. **Dependencias**: en una instalación nueva hay que correr
   `arduino-cli lib install ArduinoJson LiquidCrystal`.
6. **Intervalo de envío**: hoy 1 s. Si se quiere más fluidez en el LCD, se
   puede bajar (a costa de más tráfico Serial).
7. **Consultar al profesor por la fase `SUAVIZANTE`**: ver observación en la
   sección 9 y la propuesta de dos enjuagues en la sección 8.
8. **Probar la variante en hardware real**: compilar y subir
   `..._2enjuagues` al Arduino Uno y verificar el ciclo completo.
9. **Probar el dashboard en Windows** (ver sección 10).

---

## 8. Propuesta: ciclo con dos enjuagues (IMPLEMENTADO EN VARIANTE)

> **Estado:** prototipado en el simulador y **ya implementado como firmware
> variante** en
> `firmware/lavarropas_controller_display_error_buzzer_2enjuagues/`.
> El `.ino` **original del profesor NO se tocó**, ni tampoco la copia fiel
> `firmware/lavarropas_controller_display_error_buzzer/`. La variante es un
> archivo/carpeta aparte.

### Motivación

En el firmware original (ver O1), la fase `SUAVIZANTE` **no carga agua ni
mueve la ropa**: solo abre el relé 10 s sobre un tambor vacío y luego
`DRENAJE_3` lo expulsa. En una lavadora real, el suavizante se agrega en el
**último enjuague**, con agua y con agitación suave.

La descripción que se esperaba del ciclo era, para el cierre:

> *"Enjuague final: carga agua + suavizante, mueve la ropa y desagota."*

Eso **no es lo que hace el firmware**. La propuesta es que sí lo haga.

### Ciclo propuesto

```
REPOSO
  ▼
LLENADO → LAVADO → DRENAJE_1                     (lavado)
  ▼
LLENADO_ENJUAGUE → ENJUAGUE → DRENAJE_2          (enjuague 1)
  ▼
LLENADO_SUAVIZANTE → ENJUAGUE_SUAVIZANTE → DRENAJE_3   (enjuague final)
  ▼
CENTRIFUGADO → FIN
```

Fases nuevas respecto del original:

| Fase nueva            | Qué hace                                             |
|-----------------------|------------------------------------------------------|
| `LLENADO_SUAVIZANTE`  | carga agua **y** abre el relé de suavizante          |
| `ENJUAGUE_SUAVIZANTE` | mueve la ropa (mismo vaivén, 3 giros, mezcla suavizante) |

La fase `SUAVIZANTE` "seca" del original **desaparece** (se reemplaza por
`LLENADO_SUAVIZANTE` + `ENJUAGUE_SUAVIZANTE`).

### Cómo probarlo (sin tocar el firmware)

```bash
cd ~/proyecto-pi
source ~/proyecto-pi/venv/bin/activate
PUERTO_FORZADO=/dev/pts/X uvicorn main:app --host 0.0.0.0 --port 8000   # en una terminal
CICLO=dos_enjuagues python simulador.py /dev/pts/Y                       # en otra
```

El dashboard no necesita cambios: las fases nuevas se traducen a
`llenando` / `enjuagando` / `desagotando`, que ya existen. El único impacto
visual sería que durante `LLENADO_SUAVIZANTE` se verían encendidos a la vez
`bomba_llenado` y `suavizante`.

### Resultado de la prueba

Secuencia completa real obtenida del prototipo:

```
LAVADO → DRENAJE_1 → LLENADO_ENJUAGUE → ENJUAGUE → DRENAJE_2
       → LLENADO_SUAVIZANTE → ENJUAGUE_SUAVIZANTE → DRENAJE_3
       → CENTRIFUGADO → FIN
```

Y durante el enjuague final:

```
LLENADO_SUAVIZANTE   componentes: bomba_llenado + suavizante + bloqueo_puerta
                     LCD: "CARGANDO AGUA / Enjuague+suav."
ENJUAGUE_SUAVIZANTE  LCD: "ENJUAGUE FINAL / Pausa de vaiven"
```

### Si se aprueba, qué habría que cambiar

**Ya implementado** como firmware variante (ver abajo). Resumen de lo que se
hizo:

1. **Firmware variante**
   (`firmware/lavarropas_controller_display_error_buzzer_2enjuagues/`):
   se agregaron las fases `LLENADO_SUAVIZANTE` y `ENJUAGUE_SUAVIZANTE` al
   enum y al `switch`, y se reencadenó
   `DRENAJE_2 → LLENADO_SUAVIZANTE → ENJUAGUE_SUAVIZANTE → DRENAJE_3`.
   `LLENADO_SUAVIZANTE` prende `PIN_SUAVIZANTE` y llama a `manejarLlenado()`;
   `ENJUAGUE_SUAVIZANTE` llama a `manejarAgitadoPorCiclos()`.
   Se sacó `SUAVIZANTE` del enum (ya no existe).
2. **Simulador**: ya estaba el prototipo (`CICLO=dos_enjuagues`), con los
   mismos nombres de fase que el firmware variante.
3. **Dashboard**: sin cambios (las fases se mapean a estados existentes:
   `llenando` / `enjuagando` / `desagotando`).
4. **Backend**: sin cambios.

### Firmware variante: detalles de implementación

Archivo:
`firmware/lavarropas_controller_display_error_buzzer_2enjuagues/lavarropas_controller_display_error_buzzer_2enjuagues.ino`

Cambios respecto de la copia fiel (diff mínimo):

| Zona | Antes | Después |
|------|-------|---------|
| `enum Fase` | `SUAVIZANTE` | `LLENADO_SUAVIZANTE`, `ENJUAGUE_SUAVIZANTE` |
| `cambiarFase()` | resetea giros en `LAVADO`/`ENJUAGUE` | + `ENJUAGUE_SUAVIZANTE` |
| `nombreFase()` | caso `SUAVIZANTE` | casos de las dos fases nuevas |
| `estadoCiclo()` | `SUAVIZANTE → suavizando` | `LLENADO_SUAVIZANTE → llenando`, `ENJUAGUE_SUAVIZANTE → enjuagando` |
| `enviarEstadoJSON()` | giros en `LAVADO`/`ENJUAGUE` | + `ENJUAGUE_SUAVIZANTE` |
| detección `ERROR_AGUA` | en `LAVADO`/`ENJUAGUE` | + `ENJUAGUE_SUAVIZANTE` |
| `case DRENAJE_2` | `manejarDrenaje(SUAVIZANTE)` | `manejarDrenaje(LLENADO_SUAVIZANTE)` |
| `case SUAVIZANTE` | solo `setRelay(SUAVIZANTE)` 10 s | reemplazado por `LLENADO_SUAVIZANTE` + `ENJUAGUE_SUAVIZANTE` |
| `T_SUAVIZANTE` | 10000UL activo | comentado (ya no se usa) |

**Pines, tiempos de drenaje/centrifugado, giro/vaivén, LCD, buzzer y reporte
JSON no cambiaron.**

### Compilación y verificación

| Prueba | Resultado |
|--------|-----------|
| `arduino-cli compile` variante | OK (flash 49%, RAM 63%, 741 B libres) |
| `arduino-cli compile` copia fiel | OK (flash 48%, RAM 61%) |
| Cadena de transiciones (extraída del `.ino`) | `LLENADO → LAVADO → DRENAJE_1 → LLENADO_ENJUAGUE → ENJUAGUE → DRENAJE_2 → LLENADO_SUAVIZANTE → ENJUAGUE_SUAVIZANTE → DRENAJE_3 → CENTRIFUGADO → FIN` ✅ |
| Fases firmware == fases simulador | Coinciden 1:1 |
| `LLENADO_SUAVIZANTE` prende suavizante + `manejarLlenado()` | Sí |
| `ENJUAGUE_SUAVIZANTE` llama `manejarAgitadoPorCiclos()` | Sí |

### Cómo subirlo al Arduino (cuando se apruebe)

```bash
cd ~/proyecto-pi
~/bin/arduino-cli compile --fqbn arduino:avr:uno \
  firmware/lavarropas_controller_display_error_buzzer_2enjuagues
~/bin/arduino-cli upload -p /dev/ttyACM0 --fqbn arduino:avr:uno \
  firmware/lavarropas_controller_display_error_buzzer_2enjuagues
```

Para volver al original, se sube la copia fiel
(`firmware/lavarropas_controller_display_error_buzzer/`), que es idéntica al
comportamiento del profesor.

### Preguntas para el profesor

- ¿El suavizante debía agregarse con agua y agitación (como en la máquina
  real) o la fase "seca" del original es intencional?
- Si es lo primero, ¿el enjuague final reemplaza al `SUAVIZANTE` actual, o se
  agrega como un enjuague extra (quedando dos enjuagues **más** el bloque de
  suavizante)?

---

## 9. Observaciones sobre el firmware original (NO modificado)

Estas observaciones surgen de leer el firmware del profesor. **No se cambió
nada**: el archivo adaptado conserva el comportamiento original. Se dejan
registradas para consultar/decidir a futuro.

### O1 — La fase `SUAVIZANTE` no llena agua ni hace girar el motor

**Evidencia (código original):**

```cpp
case SUAVIZANTE:
  mostrarLCD("AGREGANDO...    ", "Suavizante      ");
  setRelay(PIN_SUAVIZANTE, true);
  if (millis() - tInicioFase >= T_SUAVIZANTE) {
    cambiarFase(DRENAJE_3);
  }
  break;
```

La fase se limita a **prender el relé de suavizante durante 10 s**
(`T_SUAVIZANTE = 10000UL`). No llama a:

- `manejarLlenado()` → no carga agua;
- `manejarAgitadoPorCiclos()` → no gira ni hace vaivén;
- `manejarDrenaje()` → no drena durante la fase (el drenaje es la fase
  siguiente, `DRENAJE_3`).

Además, `cambiarFase()` siempre llama a `apagarTodo()` al entrar, así que el
motor arranca la fase apagado y así permanece.

**Ubicación en el ciclo:**

```
ENJUAGUE
   ▼
DRENAJE_2     (10 s, vacía el agua del enjuague)
   ▼
SUAVIZANTE    (10 s, SOLO abre el relé de suavizante; sin agua, sin giro)
   ▼
DRENAJE_3     (10 s)
   ▼
CENTRIFUGADO  (15 s)
```

Es decir: **el suavizante se agrega entre `DRENAJE_2` y `DRENAJE_3`**, sin
carga de agua previa y sin agitación.

**Por qué es llamativo:** en una lavadora doméstica típica el suavizante se
agrega en un enjuague con agua y con agitación suave, y luego se drena. Acá
cae en un tambor ya vacío y 10 s después `DRENAJE_3` lo expulsa. Puede ser
intencional (por ejemplo, un dosificador que se vacía en el último enjuague)
o puede ser una simplificación/omisión del modelo. **No es un error de la
adaptación**: está así en el `.ino` original.

**Impacto en el dashboard:** el panel mostrará la fase con el texto
`AGREGANDO... / Suavizante`, el componente `suavizante` encendido y el motor
apagado; no hay un componente de agua asociado a esa fase. No requiere ningún
cambio en el dashboard.

**Acción sugerida:** preguntar al profesor si `SUAVIZANTE` debería incluir
llenado y/o giro. Si la respuesta es que sí, la corrección se hace **solo en
el firmware** (agregar `manejarLlenado(...)` y/o `manejarAgitadoPorCiclos(...)`
en esa fase), sin tocar el resto del sistema. La variante quedaría como una
rama/archivo aparte para no alterar el original.

### O2 — `PIN_TRABA` como indicador de "En marcha"

Ver decisión **D3**. Se documenta acá como observación de lectura del código
original: `PIN_TRABA` se enciende con START y se apaga en `FIN`, y
`apagarTodo()` no lo incluye, por lo que permanece activo durante todo el
ciclo. El dashboard lo rotula "En marcha" (clave JSON `bloqueo_puerta` sin
cambiar, por compatibilidad).

### O3 — El suavizante y la centrífuga no se apagan explícitamente

En `SUAVIZANTE` y `CENTRIFUGADO` solo se prende el relé correspondiente; nunca
se lo apaga dentro de la fase. El apagado ocurre de forma indirecta cuando la
siguiente fase llama a `cambiarFase()` → `apagarTodo()` (que sí incluye
`PIN_SUAVIZANTE` y `PIN_CENTRIFUGA`). No es un bug, pero conviene tenerlo
presente al leer el código.

---

## 10. Portabilidad a Windows (sin Raspberry Pi)

### Decisión

El dashboard debe poder correrse en una **PC con Windows** (que es donde el
profesor sube el firmware con el Arduino IDE), sin depender de la Raspberry.

### Análisis: ¿había algo atado a Linux?

Se revisó `main.py`: **no tiene nada específico de Linux**.

- No usa `/dev/...`, ni rutas `/home/`, ni `/tmp/`.
- Usa `pyserial` con `serial.tools.list_ports`, que es multiplataforma.
- La detección del Arduino es por **VID** (`0x2341`, `0x1A86`), que pyserial
  reporta igual en Windows (`COMx`).
- El dashboard es HTML+JS: idéntico en cualquier navegador.

Lo único "de Linux" era el entorno (`venv` creado en Linux, que no sirve en
Windows) y los scripts `iniciar_pruebas.sh` / `socat`.

### Solución

Se agregaron tres archivos, sin tocar el backend ni el dashboard:

| Archivo | Para qué |
|---------|----------|
| `requirements.txt` | Dependencias: `fastapi`, `uvicorn`, `pyserial`, `pydantic` |
| `README_WINDOWS.md` | Guía paso a paso (Python, Arduino IDE, COM, servidor) |
| `iniciar_windows.bat` | Lanzador de doble clic (acepta `COMx` opcional) |

### Diferencias Windows ↔ Linux

| Tema | Raspberry (Linux) | Windows |
|------|-------------------|---------|
| Puerto serie | `/dev/ttyACM0` | `COM3`, `COM4`, ... |
| Activar venv | `source venv/bin/activate` | `venv\Scripts\activate` |
| Forzar puerto | `export PUERTO_FORZADO=...` | `set PUERTO_FORZADO=...` |
| Scripts | `iniciar_pruebas.sh` | `iniciar_windows.bat` |
| Backend / dashboard | idénticos | idénticos |

### Notas para Windows

- El `venv` debe **recrearse** en Windows (el de Linux no es reutilizable).
- El Monitor Serie del Arduino IDE y el dashboard **no pueden usar el mismo
  `COM` a la vez**. Hay que cerrar el Monitor antes de levantar el dashboard.
- Para el uso normal con hardware real **no se necesita** `socat` ni el
  simulador. El simulador en Windows requiere `com0com` en lugar de `socat`.
- Si `uvicorn` no se reconoce: `python -m uvicorn main:app ...`.

