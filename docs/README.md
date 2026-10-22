# Índice de documentación del proyecto

Proyecto: **lavarropas didáctico** — Arduino + Raspberry Pi + dashboard.

Este índice organiza toda la documentación del repositorio por área y por
perfil de lector (alumno, profesor, desarrollador).

---

## 1. Documentos principales

| Documento | Qué contiene | Para quién |
|---|---|---|
| [`README.md`](README.md) | Visión general, reglas del proyecto y estructura de carpetas | Todos |
| [`DECISIONES.md`](DECISIONES.md) | Decisiones técnicas, problemas y soluciones del desarrollo (incluye P12–P14: reporte de falla, solución adoptada y estado de la placa) | Profesor / desarrollador |
| [`leer.txt`](leer.txt) | Notas rápidas de uso (comandos principales) | Desarrollador |

---

## 2. Guías y propuestas (carpeta `docs/`)

| Documento | Contenido |
|---|---|
| [`GUIA_ALUMNOS_ELECTRICIDAD.md`](docs/GUIA_ALUMNOS_ELECTRICIDAD.md) | Guía de práctica para alumnos de Electricidad Industrial (sin jerga informática, sin comandos Linux) |
| [`ARQUITECTURA_SEGURIDAD_LIMITACIONES.md`](docs/ARQUITECTURA_SEGURIDAD_LIMITACIONES.md) | Arquitectura, seguridad y limitaciones (visión de ingeniería) |
| [`PROPUESTA_CONSOLA_DASHBOARD.md`](docs/PROPUESTA_CONSOLA_DASHBOARD.md) | Propuesta pendiente: consola y IA dentro del dashboard (ttyd) |

---

## 3. Menú del curso (carpeta `curso-raspap/`)

Documentación y recursos del menú `curso-menu` que gestiona RaspAP (AP y
AP+STA) en la Raspberry Pi.

| Documento | Contenido |
|---|---|
| [`curso-raspap/README.md`](curso-raspap/README.md) | Índice y rutas reales de los recursos del menú |
| [`curso-raspap/INSTALACION.md`](curso-raspap/INSTALACION.md) | Requisitos e instalación (paquetes, servicios, verificación) |
| [`curso-raspap/MODOS_AP_APSTA.md`](curso-raspap/MODOS_AP_APSTA.md) | Explicación de modos AP y AP+STA, subredes y conflictos |
| [`curso-raspap/CREDENCIALES.md`](curso-raspap/CREDENCIALES.md) | Cómo generar credenciales locales sin subirlas a Git |
| [`curso-raspap/datos-sensibles/README.md`](curso-raspap/datos-sensibles/README.md) | Nota sobre información sensible no incluida |

### Recursos dentro de `curso-raspap/`

| Recurso | Función |
|---|---|
| `scripts/curso-menu` | Copia del script principal del menú |
| `scripts/curso-raspap-restore` | Restauración DEFAULT al arranque |
| `scripts/servicestart.sh` | Levanta/detiene RaspAP |
| `systemd/curso-raspap-default.service` | Unidad de restauración al boot |
| `templates/` | Plantillas sin claves |
| `rellenar_credenciales.sh` | Genera credenciales locales (carpeta `credenciales-local/`, ignorada) |
| `etc/` | Configuraciones con placeholders |

---

## 4. Portabilidad (Windows, macOS y Android)

| Documento | Contenido |
|---|---|
| [`windows/README_WINDOWS.md`](windows/README_WINDOWS.md) | Instrucciones para usar el proyecto en Windows |
| [`mac/README_MAC.md`](mac/README_MAC.md) | Instrucciones para usar el proyecto en macOS |
| [`android/README_ANDROID.md`](android/README_ANDROID.md) | Dashboard PWA para Android (Web Serial por USB, sin backend) |

---

## 5. Firmware del Arduino

| Ruta | Descripción |
|---|---|
| `firmware/lavarropas_controller_display_error_buzzer/` | Firmware original + reporte JSON (versión principal) |
| `firmware/lavarropas_controller_display_error_buzzer_2enjuagues/` | Variante con dos enjuagues |
| `firmware/original_profesor/` | Firmware original sin modificar (referencia) |

> Los detalles de pines, fases y componentes están explicados en:
> - [`docs/GUIA_ALUMNOS_ELECTRICIDAD.md`](docs/GUIA_ALUMNOS_ELECTRICIDAD.md)
> - [`DECISIONES.md`](DECISIONES.md)

---

## 6. Experimentos (carpeta local, NO subida a Git)

| Ruta | Contenido |
|---|---|
| `ap-sta_scripts_backup/` | Scripts y notas de experimentos AP+STA (sin garantía de utilidad) |

> Ver [`ap-sta_scripts_backup/README.md`](ap-sta_scripts_backup/README.md)
> (disponible localmente; la carpeta está ignorada por Git).

---

## 7. Cómo empezar

1. **Alumno** → leer [`docs/GUIA_ALUMNOS_ELECTRICIDAD.md`](docs/GUIA_ALUMNOS_ELECTRICIDAD.md).
2. **Profesor** → leer [`README.md`](README.md) y [`docs/ARQUITECTURA_SEGURIDAD_LIMITACIONES.md`](docs/ARQUITECTURA_SEGURIDAD_LIMITACIONES.md).
3. **Desarrollador** → leer [`DECISIONES.md`](DECISIONES.md) y [`leer.txt`](leer.txt).
4. **Instalar el menú del curso** → [`curso-raspap/INSTALACION.md`](curso-raspap/INSTALACION.md).

---

*Índice generado automáticamente a partir de la estructura del repositorio.*
