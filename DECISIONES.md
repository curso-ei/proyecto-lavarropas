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
usaba ~1269 en globales. Riesgo de bloqueo por desborde de pila.

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
  todas esas condiciones, como método abreviado para mirar pantallas.

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
por tiempo**, no requiere pulsar nada.

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

### P11 — El simulador había que avanzarlo manualmente en cada fase

**Problema:** además del vaivén (P10), las fases con temporizador
(`DRENAJE`, `SUAVIZANTE`, `CENTRIFUGADO`) y el `LLENADO` seguían requiriendo
pulsar la opción 1 para pasar de una a otra. Eso no refleja al firmware, que
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

### P12 — Reporte de falla: el dashboard no mostraba datos aunque el Arduino figuraba conectado

> Estado del reporte: **síntoma y evidencia confirmados; causa NO confirmada.**
> Se documenta primero el problema, tal como se observó. La solución adoptada
> está en P13.

#### Síntoma

Al usar el menú (`arduino_menu.sh`):

1. Opción 3 (*Compilar y subir firmware*) → se eligió la variante **dos enjuagues**.
2. Opción 2 (*Escuchar el puerto serie*) → **se veían datos** salir del Arduino.
3. Opción 4 (*Cargar backend*) → el dashboard marcaba **Arduino conectado**,
   pero no mostraba estado, fase, LCD ni componentes (`sin_datos`).

#### Evidencia recogida (datos crudos, sin interpretar)

| Observación | Dato obtenido |
|---|---|
| Lectura directa del puerto, sin backend | una sola línea: `b'[LCD] L1:    LAVARROPAS    | L2: Presione START  \r\n'` |
| `lsusb` | `ID 2341:0042 Arduino SA Mega 2560 R3 (CDC ACM)` |
| `dmesg` | `idVendor=2341, idProduct=0042` |
| `/dev/serial/by-id/` | `usb-Arduino__www.arduino.cc__0042_9543731343435121B070-if00` |
| `arduino-cli board list` | `/dev/ttyACM0  Arduino Mega or Mega 2560  arduino:avr:mega` |
| Subida con `--fqbn arduino:avr:uno` | `Warning: attempt N of 10: not in sync: resp=0x00` / `Error: programmer is not responding` |
| Subida con `--fqbn arduino:avr:mega` | Sin error |
| Tras esa subida, lectura del puerto | líneas JSON válidas, 1 por segundo |
| `/estado` del backend | `arduino_conectado: true`, `fase: REPOSO`, LCD correcto |

#### Qué dice el código de los sketches (dato objetivo)

| Firmware | Qué manda por Serial |
|---|---|
| `original_profesor` | `[LCD] L1: ... \| L2: ...` **siempre** (no conoce JSON) |
| `..._buzzer` | JSON cada 1 s; `[LCD]` detrás de `#ifdef DEBUG_LCD_SERIAL`, **apagado** |
| `..._2enjuagues` | JSON cada 1 s; `[LCD]` detrás de `#ifdef DEBUG_LCD_SERIAL`, **apagado** |

En el `original_profesor` ese `Serial.print` no está protegido por ningún
`#ifdef`; en las otras dos variantes sí, y el `#define` está comentado.

#### Por qué el dashboard mostraba "conectado" pero sin datos

1. El backend abre el puerto correctamente → `arduino_conectado: true`.
2. Lee la línea recibida.
3. `json.loads()` lanza `JSONDecodeError`.
4. El `except json.JSONDecodeError: pass` de `main.py` la descarta en silencio.
5. El estado queda en `ESTADO_DEFAULT` → `sin_datos`.

El menú *Escuchar puerto serie* no parsea JSON: muestra el texto tal cual. Por
eso ahí "se veía algo" y en el dashboard no. Ambos síntomas son compatibles con
un mismo hecho observable: **por el puerto no estaban llegando líneas JSON**.

#### Posibles causas (ninguna confirmada de forma concluyente)

1. **La placa no es un UNO.** Los datos de `lsusb` / `dmesg` / `board list`
   apuntan a un **Mega 2560 R3**. Pero el VID/PID lo declara el **chip
   USB-serial**, no el microcontrolador: un clon o un adaptador puede
   reportar otra cosa, y no se verificó la firma del MCU ni la serigrafía.
2. **Error de detección/tipificación (FQBN) al compilar o subir.** El menú
   compilaba con `--fqbn arduino:avr:uno` fijo, sin consultar la placa.
   Si la placa es otra, el binario no corresponde y/o el protocolo de
   bootloader es distinto.
3. **La subida falló y la placa conservó el firmware anterior.** Esto es lo que
   la evidencia respalda con más fuerza: el texto que salía por el puerto
   corresponde al sketch *original del profesor*, no a la variante elegida.
4. **Otras causas posibles no descartadas:** cable USB sin datos o defectuoso,
   puerto ocupado por el Monitor Serie del IDE, baudios distintos de 9600, o
   firmware subido a otra placa.

#### Confusión a evitar

