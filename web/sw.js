// Keeps the served page working with the network gone.
//
// The engine is 400-odd KB of WebAssembly and there is no server behind it once
// it has arrived, so a page that has been visited once has everything it needs
// to run on a train. All that is missing is somewhere to keep it.
//
// It is network-first, deliberately, and not the usual cache-first:
//
//   online  - the network answers and the cache is only written, never read, so
//             a deploy is live the moment it lands. A cache-first worker would
//             hand back yesterday's page to everyone who had visited before,
//             and the person most often caught by that is whoever just pushed
//             the fix and cannot see it.
//   offline - the fetch throws and the cache answers instead.
//
// The cost is that a return visit is no faster than a first one. That is the
// right trade here: this page is deployed on every push, and being fast was
// never the reason to cache it.

const CACHE = "jazz-learning-app";

// Claim the page on the first load rather than the second, so a visitor who
// goes offline immediately after arriving is still covered.
self.addEventListener("install", (event) => event.waitUntil(self.skipWaiting()));
self.addEventListener("activate", (event) => event.waitUntil(self.clients.claim()));

self.addEventListener("fetch", (event) => {
  const request = event.request;

  // Only plain page loads and their assets. A POST has no business being
  // replayed from a cache, and neither has anything that is not http.
  if (request.method !== "GET" || !request.url.startsWith("http")) return;

  event.respondWith((async () => {
    try {
      const response = await fetch(request);

      // Opaque responses - the PDF library from its CDN - have a status of 0
      // and are still worth keeping: they cannot be read here, only replayed,
      // which is exactly what is wanted. A partial response is not.
      if (response.status !== 206) {
        const copy = response.clone();
        caches.open(CACHE).then((cache) => cache.put(request, copy)).catch(() => {});
      }

      return response;
    } catch (offline) {
      const cached = await caches.match(request);

      if (cached) return cached;

      // A navigation to some other path with nothing cached for it still wants
      // the app rather than a browser error page.
      if (request.mode === "navigate") {
        const page = await caches.match("index.html") || await caches.match("./");
        if (page) return page;
      }

      throw offline;
    }
  })());
});
