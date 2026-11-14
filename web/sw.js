const CACHE = "ballance-wasm-v5";
const ASSETS = [
    "./",
    "./index.html",
    "./game.html",
    "./manifest.webmanifest",
    "./home.js",
    "./icon.svg",
    "./icon-192.png",
    "./icon-512.png",
    "./Player.js",
    "./Player.wasm",
    "./Player.data",
    "./CK2_3D.ini",
    "./Player.ini",
];

const ISOLATION_HEADERS = {
    "Cross-Origin-Opener-Policy": "same-origin",
    "Cross-Origin-Embedder-Policy": "require-corp",
};

self.addEventListener("install", (event) => {
    event.waitUntil((async () => {
        const cache = await caches.open(CACHE);
        for (const asset of ASSETS) {
            try {
                await cache.add(asset);
            } catch (error) {
                console.warn("Ballance precache skipped:", asset, error);
            }
        }
        await self.skipWaiting();
    })());
});

self.addEventListener("activate", (event) => {
    event.waitUntil(
        caches.keys()
            .then((keys) => Promise.all(keys.filter((key) => key !== CACHE).map((key) => caches.delete(key))))
            .then(() => self.clients.claim())
    );
});

self.addEventListener("fetch", (event) => {
    const { request } = event;
    if (request.method !== "GET" || request.headers.has("range"))
        return;
    const url = new URL(request.url);
    if (url.origin !== self.location.origin)
        return;

    event.respondWith(
        caches.match(request, { ignoreSearch: true }).then((cached) => {
            if (cached) {
                const headers = new Headers(cached.headers);
                for (const [name, value] of Object.entries(ISOLATION_HEADERS))
                    headers.set(name, value);
                return new Response(cached.body, { status: cached.status, headers });
            }
            return fetch(request).then((response) => {
                if (response.ok) {
                    const copy = response.clone();
                    caches.open(CACHE).then((cache) => cache.put(request, copy));
                }
                return response;
            });
        })
    );
});
