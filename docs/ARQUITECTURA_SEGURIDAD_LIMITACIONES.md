# Arquitectura, seguridad y limitaciones del proyecto

## Lavarropas didáctico: Arduino + Raspberry Pi + dashboard de monitoreo

> **Visión de ingeniería** — documento técnico de referencia.
> Autoría del proyecto: alumnos y profesor de Electricidad Industrial.
> Finalidad: **demostrativa y educativa** (no es un producto industrial).

---

## 1. Resumen ejecutivo

El proyecto integra tres mundos que normalmente se estudian por separado:

1. **Electricidad industrial** — actuadores (bombas, motor, electroválvula),
   sensores (presostato, pulsador), relés y potencia (220 V).
2. **Automatización** — un controlador (Arduino) con máquina de estados,
   temporizaciones, antirrebote y lógica de fallo.
3. **Software** — un backend que lee el controlador, un frontend que lo
   visualiza, y una arquitectura cliente-servidor sobre HTTP.

El resultado es un **sistema de monitoreo en tiempo real** de un ciclo de
lavado, pensado para que los alumnos **vean** lo que ocurre dentro de la
máquina.

---

## 2. Objetivos del sistema

- **Observar** el ciclo de lavado en vivo (fases, salidas, sensores).
- **Separar** el circuito de control (5 V) del circuito de potencia (220 V).
- **Conservar** el firmware original del profesor como controlador autónomo.
- **No interferir** con la lógica del lavarropas: el dashboard es de solo
  lectura por diseño.
- **Facilitar** la enseñanza: reproducir el ciclo **con hardware real
  (Arduino) o sin él (simulador)** y poder correr el sistema en Windows,
  macOS y Linux.

---

## 3. Arquitectura general

```
┌─────────────────────────────────────────────────────────────────────┐
│                          LAVARROPAS (maqueta)                        │
│                                                                      │
│  Actuadores (220 V)                Sensores (señal de nivel/pulso)   │
│   ├─ Bomba de llenado              ├─ Presostato (nivel de agua)      │
│   ├─ Bomba de desagote             └─ Pulsador START                  │
│   ├─ Motor (2 sentidos)                                               │
│   ├─ Suavizante                                                       │
│   └─ Centrífuga                                                       │
└───────────────┬───────────────────────────────▲──────────────────────┘
                │ potencia (220 V)               │ señal (contacto seco)
                ▼                               │
┌───────────────────────────────────────────────┴──────────────────────┐
│                        BANCO DE RELÉS / AISLACIÓN                     │
│   Lado de potencia (220 V)  │  Lado de control (5 V, seguro)          │
└───────────────┬──────────────────────────────────────────────────────┘
                │
                ▼
┌──────────────────────────────────────────────────────────────────────┐
│                          ARDUINO UNO (controlador)                    │
│   Firmware: máquina de estados, temporizaciones, antirrebote,        │
│   salida JSON por Serial (USB, 9600 baud).                            │
└───────────────┬──────────────────────────────────────────────────────┘
                │ USB (serial)
                ▼
┌──────────────────────────────────────────────────────────────────────┐
│                     RASPBERRY PI (servidor / gateway)                 │
│   Backend (FastAPI/Python): lee Serial, guarda estado, sirve HTTP.    │
│   Frontend (HTML/JS): dashboard de solo lectura.                      │
└───────────────┬──────────────────────────────────────────────────────┘
                │ HTTP (puerto 8000)
                ▼
┌──────────────────────────────────────────────────────────────────────┐
│                      NAVEGADOR (PC / tablet / teléfono)               │
│   Dashboard: luces, fases, LCD, alarmas, log de eventos.             │
└──────────────────────────────────────────────────────────────────────┘
```

### 3.1. Capa física (eléctrica)

- **Potencia**: 220 V AC para bombas, motor y electroválvulas.
- **Control**: Arduino con salidas digitales de 5 V / baja corriente.
- **Aislamiento**: relés. La bobina se energiza con 5 V; el contacto
  conmuta 220 V. No hay conexión galvánica entre ambos circuitos.

### 3.2. Capa de control (firmware)

- **Plataforma**: Arduino UNO (ATmega328P) según el diseño original del
  firmware. En una sesión real la evidencia (`lsusb`, `board list`) apuntó a un
  **Mega 2560 R3**, sin que se verificara el microcontrolador; el firmware
  compila para ambos. Por eso el menú ya no fija el FQBN: lo consulta con
  `arduino-cli board list`. Ver `DECISIONES.md`, P12 a P14.
- **Paradigma**: máquina de estados finita no bloqueante (`millis()`, sin
  `delay()`), lo que permite atender LCD, botón y alarma sin congelar el bucle.
- **Comunicación**: Serial a 9600 baud. El firmware emite **una línea JSON
  por segundo** con el estado completo.

### 3.3. Capa de servicios (backend)

