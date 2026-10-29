# Problema AP+STA (RaspAP) — Registro y causa raíz

**Fecha:** 2026-10-28
**Equipo:** Raspberry Pi (RaspAP)

---

## 1. Síntoma original

En modo **AP+STA** no había internet. El usuario conectó un access point por cable a `eth0` como solución temporal.

---

## 2. Diagnóstico (causa raíz definitiva)

### Lo que NO era
- No era contraseña incorrecta.
- No era conflicto de canales (AP y cliente en canal 6, correcto).
- No era el AP: `uap0`/RaspAP funcionaba siempre (hostapd, dnsmasq, dhcpcd son servicios systemd independientes).

### Lo que SÍ era

El worker "Activar AP+STA" (`/usr/local/sbin/curso-menu --apsta-worker`) corre como
**servicio transitorio** (`systemd-run --no-block --collect --unit=curso-raspap-apsta.service`).

Dentro de ese worker se lanza el wpa_supplicant de producción en background:

```bash
/sbin/wpa_supplicant -i wlan0 -c "$config" -B
```

Ese proceso queda **dentro del cgroup** del servicio transitorio. Cuando el worker
termina (después de verificar OK), **systemd limpia la unidad y, con
`KillMode=control-group` (default), mata a todos los procesos del cgroup**,
incluido el wpa_supplicant que acababa de crear.

**Resultado:** el "OK" era real en el instante de la verificación, pero duraba
~1 segundo. Verifica → escribe OK → sale → mata wpa_supplicant → se cae la conexión.

---

## 3. Evidencia (segundo a segundo, del diagnóstico 20261028-182846)

| Hora | Evento |
|---|---|
| 18:25:52 | Arranca `curso-raspap-apsta.service` |
| 18:26:00 | Prueba cliente OK: `Personal-137`, señal -68 dBm |
| 18:26:23 | Arranca wpa_supplicant de producción en wlan0 (`-B`) |
| 18:26:25 | Levanta RaspAP/AP (uap0) |
| 18:26:55 | dhcpcd obtiene `192.168.0.109` en wlan0 |
| 18:26:56 | **Verificación OK** → `last-result.txt` = "Estado: OK" |
| 18:26:57 | Worker sale → `curso-raspap-apsta.service: Deactivated successfully` |
| 18:26:57 | **`wlan0: carrier lost - roaming`** (mismo segundo) |
| 18:27:02 | Lease DHCP expira, no renueva → cae a `169.254.227.73` (link-local) |
| 18:28 | `ps`: solo existe wpa_supplicant global (`-u -s`); el de wlan0 ya no existe |

### Confirmación en vivo (sesión anterior)
A las 19:41 solo existía `wpa_supplicant -u -s` (global, sin interfaz). wlan0 estaba
"Not connected" con IP 169.254. Al lanzar manualmente desde la terminal:
`wpa_supplicant -i wlan0 -c /etc/wpa_supplicant/wpa_supplicant.conf -B`, el proceso
quedó **fuera del cgroup del worker**, sobrevivió, y la conexión quedó estable
(`wpa_state=COMPLETED`, IP 192.168.0.109).

---

## 4. Solución definitiva IMPLEMENTADA (2026-10-28/29)

El wpa_supplicant de producción **no debe ser hijo del worker**. Se eligió la
**Opción A (servicio systemd dedicado)**: usar la plantilla por-interfaz que ya
viene en el sistema (`wpa_supplicant-nl80211@.service`) en lugar de `-B`.

### Cambios en `/usr/local/sbin/curso-menu`

1. **Nueva variable** `WPA_PROD_UNIT="wpa_supplicant-nl80211@wlan0.service"`.
2. **Nueva función** `apsta_stop_production_wpa()`: detiene y resetea la unidad
   y remata procesos sueltos `wpa_supplicant.*-iwlan0` (sin tocar el global `-u -s`).
3. **`apsta_start_production_wpa()` reescrita:**
   - Ya NO usa `wpa_supplicant -B`.
   - Copia la config a `/etc/wpa_supplicant/wpa_supplicant-nl80211-wlan0.conf`
     (lo que espera la plantilla con `%I=wlan0`).
   - Arranca con `systemctl start wpa_supplicant-nl80211@wlan0.service`.
4. **Limpieza al restaurar DEFAULT / reiniciar servicios:**
   - `apsta_restore_default()` llama `apsta_stop_production_wpa` antes de levantar el AP.
   - `apsta_restart_services()` también lo llama.
   - `client_stop_ap()` y `client_restore_default()` también lo llaman.
5. **Bug corregido:** en `apsta_worker`, el fallo de asociación usaba
   `kill -9 "$APSTA_WPA_PID"` (variable no definida). Ahora llama a
   `apsta_stop_test_wpa` correctamente.
