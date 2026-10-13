# Documentación del menú del curso (curso-menu)

Esta carpeta contiene una **copia de referencia** de los recursos esenciales
del menú del curso (`curso-menu`) y su documentación.

---

## Estructura

```
curso-raspap/
├── INSTALACION.md                 ← requisitos e instalación
├── MODOS_AP_APSTA.md              ← explicación de modos AP y AP+STA
├── CREDENCIALES.md                ← cómo generar credenciales locales
├── README.md                      ← este documento
├── rellenar_credenciales.sh       ← genera credenciales locales
├── scripts/                       ← copias de los scripts reales
│   ├── curso-menu
│   ├── curso-raspap-restore
│   └── servicestart.sh
├── systemd/                       ← unidad de restauración al arranque
│   └── curso-raspap-default.service
├── etc/                           ← configs con placeholders
│   ├── dhcpcd.conf
│   ├── hostapd/hostapd.conf
│   ├── dnsmasq.d/...
│   ├── default/...
│   └── raspap/...
├── profiles/                      ← info.txt de perfiles (sin claves)
├── templates/                     ← plantillas para credenciales
└── credenciales-local/            ← credenciales generadas (ignorado por Git)
```

---

## Rutas reales (producción)

La copia de esta carpeta **no** reemplaza a los archivos reales. Los archivos
que usa el sistema están en:

| Recurso | Ruta real |
|---|---|
| Script principal | `/usr/local/sbin/curso-menu` |
| Restauración DEFAULT | `/usr/local/sbin/curso-raspap-restore` |
| Servicio de arranque | `/etc/systemd/system/curso-raspap-default.service` |
| Levantar/parar RaspAP | `/etc/raspap/hostapd/servicestart.sh` |
| Perfiles | `/etc/curso-raspap/profiles/{default,normal}/` |
| Config dhcpcd | `/etc/dhcpcd.conf` |
| Config RaspAP | `/etc/raspap/hostapd.ini`, `/etc/raspap/networking/defaults.json` |
| Config dnsmasq | `/etc/dnsmasq.d/090_raspap.conf`, `/etc/dnsmasq.d/090_uap0.conf` |
| Config hostapd | `/etc/hostapd/hostapd.conf` |
| Redes WiFi | `/etc/wpa_supplicant/wpa_supplicant.conf` |

---

## Qué NO se incluye (información sensible)

- `/etc/raspap/raspap.auth` (hash de admin)
- `/etc/wpa_supplicant/wpa_supplicant.conf` (claves WiFi)
- `/etc/hostapd/hostapd.conf` completo (passphrase reemplazada por placeholder)
- `DEEPSEEK_API_KEY` de `/home/rasp/.bashrc`
- `config.tar.gz` de los perfiles (pueden contener claves WiFi)

Ver `CREDENCIALES.md` para saber cómo generar estas claves.

---

## Flujo típico de uso

1. **Instalar** (ver `INSTALACION.md`).
2. **Generar credenciales** (ver `CREDENCIALES.md`).
3. **Elegir modo** (ver `MODOS_AP_APSTA.md`):
   - AP (DEFAULT): siempre accesible, internet por Ethernet.
   - AP + STA (NORMAL): internet por WiFi cliente + AP virtual.
4. **Activar AP + STA** desde el menú: `sudo curso-menu`.