- **Lenguaje**: Python 3.
- **Framework**: FastAPI + Uvicorn (servidor ASGI).
- **Puerto**: 8000 (HTTP).
- **Responsabilidades**:
  - Detectar el Arduino por USB (VID/PID oficial y clones CH340).
  - Leer el buffer serial completo (evita desfase temporal).
  - Parsear JSON y descartar líneas corruptas.
  - Mantener el último estado y un log de eventos (máx. 30).
  - Exponer endpoints REST.

### 3.4. Capa de presentación (frontend)

- **Tecnología**: HTML + CSS + JavaScript puro (sin frameworks).
- **Actualización**: *polling* cada 1 s (`GET /estado`).
- **Visualización**: luces de salidas/entradas, LCD reflejado, banner de
  error, indicador de alarma, barra de giros y log de eventos.

### 3.5. Capa de simulación (opcional)

- **Simulador**: réplica en Python del firmware, conectada por pseudo-terminal
  (`socat`) para probar el dashboard **sin hardware** (alternativa opcional
  al Arduino real).
- **Variantes de ciclo**: original y "dos enjuagues".
- **Velocidad**: 1x–10x para no esperar los tiempos reales en clase.

---

## 4. Protocolo de comunicación

### 4.1. Arduino → Raspberry (Serial, USB)

