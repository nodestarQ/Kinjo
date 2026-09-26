/// <reference types="@sveltejs/kit" />
/// <reference no-default-lib="true"/>
/// <reference lib="esnext" />
/// <reference lib="webworker" />

// Keeps the app usable offline: the app shell and assets are cached, pages load from the network
// when possible and from the cache when not. ENS, RPC and relayer calls are never cached.

import { build, files, version } from '$service-worker';

const sw = self as unknown as ServiceWorkerGlobalScope;
const CACHE = `kinjo-${version}`;
const SHELL = '/';
const ASSETS = [SHELL, ...build, ...files];

sw.addEventListener('install', (event) => {
	event.waitUntil(caches.open(CACHE).then((c) => c.addAll(ASSETS)).then(() => sw.skipWaiting()));
});

sw.addEventListener('activate', (event) => {
	event.waitUntil(
		caches
			.keys()
			.then((keys) => Promise.all(keys.filter((k) => k !== CACHE).map((k) => caches.delete(k))))
			.then(() => sw.clients.claim())
	);
});

sw.addEventListener('fetch', (event) => {
	const req = event.request;
	const url = new URL(req.url);
	if (req.method !== 'GET' || url.origin !== sw.location.origin) return; // RPC, wallets, helper
	if (url.pathname.startsWith('/relay') || url.pathname === '/health') return;

	// Pages: network first, the cached app shell offline (all routes are the same SPA).
	if (req.mode === 'navigate') {
		event.respondWith(fetch(req).catch(() => caches.match(SHELL).then((r) => r ?? Response.error())));
		return;
	}
	// Built assets never change under the same name: cache first.
	event.respondWith(caches.match(req).then((cached) => cached ?? fetch(req)));
});
