# Firmware del lavarropas (Arduino Mega 2560): ciclo completo + control de motor con TRIAC

Preparado el 29 de octubre de 2026 · Archivo: lavarropas\_controller\_2enjuagues\_motor\_triac\_MEGA.ino

## 1. Objetivo

Unir en un solo firmware dos programas que ya funcionaban por separado:

- **Ciclo de lavado de 2 enjuagues** (lavarropas\_controller\_display\_error\_buzzer\_2enjuagues.ino): máquina de estados con LCD, buzzer, presostato, relés y reporte JSON para el dashboard.
- **Control de motor por cruce por cero** (lavarropas\_motor\_cruce\_cero.txt): TRIAC con MOC3021, tacómetro y PID de RPM.

Resultado buscado: que el motor gire en un sentido y en el otro en los ciclos de lavado, enjuague y enjuague con suavizante, con control de RPM, y que también lo haga en el centrifugado (en un solo sentido), sin perder el JSON que alimenta al dashboard.

## 2. Qué hace el motor en cada fase

| Fase | Sentido | Control de RPM |
| --- | --- | --- |
| Lavado | Vaivén: CW, pausa de 2 s, CCW; 3 ciclos completos | Sí, a RPM\_AGITADO (600) |
| Enjuague | Vaivén, igual que el lavado | Sí, a RPM\_AGITADO |
| Enjuague con suavizante | Vaivén; antes carga agua con el suavizante abierto | Sí, a RPM\_AGITADO |
| Centrifugado | Un solo sentido (CW), sin inversión | Sí, a RPM\_CENTRIFUGADO (6000), con rampa |

Secuencia completa del ciclo: llenado, lavado, drenaje 1, llenado de enjuague, enjuague, drenaje 2, llenado con suavizante, enjuague con suavizante, drenaje 3, centrifugado, fin.

## 3. Cómo se reparten el trabajo los componentes

- **TRIAC (MOC3021):** da la potencia al motor. Dispara en cada media onda con un retardo variable (control de fase). Más retardo significa menos potencia.
- **Detector de cruce por cero:** avisa dónde empieza cada media onda para sincronizar el disparo.
- **Tacómetro:** mide las RPM reales. El PID corrige la potencia para mantener la velocidad aunque cambie la carga.
- **Relés CW y CCW:** solo eligen el sentido de giro. Ya no encienden ni apagan el motor.
- **Relé de traba o en marcha:** igual que antes; ahora no se suelta hasta que el tambor frena.
- **Relés de llenado, drenaje y suavizante:** sin cambios.
- **Relé de centrifuga (D13):** ya no se usa. Se puede volver al comportamiento original con CENTRIFUGA\_POR\_MOTOR = false.

## 4. Pines (Arduino Mega 2560)

Todos los pines del ciclo original quedan iguales. Solo se agregan tres pines para el motor.

| Función | Pin | Nota |
| --- | --- | --- |
| LCD (RS, E, D4, D5, D6, D7) | A2, D6, A3, D5, A0, A1 | Igual que el original |
| START | D2 | Igual |
| Presostato | D3 | Igual |
| Traba / en marcha | D4 | Igual |
| Buzzer | D7 | Igual |
| Llenado, CW, CCW, drenaje, suavizante | D8, D9, D10, D11, D12 | Igual |
| Centrifuga (sin uso) | D13 | Configurado y apagado |
| Cruce por cero | D19 | NUEVO, interrupción INT2 |
| Tacómetro | D18 | NUEVO, interrupción INT3, señal digital 0 a 5 V |
| Disparo MOC3021 | D22 | NUEVO |

**Punto crítico del tacómetro.** El sketch del motor usaba el comparador analógico del Uno (AIN0 en D6 y AIN1 en D7). En el Mega el comparador existe dentro del chip, pero AIN0 (PE2) no sale a ningún pin de la placa, por lo que ese circuito no se puede conectar igual. Por eso el tacómetro entra por una interrupción externa (D18) y necesita una señal digital. Si tu tacómetro entrega una señal analógica o alterna, hace falta un comparador externo (LM393 o similar) entre el tacómetro y D18.