Línea JSON por segundo:

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
  "pulsador": false
}
```

### 4.2. Raspberry → navegador (HTTP)

| Método | Ruta | Función |
|---|---|---|
| `GET` | `/` | Sirve el dashboard |
| `GET` | `/estado` | Devuelve estado + log + `arduino_conectado` |
| `POST` | `/accion/{start\|reset}` | Acción remota (desactivada por defecto) |

> El protocolo es **unidireccional** en la práctica: el Arduino reporta y el
> dashboard observa. La acción remota existe como extensión preparada, pero
> está **deshabilitada** por decisión de diseño (ver seguridad).

---

## 5. Decisiones de diseño (y su justificación)

| Decisión | Justificación |
|---|---|
| **Arduino maestro, dashboard observa** | El firmware original ya es un controlador autónomo probado; invertir la arquitectura habría duplicado lógica y riesgo. |
| **Dashboard de solo lectura** | Evita conflictos con el bucle del firmware y reduce la superficie de ataque. |
| **Máquina de estados no bloqueante** | Permite atender LCD, botón y alarma en tiempo real sin `delay()`. |
| **JSON por Serial** | Protocolo simple, legible y fácil de depurar para fines didácticos. |
| **Relés como aislamiento** | Separación obligatoria entre control (5 V) y potencia (220 V). |
| **Simulador + socat** | Permite ensayar el sistema completo sin hardware; es una alternativa opcional al Arduino real. |
| **Sin frameworks en frontend** | Menos dependencias, más fácil de leer para los alumnos. |
| **Backend multiplataforma** | El mismo código corre en Linux, Windows y macOS. |

---

## 6. Seguridad

### 6.1. Seguridad eléctrica (la más crítica)

- **El lado de 220 V puede causar la muerte.** Toda manipulación se realiza
  con el equipo **desenchufado** y bajo supervisión del profesor.
- Los relés garantizan **aislamiento galvánico** entre el circuito de control
  y el de potencia.
- El Arduino, la Raspberry y el USB operan a **5 V** (tensión segura).
- Se debe verificar el **cableado y las protecciones** (fusible/termomagnética
  aguas arriba) antes de energizar la maqueta.
- **Regla práctica:** control y potencia se tocan por separado y nunca con
  energía.

### 6.2. Seguridad de control (automatización)

- **Antirrebote** del pulsador (50 ms) para evitar falsos arranques.
- **Máquina de estados**: solo se permiten transiciones válidas; un fallo de
  nivel lleva a `ERROR_AGUA` y corta salidas.
- **Alarma sonora** (buzzer) y visual (LCD/banner) ante falta de agua.
- **Apagado total** (`apagarTodo()`) al cambiar de fase, para no dejar
  actuadores colgados.
- `RELAY_ACTIVE_LOW = true`: lógica invertida intencional; un pin en reposo
  no activa el relé (comportamiento seguro según el módulo usado).

### 6.3. Seguridad de software

- **Backend sin autenticación**: expone la red local en el puerto 8000. Es
  aceptable solo para entorno de aula/red de confianza.
- **Acciones remotas desactivadas** (`PERMITIR_ACCIONES = false`): el
  dashboard no puede arrancar ni resetear el ciclo.
- **Validación de JSON**: líneas corruptas se descartan sin romper el bucle.
- **Reconexión automática** del puerto serie si el Arduino se desconecta.
- **Buffer serial drenado completo** para evitar latencia acumulada.

### 6.4. Riesgos residuales y mitigaciones

| Riesgo | Mitigación |
|---|---|
| Acceso no autorizado a la red local | Usar solo en red de aula; no exponer a internet. |
| Arranque remoto si se habilita 3.B | Mantener desactivado por defecto; requiere modificar firmware y frontend. |
| Cableado de potencia defectuoso | Verificación visual previa + protección termomagnética. |
| Arduino desconectado en clase | El backend entra en modo "sin datos" y reconecta solo. |
| Firmware equivocado o subida fallida | El backend *parece* conectado pero no parsea JSON. Mitigado con la verificación previa del puerto (opción 4) y el bloque 8 de `check_arduino.sh`. |
| Confusión de pines | Documentar pines y usar colores/etiquetas en la maqueta. |

---

## 7. Limitaciones conocidas

### 7.1. Limitaciones del hardware

- **Arduino UNO**: RAM limitada (2 KB). El uso de `JsonDocument` dinámico
  (heap) es una solución, pero sigue siendo un recurso escaso. La maqueta de
  aula usa un **Mega 2560** (8 KB), donde la restricción es menos crítica.
- **FQBN que no corresponde a la placa**: si se compila para un modelo y la
  placa es otra, el binario no arranca y el síntoma es "Arduino conectado pero
  sin datos" en el dashboard. No es un problema eléctrico sino de herramienta;
  `arduino_menu.sh` ya consulta el FQBN en lugar de fijarlo (ver `DECISIONES.md`
  P13).
- **Salidas de 5 V**: no pueden alimentar cargas de potencia; siempre vía relé.
- **Sin medición real de corriente/tensión**: el sistema solo refleja estado
  lógico (on/off), no magnitudes eléctricas.

### 7.2. Limitaciones del firmware

- **`SUAVIZANTE`**: en el firmware original solo abre la electroválvula de
  suavizante 10 s; **no carga agua ni mueve el motor**. Es una simplificación.
- **`PIN_TRABA`** se comporta como "En marcha", no como traba de puerta real.
- **Temporizaciones fijas**: drenaje 10 s, suavizante 10 s, centrifugado 15 s,
  giros 5 s con pausa 2 s, máx. 3 giros. No hay ajuste dinámico.
- **Presostato lógico**: se modela como contacto seco (LOW = lleno); no hay
  lectura analógica de presión.
- **Sin watchdog**: si el microcontrolador se cuelga, no se reinicia solo.

### 7.3. Limitaciones del software

- **Polling cada 1 s**: no es tiempo real estricto; hay hasta ~1 s de retardo
  en pantalla.
- **Sin WebSocket**: la actualización es por sondeo HTTP, suficiente para
  fines didácticos.
- **Backend sin persistencia**: el log se guarda en memoria (máx. 30 eventos)
  y se pierde al reiniciar.
- **Sin autenticación ni HTTPS**: no apto para uso remoto o internet.
- **Detección de Arduino por VID/PID**: clones no estándar podrían no
  detectarse automáticamente.

### 7.4. Limitaciones didácticas

- Es una **maqueta demostrativa**, no un lavarropas real ni un sistema
  industrial certificado.
- No contempla todas las fallas posibles (sobrecarga, fuga a tierra, rotor
  bloqueado, temperatura).
- No reemplaza el estudio normativo (reglamentación eléctrica, IEC/IRAM,
  seguridad de máquinas).

---

## 8. Carácter educativo del proyecto

Este proyecto **no pretende ser un producto comercial ni industrial**. Su
valor está en que permite, en un entorno controlado:

- Visualizar la **cadena de control** completa: sensor → controlador →
  actuador.
- Entender el **relé** como frontera entre control y potencia.
- Relacionar cada **fase del ciclo** con sus componentes eléctricos.
- Introducir **arquitectura de software** (backend/frontend) sin perder de
  vista el objeto físico.
- Reproducir el sistema **con o sin hardware** (Arduino real o simulador),
  ideal para aula.
- Discutir **fallas y seguridad** de forma práctica.

---

## 9. Trabajo futuro (mejoras posibles)

### 9.1. Eléctricas / automatización

- Medición real de corriente y tensión (sensores no invasivos).
- Protección diferencial y termomagnética visible en la maqueta.
- Variador de frecuencia para el motor (didáctico).
- Más sensores: puerta, temperatura, desbalance.
- Modo manual de cada salida (con candado de seguridad).

### 9.2. Software

- Autenticación básica y, si se expone, HTTPS.
- WebSocket para actualización en tiempo real sin polling.
- Persistencia del log (SQLite) e histórico de ciclos.
- Control remoto con permisos y confirmación (si se decide habilitarlo).
- Empaquetado en contenedor (Docker) para reproducibilidad.

### 9.3. Didácticas

- Guía de prácticas con mediciones y preguntas de verificación.
- Simulador con fallas inyectables (falta de agua, sobrecarga).
- Diagrama unifilar de la maqueta.

---

## 10. Conclusión

El proyecto logra un **equilibrio didáctico** entre electricidad industrial,
automatización y software. Su arquitectura en capas (potencia → relés →
controlador → backend → frontend) es una **maqueta fiel de un sistema de
control real**, con las simplificaciones y limitaciones propias de un entorno
de enseñanza.

La decisión más valiosa es la **separación estricta entre control y potencia**,
reforzada por un diseño de software de **solo lectura**, que minimiza el
riesgo y maximiza el valor pedagógico.

---

*Documento de arquitectura y seguridad. Elaborado con fines educativos.*
