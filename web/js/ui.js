// DOM, feedback and formatting helpers shared by all views.

const PROPS = new Set(['value', 'checked', 'disabled', 'selected', 'hidden', 'multiple', 'indeterminate', 'open']);

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

/** Replace an element's children; skips null/false like h(). */
export function replace(el, ...children) {
  return append(clear(el), children);
}

const SVG = 'http://www.w3.org/2000/svg';
export function svg(tag, attrs = {}, ...children) {
  const el = document.createElementNS(SVG, tag);
  for (const [k, v] of Object.entries(attrs)) if (v != null) el.setAttribute(k, v);
  for (const c of children.flat()) if (c) el.append(c);
  return el;
}

export function icon(name, cls = '') {
  const el = svg('svg', { class: `icon ${cls}`.trim(), 'aria-hidden': 'true' });
  el.append(svg('use', { href: `#i-${name}` }));
  return el;
}

export function mark(cls = 'mark') {
  const el = svg('svg', { class: cls, viewBox: '0 0 701 866', 'aria-hidden': 'true' });
  el.append(svg('use', { href: '#acorn-icon' }));
  return el;
}

export function badge(text, kind = '', iconName) {
  return h('span', { class: `badge ${kind}` }, iconName && icon(iconName), text);
}

export function button(label, { kind = '', size = '', iconName, onclick, type = 'button', disabled, title } = {}) {
  return h('button', {
    class: `btn ${kind} ${size} ${label ? '' : 'icon-only'}`.replace(/\s+/g, ' ').trim(),
    type, onclick, disabled, title, 'aria-label': label ? null : title,
  }, iconName && icon(iconName, size === 'sm' ? 'sm' : ''), label);
}

/** Run an async action with a busy state on the button that triggered it. */
export async function busy(btn, fn) {
  if (!btn) return fn();
  btn.classList.add('busy');
  btn.disabled = true;
  try { return await fn(); } finally { btn.classList.remove('busy'); btn.disabled = false; }
}

export function field(label, control, { hint, key, id } = {}) {
  const cid = id || (control.id ||= `f-${Math.random().toString(36).slice(2, 8)}`);
  return h('div', { class: 'field', dataset: key ? { field: key } : undefined },
    h('label', { for: cid }, label),
    control,
    hint && h('div', { class: 'hint' }, hint),
  );
}

export function section(title, ...actions) {
  return h('div', { class: 'section-title' }, h('h2', null, title), actions.length > 0 && h('div', { class: 'actions' }, actions));
}

export function pageHead(title, sub, ...actions) {
  return h('div', { class: 'page-head' },
    h('h1', null, title),
    actions.length > 0 && h('div', { class: 'actions' }, actions),
    sub && h('p', { class: 'sub' }, sub),
  );
}

export function skeleton(heights = [140, 220]) {
  return h('div', { class: 'stack' }, heights.map((px) => h('div', { class: 'skeleton', style: { height: `${px}px` } })));
}

export function errorState(err, onRetry) {
  return h('div', { class: 'card error-state' },
    h('div', { class: 'notice fault' }, icon('alert'),
      h('div', null, h('b', null, err.message), err.code && h('code', null, err.code))),
    onRetry && button('Retry', { iconName: 'refresh', onclick: onRetry }),
  );
}

export function notice(kind, title, body, ...extra) {
  return h('div', { class: `notice ${kind}` }, icon(kind === 'fault' || kind === 'warn' ? 'alert' : 'check'),
    h('div', null, title && h('b', null, title), body && h('span', null, body), extra));
}

// ---- Toasts -----------------------------------------------------------------------------

export function toast(message, kind = '', { code, timeout = 3800 } = {}) {
  const host = document.getElementById('toasts');
  const el = h('div', { class: `toast ${kind}`, role: kind === 'fault' ? 'alert' : 'status' },
    icon(kind === 'fault' ? 'alert' : 'check'),
    h('div', null, message, code && h('code', null, code)));
  host.append(el);
  setTimeout(() => el.remove(), timeout);
}

export function toastError(err) {
  toast(err.message || String(err), 'fault', { code: err.code, timeout: 6000 });
}

// ---- Dialog -----------------------------------------------------------------------------

export function confirmDialog({ title, body, confirm = 'Confirm', cancel = 'Cancel', danger = false }) {
  const dlg = document.getElementById('dialog');
  return new Promise((resolve) => {
    clear(dlg);
    const ok = h('button', { class: `btn ${danger ? 'danger' : 'primary'}`, value: 'ok' }, confirm);
    dlg.append(h('form', { method: 'dialog' },
      h('h2', null, title),
      body && h('div', { class: 'body' }, body),
      h('div', { class: 'actions' }, h('button', { class: 'btn', value: 'cancel' }, cancel), ok)));
    dlg.onclose = () => resolve(dlg.returnValue === 'ok');
    dlg.returnValue = '';
    dlg.showModal();
    ok.focus();
  });
}