6. **Sincronización:** al restaurar la config completa de redes, también se
   actualiza `/etc/wpa_supplicant/wpa_supplicant-nl80211-wlan0.conf` para que
   `wpa_cli reconfigure` cargue todas las redes.
7. **Aplicar perfil en crudo (`apply_profile`):** ahora también detiene la
   unidad wpa de producción (para no dejarla compitiendo por wlan0 si se aplica
   DEFAULT o NORMAL directamente desde "Administrar configuraciones").
8. **Restaurador de arranque (`curso-raspap-restore`):** detiene la unidad
   wpa de producción antes de extraer DEFAULT, para que en el boot no quede
   una instancia cliente compitiendo con hostapd.
9. **Nueva opción de menú (opción 9 del menú RaspAP):**
   "Servicio wpa_supplicant (cliente WiFi)". Permite:
   - Arrancar/detener la unidad ahora.
   - Habilitar/deshabilitar al arranque.
   - Ver el estado completo.
   - Función auxiliar `wpa_prod_ensure_config()` que crea la config
     `wpa_supplicant-nl80211-wlan0.conf` si no existe.
10. **Aviso de persistencia en "Activar AP + STA" (opción 3):**
    antes de elegir la red, se informa que el modo AP+STA no sobrevive a
    un reinicio, y se indican los pasos para hacerlo persistente:
    habilitar el servicio wpa (opción 9) y deshabilitar el servicio DEFAULT.

### Drop-in de auto-recuperación

Creado `/etc/systemd/system/wpa_supplicant-nl80211@.service.d/restart.conf`:

```ini
[Service]
Restart=on-failure
RestartSec=3
```

Esto resuelve también el pendiente 6.2 (reconexión automática): si wpa_supplicant
se cae por un micro-corte de radio, systemd lo relanza solo.

### Por qué Opción A y no B/C
- **A (elegida):** robusta, persistente, con `Restart=`, y usa una plantilla que
  ya existe en el sistema (menos código propio).
- B (`systemd-run` transitorio): más simple pero efímera y sin auto-reinicio.
- C (`--scope`): solo despega del cgroup, no gestiona el ciclo de vida.

### Backups
- `curso-menu.backup-pre-wpafix` (hash b12494d1…)
- Script modificado: hash 8fb32ce3… (incluye opción 9 y aviso de persistencia)
- `curso-raspap-restore`: hash 51c0a35f…
- Sintaxis verificada: `bash -n` OK en ambos.
- **Pruebas realizadas en casa (28/10 noche):**
  - La unidad `wpa_supplicant-nl80211@wlan0.service` arranca y queda `active`;
    `wpa_cli` reporta `wpa_state=SCANNING` (esperado: no existe la red del curso).
  - La unidad se detiene limpiamente; el wpa global (`-u -s`) sigue vivo.
  - `wpa_prod_ensure_config()` crea correctamente la config por-interfaz.
  - Menú RaspAP muestra las opciones 1-9 + 0.

### Comportamiento con los otros modos del menú

| Acción del menú | ¿Qué pasa con la unidad wpa de producción? |
|---|---|
| **Activar AP + STA** | La arranca (`systemctl start`) y la deja corriendo al terminar (correcto). |
| **Activar solo cliente WiFi** | La arranca y la deja corriendo (correcto). |
| **Aplicar DEFAULT (solo AP) en crudo** | `apply_profile` la **detiene** (nuevo). |
| **Aplicar NORMAL en crudo** | `apply_profile` la **detiene**; para reactivarla hay que usar el flujo seguro "Activar AP + STA" (nuevo). |
| **Restaurar DEFAULT por error del worker** | `apsta_restore_default` la **detiene** (nuevo). |
| **Reinicio de servicios (test wifi / prueba)** | `apsta_restart_services` la **detiene** (nuevo). |
| **Arranque con servicio DEFAULT activo** | `curso-raspap-restore` la **detiene** antes de hostapd (nuevo). |
| **Solo-cliente: limpiar/restaurar** | `client_stop_ap` y `client_restore_default` la **detienen** (nuevo). |
| **Menú: Servicio wpa_supplicant (opción 9)** | Control manual: start/stop/enable/disable (nuevo). |

Conclusión: la unidad wpa de producción **solo queda corriendo en los dos modos
que realmente la necesitan** (AP+STA y solo-cliente). En todos los caminos hacia
"solo AP/DEFAULT" se detiene, evitando competencia por wlan0. Adicionalmente, el
usuario puede gestionarla manualmente desde la opción 9 del menú RaspAP.

---

