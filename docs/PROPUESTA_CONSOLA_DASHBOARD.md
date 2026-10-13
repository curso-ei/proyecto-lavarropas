# Propuesta: Consola y IA dentro del Dashboard

> Estado: **PENDIENTE DE DECISIÓN** — documento para discutir antes de implementar.

---

## Objetivo

Permitir, desde el dashboard web de la Raspberry, lo siguiente:

1. **Reflejar el menú del proyecto** (`arduino_menu.sh` / `curso-menu`) en una página del dashboard.
2. **Ejecutar una consola/SSH** desde el navegador.
3. **Correr `pi` (IA) dentro del dashboard**, para que se vea bien el texto de la IA (TUI a todo color).

---

## Lo que hay disponible

| Herramienta | Estado |
|---|---|
| `ttyd` (terminal web) | ✅ Disponible en apt (1.7.4) |
| `node` / `npm` | ✅ v22.23.2 |
| `pi` (IA) | ✅ en `/home/rasp/.local/share/pi-node/node-v22.23.2-linux-armv7l/bin/pi` |
| Dashboard | FastAPI en puerto 8000, sirve `static/dashboard.html` |
| Puerto 80 | Ocupado (RaspAP web UI) |
| Puerto 22 (SSH) | Ocupado por el sistema |

---

## La idea clave: `ttyd`

`ttyd` es un programa que **comparte una terminal en el navegador** (usando xterm.js).
Eso resuelve las 3 necesidades:

1. **Menú en el dashboard** → un `ttyd` corriendo `arduino_menu.sh` o `curso-menu`.
2. **SSH desde el dashboard** → un `ttyd` corriendo `ssh localhost` (o un shell).
3. **`pi` (IA) en el dashboard** → un `ttyd` corriendo `pi`, con su TUI bien renderizada.

---

## Arquitectura propuesta

```
Dashboard (puerto 8000)
   ├─ página principal (estado del lavarropas)
   └─ barra de navegación con pestañas:
        ├─ Dashboard
        ├─ Menú Arduino
        ├─ Consola (SSH / shell)
        └─ IA (pi)
```

Cada pestaña de terminal sería un **iframe** a un `ttyd` en su propio puerto:

| Pestaña | Comando | Puerto |
|---------|---------|--------|
| Dashboard | FastAPI actual | 8000 |
| Menú Arduino | `ttyd ... arduino_menu.sh` | 8081 |
| Consola SSH | `ttyd ... ssh localhost` | 8082 |
| IA (pi) | `ttyd ... pi` | 8083 |

---

## Opciones de implementación

### Opción A — Rápida (ttyd + iframes)
- Instalar `ttyd` por apt.
- Crear un script que levante los 3 `ttyd` con contraseña.
- Modificar `dashboard.html` con pestañas/iframes.

**Ventaja:** poco código, usa terminal real (menú y `pi` funcionan perfecto).
**Contra:** hay que manejar 4 procesos (1 FastAPI + 3 `ttyd`).

### Opción B — Integrada (xterm.js + WebSocket en FastAPI)
- Implementar terminal con `xterm.js` + `websockets` + `pty` dentro del propio FastAPI.

**Ventaja:** todo en un solo servidor (puerto 8000).
**Contra:** más código, más frágil; reimplementa lo que `ttyd` ya hace bien.

### Opción C — Mixta (la recomendada)
- Instalar `ttyd` + un **script de arranque único** que levante FastAPI y los 3 `ttyd` juntos.
- Agregar al dashboard una barra de navegación con iframes.
- Autenticación básica en los `ttyd`.

**Ventaja:** terminal real, robusta, con seguridad básica, y arranque simple.

---

## Seguridad (punto crítico)

Exponer una terminal con `sudo` por la web es **muy peligroso** en una red de curso.

Por eso:

- `ttyd` soporta **autenticación básica** (`-c usuario:password`).
- Conviene **no ejecutar root directamente**; usar `sudo` solo cuando se necesite.
- Los puertos 8081-8083 deben quedar **solo en la red local** (que ya es el caso).

---

## Sobre `pi` dentro del dashboard

`pi` es una **TUI** (interfaz de terminal). En un iframe de `ttyd` se ve **perfecto**,
con colores, prompts, etc. Es la forma correcta de correrlo en el navegador
(no tiene API web propia).

---

## Pregunta final (para decidir)

¿Qué alcance querés para la consola web?

1. **Solo el menú y la IA** → `ttyd` con `arduino_menu.sh` y `pi`, sin shell libre.
2. **Menú + IA + shell completo** → con `sudo` disponible (más poderoso, más riesgo).
3. **Otra combinación** (definir cuál).

Y sobre la contraseña:

- ¿Usar una fija tipo `curso` / `rasp`?
- ¿O mostrarte cómo cambiarla fácilmente?

---

## Estado

- [ ] Decidir alcance (opción 1, 2 o 3)
- [ ] Decidir contraseña / método de autenticación
- [ ] Implementar (según la opción elegida)
- [ ] Probar y documentar

*Documento creado para discusión. Nada implementado todavía.*