## 5. Cómo funciona el control del motor

### 5.1 Secuencia segura de arranque y parada

1. El ciclo pide arrancar el motor en un sentido.
2. Se acciona el relé del sentido elegido, con el TRIAC apagado.
3. Se esperan 100 ms (T\_RELE\_MS) para que el contacto se asiente.
4. Se habilita el TRIAC y arranca el PID.
5. Para detener: se corta el TRIAC de inmediato y los relés se sueltan 100 ms después.

Así los contactos de los relés nunca cortan ni cierran corriente de carga.

### 5.2 PID de velocidad

- Mide las RPM cada 100 ms con los pulsos del tacómetro (8 pulsos por vuelta) y las filtra.
- Potencia inicial de 20 % (POT\_ARRANQUE) y tope de 60 % en lazo cerrado (POT\_MAX\_PID).
- El objetivo sube con rampa (RAMPA\_RPM\_S = 1000 RPM por segundo) para no acelerar de golpe.
- Kp = 0.001 y Ki = 0.003, copiados del sketch del motor. Hay que afinarlos en la máquina real.

### 5.3 Disparo del TRIAC por interrupción

El disparo lo hace el Timer1 y no loop(). El motivo: el JSON a 9600 baudios y el LCD bloquean el loop durante decenas o cientos de milisegundos, y con el disparo en el loop se perderían medias ondas.

### 5.4 Protección por latido

Como el disparo ya no depende del loop, se agregó un latido: si loop() no corre durante más de 800 ms, el TRIAC deja de disparar solo.

## 6. Seguridad

- **Falla de tacómetro:** si pasan 2 s sin pulsos con el motor en marcha, se corta todo y el sistema pasa a ERROR\_MOTOR con alarma sonora. START reanuda el ciclo; si el tacómetro sigue sin señal, vuelve a dar error.
- **Falta de agua:** sin cambios. En lavado y enjuagues, si el presostato indica falta de agua, se detiene el motor y pasa a ERROR\_AGUA.
- **Traba de puerta:** después del centrifugado la puerta se libera recién cuando el motor está parado y el tacómetro marca menos de 100 RPM (RPM\_PUERTA\_SEGURA). En el original se soltaba de inmediato. Limitación: por debajo de unas 150 RPM del motor el tacómetro entrega menos de 2 pulsos por ventana y se lee 0, y si el tacómetro falla, esa protección no puede detectar que el tambor sigue girando.
- **Errores:** en ERROR\_MOTOR la traba queda cerrada.
- **Tensión de red:** se trabaja con 220 V. Conviene snubber RC en el TRIAC, fusible, y verificar que los contactos de los relés soporten la corriente de arranque del motor.

## 7. JSON para el dashboard

Se mantiene una línea JSON por segundo a 9600 baudios, con los mismos campos y orden: estado\_ciclo, fase, error, giros, max\_giros, lcd, componentes, presostato y pulsador.

**Novedades:**

- Fase nueva ERROR\_MOTOR, con estado\_ciclo = error y error = FALLA TACOMETRO.
- Objeto nuevo motor, con rpm, objetivo y potencia. El dashboard puede ignorarlo.
- El campo componentes.centrifuga ahora indica que el motor está en marcha durante el centrifugado, ya que no hay relé.
- Se eliminaron los comandos de texto por serie del sketch del motor (R y P) y su salida de depuración, para no contaminar el JSON.

**A revisar en main.py:** que tolere la fase ERROR\_MOTOR, el texto FALLA TACOMETRO y el objeto motor. Además, el firmware original ya imprime dos líneas de texto plano al reanudar tras un error de agua; si el parser es estricto, conviene eliminarlas.

**Baudios.** A 9600 el envío del JSON bloquea el loop unos 0,4 s por segundo, lo que pausa las actualizaciones del PID. Subir a 115200 en el firmware y en el pyserial de la Raspberry mejora la regulación. Se dejó en 9600 para no romper la comunicación actual.

## 8. Parámetros a ajustar en la máquina