Que el monitor serie muestre texto **no prueba** que el firmware elegido se haya
grabado: el sketch original también escribe por Serial. Del mismo modo,
`arduino_conectado: true` solo significa que el puerto se abrió, **no** que el
Arduino esté mandando JSON.

---

### P13 — Solución adoptada: no fijar el FQBN y verificar el puerto antes de arrancar

Sin cerrar la discusión sobre la causa exacta (P12), se atacó el problema de
forma que sirva en cualquiera de los escenarios posibles.

**Cambios en `arduino_menu.sh`:**

- Nueva función `detectar_fqbn()`, que consulta `arduino-cli board list
  --format json` y usa el FQBN que la herramienta reporte para ese puerto. Si
  no puede determinarlo, cae en `arduino:avr:uno`.
- La opción 3 compila **y sube** con ese FQBN, en lugar de `arduino:avr:uno`
  fijo, y lo muestra en pantalla antes de compilar. Si el FQBN detectado es
  `uno`, ofrece escribirlo manualmente.
- La opción 4 (backend) hace una **verificación previa**: abre el puerto, lee
  unos segundos y avisa antes de arrancar si el Arduino entrega JSON, si
  entrega otra cosa, o si no entrega nada.

**Cambios en `check_arduino.sh`:**

- Bloque 8 nuevo: muestra el FQBN reportado por `arduino-cli board list` y
  prueba si el puerto entrega JSON del proyecto.

**Resultado de las pruebas realizadas:**

| Prueba | Resultado |
|---|---|
| `detectar_fqbn /dev/ttyACM0` | devolvió `arduino:avr:mega` |
| `compile --fqbn arduino:avr:mega` | OK (flash 6%, RAM 15%) |
| `upload --fqbn arduino:avr:uno` | falló (`not in sync`) |
| `upload --fqbn arduino:avr:mega` | sin error |
| lectura del puerto tras esa subida | JSON válido, 1 línea por segundo |
| `/estado` del backend | `conectado: true`, `fase: REPOSO`, LCD correcto |
| verificación previa de la opción 4 | detectó JSON correctamente |

**Alcance de la solución:** que la subida y el arranque funcionen **no
determina por sí solo** qué placa es. Lo que sí queda resuelto es que el menú
ya no asume el modelo, y que un firmware/placa incompatible se detecta antes de
abrir el dashboard en lugar de mostrarse como "conectado sin datos".

**Aclaración sobre el código de salida de `arduino-cli`:** en una primera
lectura se creyó que `upload` devolvía `exit=0` al fallar. Fue un error de
medición: ese `0` era el del `| tail` del pipe. Medido sin pipe devuelve `1`.
Lo que sí era cierto es que **`arduino_menu.sh` no revisaba ese código**: la
subida se lanzaba sin `if !` ni comprobación posterior, así que los errores de
avrdude quedaban en pantalla pero el menú continuaba. **Ya está corregido**,
ver P15.

---

### P14 — Estado de la placa al terminar la sesión (a restaurar)

Se grabó firmware en la placa **sin consultar antes**, y además no fue la
variante que el usuario había elegido:

| | Variante |
|---|---|
| Elegida por el usuario | `..._2enjuagues` |
| Grabada en la placa | `lavarropas_controller_display_error_buzzer` (fiel al profesor) |

Es decir, la placa quedó con el ciclo de un solo enjuague. Para dejarla como el
usuario quería hay que volver a subir la variante 2, con el FQBN que reporte
`arduino-cli board list`.

Al final de la sesión la placa quedó desconectada (`lsusb` ya no listaba ningún
`2341:*` y `/dev/serial/by-id/` no existía).

#### Cronología de lo realizado (trazabilidad)

| # | Acción | Resultado |
|---|---|---|
| 1 | Lectura cruda del puerto, sin backend | `[LCD] ...` (texto del sketch original) |
| 2 | `lsusb` / `dmesg` / `by-id` / `board list` | evidencia de Mega 2560 R3 |
| 3 | `compile --fqbn arduino:avr:uno` | compila (el fallo aparece al subir) |
| 4 | `upload --fqbn arduino:avr:uno` | `not in sync: resp=0x00`, no graba |
| 5 | `compile + upload --fqbn arduino:avr:mega` | graba (variante fiel al profesor) |
| 6 | Lectura del puerto tras grabar | JSON válido, 1 línea/segundo |
| 7 | Backend + `GET /estado` | `conectado: true`, `fase: REPOSO`, LCD correcto |
| 8 | Corrección de una afirmación previa errónea | `arduino-cli` sí devuelve exit≠0 |

---

### P15 — El menú no verificaba la subida ni liberaba el puerto serie

Dos mejoras sobre `arduino_menu.sh`, motivadas por el incidente de P12/P14.

#### 1. Verificación del resultado de `upload`

Antes:

```bash
"$ARDUINO_CLI" upload -p "$puerto" --fqbn "$fqbn" "$sketch"
```

Se ejecutaba y **se ignoraba el código de salida**. Si la subida fallaba, los
errores de avrdude quedaban en pantalla pero el menú volvía al loop como si
nada. Eso es lo que hizo posible confundir "no grabó" con "grabó bien".

