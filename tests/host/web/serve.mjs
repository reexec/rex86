// The browser runner's static server (design #48, decision 3): this
// directory at /, a REX86_BROWSER build's tools at /bin/. Run directly to
// open the runner on a real device over the LAN:
//
//   node tests/host/web/serve.mjs <build>/bin [port]
//
// With --site it serves a site assembled by scripts/pages_site.mjs as is,
// the way GitHub Pages does (design #52, decision 5):
//
//   node tests/host/web/serve.mjs --site <dir> [port]
//
// drive.mjs imports startServer and drive_pages.mjs startSiteServer. Nothing
// here is meant for an untrusted network: it serves directories read-only,
// without caching.

import fs from 'node:fs';
import http from 'node:http';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const webDir = path.dirname(fileURLToPath(import.meta.url));

const types = {
  '.html': 'text/html; charset=utf-8',
  '.js': 'text/javascript; charset=utf-8',
  '.mjs': 'text/javascript; charset=utf-8',
  '.json': 'application/json; charset=utf-8',
  '.css': 'text/css; charset=utf-8',
  '.wasm': 'application/wasm',
};

// The file a request path names under mounts ([url prefix, directory],
// longest prefix first), or null when it leaves them. A path ending in /
// names that directory's index.html.
function resolve(mounts, requestPath) {
  const mount = mounts.find(([prefix]) => requestPath.startsWith(prefix));
  if (!mount) {
    return null;
  }
  const [prefix, base] = mount;
  let rest = requestPath.slice(prefix.length);
  if (rest === '' || rest.endsWith('/')) {
    rest += 'index.html';
  }
  const file = path.resolve(base, rest);
  return file.startsWith(path.resolve(base) + path.sep) ? file : null;
}

function serve(mounts, { port = 0, host = '127.0.0.1' } = {}) {
  const server = http.createServer((request, response) => {
    let requestPath;
    try {
      requestPath = decodeURIComponent(new URL(request.url, 'http://host').pathname);
    } catch {
      response.writeHead(400).end();
      return;
    }
    const file = resolve(mounts, requestPath);
    if (file === null || (request.method !== 'GET' && request.method !== 'HEAD')) {
      response.writeHead(file === null ? 404 : 405).end();
      return;
    }
    fs.readFile(file, (error, data) => {
      if (error) {
        response.writeHead(404).end();
        return;
      }
      response.writeHead(200, {
        'Content-Type': types[path.extname(file)] ?? 'application/octet-stream',
        'Cache-Control': 'no-store',
      });
      response.end(request.method === 'HEAD' ? undefined : data);
    });
  });
  return new Promise((resolvePromise, reject) => {
    server.once('error', reject);
    server.listen(port, host, () => resolvePromise(server));
  });
}

export function startServer(binDir, options) {
  return serve([['/bin/', binDir], ['/', webDir]], options);
}

export function startSiteServer(siteDir, options) {
  return serve([['/', siteDir]], options);
}

if (process.argv[1] === fileURLToPath(import.meta.url)) {
  const args = process.argv.slice(2);
  const site = args[0] === '--site';
  const [dir, port] = site ? args.slice(1) : args;
  const marker = site ? 'versions.json' : 'rex86_browser.json';
  if (!dir || !fs.existsSync(path.join(dir, marker))) {
    console.error('usage: node serve.mjs <REX86_BROWSER build>/bin [port]\n' +
      '       node serve.mjs --site <site from scripts/pages_site.mjs> [port]');
    process.exit(2);
  }
  const options = { port: Number(port ?? 8048), host: '0.0.0.0' };
  const server = await (site ? startSiteServer : startServer)(path.resolve(dir), options);
  const { port: bound } = server.address();
  console.log(`[rex86-serve] serving ${path.resolve(dir)} on port ${bound}`);
  for (const addresses of Object.values(os.networkInterfaces())) {
    for (const address of addresses ?? []) {
      if (address.family === 'IPv4') {
        console.log(`[rex86-serve] open http://${address.address}:${bound}/`);
      }
    }
  }
}
