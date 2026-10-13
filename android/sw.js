/* Service worker del dashboard Android (PWA).
   Cachea el dashboard, el manifest y los iconos para que funcione sin
   conexión una vez instalado. Estrategia: cache-first.
*/
const CACHE = "lavarropas-monitor-v1";
const RECURSOS = [
  "./dashboard.html",
  "./manifest.webmanifest",
  "./icon-192.png",
  "./icon-512.png",
];

self.addEventListener("install", (event) => {
  event.waitUntil(
    caches.open(CACHE).then((cache) => cache.addAll(RECURSOS))
  );
  self.skipWaiting();
});

self.addEventListener("activate", (event) => {
  event.waitUntil(
    caches.keys().then((keys) =>
      Promise.all(keys.filter((k) => k !== CACHE).map((k) => caches.delete(k)))
    )
  );
  self.clients.claim();
});

self.addEventListener("fetch", (event) => {
  if (event.request.method !== "GET") return;
  event.respondWith(
    caches.match(event.request).then((resp) => {
      return (
        resp ||
        fetch(event.request).then((red) => {
          const clon = red.clone();
          caches.open(CACHE).then((cache) => cache.put(event.request, clon));
          return red;
        })
      );
    })
  );
});
