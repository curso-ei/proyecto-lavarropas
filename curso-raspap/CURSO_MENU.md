# curso-menu — Manual de uso

Documento de referencia del menú del curso (`/usr/local/sbin/curso-menu`).

> Última actualización: 2026-10-22
> Hardware de referencia: Raspberry Pi 3 Model B, Raspbian 13 (trixie)

---

## 1. Qué es

`curso-menu` es el menú principal del curso para administrar la red de la
Raspberry. Se ejecuta como administrador y controla:

- Los **modos de red** de la Raspberry (AP, AP + STA, Solo Cliente WiFi).
- Las **redes WiFi cliente** guardadas.
- Los **perfiles** de configuración (DEFAULT y NORMAL).
- El **arranque** (restauración automática de DEFAULT).
- Herramientas auxiliares: Arduino, IA (pi), modo sudo.

---

## 2. Cómo se inicia

```bash
sudo curso-menu
```

Al abrirlo aparece el **Menú Principal del Curso**:

```
  1) Configuración RaspAP
  2) Red y conectividad
  3) Raspberry Pi
  4) Arduino
  5) Herramientas del curso
  6) IA (pi)
  7) Modo Sudo

  0) Salir
```

| Opción | Qué hace |
|---|---|
| `1` | Entra al menú de configuración de RaspAP (red WiFi / AP). |
| `2` | Red y conectividad (pendiente de implementar). |
| `3` | Raspberry Pi (pendiente de implementar). |
| `4` | Abre el menú Arduino del proyecto (`arduino_menu.sh`). |
| `5` | Herramientas del curso (pendiente de implementar). |
| `6` | Abre el agente de IA `pi` como usuario `rasp`. |
| `7` | Activa/desactiva sudo sin contraseña para `rasp`. |

---

## 3. Menú Configuración RaspAP

Desde el menú principal, la opción `1` abre:

```
  1) Administrar configuraciones
  2) Redes WiFi Cliente
  3) Activar AP + STA
  4) Configuración de arranque
  5) Estado de RaspAP
  6) Logs de AP + STA
  7) Activar solo cliente WiFi (sin AP)
  8) Guardar diagnóstico

  0) Volver al menú principal
```

### 3.1. Administrar configuraciones

Maneja los perfiles **DEFAULT** y **NORMAL**.

```
  1) Guardar actual como DEFAULT
  2) Aplicar DEFAULT
  3) Guardar actual como NORMAL
  4) Aplicar NORMAL
  5) Información de configuraciones
```

- **Guardar actual como DEFAULT/NORMAL:** guarda la configuración actual de
  RaspAP en el perfil indicado (`config.tar.gz`).
- **Aplicar DEFAULT/NORMAL:** aplica un perfil guardado. Es más directo pero
  **no verifica** la conexión; para cambios de modo se recomienda usar el
  flujo seguro de las opciones 3 y 7.

### 3.2. Redes WiFi Cliente

Gestiona las redes WiFi a las que la Raspberry se conecta como cliente.

```
  1) Ver redes guardadas
  2) Agregar una red
  3) Eliminar una red
  4) Guardar backup de redes actuales
  5) Restaurar backup de redes
  6) Probar conexión a una red guardada
```

- **Probar conexión:** apaga el AP temporalmente, intenta conectar `wlan0` a
  la red elegida y luego restaura el AP. Sirve para validar una red antes de
  usarla en AP + STA.

### 3.3. Activar AP + STA

Activa el modo **AP + STA (NORMAL)**: la Raspberry se conecta a un WiFi
externo y a la vez ofrece su propio AP.

- Selecciona la red WiFi cliente (o agrega una nueva).
- Baja el AP, prueba la conexión cliente, aplica el perfil NORMAL y verifica.
- Si falla, restaura DEFAULT automáticamente.
- Durante el proceso el AP se apaga ~40 s.

### 3.4. Configuración de arranque

Controla el servicio de restauración DEFAULT al arrancar.

