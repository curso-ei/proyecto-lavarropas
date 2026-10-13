# Guía de práctica — Control de lavarropas con Arduino y panel de monitoreo

> **Para alumnos de Electricidad Industrial.**
> No hace falta saber programar ni usar Linux. Sí vamos a entender **qué
> software se usa, por qué se usa y cómo se conecta todo**, porque es parte
> de la práctica: la máquina real, el controlador y la pantalla que la
> observa.

---

## 1. ¿Qué estamos haciendo?

Desarmamos un lavarropas para conocer sus piezas. Ahora conectamos esas
piezas a un **Arduino** y agregamos un **panel de monitoreo en la
computadora** para ver, en vivo, qué pieza se enciende en cada momento del
ciclo.

El objetivo no es solo "ver luces": es entender la **cadena completa**:

```
Pieza del lavarropas (220 V)
        │
        ▼
   Relé (interruptor pilotado)
        │
        ▼
   Arduino (controlador, 5 V)
        │
        ▼
   Cable USB
        │
        ▼
   Raspberry Pi (computadora)
        │
        ▼
   Panel web en el navegador (lo que vemos)
```

---

## 2. Las partes del sistema

### 2.1. Arduino — el "cerebro" de control

El Arduino es un **controlador**: una placa con entradas y salidas.

- **Entradas**: leen sensores (botón, presostato).
- **Salidas**: activan actuadores (bombas, motor, buzzer) mediante relés.

El Arduino ejecuta un **programa (firmware)** que decide, según la fase del
ciclo, qué salida encender y cuándo.

### 2.2. Firmware — el programa que corre en el Arduino

El firmware es el programa que el profesor cargó en el Arduino. Está escrito
en un lenguaje parecido al C, pensado para que sea **fácil de leer**.

No hace falta saber programar, pero conviene entender su lógica:

1. Hay una lista de **fases**: `REPOSO → LLENADO → LAVADO → DRENAJE_1 →
   LLENADO_ENJUAGUE → ENJUAGUE → DRENAJE_2 → SUAVIZANTE → DRENAJE_3 →
   CENTRIFUGADO → FIN`.
2. En cada fase, el firmware **enciende o apaga salidas** con funciones simples.
3. Un **loop** (bucle) revisa todo el tiempo qué fase corresponde y qué hay
   que activar.
4. Cada 1 segundo, el firmware **envía por el cable USB** un resumen del
   estado (en formato JSON) para que la computadora lo muestre.

### 2.3. Raspberry Pi — la computadora que hace de puente

La Raspberry Pi es una computadora chica. Su trabajo acá es:

- **Leer** lo que el Arduino envía por USB.
- **Servir** esa información a una página web.
- **Mostrar** el panel en el navegador.

No es la que controla el lavarropas; solo **observa y traduce**.

### 2.4. Backend — el programa que habla con el Arduino

En la Raspberry corre un programa llamado **backend** (en este proyecto,
`main.py`). "Backend" significa "lo que corre por detrás, en el servidor".

Hace tres cosas:

1. Se conecta al Arduino por el puerto USB.
2. Lee la línea de texto que el Arduino manda cada segundo.
3. Guarda ese estado y lo entrega cuando el navegador lo pide.

> **Analogía eléctrica:** el backend es como un **relé de datos**: recibe una
> señal (del Arduino) y la "retransmite" a otro lado (el navegador), sin
> modificar la lógica.

### 2.5. Frontend — lo que vemos en el navegador

El **frontend** es la parte visible: la página web con luces, textos y el log.
En este proyecto es un archivo (`dashboard.html`) que el navegador dibuja.

- El frontend **pregunta** al backend cada poco tiempo: "¿cómo está el ciclo?".
- El backend le responde con el último estado que leyó del Arduino.
- El frontend lo dibuja: enciende luces, cambia textos, muestra alarmas.

### 2.6. Dashboard — el panel de control

**Dashboard** es la palabra en inglés para "tablero de control". Es la página
donde se ve todo junto:

- Estado del ciclo y fase actual.
- Las **salidas** (relés) como luces.
- Las **entradas** (sensores) como luces.
- El **LCD** del lavarropas, reproducido en pantalla.
- Las **alarmas** y el **historial de eventos (log)**.

> Importante: este dashboard es **de solo lectura**. Muestra lo que hace el
> Arduino, pero **no** enciende ni apaga piezas por su cuenta.

---

## 3. Los pines del Arduino y qué controla cada uno

Los **pines** son las patitas de conexión del Arduino. El firmware del
profesor los asigna así:

### Entradas (sensores)

| Pin | Conexión | Qué detecta |
|-----|----------|-------------|
| **2** | Pulsador de inicio | Cuando se presiona START |
| **3** | Presostato | Si el tambor tiene agua suficiente |

### Salidas (actuadores, vía relés)

| Pin | Conexión | Qué activa |
|-----|----------|------------|
| **4** | Relé "En marcha" | Indicador de ciclo activo |
| **7** | Buzzer | Alarma sonora |
| **8** | Bomba de llenado | Entrada de agua |
| **9** | Motor sentido 1 | Gira el tambor (→) |
| **10** | Motor sentido 2 | Gira el tambor (←) |
| **11** | Bomba de desagote | Salida de agua |
| **12** | Suavizante | Entrada de suavizante |
| **13** | Centrífuga | Giro rápido de escurrido |