Ahora:

```bash
if ! "$ARDUINO_CLI" upload -p "$puerto" --fqbn "$fqbn" "$sketch"; then
    echo "❌ La subida FALLÓ. El Arduino sigue con el firmware anterior."
    ...
    return 1
fi
echo "✅ Firmware grabado en $puerto."
```

El `if !` evalúa el código de retorno: distinto de 0 = fallo. Se avisa, se
sugieren las causas típicas (puerto tomado, FQBN incorrecto, cable) y **no se
sigue como si hubiera funcionado**.

#### 2. Liberación del puerto serie antes de subir/escuchar

Un puerto serie tiene **un solo dueño**: si el backend, el simulador o el
Monitor Serie del IDE lo tienen abierto, la subida o la escucha fallan.

Funciones nuevas:

| Función | Qué hace |
|---|---|
| `puerto_serie_ocupado <dev>` | 0 si otro proceso lo tiene tomado, 1 si está libre. Usa `fuser` y cae en `lsof`. |
| `quien_usa_puerto <dev>` | Muestra usuario/PID del proceso que lo tiene. |
| `liberar_puerto_si_hace_falta <dev>` | Si está ocupado, avisa, ofrece ejecutar la opción 8 (`matar_todo`), espera a que se libere y reintenta. Devuelve 1 si no se pudo. |

Se llama en **dos puntos**: al empezar la opción 3 (antes de compilar, para no
perder tiempo) y al empezar la opción 2 (escuchar).

Casos contemplados:

| Caso | Comportamiento |
|---|---|
| Puerto libre | No pregunta nada, sigue. |
| Ocupado por el proyecto (backend/simulador/socat) y el usuario acepta | `matar_todo`, espera hasta 5 s, confirma que quedó libre. |
| Ocupado y el usuario rechaza | Cancela la operación y explica qué cerrar. |
| Ocupado por un programa ajeno (IDE, minicom) | Avisa que `matar_todo` no lo mató, muestra el PID y cancela. |

#### Pruebas realizadas (sin hardware)

| Prueba | Resultado |
|---|---|
| `puerto_serie_ocupado` con PTY tomado por un proceso | 0 (ocupado) ✅ |
| `puerto_serie_ocupado` con puerto libre | 1 (libre) ✅ |
| `quien_usa_puerto` | muestra usuario, PID y modo ✅ |
| Ocupado por `simulador.py` + responder `n` | cancela, rc=1 ✅ |
| Ocupado por `simulador.py` + responder `s` | mata el proceso, puerto libre, rc=0 ✅ |
| Ocupado por programa ajeno + responder `s` | avisa que sigue ocupado, rc=1 ✅ |
| `upload` con exit≠0 (arduino-cli simulado) | muestra ❌, `return 1` ✅ |
| `upload` con exit=0 | muestra ✅ ✅ |

El firmware y el backend no se tocaron.

---

## 5. Cambios concretos por archivo

### `firmware/lavarropas_controller_display_error_buzzer/*.ino`
- `#include <ArduinoJson.h>`.
- `#define DEBUG_LCD_SERIAL` (comentado).- Variables `tUltimoEnvioEstado`, `INTERVALO_ENVIO_MS`, `pulsadorActivo`.
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

### `arduino_menu.sh` (corregido en P12 y P15)
- Nueva función `detectar_fqbn()` (lee el FQBN real de la placa conectada con
  `arduino-cli board list --format json`).
- Opción 3: compila y sube con el FQBN detectado (antes: `arduino:avr:uno`
  fijo), lo muestra en pantalla y permite escribirlo manualmente si es un UNO.
- Opción 4: verificación previa del puerto antes de arrancar el backend
  (¿responde JSON? / ¿manda otra cosa? / ¿no manda nada?).
- Verificación del código de salida de `arduino-cli upload`: si falla, avisa
  y no continúa (P15).
- `puerto_serie_ocupado()`, `quien_usa_puerto()` y
  `liberar_puerto_si_hace_falta()`: detectan quién tiene tomado el puerto
  serie y ofrecen liberarlo (equivale a la opción 8) antes de subir y antes
  de escuchar (P15).

### `check_arduino.sh` (ampliado en P12)
- Bloque 8 nuevo: muestra `arduino-cli board list` (FQBN real) y prueba si el
  puerto entrega JSON del proyecto, con diagnóstico explícito de "firmware
  compilado con FQBN equivocado".

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
| `upload --fqbn arduino:avr:mega` | OK (JSON estable 9600) |
| Verificación previa del puerto en opción 4 | OK |
| `arduino-cli upload` a puerto inexistente (exit real) | 1 (ver P13) |
| Lectura cruda del puerto antes de corregir | `[LCD] ...` (ver P12) |

---

## 7. Pendientes / a futuro

1. **Probar con el Arduino real** (no solo simulador): verificar que el JSON
   se lea estable a 9600 baudios.
   (Parcialmente hecho: verificado con la placa real, ver P12/P13.)
