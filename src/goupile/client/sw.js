// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

let env = {
    app_key: '{APP_KEY}',
    base_url: '{BASE_URL}',

    use_offline: {USE_OFFLINE},
    cache_key: '{CACHE_KEY}'
};

let cache_urls = [
    env.base_url,
    `${env.base_url}static/goupile.pk.css`,
    `${env.base_url}static/goupile.pk.js`,
    `${env.base_url}static/ace.js`,
    `${env.base_url}static/theme-merbivore_soft.js`,
    `${env.base_url}static/mode-css.js`,
    `${env.base_url}static/mode-html.js`,
    `${env.base_url}static/mode-javascript.js`,
    `${env.base_url}static/ext-searchbox.js`,
    `${env.base_url}static/xlsx.core.min.js`,
    `${env.base_url}static/jspdf.min.js`,
    `${env.base_url}static/OpenSans-Regular.woff2`,
    `${env.base_url}static/OpenSans-Bold.woff2`,
    `${env.base_url}static/OpenSans-Italic.woff2`,
    `${env.base_url}static/OpenSans-BoldItalic.woff2`,
    `${env.base_url}static/icons.png`,

    `${env.base_url}manifest.json`,
    `${env.base_url}favicon.png`
];

self.addEventListener('install', e => {
    e.waitUntil(async function() {
        if (env.use_offline) {
            let cache = await caches.open(env.cache_key);
            await cache.addAll(cache_urls);
        }

        await self.skipWaiting();
    }());
});

self.addEventListener('activate', e => {
    e.waitUntil(async function() {
        let keys = await caches.keys();

        for (let key of keys) {
            if (key !== env.cache_key)
                await caches.delete(key);
        }

        await clients.claim();
    }());
});

self.addEventListener('fetch', e => {
    e.respondWith(async function() {
        let url = new URL(e.request.url);

        // Ignore sync GET requests for app files (?direct=1)
        if (e.request.method === 'GET' && url.pathname.startsWith(env.base_url) &&
                                          !url.searchParams.get('direct')) {
            let path = url.pathname.substr(env.base_url.length - 1);

            if (path.startsWith('/app/') || path.startsWith('/main/'))
                return await caches.match(env.base_url) || await net.fetch(env.base_url);

            if (path.startsWith('/files/') || path == '/favicon.png') {
                let db_path;
                if (path == '/favicon.png') {
                    db_path = '/files/favicon.png';
                } else {
                    db_path = path;
                }

                try {
                    let db_name = `goupile+${env.app_key}`;
                    let db = await idb.open(db_name);

                    let file_data = await db.load('fs_data', db_path);
                    if (file_data)
                        return new Response(file_data);
                } catch (err) {
                    // IndexedDB sucks, you can't open a database without creating it
                    // if it does not exist. And right now, you can't even test if a database
                    // exists beforehand on all browsers... F*cking crap.
                    // Still, we want this to be transparent. If the database does not exist,
                    // it will get created by the idb.open() call with version 1. The code
                    // in goupile.js will upgrade it to version 2 (or more). All we have to do
                    // here is ignore errors, such as missing object store.
                }
            }

            return await caches.match(e.request) || await net.fetch(e.request);
        }

        // Nothing matched, do the usual
        return await net.fetch(e.request);
    }());
});
