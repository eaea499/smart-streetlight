const CACHE_NAME = "smart-streetlight-ui-v1";
const BASE_PATH = new URL(".", self.registration.scope).pathname;
const APP_SHELL = [BASE_PATH, `${BASE_PATH}manifest.webmanifest`, `${BASE_PATH}icon.svg`];

self.addEventListener("install", (event) => {
  event.waitUntil(caches.open(CACHE_NAME).then((cache) => cache.addAll(APP_SHELL)));
});

self.addEventListener("activate", (event) => {
  event.waitUntil(
    caches
      .keys()
      .then((keys) => Promise.all(keys.filter((key) => key !== CACHE_NAME).map((key) => caches.delete(key))))
  );
});

self.addEventListener("fetch", (event) => {
  const request = event.request;
  if (request.method !== "GET" || request.url.includes("/api/")) {
    return;
  }

  event.respondWith(
    caches.match(request).then((cached) => {
      return cached || fetch(request);
    })
  );
});
