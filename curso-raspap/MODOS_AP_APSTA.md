# Modos de operación: AP, AP + STA y Solo Cliente WiFi

Este documento explica los modos de red que maneja el menú del curso y
cómo se comporta cada uno.

---

## 1. Modo AP (Access Point) — "DEFAULT"

### Qué hace

La Raspberry crea la red WiFi `RaspAP`. Los dispositivos que se conectan
reciben una IP en la subred `10.3.141.x` y pueden llegar a la Raspberry
(web de RaspAP, SSH, etc.).

```
Dispositivo ── WiFi ──> Raspberry (AP) ── Ethernet (eth0) ──> Internet (opcional)
```

### Para qué sirve

- Tener un punto de acceso propio para **acceder a la Raspberry** y permitir
  que **varios dispositivos se conecten a ella**, sin depender de una red
  externa.
- Es el **modo seguro** de recuperación.

### Internet

Es opcional. Si el cable `eth0` está conectado a una red con internet, los
dispositivos conectados al AP también pueden salir a internet. Si no hay
cable, el AP sigue funcionando para acceder a la Raspberry.

### Cómo se activa

- Es el estado base.
- Se aplica al arrancar si `curso-raspap-default.service` está habilitado.

### Características

- Interfaz AP: `wlan0`
- IP del AP: `10.3.141.1/24` (configuración estática)
- SSID por defecto: `RaspAP`
- Contraseña AP por defecto: `ChangeMe` (se recomienda cambiarla)
- DHCP para clientes: rango `10.3.141.50–10.3.141.150` (dnsmasq)

### Configuración clave

- `/etc/dhcpcd.conf`:
  ```
  interface wlan0
  static ip_address=10.3.141.1/24
  nogateway
  ```
- `/etc/hostapd/hostapd.conf`: `interface=wlan0`
- `/etc/raspap/hostapd.ini`: `WifiInterface = wlan0`, `WifiAPEnable = 0`

> **Nota importante:** en modo AP, `wlan0` **no debe** tener `static routers`
> apuntando a sí misma. Eso fue la causa de la pérdida de internet (ruta
> predeterminada incorrecta). Se corrigió quitando `static routers` y usando
> `nogateway`.

---

## 2. Modo AP + STA (Access Point + Station) — "NORMAL"

### Qué hace

La Raspberry se conecta a una red WiFi externa y, al mismo tiempo, ofrece su
propio punto de acceso para que varios dispositivos se conecten a ella.

```
Dispositivo ── WiFi ──> AP (uap0) ── NAT ──> wlan0 (cliente WiFi) ──> Internet
```

### Para qué sirve

- Que la Raspberry use un WiFi externo y, además, ofrezca un AP para que
  varios dispositivos se conecten a ella.

### Una sola tarjeta WiFi

La Raspberry tiene una única radio WiFi física. En este modo, esa misma radio
cumple dos funciones a la vez: conectarse a la red externa (`wlan0`) y crear
el punto de acceso (`uap0`). Por eso ambas interfaces comparten el mismo
canal, y la estabilidad depende en buena parte de la señal de la red externa.
Si la señal es débil, pueden aparecer cortes en la conexión cliente.

### Internet

Sale por la conexión cliente (`wlan0`) hacia la red externa. Si además hay
`eth0`, puede sumar una salida, evitando conectar `eth0` y `wlan0` a la misma
red/subred.

### Cómo se activa

- Menú → Configuración RaspAP → Activar AP + STA.

### Características

- Interfaz cliente: `wlan0` (DHCP de la red externa)
- Interfaz AP virtual: `uap0`
- IP del AP: `192.168.50.1/24`
- DHCP para clientes del AP: `192.168.50.50–192.168.50.150`

### Configuración clave

- `/etc/dhcpcd.conf`:
  ```
  interface uap0
  static ip_address=192.168.50.1/24
  nohook wpa_supplicant
  ```
- `/etc/hostapd/hostapd.conf`: `interface=uap0`
- `/etc/raspap/hostapd.ini`: `WifiInterface = uap0`, `WifiManaged = wlan0`,
  `WifiAPEnable = 1`

> **Nota:** en AP + STA, `uap0` **no** debe tener `static routers` apuntando a
> sí misma. Se eliminó de `defaults.json` para evitar el mismo problema que en
> el modo AP.

---

## 3. Modo Solo Cliente WiFi (STA)

### Qué hace

La Raspberry se conecta con `wlan0` a la red WiFi elegida y obtiene una IP de
esa red. El AP queda apagado y deshabilitado.

```
Raspberry (cliente WiFi) ── WiFi ──> Router externo ──> Internet
```

### Para qué sirve

- Usar la Raspberry como un equipo más conectado a una red WiFi externa, sin
  ofrecer un punto de acceso.

### Internet

Sale por `wlan0` hacia la red externa.

### Atención

