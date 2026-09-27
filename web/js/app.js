// App shell: routing, header, connection banner and session timer.

import { api } from './api.js';
import { store, subscribe, refresh } from './store.js';
import { h, clear, replace, icon, button, busy, toastError } from './ui.js';
import * as statusView from './views/status.js';
import * as programsView from './views/programs.js';
import * as editorView from './views/editor.js';
import * as audioView from './views/audio.js';
import * as dataView from './views/data.js';
import * as deviceView from './views/device.js';

const routes = [
  { re: /^\/status$/, nav: 'status', view: statusView, title: 'Status' },
  { re: /^\/programs$/, nav: 'programs', view: programsView, title: 'Programs' },
  { re: /^\/programs\/new$/, nav: 'programs', view: editorView, title: 'New program' },
  { re: /^\/programs\/([^/]+)$/, nav: 'programs', view: editorView, title: 'Edit program' },
  { re: /^\/audio$/, nav: 'audio', view: audioView, title: 'Audio' },
  { re: /^\/data$/, nav: 'data', view: dataView, title: 'Data' },
  { re: /^\/device$/, nav: 'device', view: deviceView, title: 'Device' },
];

const viewEl = document.getElementById('view');
let current = null;
let currentHash = '';
let suppressNext = false;

function parseHash() {
  const raw = location.hash.replace(/^#/, '') || '/status';
  const [path, query] = raw.split('?');
  return { path, query: new URLSearchParams(query || '') };
}

async function navigate() {
  if (suppressNext) { suppressNext = false; return; }
  const { path, query } = parseHash();

  if (current && current.canLeave && !(await current.canLeave())) {
    suppressNext = true;
    location.hash = currentHash;
    return;
  }

  let route = routes.find((r) => r.re.test(path));
  if (!route) { location.replace('#/status'); return; }
  const params = path.match(route.re).slice(1).map(decodeURIComponent);

  if (current && current.unmount) current.unmount();
  current = null;
  currentHash = location.hash;
  clear(viewEl);
  document.title = `${route.title} · ACORN`;
  document.querySelectorAll('[data-nav]').forEach((a) => a.classList.toggle('active', a.dataset.nav === route.nav));

  current = route.view.mount(viewEl, { params, query }) || {};
  viewEl.focus({ preventScroll: true });
  window.scrollTo(0, 0);
}

window.addEventListener('hashchange', navigate);
window.addEventListener('beforeunload', (e) => {
  if (current && current.isDirty && current.isDirty()) { e.preventDefault(); e.returnValue = ''; }
});

// ---- Header -----------------------------------------------------------------------------

const chip = document.getElementById('device-chip');
const sessionEl = document.getElementById('session');
const bannerEl = document.getElementById('banner');
const themeBtn = document.getElementById('theme-toggle');

function renderTheme() {
  const dark = document.documentElement.dataset.theme === 'dark';
  replace(themeBtn, icon(dark ? 'sun' : 'moon'));
}
themeBtn.addEventListener('click', () => {
  const next = document.documentElement.dataset.theme === 'dark' ? 'light' : 'dark';
  document.documentElement.dataset.theme = next;
  try { localStorage.setItem('acorn-theme', next); } catch { /* private mode */ }
  renderTheme();
});
renderTheme();

let sessionRemaining = null;
let sessionTick = null;

function renderSession() {
  if (sessionRemaining == null) { sessionEl.hidden = true; return; }
  sessionEl.hidden = false;
  const m = Math.floor(sessionRemaining / 60), s = sessionRemaining % 60;
  sessionEl.className = `session ${sessionRemaining <= 60 ? 'warn' : ''}`;
  sessionEl.title = 'Wi-Fi access point closes after this much inactivity';
  replace(sessionEl, icon('wifi', 'sm'), h('span', { class: 'num' }, `${m}:${String(s).padStart(2, '0')}`));
}

function renderBanner() {
  clear(bannerEl);
  if (!store.online) {
    bannerEl.append(h('div', { class: 'banner fault', role: 'alert' }, icon('wifi'),
      h('span', null, 'Connection to ACORN lost. Reconnecting…'),
      button('Retry', { size: 'sm', onclick: () => refresh() })));
    return;
  }
  if (sessionRemaining != null && sessionRemaining <= 60) {
    const keep = button('Stay connected', { size: 'sm' });
    keep.onclick = () => busy(keep, async () => {
      try { await api.extendSession(); await refresh(); } catch (e) { toastError(e); }
    });
    bannerEl.append(h('div', { class: 'banner warn' }, icon('clock'),
      h('span', null, `Wi-Fi closes in ${sessionRemaining} s due to inactivity.`), keep));
  }
}

subscribe((s) => {
  const st = s.status;
  if (st) {
    replace(chip, h('b', null, st.device.name), h('span', null, st.device.id));
    sessionRemaining = st.wifi ? st.wifi.idle_remaining_s : null;
    const blocking = st.faults.filter((f) => f.blocking);
    document.querySelectorAll('[data-nav]').forEach((a) => {
      const hasFault = a.dataset.nav === 'status' && blocking.length > 0;
      const dot = a.querySelector('.dot');
      if (hasFault && !dot) a.append(h('span', { class: 'dot', 'aria-label': 'Blocking fault' }));
      if (!hasFault && dot) dot.remove();
    });
  }
  renderSession();
  renderBanner();
  if (current && current.onStatus && st) current.onStatus(st, s);
});

clearInterval(sessionTick);
sessionTick = setInterval(() => {
  if (sessionRemaining > 0) {
    sessionRemaining--;
    renderSession();
    if (sessionRemaining === 60 || sessionRemaining <= 0) renderBanner();
  }
}, 1000);

refresh();
navigate();