### El LCD

La pantalla LCD 16x2 (16 caracteres por 2 líneas) se conecta a los pines
`A0, A1, A2, A3, 5 y 6`. Muestra mensajes como `FASE: LAVADO` o `Presione START`.

---

## 4. ¿Por qué relés? (la parte de electricidad)

El Arduino funciona con **5 V** y corrientes muy chicas. El lavarropas usa
**220 V** y corrientes grandes. No se pueden conectar directo.

El **relé** resuelve esto:

```
Circuito de control (5 V, seguro)   │   Circuito de potencia (220 V, peligroso)
   Arduino ──► bobina del relé      │   contacto abierto/cerrado ──► pieza
```

- La bobina se energiza con 5 V del Arduino.
- Eso mueve un contacto que cierra/abre el circuito de 220 V.
- Los dos circuitos están **aislados**.

En el firmware hay una función `setRelay(pin, encender)` que hace exactamente
eso: prende o apaga el relé de una salida.

---

## 5. El ciclo de lavado y las piezas

El firmware sigue una secuencia fija. Vamos a usarla para **predecir y
verificar** qué pieza debe encenderse en cada fase.

| Fase | Qué hace | Salida esperada |
|------|----------|-----------------|
| LLENADO | Entra agua | 💧 Bomba llenado |
| LAVADO | Gira con agua y jabón | 🌀 Motor → / ← |
| DRENAJE_1 | Saca agua sucia | 🚰 Bomba desagote |
| LLENADO_ENJUAGUE | Entra agua limpia | 💧 Bomba llenado |
| ENJUAGUE | Gira para enjuagar | 🌀 Motor → / ← |
| DRENAJE_2 | Saca agua | 🚰 Bomba desagote |
| SUAVIZANTE | Agrega suavizante | 🧴 Suavizante |
| DRENAJE_3 | Saca agua | 🚰 Bomba desagote |
| CENTRIFUGADO | Gira rápido | 🌀 Centrífuga |
| FIN | Termina | Todo apagado, "En marcha" se apaga |

> **Dato importante:** el presostato (pin 3) le avisa al Arduino cuándo hay
> agua suficiente. Durante LLENADO, la bomba sigue encendida **hasta que el
> presostato marca nivel**. Si no llega agua, el firmware se queda esperando
> y, en ciertas fases, dispara la alarma "FALTA DE AGUA".

---

## 6. Actividades de observación

### Actividad 1 — Seguir la cadena completa

1. Iniciar el ciclo.
2. Anotar, en orden, qué **salidas** se encienden.
3. Comparar con la tabla de arriba.

**Preguntas:**

- ¿En qué fase el motor alterna entre Motor → y Motor ←? ¿Por qué?
- ¿La bomba de llenado y la de desagote se encienden juntas? ¿Por qué no?

---

### Actividad 2 — El presostato como sensor de nivel

1. Mirar la luz de "Presostato" durante LLENADO.
2. Observar la pista "⏳ Esperando nivel de agua…".

**Preguntas:**

- ¿Qué condición apaga la bomba de llenado?
- Si el presostato fallara, ¿qué pasaría con el ciclo?
- ¿Qué otro sensor de nivel conocés en la industria?

---

### Actividad 3 — La alarma (buzzer) y el error

1. Simular una falla (cortar el agua, por ejemplo).
2. Observar el panel: banner rojo + alarma sonora.

**Preguntas:**

- ¿Qué pin activa el buzzer? ¿Qué tipo de salida es (actuador)?
- ¿Por qué una máquina debe avisar con sonido y luz?

---

### Actividad 4 — Relé: control vs. potencia

1. Identificar en la maqueta un relé.
2. Distinguir su lado de control (5 V) y su lado de potencia (220 V).

**Preguntas:**

- ¿Qué pasaría si conectáramos una bomba de 220 V directo a un pin del Arduino?
- ¿Por qué el relé es imprescindible?

---

## 7. Seguridad (regla de oro)

- **El lado de 220 V puede matar. Nunca se toca con el equipo enchufado.**
- Toda manipulación de relés, bombas y motor se hace **desenchufado** y con el
  profesor presente.
- El Arduino, la Raspberry y el USB trabajan a **5 V**, que es seguro.
- La conexión por relé **separa** el circuito de control del de potencia.
- Ante cualquier duda, **no tocar y preguntar**.

---

## 8. Resumen: qué aprendimos

1. Qué es un **Arduino** y qué son sus **pines** de entrada y salida.
2. Qué es el **firmware** y cómo controla las fases del lavarropas.
3. Qué es un **relé** y por qué separa 5 V de 220 V.
4. Qué son **backend** y **frontend**, y cómo se comunican Arduino ↔
   Raspberry ↔ navegador.
5. Qué es un **dashboard** y qué información muestra.
6. Cómo relacionar cada fase del ciclo con sus **actuadores** y **sensores**.

---

*Documento pensado para electricidad industrial. No se requieren conocimientos
de programación ni de Linux, pero sí atención a los conceptos de control,
sensores, actuadores y seguridad eléctrica.*
