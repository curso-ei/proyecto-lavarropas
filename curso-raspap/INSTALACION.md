# Instalación y requisitos — Menú del curso (RaspAP)

Documento de referencia para instalar y configurar todo lo necesario para
ejecutar el menú del curso (`curso-menu`) con los modos **AP** y **AP + STA**.

> Última verificación: 2026-10-09
> Hardware: Raspberry Pi 3 Model B Rev 1.2
> SO: Raspbian GNU/Linux 13 (trixie)
> RaspAP: 3.5.5

---

## 1. Requisitos de hardware

- Raspberry Pi 3 Model B (o superior).
- Tarjeta microSD con Raspberry Pi OS (Trixie en este caso).
- Fuente de alimentación adecuada.
- Opcional: cable Ethernet (RJ45) para tener internet/administración durante la
  configuración inicial.

---

## 2. Requisitos de software (paquetes)

### 2.1. Paquetes instalados por RaspAP

El instalador oficial de RaspAP instala estos paquetes clave:

| Paquete | Versión verificada | Función |
|---|---|---|
| `hostapd` | 2:2.10-24+rpt1 | Punto de acceso WiFi |
| `dnsmasq` | 2.91-1+deb13u2 | DHCP y DNS para los clientes del AP |
| `dhcpcd5` | 1:10.1.0-11+deb13u4 | Gestor de red (IP, rutas) |
| `lighttpd` | 1.4.79-1 | Servidor web de RaspAP |
| `php-cgi` / `php-cli` | PHP 8.4.24 | Backend web y generación de hashes |
| `iw` | 6.9-1 | Gestión de interfaces WiFi |
| `iptables` | 1.8.11-2 | NAT y firewall |
| `iptables-persistent` | 1.0.23 | Persistencia de reglas |
| `jq` | 1.7.1-6+deb13u3 | Procesado JSON |
| `vnstat` | 2.13-1 | Monitor de tráfico |
| `qrencode` | 4.1.1-2 | Códigos QR |
| `wpa_supplicant` | (binario en `/usr/sbin/`) | Cliente WiFi (modo STA) |

### 2.2. Paquetes extra necesarios para el menú del curso

Estos son necesarios para funciones específicas del menú:

| Paquete / herramienta | Uso |
|---|---|
| `php-cli` | Generar hash bcrypt para el admin de RaspAP (script `rellenar_credenciales.sh`) |
| `systemd` | Servicios y `systemd-run` para procesos en segundo plano |
| `runuser` | Ejecutar el menú Arduino como usuario `rasp` |
| `tar`, `gzip` | Manejo de perfiles (`config.tar.gz`) |
| `wpa_passphrase` | Agregar redes WiFi desde el menú |

### 2.3. Instalación de RaspAP

El menú asume que RaspAP fue instalado con el instalador oficial. En este
proyecto existe un script de ayuda:

```bash
sudo /home/rasp/instalar_raspap.sh
```

Ese script:

1. Verifica la red y `eth0`.
2. Actualiza el sistema (`apt update && apt full-upgrade`).
3. Ejecuta el instalador oficial: `curl -sL https://install.raspap.com | bash`.
4. Verifica que `NetworkManager` quede **deshabilitado** y `dhcpcd` activo.

> **Importante:** en este sistema el gestor de red es **dhcpcd**, no
> NetworkManager. RaspAP usa dhcpcd para las IPs y rutas.

---

## 3. Servicios involucrados

| Servicio | Estado esperado | Función |
|---|---|---|
| `hostapd` | `active` y `enabled` | Punto de acceso |
| `dnsmasq` | `active` | DHCP/DNS del AP |
| `dhcpcd` | `active` y `enabled` | IPs y rutas |
| `raspapd` | `inactive` (se levanta a demanda) | Daemon de control de RaspAP |
| `raspap-network-activity@wlan0.service` | `active` | Monitor de actividad de red |
| `wpa_supplicant` | `active` | Cliente WiFi |
| `curso-raspap-default.service` | `enabled` | Restaura DEFAULT al arrancar |
| `tailscaled` | `active` | Acceso remoto (opcional) |

> `tailscaled` es opcional. No es necesario para el funcionamiento del menú.

---

## 4. Verificación rápida post-instalación

```bash
# Sistema
cat /etc/os-release | grep PRETTY_NAME

# Paquetes clave
dpkg-query -W -f='${Package} ${Version}\n' hostapd dnsmasq dhcpcd5 lighttpd php-cli iptables-persistent

# Servicios
systemctl is-active hostapd dnsmasq dhcpcd
systemctl is-enabled hostapd dhcpcd curso-raspap-default.service

# Red
ip -4 route
ip -br addr
```

---

## 5. Referencias

- Documentación del menú: `curso-raspap/README.md`
- Modos AP y AP+STA: ver `docs/` del proyecto o `curso-raspap/README.md`
- Generar credenciales: `curso-raspap/rellenar_credenciales.sh`
- Scripts de diagnóstico: `ap-sta_scripts_backup/` (experimentos, no oficiales)