2. ~~Hacer que `arduino_menu.sh` revise el resultado de `upload`~~ → **hecho**
   (ver P15), junto con la liberación del puerto serie.
3. **Restaurar en la placa la variante elegida por el usuario**: al final de
   la sesión de diagnóstico quedó grabada la variante fiel al profesor
   (`..._buzzer`), no la de dos enjuagues (ver P14).
4. **Decidir Opción 3.B**: habilitar arranque/reset remoto o dejarlo así.
5. **Confirmar cableado real del pin 4**: si es traba física, cambiar la
   etiqueta "En marcha" por "Bloqueo puerta".
6. **Alinear `arduino_menu.sh`**: la opción "Info de pines" y la plantilla
   siguen mostrando el pinout viejo (pines 2..9, sin LCD ni buzzer).
   (El FQBN fijo ya se corrigió — ver P13.)
7. **Dependencias**: en una instalación nueva hay que correr
   `arduino-cli lib install ArduinoJson LiquidCrystal`.
8. **Intervalo de envío**: hoy 1 s. Si se quiere más fluidez en el LCD, se
   puede bajar (a costa de más tráfico Serial).
9. **Consultar al profesor por la fase `SUAVIZANTE`**: ver observación en la
   sección 9 y la propuesta de dos enjuagues en la sección 8.
10. **Probar la variante en hardware real**: compilar y subir
    `..._2enjuagues` a la placa de la maqueta con el FQBN que reporte
    `arduino-cli board list`, y verificar el ciclo completo.
11. **Probar el dashboard en Windows** (ver sección 10).
12. **Definir la placa de referencia**: el proyecto se pensó para UNO
    (2 KB de RAM); la evidencia de P12 sugiere Mega (8 KB). Conviene decidir si se
    documenta el Mega como placa objetivo o se mantiene el UNO como mínimo
    soportado (hoy el firmware compila para ambos).

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
   `LLENADO_SUAVIZANTE` enciende `PIN_SUAVIZANTE` y llama a `manejarLlenado()`;
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
| `LLENADO_SUAVIZANTE` enciende suavizante + `manejarLlenado()` | Sí |
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

La fase se limita a **encender el relé de suavizante durante 10 s**
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

En `SUAVIZANTE` y `CENTRIFUGADO` solo se enciende el relé correspondiente; nunca
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

---

## 11. Fase 2: telemetría del motor y contrato de muestras

### D11 — Etiqueta "Puerta trabada" en el dashboard de motor

**Decisión:** en `static/dashboard_motor.html`, la salida `bloqueo_puerta`
(`PIN_TRABA`) se etiqueta **"Puerta trabada"**, no "En marcha". En este
firmware (`lavarropas_controller_2enjuagues_motor_triac_MEGA.ino`) la traba es
una seguridad real: se suelta solo cuando `motorEstado == M_PARADO` y
`rpm < RPM_PUERTA_SEGURA` (100 RPM).

**Por qué:** D3 documentaba la etiqueta "En marcha" para el firmware de relés
del profesor, donde `PIN_TRABA` solo indicaba "ciclo en marcha". En el
firmware de motor TRIAC el comportamiento es distinto (traba física), así que
el dashboard de motor usa la etiqueta correcta para su contrato.

**Alcance:** solo `dashboard_motor.html`. `dashboard.html` (relés) mantiene su
etiqueta original para no romper la compatibilidad.

### D12 — Contrato de telemetría del motor (Fase 2)

**Decisión:** el firmware agrega tres campos al objeto `motor` del JSON, sin
quitar los existentes:

- `objetivo_final = (int)setpointFinal`
- `estado = (int)motorEstado` (0 parado, 1 arrancando, 2 en marcha, 3 frenando)
- `pot_max = (int)POT_MAX_PID`

`objetivo` se mantiene como el **valor instantáneo** de la rampa.

**Backend (`main.py`):** `/estado` agrega, sin cambiar `datos` ni `log`:

- `ahora` (epoch s del servidor),
- `ultimo_json_ts` (epoch s del último JSON válido, `None` si nunca),
- `lineas_descartadas` (contador de líneas que fallaron `json.loads`),
- `seq` (último número de secuencia) y `muestras` (cola circular `deque(maxlen=600)`,
  solo se devuelven las posteriores a `?desde=`, máximo 200, vacías sin parámetro).

**Dashboard de motor:** conexión en 3 niveles (rojo sin puerto, amarillo puerto
abierto sin JSON reciente con `edad = ahora - ultimo_json_ts > 3 s`, verde datos
frescos), tarjeta de motor unificada (sentido, estado, RPM, objetivo de rampa,
objetivo final, potencia con barra y saturación, error `objetivo_final - rpm`
visible solo en `estado == 2` con rampa terminada). Degrada sin errores si faltan
los campos nuevos.

**Por qué:** permite diagnosticar la conexión y ajustar el PID sin tocar la
máquina de estados ni los pines del firmware.

### D13 — Cola de muestras en el backend (preparación Fase 3)

