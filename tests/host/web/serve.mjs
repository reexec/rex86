// The browser runner's static server (design #48, decision 3): this
// directory at /, a REX86_BROWSER build's tools at /bin/. Run directly to
// open the runner on a real device over the LAN:
//
//   node tests/host/web/serve.mjs <build>/bin [port]
//
// drive.mjs imports startServer. Nothing here is meant for an untrusted
// network: it serves two directories read-only, without caching.

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

// The file a request path names, or null when it leaves both directories.
function resolve(binDir, requestPath) {
  const [base, rest] = requestPath.startsWith('/bin/')
    ? [binDir, requestPath.slice('/bin/'.length)]
    : [webDir, requestPath === '/' ? 'index.html' : requestPath.slice(1)];
  const file = path.resolve(base, rest);
  return file.startsWith(path.resolve(base) + path.sep) ? file : null;
}

export function startServer(binDir, { port = 0, host = '127.0.0.1' } = {}) {
  const server = http.createServer((request, response) => {
    let requestPath;
    try {
      requestPath = decodeURIComponent(new URL(request.url, 'http://host').pathname);
    } catch {
      response.writeHead(400).end();
      return;
    }
    const file = resolve(binDir, requestPath);
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

if (process.argv[1] === fileURLToPath(import.meta.url)) {
  const [binDir, port] = process.argv.slice(2);
  if (!binDir || !fs.existsSync(path.join(binDir, 'rex86_browser.json'))) {
    console.error('usage: node serve.mjs <REX86_BROWSER build>/bin [port]');
    process.exit(2);
  }
  const server = await startServer(path.resolve(binDir), { port: Number(port ?? 8048), host: '0.0.0.0' });
  const { port: bound } = server.address();
  console.log(`[rex86-serve] serving ${path.resolve(binDir)} on port ${bound}`);
  for (const addresses of Object.values(os.networkInterfaces())) {
    for (const address of addresses ?? []) {
      if (address.family === 'IPv4') {
        console.log(`[rex86-serve] open http://${address.address}:${bound}/`);
      }
    }
  }
}
