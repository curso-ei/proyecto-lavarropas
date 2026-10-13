# Credenciales locales — Cómo generarlas

El repositorio **no contiene claves reales**. Para que los archivos de
configuración funcionen en una instalación real, hay que rellenarlos con los
valores correctos usando el script `rellenar_credenciales.sh`.

---

## 1. Qué información es sensible

| Dato | Archivo | Tipo |
|---|---|---|
| Contraseña del WiFi cliente | `wpa_supplicant.conf` | `psk=` |
| Contraseña del AP de RaspAP | `hostapd.conf` | `wpa_passphrase=` |
| Contraseña del admin web de RaspAP | `raspap.auth` | hash bcrypt |
| API key de DeepSeek | `.bashrc` | `DEEPSEEK_API_KEY=` |

---

## 2. Estructura

```
curso-raspap/
├── templates/           ← plantillas SIN claves (se suben a Git)
├── etc/                 ← config con placeholders (se suben a Git)
├── credenciales-local/  ← se generan acá las claves reales (NO se sube)
└── rellenar_credenciales.sh ← genera los archivos reales
```

---

## 3. Cómo generar las credenciales

```bash
cd /home/rasp/proyecto-pi/curso-raspap
./rellenar_credenciales.sh
```

El script pregunta qué generar:

| Opción | Genera |
|---|---|
| 1 | `credenciales-local/wpa_supplicant.conf` |
| 2 | `credenciales-local/hostapd.conf` |
| 3 | `credenciales-local/raspap.auth` |
| 4 | `credenciales-local/bashrc.deepseek` |
| 5 | Todos |

### Ejemplo (opción 1)

```
--- WiFi cliente ---
SSID de la red WiFi: MiRed
Contraseña (psk) del WiFi: ******
OK: .../credenciales-local/wpa_supplicant.conf
```

---

## 4. Dónde quedan los archivos generados

Los archivos con valores reales van a:

```
curso-raspap/credenciales-local/
```

Esa carpeta está **ignorada por Git** (ver `.gitignore` del proyecto), así que
nunca se sube a GitHub.

---

## 5. Sobre el hash de `raspap.auth`

El archivo `raspap.auth` guarda el usuario y un **hash bcrypt** de la
contraseña del panel web de RaspAP:

```
admin
$2y$12$...
```

- No es la contraseña en texto plano.
- El script genera el hash con `php password_hash` (necesita `php-cli`).
- Si no hay `php-cli`, deja el placeholder `__RASPAP_ADMIN_HASH__` para
  completar manualmente.

---

## 6. Archivos de ejemplo

Actualmente en `credenciales-local/` hay un `wpa_supplicant.conf` de **ejemplo** con
datos ficticios (`RedPrueba` / `ClaveDePrueba123`). Se sobrescribe al ejecutar
la opción 1 con datos reales.

---

## 7. Importante

- **No** subir los archivos de `credenciales-local/` a GitHub.
- **No** pegar claves reales en `templates/` ni en `etc/` versionados.
- Para regenerar el hash del admin, usar `php -r "echo password_hash('TU_CLAVE', PASSWORD_BCRYPT);"`.