**Decisión:** `_leer_estado()` ya no guarda solo la última línea: cada JSON
válido se agrega a `deque(maxlen=600)` como `{"seq": n, "t": epoch, "d": dict}`,
con `seq` creciente. `estado` sigue siendo la última muestra (compatibilidad con
`dashboard.html`).

**Por qué:** la Fase 3 necesita el historial para el gráfico PID. La cola acota
la RAM del backend y `?desde=<seq>` evita incrementar el tamaño de la respuesta para
`dashboard.html`.

### P16 — Líneas no-JSON se contaban en silencio

**Problema:** el `except json.JSONDecodeError: pass` de `main.py` descartaba
líneas corruptas sin dejar rastro. Eso dificultaba distinguir "firmware
equivocado" de "baudrate distinto" (ver P12).

**Solución:** `lineas_descartadas` cuenta cada descarte. Para no inundar el log,
solo se registra una entrada cada `LOG_DESCARTES_CADA = 20` descartes. El
simulador de motor agrega la opción `L` para inyectar una línea no-JSON y probar
el contador.

### P17 — El caso `FIN` con tambor girando no era reproducible en el simulador

**Problema:** `simulador_motor.py` hacía decaer `rpm` de forma no determinista
al entrar a `FIN`, así que el aviso "Tambor girando" podía no verse.

**Solución:** al entrar en `FIN`, `aplicar_fase()` mantiene `rpm =
RPM_CENTRIFUGADO` y deja que decaiga unos segundos. Así el dashboard muestra el
aviso de forma reproducible.

### D14 — `sesion` y relojes del backend

**Decisión:** `SerialManager` genera `sesion = str(uuid.uuid4())` una sola vez en
su `__init__`. `/estado` también agrega:

- `t_servidor` = `time.monotonic() - t0` (segundos desde el arranque del backend),
- `edad_ultimo_json` = `time.monotonic() - _ultimo_json_mono` (None si nunca llegó JSON).

**Por qué:** `sesion` permite al dashboard detectar un reinicio del backend (el
`seq`/cola de muestras vuelven a 0) y resetear su estado incremental. La edad con
reloj monotónico no se ve afectada por cambios de hora del sistema, a diferencia
de `ahora - ultimo_json_ts`.

### D15 — Muestras livianas

**Decisión:** cada elemento de la cola `muestras` guarda solo
`{"seq", "t", "fase", "rpm", "objetivo", "objetivo_final", "estado", "potencia"}`
en lugar del JSON completo. `t` = `time.monotonic() - t0`. Los campos del motor
se toman con `.get`; si no hay objeto `motor`, quedan en `None`. `datos` sigue
llevando el JSON completo.

**Por qué:** acota la RAM del backend (600 muestras × ~8 campos livianos en vez de
600 dicts completos) y le da a la Fase 3 justo lo que necesita para el gráfico.

### P18 — JSON que no es objeto pasaba sin control

**Problema:** `json.loads()` acepta arrays, strings o números; el código anterior
hacía `nuevo.get(...)` asumiendo que era dict y una línea no-objeto podía romper
el lector.

**Solución:** tras `json.loads`, si `not isinstance(nuevo, dict)` se suma a
`lineas_descartadas` (mismo criterio de log) y se hace `continue`.


## 12. Fase 3: gráfico PID en el dashboard

### D16 — Buffer de muestras en el navegador (180 s)

**Decisión:** el dashboard reutiliza el mismo pedido `/estado` y le agrega
`?desde=<ultimoSeqMuestras>` (primera vez `desde=0` para recuperar el historial).
Solo se agregan muestras con `seq > ultimoSeqMuestras`; se descartan las de
`t < t_servidor - 180`. Si `sesion` cambia o `seq < ultimoSeqMuestras`, se vacía
el buffer y se pide desde 0. Si el pedido falla o devuelve 422, el buffer no se
toca.

**Por qué:** un solo temporizador evita superponer dos sondeos; la ventana de
180 s acota la memoria y el dibujo; `desde` incremental evita retransmitir todo.

### D17 — Gráfico `<canvas>` sin dependencias

**Decisión:** panel debajo de la tarjeta del motor con `<canvas>` vanilla
(devicePixelRatio, alto ~220 px). Eje izquierdo RPM (0 a
`max(objetivo_final, rpm) * 1.15` redondeado), eje derecho potencia 0–100 %, eje
x de -180 s a 0. Series: rpm (línea gruesa), objetivo (fina), objetivo_final
(punteada), potencia (otro color, eje derecho); línea horizontal en `pot_max`.
Marcas verticales en cada cambio de fase con abreviatura (`ABREV_FASE`, fallback
primeras 3 letras). Hueco de línea si `dt > 3 s` o salto de `seq > 1`.

**Por qué:** permite ajustar el PID visualmente sin librerías externas (la Pi
puede funcionar como AP sin internet).

### D18 — Controles y exportación CSV

**Decisión:** botones Pausar/Reanudar (congela el dibujo, el buffer sigue),
Limpiar (vacía el buffer sin cambiar `ultimoSeqMuestras`) y Exportar CSV de la
ventana visible (Blob + descarga, nombre `lavarropas_AAAAMMDD_HHMMSS.csv`).

