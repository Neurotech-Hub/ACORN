// Data: environmental logging settings/readings and CSV log access.

import { api } from '../api.js';
import { refresh, deviceNow, offsetMinutes } from '../store.js';
import {
  h, clear, replace, append, icon, button, busy, pageHead, section, skeleton, errorState, toast, toastError, notice,
  fmtBytes, fmtRelative, fmtDateTime, toLocalIso, confirmDiscard,
} from '../ui.js';

const PRESETS = [1, 5, 10, 15, 30, 60];

function parseCsv(text) {
  const lines = text.trim().split(/\r?\n/);
  return { header: lines[0].split(','), rows: lines.slice(1).filter(Boolean).map((l) => l.split(',')) };
}

export function mount(el) {
  let env = null;
  let saved = null;
  let logs = null;
  let logsError = null;
  const tails = {};

  const envHost = h('div');
  const logsHost = h('div');

  const isDirty = () => env && saved && (env.enabled !== saved.enabled || env.interval_minutes !== saved.interval_minutes);

  // ---- environment ----------------------------------------------------------------------
  function readingTile(label, value, unit) {
    return h('div', { class: 'card tile', style: { minHeight: '96px' } },
      h('div', { class: 'eyebrow' }, label),
      value == null ? h('div', { class: 'value', style: { color: 'var(--warn)' } }, '—') : h('div', { class: 'value' }, value, h('small', null, unit)));
  }

  function renderEnv() {
    clear(envHost);
    if (!env) { envHost.append(skeleton([100, 180])); return; }
    const r = env.readings;
    const custom = !PRESETS.includes(env.interval_minutes);
    const customInput = h('input', {
      class: 'input num', type: 'number', min: 1, max: 1440, step: 1, inputmode: 'numeric',
      value: env.interval_minutes, style: { maxWidth: '120px' }, 'aria-label': 'Custom interval in minutes',
    });
    customInput.oninput = () => {
      const n = parseInt(customInput.value, 10);
      if (Number.isInteger(n)) { env.interval_minutes = n; renderEnvActions(); highlightChips(); }
    };
    const chips = h('div', { class: 'chips', role: 'group', 'aria-label': 'Interval presets' },
      PRESETS.map((m) => h('button', {
        type: 'button', class: 'chip', 'aria-pressed': String(env.interval_minutes === m), dataset: { m },
        onclick: () => { env.interval_minutes = m; customInput.value = m; renderEnvActions(); highlightChips(); },
      }, m < 60 ? `${m} min` : '1 h')));
    function highlightChips() {
      chips.querySelectorAll('.chip').forEach((c) => c.setAttribute('aria-pressed', String(+c.dataset.m === env.interval_minutes)));
    }

    const toggle = h('input', { type: 'checkbox', checked: env.enabled });
    toggle.onchange = () => { env.enabled = toggle.checked; renderEnvActions(); intervalBlock.hidden = !env.enabled; };

    const intervalBlock = h('div', { class: 'field', hidden: !env.enabled },
      h('div', { class: 'label' }, 'Sample interval'),
      chips,
      h('div', { class: 'row', style: { marginTop: '4px' } }, h('span', { class: 'small muted' }, 'Custom'), customInput, h('span', { class: 'small muted' }, 'minutes')),
      h('div', { class: 'hint' }, `Minimum ${env.min_interval_minutes} min. Samples align to UTC interval boundaries (e.g. :00, :10, :20) so they never drift.`),
      custom && h('div', { class: 'hint' }, 'Custom interval in use.'));

    envActions = h('div', { class: 'row', style: { justifyContent: 'space-between', rowGap: '12px' } });
    append(envHost, [
      !r.ok && notice('warn', 'Sensors not responding', 'Values will be blank in the log until sensors recover. Playback is not affected.'),
      !r.ok && h('div', { style: { height: '12px' } }),
      h('div', { class: 'grid', style: { gridTemplateColumns: 'repeat(auto-fit, minmax(150px, 1fr))' } },
        readingTile('Temperature', r.temperature_c?.toFixed(1), '°C'),
        readingTile('Humidity', r.relative_humidity_pct?.toFixed(1), '% RH'),
        readingTile('Ambient light', r.ambient_light_lux?.toLocaleString(undefined, { maximumFractionDigits: 1 }), 'lux')),
      h('div', { class: 'card form', style: { marginTop: '12px' } },
        h('label', { class: 'switch' }, toggle, h('span', { class: 'track' }), 'Environmental logging'),
        h('p', { class: 'small muted' }, 'Logs temperature, humidity, light and battery to /logs/environment.csv, independently of the playback schedule.'),
        intervalBlock,
        envActions),
    ]);
    renderEnvActions();
  }

  let envActions = null;
  function renderEnvActions() {
    if (!envActions) return;
    const dirty = isDirty();
    const save = button('Save & Apply', { kind: 'primary', iconName: 'check', disabled: !dirty });
    save.onclick = () => busy(save, async () => {
      try {
        env = await api.updateEnvironment({ enabled: env.enabled, interval_minutes: env.interval_minutes });
        saved = { ...env };
        toast('Environmental logging updated.', 'ok');
        refresh();
        renderEnv();
      } catch (e) { toastError(e); }
    });
    const next = saved.enabled && env.next_sample_utc
      ? h('span', { class: 'small muted' }, `Next sample ${fmtRelative(Date.parse(env.next_sample_utc) - deviceNow())}`)
      : h('span', { class: 'small muted' }, saved.enabled ? '' : 'Logging is off');
    replace(envActions, next, h('div', { class: 'row', style: { marginLeft: 'auto' } },
      dirty && button('Discard', { onclick: () => { env = { ...saved }; renderEnv(); } }), save));
  }

  // ---- logs -----------------------------------------------------------------------------
  async function toggleTail(kind, btn) {
    if (tails[kind]) { tails[kind] = null; renderLogs(); return; }
    await busy(btn, async () => {
      try {
        tails[kind] = parseCsv(await api.logTail(kind, 20));
        renderLogs();
      } catch (e) { toastError(e); }
    });
  }

  function tailTable(t) {
    if (!t.rows.length) return h('div', { class: 'empty' }, 'No rows yet.');
    return h('div', { class: 'table-wrap scroll-y', style: { borderTop: '1px solid var(--line)' } },
      h('table', { class: 'table' },
        h('thead', null, h('tr', null, t.header.map((c) => h('th', null, c)))),
        h('tbody', null, [...t.rows].reverse().map((r) => h('tr', null, r.map((c) => h('td', null, c)))))));
  }

  function logCard(kind, title, desc) {
    const info = logs[kind];
    const tailBtn = button(tails[kind] ? 'Hide recent' : 'Show recent', { size: 'sm', iconName: 'eye' });
    tailBtn.onclick = () => toggleTail(kind, tailBtn);
    const last = info.modified_utc ? toLocalIso(Date.parse(info.modified_utc), offsetMinutes()) : null;
    return h('div', { class: 'card flush' },
      h('div', { style: { padding: '18px' } },
        h('div', { class: 'card-head', style: { marginBottom: '6px' } }, h('h3', null, title), h('span', { class: 'muted small mono' }, info.path)),
        h('p', { class: 'muted small' }, desc),
        h('dl', { class: 'kv', style: { margin: '14px 0' } },
          h('dt', null, 'Rows'), h('dd', null, info.rows.toLocaleString()),
          h('dt', null, 'Size'), h('dd', null, fmtBytes(info.size_bytes)),
          h('dt', null, 'Last entry'), h('dd', null, last ? `${fmtDateTime(last)} local` : '—')),
        h('div', { class: 'row' },
          h('a', { class: 'btn sm primary', href: api.logUrl(kind), download: `${kind}.csv` }, icon('download', 'sm'), 'Download CSV'),
          tailBtn)),
      tails[kind] && tailTable(tails[kind]));
  }

  function renderLogs() {
    clear(logsHost);
    if (logsError) {
      logsHost.append(logsError.code === 'SD_NOT_PRESENT'
        ? notice('fault', 'SD card not available', 'Logs are stored on the SD card.')
        : errorState(logsError, loadLogs));
      return;
    }
    if (!logs) { logsHost.append(skeleton([160, 160])); return; }
    logsHost.append(h('div', { class: 'grid two' },
      logCard('playback', 'Playback log', 'Every scheduled and manual playback with program revision, result, battery and environment.'),
      logCard('environment', 'Environment log', 'Periodic temperature, humidity, light and battery samples.')),
    h('p', { class: 'muted small', style: { marginTop: '10px' } }, 'All log timestamps are UTC (ISO-8601, “Z”).'));
  }

  async function loadEnv() {
    try {
      env = await api.environment();
      saved = { ...env };
      renderEnv();
    } catch (e) {
      replace(envHost, errorState(e, loadEnv));
    }
  }

  async function loadLogs() {
    try {
      logs = await api.logs();
      logsError = null;
    } catch (e) { logsError = e; }
    renderLogs();
  }

  replace(el, 
    pageHead('Data', 'Environmental sensing and experiment logs stored on the SD card.'),
    section('Environment'),
    envHost,
    section('Logs', button('', { size: 'sm', kind: 'ghost', iconName: 'refresh', title: 'Refresh logs', onclick: () => { Object.keys(tails).forEach((k) => (tails[k] = null)); loadLogs(); } })),
    logsHost,
  );
  renderEnv();
  renderLogs();
  loadEnv();
  loadLogs();

  return {
    isDirty,
    canLeave: async () => !isDirty() || confirmDiscard(),
  };
}