## 4bis. ¿Por qué RaspAP no sufría este bug?

**Contexto de la pregunta del usuario:** RaspAP (el web UI) también lanza
`wpa_supplicant -B` desde su PHP, entonces ¿por qué no tenía el mismo problema?

**Respuesta:**
- RaspAP lanza el `wpa_supplicant -B` desde un request HTTP que corre bajo
  **php-fpm** (un servicio de **larga vida**). Cuando el request termina,
  el `wpa_supplicant` queda huérfano pero **no muere** (se re-parenta a PID 1
  o al master de php-fpm).
- El menú-curso lanza el worker con **`systemd-run`** (servicio **transitorio**
  con `KillMode=control-group`). Al terminar el worker, systemd **mata a todos
  los procesos del cgroup**, incluido el `wpa_supplicant -B`.
- Es decir: RaspAP "se salva" **por accidente** (no usa un cgroup transitorio),
  no porque su código sea correcto. El sistema ya tenía la herramienta correcta
  (`wpa_supplicant-nl80211@.service`) pero nadie la usaba.

---

## 5. Estado actual (casa, 28/10 noche)

| Interfaz | Modo | Estado | IP |
|---|---|---|---|
| `eth0` | cable | ✅ Internet OK | 192.168.1.36 |
| `uap0` | AP (RaspAP) | ✅ Activo | 192.168.50.1 |
| `wlan0` | STA (cliente) | ⬇️ DOWN — esperado (no existe `Personal-137` en casa) | — |

Nota: la unidad `wpa_supplicant-nl80211@wlan0.service` arranca y queda en
`SCANNING` (sin red disponible), pero se detuvo para dejar el sistema limpio.

---

## 6. Pendientes secundarios

### 6.1 Doble ruta por defecto (cuando hay eth0 + wlan0 en la misma subred)
En el curso, con `eth0` al access point y `wlan0` cliente, ambas quedan en
`192.168.0.0/24` con dos rutas default `via 192.168.0.1`. Genera ambigüedad de
ruteo/ARP. En AP+STA conviene **una sola salida**:
- A: cliente WiFi `wlan0` → Personal-137, sin cable.
- B: solo `eth0` → access point (AP normal, sin STA).

### 6.2 Reconexión automática de wlan0
**RESUELTO con el drop-in `Restart=on-failure`** (ver sección 4): si wpa_supplicant
se cae, systemd lo relanza. El detalle del hook de dhcpcd
(`10-wpa_supplicant` en `/usr/share/dhcpcd/hooks/` pero no en
`/usr/lib/dhcpcd/dhcpcd-hooks/`) queda como nota histórica; ya no es el mecanismo
que se necesita para reconectar.

---

## 7. Configuración relevante (referencia)

- **hostapd:** `/etc/hostapd/hostapd.conf` → interface `uap0`, SSID `RaspAP`, canal 6.
- **wpa_supplicant:** `/etc/wpa_supplicant/wpa_supplicant.conf` → red `Personal-137`.
- **dhcpcd:** `/etc/dhcpcd.conf` → `uap0` estática 192.168.50.1/24, `nohook wpa_supplicant`.
- **dnsmasq:** `/etc/dnsmasq.d/090_*.conf` → DHCP en uap0 (192.168.50.50-150) y wlan0 (10.3.141.x).
- **RaspAP config:** `/etc/raspap/hostapd.ini` → `WifiAPEnable=1`, `WifiManaged=wlan0`.
- **servicestart.sh:** `/etc/raspap/hostapd/servicestart.sh` → termina con `wpa_cli -i wlan0 reassociate`.
- **NAT/forward:** `ip_forward=1`, MASQUERADE activa para 192.168.50.0/24.
- **Worker AP+STA:** `/usr/local/sbin/curso-menu` (funciones `apsta_*`, en especial `apsta_start_production_wpa`).
- **Config por-interfaz (nueva):** `/etc/wpa_supplicant/wpa_supplicant-nl80211-wlan0.conf`.
- **Unidad systemd (nueva):** `wpa_supplicant-nl80211@wlan0.service` (+ drop-in `restart.conf`).

---

## 8. Comandos útiles de diagnóstico

```bash
# Estado de asociación STA
sudo wpa_cli -i wlan0 status

# Estado del AP
sudo hostapd_cli status

# Interfaces e IPs
ip -br addr && ip route

# Internet por interfaz
ping -I wlan0 -c 3 8.8.8.8
ping -I eth0 -c 3 8.8.8.8

# Procesos wpa_supplicant (ver si hay instancia por-interfaz)
ps aux | grep wpa_supplicant

# Guardar diagnóstico (menú-curso)
sudo ./scripts/guardar_diagnostico.sh
```