**Por qué:** facilita guardar/analizar el comportamiento del lazo.

### P19 — Flujo continuo de basura podía trabar el bucle de eventos

**Problema:** `_leer_estado()` leía todas las líneas pendientes sin límite; un
flujo continuo de líneas no-JSON podía monopolizar el loop.

**Solución:** se limita a 50 líneas por pasada (`while self.conn.in_waiting and
n < 50`). El resto se lee en la pasada siguiente.

## 13. Fase 3: ajustes del gráfico

### D19 — Eje x anclado al reloj del servidor y sesión propia del gráfico

**Decisión:** el dashboard guarda `ultimoTServidor = data.t_servidor` en cada
respuesta válida (aunque no haya muestras nuevas) y lo usa como borde derecho
del eje x, para el filtro de la ventana de 180 s y en las etiquetas
("-180 s" / "ahora"). Si `t_servidor` no es número, no se filtra la ventana
(no se vacía el buffer). El gráfico usa una variable `sesionGrafico` propia,
comparada con `data.sesion` antes de evaluar `d.motor`; si cambia, se vacía el
buffer y se pide desde 0.

**Por qué:** el eje x debe quedar anclado al reloj del servidor (no a la última
muestra), así cuando se corta la telemetría la señal se corre a la izquierda en
vez de quedarse pegada al borde derecho. La sesión del gráfico es independiente
de la sesión del badge.

### D20 — CSV compatible con configuración regional es-AR

**Decisión:** el CSV se exporta con separador `;`, decimales con coma en
`t_rel_s`, BOM `\uFEFF` al inicio, una columna `hora` (HH:MM:SS del dispositivo,
calculada como `Date.now() - (ultimoTServidor - m.t) * 1000`) y
`URL.revokeObjectURL` diferido con `setTimeout(…, 1000)`.

**Por qué:** Excel/LibreOffice con configuración regional es-AR espera `;` y
coma decimal; el BOM evita problemas de codificación; revocar la URL tarde
garantiza que el navegador terminó de iniciar la descarga.

## 14. Detalles finales del dashboard y simulador

### D21 — Panel de motor sin saltos de layout

**Decisión:** la fila "Diferencia con el objetivo" está SIEMPRE visible
(`display: flex`); cuando no aplica (estado != 2, rampa sin terminar o menos de
3 s en marcha) muestra "—" en gris sin énfasis. "⚠ potencia saturada" reserva su
lugar con `visibility: hidden/visible` en lugar de `display: none`. El resto del
panel del motor y del gráfico no tiene elementos internos que aparezcan y
desaparezcan moviendo el layout (solo las alarmas, el aviso del tambor y
"esperando agua" siguen usando display, que es lo permitido).

**Por qué:** evita que el contenido de abajo salte cuando la telemetría cambia.

### D22 — CSV con hora del dispositivo y marcas de fase diferenciadas

**Decisión:** se guarda `ultimoRelojCliente = Date.now()` al asignar
`ultimoTServidor`; en `exportarCSV`, la columna `hora` se calcula como
`ultimoRelojCliente - (ultimoTServidor - m.t) * 1000` (celda vacía si no son
números). Las marcas verticales de fase usan `colTexto` con `globalAlpha = 0.4`
(restaurando a 1), para distinguirlas de la rejilla.

### P20 — Potencia en agitado del simulador

**Problema:** el simulador dejaba la potencia en 0 % durante el agitado porque
no replicaba el arranque del firmware (integral = POT_ARRANQUE, setpoint desde
RPM_MIN).

**Solución:** al iniciar cada giro de agitación se setea `setpoint = RPM_MIN` e
`integral = POT_ARRANQUE`, igual que el firmware; el PID común calcula la
potencia y la satura contra `POT_MAX_PID`. Verificado: agitado en marcha con
potencia ~20 %, centrifugado ~28 %.

### P21 — START real en el simulador

**Problema:** `estado["pulsador"]` solo se inicializaba y se alternaba con la
opción 3; ninguna lógica lo leía. El ciclo se avanzaba con opción 1 y los
errores con opción 6.

**Solución:** se agrega la opción "P) Pulsar START" que emula el flanco
true→false del botón (~0,3 s, sin bloquear el menú) y `procesar_start_flanco()`
replica las condiciones del firmware: REPOSO arranca y traba, FIN vuelve a
REPOSO solo con tambor parado, ERROR_AGUA reanuda solo con presostato lleno y
ERROR_MOTOR reintenta. Las opciones 1 y 6 quedan como accesos directos de prueba.

## 15. Ajustes de simulador, dashboard y preparación del baudrate (fase 4a)

### D23 — ERROR_MOTOR persistente en el simulador

**Decisión:** `reset_error()` NO limpia `taco_sin_senal`; solo `t_sin_taco = 0`.
La opción 5 alterna la falla (ACTIVA/reparada); la opción 6 (acceso directo de prueba)
puede limpiar también `taco_sin_senal`. `t_sin_taco` solo se acumula con el
motor en marcha (estado 2 y objetivo > 0), como el firmware. Se elimina la
variable `falla_tacometro` (solo se asignaba).