```
  1) Crear / actualizar servicio DEFAULT
  2) Activar servicio DEFAULT
  3) Desactivar servicio DEFAULT
  4) Ver estado del servicio
  5) Ver registro del último arranque
```

### 3.5. Estado de RaspAP

Muestra un resumen del estado actual:

- Perfiles disponibles (DEFAULT / NORMAL).
- Redes WiFi cliente guardadas.
- Último resultado de AP + STA.
- Estado del servicio de arranque.
- Servicios (`raspapd`, `hostapd`, `dnsmasq`).
- Interfaces e IPs.

### 3.6. Logs de AP + STA

Muestra el último resultado (`last-result.txt`) y el log completo
(`last.log`) de la activación AP + STA.

### 3.7. Activar solo cliente WiFi (sin AP)

Activa el modo **Solo Cliente WiFi (STA)**: la Raspberry se conecta a un WiFi
externo y **no** ofrece punto de acceso.

- Apaga y **deshabilita** `hostapd`, `dnsmasq` y `raspapd`.
- Conecta `wlan0` a la red elegida.
- Si falla, restaura DEFAULT automáticamente.
- **Aviso de seguridad:** si el servicio DEFAULT está desactivado, el menú
  pregunta si se quiere activar antes de continuar.

### 3.8. Guardar diagnóstico

Ejecuta `guardar_diagnostico.sh` (solo lectura) para capturar el estado del
sistema:

```
  1) Guardar diagnóstico (estado actual)
  2) Guardar diagnóstico como referencia previa (tag: antes)
```

Los resultados quedan en `~/diagnostico/<timestamp>/` y en un `.tar.gz`.

---

## 4. Modos de red

El menú maneja tres modos:

| Modo | Opción del menú | Resultado |
|---|---|---|
| AP (DEFAULT) | Arranque / aplicar DEFAULT | La Pi da su propio AP; internet opcional por `eth0`. |
| AP + STA (NORMAL) | `3) Activar AP + STA` | Cliente WiFi `wlan0` + AP virtual `uap0`. |
| Solo Cliente WiFi (STA) | `7) Activar solo cliente WiFi` | Solo cliente WiFi, sin AP. |

Ver `MODOS_AP_APSTA.md` para la explicación completa de cada modo.

---

## 5. Archivos y rutas principales

| Recurso | Ruta |
|---|---|
| Script principal | `/usr/local/sbin/curso-menu` |
| Restauración DEFAULT | `/usr/local/sbin/curso-raspap-restore` |
| Servicio de arranque | `/etc/systemd/system/curso-raspap-default.service` |
| Levantar/parar RaspAP | `/etc/raspap/hostapd/servicestart.sh` |
| Perfiles | `/etc/curso-raspap/profiles/{default,normal}/` |
| Logs AP + STA | `/etc/curso-raspap/apsta/last.log` y `last-result.txt` |
| Logs solo cliente | `/etc/curso-raspap/apsta/last-client.log` y `last-client-result.txt` |
| Redes WiFi | `/etc/wpa_supplicant/wpa_supplicant.conf` |
| Diagnóstico | `/home/rasp/proyecto-pi/scripts/guardar_diagnostico.sh` |

---

## 6. Procesos en segundo plano

Las opciones que modifican la red corren como servicios transitorios de
systemd, para que sigan aunque se cierre la sesión:

- `curso-raspap-apsta.service` → Activar AP + STA.
- `curso-raspap-client.service` → Activar solo cliente WiFi.

Se pueden seguir con:

```bash
systemctl status curso-raspap-apsta.service
journalctl -u curso-raspap-apsta.service -f
```

---

## 7. Referencias

- Modos de red: `MODOS_AP_APSTA.md`
- Instalación: `INSTALACION.md`
- Credenciales: `CREDENCIALES.md`
- Diagnóstico AP + STA: `../scripts/DIAGNOSTICO_AP_STA.md`
- Script de diagnóstico: `../scripts/guardar_diagnostico.sh`
