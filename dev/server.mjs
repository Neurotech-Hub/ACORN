#!/usr/bin/env node
// ACORN UI dev server: serves web/ exactly as the device will, plus a mock /api/v1.
// Usage: node dev/server.mjs [--port 8080] [--host 127.0.0.1] [--scenario normal] [--latency 150] [--dist]

import http from 'node:http';
import { readFile } from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { MockDevice, SCENARIOS } from './mock/state.mjs';
import { handleApi } from './mock/api.mjs';

const REPO = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');

const args = Object.fromEntries(
  process.argv.slice(2).reduce((acc, a, i, arr) => (a.startsWith('--') ? [...acc, [a.slice(2), arr[i + 1]?.startsWith('--') ? true : arr[i + 1] ?? true]] : acc), []),
);
// --dist serves the gzipped output of tools/build-web.mjs, byte-for-byte what the firmware embeds.
const DIST = !!args.dist;
const ROOT = DIST ? path.join(REPO, 'build', 'web') : path.join(REPO, 'web');
const PORT = +(args.port || process.env.PORT || 8080);
const HOST = args.host || '127.0.0.1';
const LATENCY = +(args.latency ?? 150);
const REBOOT_MS = 5000;

const MIME = {
  '.html': 'text/html; charset=utf-8',
  '.css': 'text/css; charset=utf-8',
  '.js': 'text/javascript; charset=utf-8',
  '.svg': 'image/svg+xml',
  '.json': 'application/json',
  '.png': 'image/png',
  '.ico': 'image/x-icon',
};

const ctx = {
  device: new MockDevice(args.scenario || 'normal'),
  rebootUntil: 0,
  reboot() {
    setTimeout(() => { ctx.rebootUntil = Date.now() + REBOOT_MS; }, 300);
  },
};

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

function devPage() {
  const cur = ctx.device.scenario;
  const rows = Object.entries(SCENARIOS).map(([k, s]) => `
    <form method="post" action="/__dev/scenario">
      <input type="hidden" name="name" value="${k}">
      <button ${k === cur ? 'class="cur"' : ''}><b>${s.label}</b><span>${s.description}</span><code>${k}</code></button>
    </form>`).join('');
  return `<!doctype html><meta name="viewport" content="width=device-width,initial-scale=1"><title>ACORN dev</title>
<style>
  body{font:15px system-ui,sans-serif;max-width:640px;margin:32px auto;padding:0 16px;background:#f3f1ea;color:#111}
  h1{letter-spacing:.14em;font-size:18px} p{color:#555} form{margin:0 0 8px}
  button{all:unset;box-sizing:border-box;display:grid;gap:2px;width:100%;padding:12px 14px;background:#fff;border:1px solid #ddd;border-radius:10px;cursor:pointer}
  button:hover{border-color:#111} button.cur{background:#111;color:#fff;border-color:#111} button.cur span{color:#ccc}
  span{color:#666;font-size:13px} code{font-size:12px;opacity:.6} a{color:inherit}
</style>
<h1>ACORN · MOCK DEVICE</h1>
<p>Selecting a scenario resets the mock device state. Current: <b>${SCENARIOS[cur].label}</b>. <a href="/">Open UI →</a></p>
${rows}`;
}

const server = http.createServer(async (req, res) => {
  const url = new URL(req.url, `http://${req.headers.host}`);

  if (url.pathname === '/__dev' || url.pathname === '/__dev/') {
    res.writeHead(200, { 'Content-Type': 'text/html; charset=utf-8' });
    return res.end(devPage());
  }
  if (url.pathname === '/__dev/scenario' && req.method === 'POST') {
    let body = '';
    for await (const c of req) body += c;
    const name = new URLSearchParams(body).get('name') || (() => { try { return JSON.parse(body).name; } catch { return null; } })();
    ctx.device = new MockDevice(name);
    console.log(`[dev] scenario -> ${ctx.device.scenario}`);
    res.writeHead(303, { Location: '/__dev' });
    return res.end();
  }

  if (url.pathname.startsWith('/api/')) {
    if (Date.now() < ctx.rebootUntil) return req.socket.destroy();
    if (LATENCY) await sleep(LATENCY * (0.6 + Math.random() * 0.8));
    const t0 = Date.now();
    await handleApi(req, res, url, ctx);
    if (url.pathname !== '/api/v1/status') console.log(`${req.method} ${url.pathname}${url.search} -> ${res.statusCode} (${Date.now() - t0}ms)`);
    return;
  }

  let rel = decodeURIComponent(url.pathname);
  if (rel.endsWith('/')) rel += 'index.html';
  const file = path.join(ROOT, rel);
  if (!file.startsWith(ROOT)) { res.writeHead(403); return res.end(); }
  try {
    const data = await readFile(DIST ? file + '.gz' : file);
    res.writeHead(200, {
      'Content-Type': MIME[path.extname(file)] || 'application/octet-stream',
      'Cache-Control': 'no-cache',
      ...(DIST ? { 'Content-Encoding': 'gzip' } : {}),
    });
    res.end(data);
  } catch {
    res.writeHead(404, { 'Content-Type': 'text/plain' });
    res.end('Not found');
  }
});

server.listen(PORT, HOST, () => {
  const shown = HOST === '0.0.0.0' ? 'localhost' : HOST;
  console.log(`\n  ACORN UI (mock device)   http://${shown}:${PORT}/`);
  console.log(`  Scenario switcher        http://${shown}:${PORT}/__dev`);
  console.log(`  Scenario: ${ctx.device.scenario} · latency ~${LATENCY}ms${DIST ? ' · serving gzipped build/web' : ''}\n`);
});