/** Leave-guard for views with staged (unsaved) edits. */
export function confirmDiscard() {
  return confirmDialog({
    title: 'Discard unsaved changes?',
    body: 'Your edits have not been saved to the device.',
    confirm: 'Discard',
    danger: true,
  });
}

// ---- Formatting -------------------------------------------------------------------------

export function fmtBytes(n) {
  if (n == null) return '—';
  const u = ['B', 'KB', 'MB', 'GB', 'TB'];
  let i = 0;
  while (n >= 1024 && i < u.length - 1) { n /= 1024; i++; }
  return `${n.toFixed(n >= 100 || i === 0 ? 0 : 1)} ${u[i]}`;
}

export function fmtDuration(ms) {
  if (ms == null) return '—';
  const s = ms / 1000;
  if (s < 60) return `${s.toFixed(1)} s`;
  const m = Math.floor(s / 60);
  return `${m}:${String(Math.round(s % 60)).padStart(2, '0')}`;
}

export function fmtClockMs(ms) {
  const s = Math.max(0, Math.round(ms / 1000));
  return `${Math.floor(s / 60)}:${String(s % 60).padStart(2, '0')}`;
}

export function fmtOffset(min) {
  const sign = min < 0 ? '−' : '+';
  const a = Math.abs(min);
  return `UTC${sign}${String(Math.floor(a / 60)).padStart(2, '0')}:${String(a % 60).padStart(2, '0')}`;
}

/** "2026-09-28T06:00:00" -> "06:00" (seconds shown only when non-zero). */
export function fmtClock(localIso, { seconds = false } = {}) {
  if (!localIso) return '—';
  const t = localIso.slice(11, 19);
  return seconds || !t.endsWith(':00') ? t : t.slice(0, 5);
}

export function fmtUtcClock(utcIso) {
  return utcIso ? `${fmtClock(utcIso.slice(0, 19))}Z` : '—';
}

/** Device-local ISO (no zone) for a UTC instant. */
export function toLocalIso(utcMs, offsetMin) {
  return new Date(utcMs + offsetMin * 60000).toISOString().slice(0, 19);
}

export function dayLabel(localIso, todayLocalIso) {
  if (!localIso) return '';
  const d = Date.parse(localIso.slice(0, 10) + 'T00:00:00Z');
  const t = Date.parse(todayLocalIso.slice(0, 10) + 'T00:00:00Z');
  const diff = Math.round((d - t) / 86400000);
  if (diff === 0) return 'Today';
  if (diff === 1) return 'Tomorrow';
  if (diff === -1) return 'Yesterday';
  return new Date(d).toLocaleDateString(undefined, { weekday: 'short', month: 'short', day: 'numeric', timeZone: 'UTC' });
}

export function fmtRelative(ms) {
  const future = ms >= 0;
  let s = Math.abs(Math.round(ms / 1000));
  let out;
  if (s < 60) out = `${s} s`;
  else if (s < 3600) out = `${Math.floor(s / 60)} min${s % 60 && s < 600 ? ` ${s % 60} s` : ''}`;
  else if (s < 86400) { const hh = Math.floor(s / 3600), mm = Math.floor((s % 3600) / 60); out = `${hh} h${mm ? ` ${mm} min` : ''}`; }
  else out = `${Math.floor(s / 86400)} d ${Math.floor((s % 86400) / 3600)} h`;
  return future ? `in ${out}` : `${out} ago`;
}

export function fmtDateTime(localIso) {
  if (!localIso) return '—';
  return `${localIso.slice(0, 10)} ${fmtClock(localIso, { seconds: true })}`;
}

/** Normalize "6:05", "06:05:30", "0605", "060530" to "HH:MM:SS" (24 h); null if invalid. */
export function normalizeTime(s) {
  let str = String(s || '').trim();
  if (/^\d{3,6}$/.test(str)) {
    if (str.length % 2) str = '0' + str;
    str = str.match(/\d\d/g).join(':');
  }
  const m = /^(\d{1,2}):(\d{1,2})(?::(\d{1,2}))?$/.exec(str);
  if (!m) return null;
  const [hh, mm, ss] = [+m[1], +m[2], +(m[3] || 0)];
  if (hh > 23 || mm > 59 || ss > 59) return null;
  return [hh, mm, ss].map((n) => String(n).padStart(2, '0')).join(':');
}

export function downloadText(filename, text, type = 'application/json') {
  const url = URL.createObjectURL(new Blob([text], { type }));
  const a = h('a', { href: url, download: filename });
  document.body.append(a);
  a.click();
  a.remove();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
}

export function batteryKind(pct) {
  return pct == null ? '' : pct <= 15 ? 'fault' : pct <= 35 ? 'warn' : 'ok';
}

export const clone = (o) => JSON.parse(JSON.stringify(o));