**Por qué:** replica la persistencia real de la falla de tacómetro: START
reanuda y, si el sensor sigue sin señal, vuelve a fallar a los 2 s.

### D24 — Baudrate configurable por variante (fase 4a)

**Decisión:** `main.py` define `BAUD_POR_VARIANTE = {"relay": 9600, "motor":
9600}` (el valor de motor pasará a 115200 en la fase 4b) y la variable de
entorno `BAUDRATE` (entero) tiene prioridad; un valor inválido produce un error
claro. `arduino_menu.sh` define `BAUD_RELES`/`BAUD_MOTOR` y los usa en la
escucha, la verificación previa y el arranque del backend; `check_arduino.sh`
acepta `CHECK_BAUD` (default 9600); `iniciar_pruebas_motor.sh` usa
`BAUDRATE=${BAUDRATE:-9600}`.

**Por qué:** deja listo el salto a 115200 cambiando una línea por lugar, sin
romper el firmware de relés ni el del profesor.

### D25 — Cola de muestras mayor y bucle más rápido

**Decisión:** `MAX_MUESTRAS = 1000`, `MUESTRAS_POR_RESPUESTA = 800` y el
`asyncio.sleep(0.3)` del bucle conectado baja a `0.1`.

**Por qué:** a 250 ms de envío la cola acumula 4 muestras/s; 800 muestras cubren
~200 s y el poll más frecuente reduce la latencia del gráfico.

### P22 — Intervalo de envío configurable en el simulador

**Problema:** el simulador emitía siempre 1 línea/s, sin forma de probar la
telemetría rápida de 250 ms.

**Solución:** se agrega la opción "T) Intervalo de envío 1000/250 ms" que
alterna `intervalo_envio` entre 1.0 y 0.25 s. Verificado: 800 muestras llenan la
cola sin duplicados ni saltos de `seq`.

## 16. Ajustes de simulador/menú y compresión (tanda de verificación)

### D26 — t_sin_taco se reinicia al arrancar el motor

**Decisión:** como el firmware (`actualizarMotor`, líneas 294-303 del `.ino`)
reinicia `msSinTaco = 0` al pasar de `M_ARRANQUE` a `M_MARCHA`, el simulador
también pone `t_sin_taco = 0.0` al arrancar cada giro de agitación y al arrancar
el centrifugado.

**Por qué:** evita que una falla acumulada se arrastre al arrancar el motor; el
conteo de "sin señal" solo tiene sentido con el motor girando.

### D27 — Baudrate y GZip

**Decisión:** `_resolver_baudrate()` exige entero > 0 (error claro si no).
`main.py` agrega `GZipMiddleware(minimum_size=2000, compresslevel=3)` para
comprimir `/estado?desde=0` (cola de hasta 800 muestras).

**Por qué:** la respuesta con la cola llena (~92 KB) se beneficia de compresión;
las respuestas chicas en régimen no se comprimen por el umbral mínimo.

### P23 — El menú "Escuchar" no conocía la variante

**Problema:** `opcion_escuchar()` usaba `$DASHBOARD`, que no está definida en ese
punto (la elección relés/motor se hace en `opcion_backend`).

**Solución:** se muestra el default de relés (`$BAUD_RELES`) y se recuerda el del
motor (`$BAUD_MOTOR`) en el prompt; la ayuda de ARDUINO CLI lista ambos comandos
de monitor (relés y motor).

## 17. Corrección de aplicar_fase() y globals de t_sin_taco

### D28 — Valores de telemetría al entrar en cada fase del simulador

**Decisión:** `aplicar_fase()` reestructurada con UNA sola asignación de
`setpoint`/`integral` por rama, sin pisadas:
- Fase de agitación (LAVADO/ENJUAGUE/ENJUAGUE_SUAVIZANTE): `setpoint=0.0`,
  `integral=0.0` (el firmware no arranca el motor al entrar; recién tras
  `T_PAUSA_GIRO`, .ino:464-467).
- CENTRIFUGADO: `setpoint=RPM_MIN`, `integral=POT_ARRANQUE`, `t_sin_taco=0.0`
  (arranca de inmediato, .ino:731-737, y reinicia msSinTaco al pasar a M_MARCHA,
  .ino:298).
- Cualquier otra fase: `setpoint=0.0`, `integral=0.0`.

**Por qué:** el bloque suelto de `if CENTRIFUGADO/else` (4 espacios) pisaba los
valores de la rama de agitación, dejando el motor muerto.

### P24 — `t_sin_taco` era local en aplicar_fase/actualizar_agitacion

**Problema:** `t_sin_taco = 0.0` se asignaba sin `global`, así que era local y
no tenía efecto; pyflakes detectó `global` innecesarios.

