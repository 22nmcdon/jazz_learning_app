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

// What a visit needs and nothing else: the page, and the engine behind it.
// Paths are relative, so they resolve against wherever this is served from -
// a project site lives under a path, not at a domain root.
const ESSENTIALS = ["./", "index.html", "jazz-engine.js"];

/* Fetched on install rather than left to the fetch handler below, and that is
   the whole difference between working offline and only seeming to.

   A worker is registered once the page has loaded, which is already after the
   page and the engine have been fetched - so those two requests never pass
   through it and never reach the cache. A first visit would leave the cache
   empty, and a visitor who went offline afterwards would get nothing. Worse,
   it looks like it works: the browser's own HTTP cache will answer a reload
   through the worker's fetch often enough to pass a test.

   So the first thing this worker does is go and get them itself. */
self.addEventListener("install", (event) => event.waitUntil((async () => {
  const cache = await caches.open(CACHE);

  // One at a time, and failures are survivable: a worker that refuses to
  // install because one file was slow is worse than one that caches two of
  // three now and the third on the next visit.
  await Promise.all(ESSENTIALS.map(async (path) => {
    try {
      // The browser's own cache is allowed to answer these, and usually does:
      // the page has this second ago fetched every one of them, and forcing a
      // second trip to the network would download the 400-odd KB engine twice
      // on a first visit to put a copy of it in a different cache. A slightly
      // old fallback is no loss when the network always wins while it is there.
      const response = await fetch(path);

      if (response.ok) await cache.put(path, response);
    } catch (unreachable) { /* the fetch handler will get it later */ }
  }));

  await self.skipWaiting();
})()));

// Claim the page on the first load rather than the second, so a visitor who
// goes offline immediately after arriving is still covered.
self.addEventListener("activate", (event) => event.waitUntil(self.clients.claim()));

self.addEventListener("fetch", (event) => {
  const request = event.request;

  // Only plain page loads and their assets. A POST has no business being
  // replayed from a cache, and neither has anything that is not http.
  if (request.method !== "GET" || !request.url.startsWith("http")) return;

  const isPage = request.mode === "navigate";

  event.respondWith((async () => {
    try {
      /* Network-first is only as fresh as the network it asks. A navigation
         reissued as `fetch(request)` keeps that request's cache mode, and
         GitHub Pages serves HTML with `max-age=600` - so for ten minutes after
         a deploy the browser answers out of its own HTTP cache and the worker
         hands that straight back, having done nothing wrong and shown an old
         page anyway. Asking for the page by URL with `reload` skips the HTTP
         cache and goes to the network, which is what network-first was for.

         Only the page. The engine beside it is 400-odd KB and changes far less
         often, so it keeps the HTTP cache it has earned. */
      const response = isPage
        ? await fetch(request.url, { cache: "reload", credentials: "same-origin" })
        : await fetch(request);

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
      if (isPage) {
        const page = await caches.match("index.html") || await caches.match("./");
        if (page) return page;
      }

      throw offline;
    }
  })());
});
