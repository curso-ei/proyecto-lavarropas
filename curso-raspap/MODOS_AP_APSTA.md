# Modos de operación: AP y AP + STA

Este documento explica los dos modos de red que maneja el menú del curso y
cómo se comporta cada uno.

---

## 1. Modo AP (Access Point) — "DEFAULT"

### Qué hace

La Raspberry crea un **punto de acceso WiFi** al que se conectan los
dispositivos (teléfono, PC, etc.).

```
Dispositivo ── WiFi ──> Raspberry (AP) ── Ethernet (eth0) ──> Internet
```

### Características

- Interfaz AP: `wlan0`
- IP del AP: `10.3.141.1/24` (configuración estática)
- SSID por defecto: `RaspAP`
- Contraseña AP por defecto: `ChangeMe` (se recomienda cambiarla)
- DHCP para clientes: rango `10.3.141.50–10.3.141.150` (dnsmasq)
- Internet sale por `eth0` (Ethernet), si está conectado.

### Cuándo se usa

- Es el **modo seguro** de recuperación.
- Se restaura automáticamente al arrancar si `curso-raspap-default.service`
  está habilitado.

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

La Raspberry hace **dos cosas a la vez**:

1. **STA (cliente WiFi)**: `wlan0` se conecta a un WiFi externo (p. ej. la red
   de casa) para obtener internet.
2. **AP (punto de acceso)**: crea un AP virtual (`uap0`) al que se conectan
   tus dispositivos.

```
Dispositivo ── WiFi ──> AP (uap0) ── NAT ──> wlan0 (cliente WiFi) ──> Internet
```

### Características

- Interfaz cliente: `wlan0` (DHCP de la red externa)
- Interfaz AP virtual: `uap0`
- IP del AP: `192.168.50.1/24`
- DHCP para clientes del AP: `192.168.50.50–192.168.50.150`
- Internet sale por `wlan0` (cliente WiFi) — y también por `eth0` si está.

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

## 3. Diferencias clave

| | AP (DEFAULT) | AP + STA (NORMAL) |
|---|---|---|
| Interfaz AP | `wlan0` | `uap0` |
| Interfaz cliente | — | `wlan0` |
| IP AP | `10.3.141.1/24` | `192.168.50.1/24` |
| Internet | por `eth0` | por `wlan0` (cliente WiFi) |
| DHCP clientes | `10.3.141.x` | `192.168.50.x` |
| `hostapd.ini` | `WifiInterface=wlan0` | `WifiInterface=uap0` |

---

## 4. Subredes y conflictos

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

## 5. Cambios entre modos

- **De AP a AP + STA**: usar el menú → "Activar AP + STA". El menú prueba la
  red WiFi cliente, aplica el perfil NORMAL y verifica.
- **De AP + STA a AP**: reiniciar (vuelve a DEFAULT) o usar la restauración
  automática.
- **Aplicar perfiles manualmente**: existe en "Administrar configuraciones",
  pero es más riesgoso (no verifica). Se recomienda el flujo "Activar AP + STA".

---

## 6. Problemas conocidos y soluciones

### 6.1. Sin internet tras reiniciar (ruta por defecto incorrecta)

**Síntoma:** `ping 192.168.1.1` OK, pero `ping 1.1.1.1` → `Destination Host
Unreachable`.

**Causa:** ruta `default via 10.3.141.1 dev wlan0` (apuntaba a la propia
Raspberry). Se producía por:

- `nogateway` en `eth0` (impedía la ruta del módem).
- `static routers=10.3.141.1` en `wlan0` (instalaba ruta self).

**Solución aplicada:**

- `eth0` sin `nogateway` (acepta gateway DHCP).
- `wlan0` con `static ip_address=10.3.141.1/24` y `nogateway` (sin `static routers`).

### 6.2. Conflicto de subred en AP + STA

**Síntoma:** el AP funciona pero los clientes no tienen internet o hay rutas
raras.

**Causa:** la red WiFi cliente usa la misma subred que el AP.

**Solución:** el menú detecta el conflicto y aborta. Usar otra red WiFi o
cambiar la subred del AP.

---

## 7. Referencias

- Documentación principal: `curso-raspap/README.md`
- Instalación: `curso-raspap/INSTALACION.md`
- Credenciales: `curso-raspap/rellenar_credenciales.sh`
- Propuesta auto-cambio de subred: `curso-raspap/docs/PROPUESTA_OPCION_B_SUBRED_AUTOMATICA.md`