**Solución:** se agregó `t_sin_taco` a los `global` de `aplicar_fase()` y
`actualizar_agitacion()`, y se limpiaron los `global` que pyflakes marcó como
no asignados (`rpm`, `potencia`, `taco_sin_senal`). `pyflakes` queda en limpio.

## 18. Integración ciclo + motor: arquitectura maestro-esclavo

### D29 — Arquitectura propuesta: Mega (ciclo) + UNO (motor)

**Propuesta:** la integración del ciclo de 2 enjuagues con el motor TRIAC
mejorado se encara con **arquitectura maestro-esclavo** (camino probable,
a confirmar):
- **Arduino Mega**: maestro. Ejecuta el ciclo completo (máquina de estados,
  LCD, presostato, relés, reporte JSON) y le ordena al UNO por UART.
- **Arduino UNO**: esclavo. Ejecuta el lazo del motor (cruce por cero, TRIAC,
  tacómetro D6/D7, PID mejorado) con el circuito del profesor.
- **Raspberry Pi**: opcional. Solo recibe el JSON del Mega por USB para el
  dashboard.

**Por qué:**
- El ciclo ya está cableado y probado en el Mega (sin motor ni tacómetro).
- El motor + tacómetro funcionan en el UNO con el circuito del profesor
  (comparador analógico D6/D7), que **no se puede conectar al Mega** (AIN0/PE2
  no sale a ningún conector).
- Integrar todo en el UNO no es viable por conflicto de pines (D2/D3/D6/D7
  chocan entre ciclo y motor).
- Hay disponibles UNO y Mega en el curso.

**Requisito clave:** Mega + UNO deben funcionar de forma **independiente**, sin
Raspberry. La Raspberry es opcional (solo dashboard). Por lo tanto el Mega no
puede depender de la Raspberry y el UNO debe tener **timeout de seguridad**
(detener el motor si pierde el enlace con el Mega).

**Detalle:** ver
[`docs/INTEGRACION_CICLO_MOTOR_MEJORADO.md`](docs/INTEGRACION_CICLO_MOTOR_MEJORADO.md).

## 19. Clasificación de firmwares y política de modificación

### D30 — Clasificación formal de los firmwares

**Decisión:** los firmwares del repositorio se clasifican en cuatro grupos,
según su origen y si se modifican o no:

| Categoría | Firmwares | ¿Se modifica? |
|---|---|---|
| Referencia pura (cátedra) | `original_profesor/` | No, nunca |
| Del profesor (mantenido tal cual) | `lavarropas_motor_cruce_cero/`, `lavarropas_motor_cruce_cero_mejorado/` | No |
| Del profesor + JSON (lógica intacta) | `lavarropas_controller_display_error_buzzer/` | Solo se agregó reporte JSON |
| Variantes derivadas | `lavarropas_controller_display_error_buzzer_2enjuagues/`, `lavarropas_controller_2enjuagues_motor_triac_MEGA/` | Sí |

**Por qué:** evita ambigüedad sobre qué firmwares son intocables. La regla
“los que dicen profesor no se modifican” se precisa así:
- La **referencia pura** es `original_profesor/` (copia fiel, sin tocar).
- Los firmwares **del profesor** (motor standalone) se mantienen tal cual.
- `lavarropas_controller_display_error_buzzer/` es la base del profesor **con
  reporte JSON agregado**; la lógica no se cambia.
- Las **variantes derivadas** son variantes del proyecto y sí se pueden
  modificar.

**Detalle:** ver README (sección 4) y docs/README.md (sección 5).

## 20. Propuesta de traba de puerta (rama propuesta-traba)

### D31 — Traba de puerta con tiempo sin pulsos (PROPUESTA pendiente)

**Estado: PROPUESTA pendiente de revisión del docente; no aplicar sin su
aprobación.** Vive en la rama `propuesta-traba`, **no** en `main`.

**Problema:** en `FIN` la traba se suelta con
`motorEstado == M_PARADO && rpm < RPM_PUERTA_SEGURA` (.ino línea 750). Con 8
pulsos por vuelta, por debajo de ~150 RPM algunas ventanas de 100 ms traen un
solo pulso (se lee 0) y el filtro 0,5/0,5 baja la lectura, así que `rpm < 100`
puede cumplirse con el tambor girando por inercia.

**Cambios propuestos** (solo en el .ino del Mega, el dashboard del motor y el
simulador):
- `.ino`: `tUltimoPulsoMs` actualizado en cada ventana con `c > 0`, constante
  `T_SIN_PULSO_PUERTA_MS = 2000` y condición extra `sinPulsos` en `FIN`.
- JSON: `motor["ms_sin_pulso"]` (acotado a 60000 ms).
- `dashboard_motor.html`: el aviso "Tambor girando" usa `rpm >= 100` **o**
  `ms_sin_pulso < 2000` (si el campo falta, comportamiento actual).
- `simulador_motor.py`: emite `ms_sin_pulso` con un modelo simple y aplica la
  misma condición para soltar la traba.

**Verificación:** compila para Mega (flash 21532→21730 bytes; RAM 1505→1521
bytes). Sin subir a placa. Ver rama `propuesta-traba`.

