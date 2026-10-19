// DOM, theme, feedback and formatting helpers shared by the utilities pages.
// The builder and icon helpers mirror web/js/ui.js.

const SPRITE = new URL('../icons.svg', import.meta.url).href;
const PROPS = new Set(['value', 'checked', 'disabled', 'selected', 'hidden', 'multiple']);

/** Hyperscript-style element builder: h('div', { class: 'x', onclick }, child, ...) */
export function h(tag, props, ...children) {
  const el = document.createElement(tag);
  if (props) {
    for (const [k, v] of Object.entries(props)) {
      if (v == null || v === false) continue;
      if (k === 'class') el.className = v;
      else if (k === 'style' && typeof v === 'object') Object.assign(el.style, v);
      else if (k === 'dataset') Object.assign(el.dataset, v);
      else if (k.startsWith('on') && typeof v === 'function') el.addEventListener(k.slice(2), v);
      else if (PROPS.has(k)) el[k] = v;
      else el.setAttribute(k, v === true ? '' : v);
    }
  }
  append(el, children);
  return el;
}

export function append(el, children) {
  for (const c of [children].flat(Infinity)) {
    if (c == null || c === false || c === true) continue;
    el.append(c instanceof Node ? c : String(c));
  }
  return el;
}

export function clear(el) {
  while (el.firstChild) el.firstChild.remove();
  return el;
}

export function replace(el, ...children) {
  return append(clear(el), children);
}

const SVG = 'http://www.w3.org/2000/svg';
function svg(tag, attrs = {}) {
  const el = document.createElementNS(SVG, tag);
  for (const [k, v] of Object.entries(attrs)) if (v != null) el.setAttribute(k, v);
  return el;
}

export function icon(name, cls = '') {
  const el = svg('svg', { class: `icon ${cls}`.trim(), 'aria-hidden': 'true' });
  el.append(svg('use', { href: `${SPRITE}#i-${name}` }));
  return el;
}

export function button(label, { kind = '', size = '', iconName, onclick, disabled, title } = {}) {
  return h('button', {
    class: `btn ${kind} ${size}`.replace(/\s+/g, ' ').trim(),
    type: 'button', onclick, disabled, title,
  }, iconName && icon(iconName, size === 'sm' ? 'sm' : ''), label);
}

export function setButton(btn, label, iconName) {
  replace(btn, iconName && icon(iconName, btn.classList.contains('sm') ? 'sm' : ''), label);
}

/** Run an async action with a busy state on the button that triggered it. */
export async function busy(btn, fn) {
  if (!btn) return fn();
  btn.classList.add('busy');
  btn.disabled = true;
  try { return await fn(); } finally { btn.classList.remove('busy'); btn.disabled = false; }
}

export function badge(text, kind = '', iconName) {
  return h('span', { class: `badge ${kind}` }, iconName && icon(iconName), text);
}

export function notice(kind, title, body) {
  return h('div', { class: `notice ${kind}` }, icon(kind === 'ok' ? 'check' : 'alert'),
    h('div', null, title && h('b', null, title), body && h('span', null, body)));
}

export function toast(message, kind = '', { timeout = 3800 } = {}) {
  const host = document.getElementById('toasts');
  const el = h('div', { class: `toast ${kind}`, role: kind === 'fault' ? 'alert' : 'status' },
    icon(kind === 'fault' ? 'alert' : 'check'), h('div', null, message));
  host.append(el);
  setTimeout(() => el.remove(), timeout);
}

// ---- Theme ------------------------------------------------------------------------------

const themeListeners = new Set();

export function onThemeChange(fn) {
  themeListeners.add(fn);
  return () => themeListeners.delete(fn);
}

/** Wires the #theme-toggle button. The inline <head> script sets the initial theme. */
export function initTheme() {
  const btn = document.getElementById('theme-toggle');
  const render = () => replace(btn, icon(document.documentElement.dataset.theme === 'dark' ? 'sun' : 'moon'));
  btn.addEventListener('click', () => {
    const next = document.documentElement.dataset.theme === 'dark' ? 'light' : 'dark';
    document.documentElement.dataset.theme = next;
    try { localStorage.setItem('acorn-theme', next); } catch { /* private mode */ }
    render();
    themeListeners.forEach((fn) => fn(next));
  });
  render();
}

// ---- Formatting -------------------------------------------------------------------------

export function fmtBytes(n) {
  if (n == null) return '—';
  const u = ['B', 'KB', 'MB', 'GB'];
  let i = 0;
  while (n >= 1024 && i < u.length - 1) { n /= 1024; i++; }
  return `${n.toFixed(n >= 100 || i === 0 ? 0 : 1)} ${u[i]}`;
}

/** Seconds -> "M:SS.mmm". */
export function fmtTime(sec) {
  const ms = Math.max(0, Math.round(sec * 1000));
  const m = Math.floor(ms / 60000);
  const s = Math.floor((ms % 60000) / 1000);
  return `${m}:${String(s).padStart(2, '0')}.${String(ms % 1000).padStart(3, '0')}`;
}

/** Short axis label: "0.75 s", "12.5 s", or "1:30" for clips of 20 s and longer. */
export function fmtTick(sec, span) {
  if (span < 8) return `${sec.toFixed(2)} s`;
  if (span < 20) return `${sec.toFixed(1)} s`;
  const s = Math.round(sec);
  return `${Math.floor(s / 60)}:${String(s % 60).padStart(2, '0')}`;
}

/** Parses "12.5", "1:02.25" or "1:02:03.5" into seconds; null if invalid. */
export function parseTime(str) {
  const t = String(str || '').trim();
  if (!/^(\d+:){0,2}\d+(\.\d*)?$|^(\d+:){0,2}\.\d+$/.test(t)) return null;
  return t.split(':').reduce((acc, part) => acc * 60 + Number(part || 0), 0);
}

export function fmtRate(hz) {
  return `${(hz / 1000).toLocaleString(undefined, { maximumFractionDigits: 2 })} kHz`;
}

export function fmtDb(linear) {
  if (!(linear > 0)) return '−∞ dBFS';
  const db = 20 * Math.log10(linear);
  return `${db > -0.05 && db < 0.05 ? '0.0' : db.toFixed(1).replace('-', '−')} dBFS`;
}

export function downloadBlob(filename, blob) {
  const url = URL.createObjectURL(blob);
  const a = h('a', { href: url, download: filename });
  document.body.append(a);
  a.click();
  a.remove();
  setTimeout(() => URL.revokeObjectURL(url), 2000);
}
