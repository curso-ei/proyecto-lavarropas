# Dashboard para Android (sin PC, sin Raspberry, sin backend)

Esta versión corre **sola en el celular**: el navegador lee el Arduino
directamente por **USB OTG** usando la **Web Serial API**. No hace falta
`main.py`, ni Python, ni PC, ni Raspberry, ni root.

```
   Arduino Uno                       Android (Chrome/Edge)
  ┌────────────┐   USB OTG        ┌──────────────────────────┐
  │  firmware  │ ───────────────► │  dashboard.html          │
  │  (.ino)    │  líneas JSON     │  (JS lee el serial 9600) │
  └────────────┘                   └──────────────────────────┘
```

El **firmware del Arduino no cambia**: sigue mandando el mismo JSON por
Serial, igual que para Windows, macOS y Raspberry.

> ⚠ **Solo funciona con Arduino físico.** Esta versión para Android **no corre
> simulación** ni tiene simulador: Web Serial lee un dispositivo USB real
> conectado por OTG. Sin un Arduino físico, el panel se ve pero no muestra
> datos del ciclo.

---

## 1. Requisitos

- **Android** con soporte USB host (OTG). Casi todos los Android modernos lo
  tienen; se puede verificar con una app tipo "USB OTG Checker".
- **Chrome o Edge** en el celular (Web Serial API no está en Firefox).
- Un **cable USB OTG** (o adaptador OTG) para conectar el Arduino al celular.
- Un **Arduino Uno** (oficial o clon CH340) con el firmware ya grabado.
- Internet **solo la primera vez**, para abrir el dashboard por HTTPS.

> **No hace falta root**, ni desbloquear el bootloader, ni modo desarrollador,
> ni Python, ni PC, ni Raspberry.

---

## 2. Cómo abrirlo (dos formas)

### Por qué hace falta "abrir por HTTP/HTTPS" (no es por Python)

El dashboard es **100 % HTML/JS** y corre en el Chrome del celular: **no usa
Python para nada**. El único detalle es que la Web Serial API solo funciona en
un **contexto seguro** (HTTPS o `localhost`). Por eso no se puede abrir como
archivo suelto (`file://`): hay que publicarlo en una URL HTTPS **una sola
vez**. Después, al instalarlo como app, queda cacheado y ya no hace falta la
URL.

### Opción A — Publicarlo en un hosting estático (recomendada, sin Python)

Subir los archivos de `android/` a cualquier hosting estático gratuito con
HTTPS:

- **GitHub Pages** (este repo ya está en GitHub): *Settings → Pages → Deploy
  from branch → main*, y usar la URL de la carpeta `android/`.
- Netlify, Vercel, Cloudflare Pages, etc.

Luego, en el celular, abrir esa URL con **Chrome**. No hay que encender
ninguna PC ni instalar Python: solo hace falta internet la primera vez.

### Opción B — Instalar como app (PWA)

Una vez abierta la URL HTTPS:

1. Abrir el dashboard en Chrome.
2. Menú de Chrome (⋮) → **"Añadir a pantalla de inicio"** / **"Instalar app"**.
3. Queda un icono "Lavarropas" en el home, a pantalla completa y cacheado.

### Opción C — Probarla en red local (alternativa, solo si hay una PC a mano)

Si se quiere probar sin subir a internet, se puede servir la carpeta desde una
PC con **cualquier servidor de archivos estáticos** (no es el backend: solo
entrega los archivos). Un ejemplo rápido con Python, **si la PC lo tiene**:

```bash
cd proyecto-pi/android
python3 -m http.server 8000
```

Y en el celular: `http://IP_de_la_PC:8000/dashboard.html`

> ⚠ Acá `python3 -m http.server` **no es el backend del lavarropas**: es solo
> un repartidor de archivos para que Chrome los baje por HTTP. El Arduino se
> sigue leyendo directo desde el celular. Si la PC no tiene Python, sirve
> cualquier otro servidor estático (o GitHub Pages, como en la Opción A).

---

## 3. Conexión al Arduino

1. Conectar el **Arduino por USB OTG** al celular.
2. Abrir el dashboard y tocar **"🔌 Conectar Arduino"**.
3. Chrome muestra un diálogo con los dispositivos USB: elegir el Arduino.
4. Listo. El panel muestra "🟢 Arduino conectado" y empieza a actualizarse.

Para soltar el puerto, tocar el mismo botón (ahora dice "Desconectar").

> ⚠ El **Monitor Serie del Arduino IDE** y el dashboard **no pueden usar el
> puerto a la vez** (aplica si el Arduino está enchufado a una PC).

---

## 4. Si no detecta el Arduino

- Verificar que el cable sea **OTG** y no un cable de carga sin datos.
- Probar en **Chrome** (no Firefox).
- Revisar que el Arduino esté alimentado (en general el propio USB del celular
  lo alimenta; si no, alimentar el Arduino por su jack externo).
- Si el celular no lista el dispositivo, probar con la app "USB OTG Checker"
  para confirmar que soporta USB host.
- Si Chrome no muestra el diálogo, tocar de nuevo "Conectar Arduino".

---

## 5. Limitaciones de esta versión

- **Solo Chrome/Edge en Android** (Web Serial no está en Firefox ni en iOS).
- Es un dashboard de **solo lectura**, igual que las versiones de PC/Mac.
- La PWA **no puede leer el serial** si se abre como archivo local
  (`file://`): hay que abrirla por HTTPS (ver sección 2).
- Para **varios celulares a la vez**, esta opción no sirve (cada uno
  necesitaría su propio Arduino). Ahí conviene la versión con backend
  (Raspberry/PC) y los celulares solo como pantalla.

---

## 6. Comparación con las otras versiones

| | Raspberry / PC / Mac | Android (esta) |
|---|---|---|
| ¿Backend Python? | Sí (`main.py`) | **No** |
| ¿Quién lee el serial? | Python (`pyserial`) | El navegador (Web Serial) |
| ¿Necesita red WiFi? | Sí (para el navegador) | No (USB directo; solo internet la primera vez para abrir la URL) |
| ¿Root? | No | No |
| Firmware del Arduino | No cambia | No cambia |

---

## 7. Archivos

| Archivo | Función |
|---|---|
| `dashboard.html` | Dashboard completo + lógica Web Serial |
| `manifest.webmanifest` | Datos de la PWA (nombre, icono, pantalla completa) |
| `sw.js` | Service worker (cache offline) |
| `icon-192.png` / `icon-512.png` | Iconos de la app |

---

*Versión Android elaborada con fines educativos.*