| Parámetro | Valor inicial | Qué hace |
| --- | --- | --- |
| RPM\_AGITADO | 600 | Velocidad del vaivén en lavado y enjuagues |
| RPM\_CENTRIFUGADO | 6000 | Velocidad del centrifugado |
| RAMPA\_RPM\_S | 1000 | Subida del objetivo en RPM por segundo |
| POT\_MAX\_PID | 60 % | Tope de potencia; puede quedar corto al centrifugar con carga |
| POT\_ARRANQUE | 20 % | Potencia inicial del PID |
| Kp y Ki | 0.001 y 0.003 | Ganancias del PID |
| PULSOS\_POR\_VUELTA | 8 | Pulsos del tacómetro por vuelta del motor |
| T\_SIN\_TACO\_MS | 2000 | Tiempo sin pulsos antes de declarar falla |
| T\_GIRO y T\_PAUSA\_GIRO | 5000 y 2000 ms | Duración del giro y de la pausa del vaivén |
| T\_CENTRIFUGADO | 15000 ms | Duración del centrifugado |
| SENTIDO\_CENTRIFUGADO\_CW | true | Sentido de giro al centrifugar |
| CENTRIFUGA\_POR\_MOTOR | true | false vuelve al relé D13 del original |

Las RPM son las del eje del motor, no las del tambor. La relación de poleas las convierte.

Hoy lavado, enjuague y enjuague con suavizante comparten la misma velocidad. Si se quieren velocidades distintas, se puede dividir RPM\_AGITADO en tres constantes.

## 9. Cambios respecto del firmware original de 2 enjuagues

- El motor pasa de relés directos a TRIAC con PID; los relés CW y CCW solo eligen el sentido.
- El centrifugado lo hace el motor y no el relé D13.
- Fase nueva ERROR\_MOTOR y buzzer también en esa falla.
- Disparo por Timer1, latido de seguridad y traba de puerta retenida hasta que el tambor frena.
- Se agregan los pines D18, D19 y D22.
- Sin cambios: máquina de estados, tiempos, LCD, buzzer, presostato, antirrebote y reporte JSON (con las novedades indicadas).

## 10. Qué se verificó y qué no

**Verificado:**

- El firmware compila para Arduino Mega 2560 sin errores ni advertencias (8 % de flash y 18 % de RAM).

**No verificado (requiere probar en la máquina):**

- Temporización real del disparo del TRIAC con tu circuito de cruce por cero.
- Que Kp, Ki y las RPM funcionen con tu motor y con carga.
- Que el tacómetro entregue una señal compatible con D18.
- Compatibilidad del dashboard con los campos nuevos. La versión del Mega arma el JSON con ArduinoJson como el original; se compiló pero no se comparó byte a byte.

## 11. Plan de pruebas sugerido

1. **Motor solo.** Afinar Kp, Ki y POT\_ARRANQUE con el sketch original del motor, sin el ciclo.
2. **Placa sin carga.** Verificar LCD, botón START, presostato, relés y el JSON en el dashboard.
3. **TRIAC con una lámpara** como carga, para comprobar el disparo y el cruce por cero.
4. **Motor con el tambor vacío**, con forma de cortar la alimentación a mano. Probar el vaivén y el centrifugado.
5. **Fallas a propósito:** desconectar el tacómetro (debe dar ERROR\_MOTOR a los 2 s) y simular falta de agua (debe dar ERROR\_AGUA).
6. **Ciclo completo** con carga real.

## 12. Puntos abiertos

- Confirmar que el motor es universal de escobillas alimentado en corriente alterna, y que está cableado así: red, TRIAC, relés de sentido y motor.
- Confirmar el tipo de señal del tacómetro (digital o analógica).
- Confirmar que el relé D13 no cumple otra función además de centrifugar.
- Decidir si se sube el baudrate a 115200 y se actualiza main.py.
- Decidir si lavado, enjuague y suavizante necesitan velocidades distintas.

## 13. Archivos

- lavarropas\_controller\_2enjuagues\_motor\_triac\_MEGA.ino: versión definitiva para Mega 2560.

Librerías: LiquidCrystal y ArduinoJson (v7).