Al quedar sin AP, si la conexión falla y no hay cable `eth0` ni otro acceso,
puede ser difícil volver a administrarla. Por eso conviene tener activo el
servicio de restauración DEFAULT antes de usar este modo.

### Cómo se activa

- Menú → Configuración RaspAP → Activar solo cliente WiFi (sin AP).

### Características

- Interfaz cliente: `wlan0` (DHCP de la red externa)
- AP: **apagado y deshabilitado** (`hostapd`, `dnsmasq`, `raspapd`)
- `uap0`: no existe

### Configuración clave

- `/etc/raspap/hostapd.ini`: `WifiInterface = wlan0`, `WifiAPEnable = 0`
- `/etc/hostapd/hostapd.conf`: `interface=wlan0` (aunque `hostapd` queda parado)
- `/etc/dhcpcd.conf`: sin IP estática de AP en `wlan0` ni `nohook
  wpa_supplicant` para que dhcpcd maneje la IP del cliente.
- Servicios `hostapd`, `dnsmasq` y `raspapd`: **disabled**.

---

## 4. Diferencias clave

| | AP (DEFAULT) | AP + STA (NORMAL) | Solo Cliente (STA) |
|---|---|---|---|
| Interfaz AP | `wlan0` | `uap0` | — |
| Interfaz cliente | — | `wlan0` | `wlan0` |
| IP AP | `10.3.141.1/24` | `192.168.50.1/24` | — |
| Internet | por `eth0` | por `wlan0` (cliente) | por `wlan0` (cliente) |
| DHCP clientes | `10.3.141.x` | `192.168.50.x` | — |
| `hostapd.ini` | `WifiInterface=wlan0` | `WifiInterface=uap0` | `WifiInterface=wlan0` |
| `WifiAPEnable` | `0` | `1` | `0` |
| Servicios AP | activos | activos | **deshabilitados** |

---

## 5. Subredes y conflictos

### Subredes reservadas

| Subred | Uso |
|---|---|
| `10.3.141.0/24` | AP en modo DEFAULT |
| `192.168.50.0/24` | AP en modo AP + STA |
| `100.64.0.0/10` | Tailscale |

### Regla de oro

La red WiFi cliente (`wlan0`) **no debe** caer en ninguna de esas subredes.
El menú detecta esto y aborta la activación de AP + STA si hay conflicto.

> Ver `curso-raspap/docs/` o el script `curso-menu` (función
> `apsta_conflicting_subnet`) para más detalle.

---

## 6. Cambios entre modos

- **De AP a AP + STA**: usar el menú → "Activar AP + STA". El menú prueba la
  red WiFi cliente, aplica el perfil NORMAL y verifica.
- **De AP + STA a AP**: reiniciar (vuelve a DEFAULT si el servicio está
  habilitado) o usar la restauración automática.
- **A solo cliente WiFi**: menú → "Activar solo cliente WiFi (sin AP)".
  Apaga y deshabilita el AP y deja `wlan0` como cliente.
- **De solo cliente WiFi a AP**: aplicar DEFAULT (Administrar configuraciones
  → Aplicar DEFAULT), activar el servicio DEFAULT y reiniciar, o usar
  "Activar AP + STA" si además se quiere el cliente WiFi.
- **Aplicar perfiles manualmente**: existe en "Administrar configuraciones",
  pero es más riesgoso (no verifica). Se recomienda el flujo seguro del menú.

---

## 7. Problemas conocidos y soluciones

### 7.1. Sin internet tras reiniciar (ruta por defecto incorrecta)

**Síntoma:** `ping 192.168.1.1` OK, pero `ping 1.1.1.1` → `Destination Host
Unreachable`.

**Causa:** ruta `default via 10.3.141.1 dev wlan0` (apuntaba a la propia
Raspberry). Se producía por:

- `nogateway` en `eth0` (impedía la ruta del módem).
- `static routers=10.3.141.1` en `wlan0` (instalaba ruta self).

**Solución aplicada:**

- `eth0` sin `nogateway` (acepta gateway DHCP).
- `wlan0` con `static ip_address=10.3.141.1/24` y `nogateway` (sin `static routers`).

### 7.2. Conflicto de subred en AP + STA

**Síntoma:** el AP funciona pero los clientes no tienen internet o hay rutas
raras.

**Causa:** la red WiFi cliente usa la misma subred que el AP.

**Solución:** el menú detecta el conflicto y aborta. Usar otra red WiFi o
cambiar la subred del AP.

---

## 8. Referencias

- Documentación principal: `curso-raspap/README.md`
- Instalación: `curso-raspap/INSTALACION.md`
- Credenciales: `curso-raspap/rellenar_credenciales.sh`
- Propuesta auto-cambio de subred: `curso-raspap/docs/PROPUESTA_OPCION_B_SUBRED_AUTOMATICA.md`
