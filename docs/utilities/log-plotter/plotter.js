// Log Plotter: summary cards, a copyable text digest and plots for one ACORN playback or
// environment log. Everything runs locally; the file never leaves the browser.

import { h, replace, icon, button, setButton, busy, badge, notice, toast, initTheme, fmtBytes } from '../shared/js/ui-helpers.js';
import { parseLog, LogError, stats, tally, sites, cadence } from '../shared/js/acorn-logs.js';
import { timeSeries, columns, legend, parts, fmtDay } from '../shared/js/charts.js';
import { demoPlayback, demoEnvironment, DEMO_OFFSET_MIN } from './demo.js';

const MIN = 60_000;
const HOUR = 60 * MIN;
const DAY = 24 * HOUR;
const LARGE_FILE = 50 * 1024 * 1024;
const OFFSETS = Array.from({ length: (840 + 720) / 15 + 1 }, (_, i) => -720 + i * 15);
const KIND_LABEL = { playback: 'Playback log', environment: 'Environment log' };
const SOURCE_LABEL = { scheduled: 'scheduled', manual_ui: 'manual (UI)', manual_test: 'manual test' };

initTheme();

const state = { name: '', size: 0, log: null, error: null, loading: false, offset: 0 };
let charts = [];

// ---- Formatting -------------------------------------------------------------------------

const pad2 = (n) => String(n).padStart(2, '0');
const fmtOffset = (o) => (o === 0 ? 'UTC' : `UTC${o < 0 ? '−' : '+'}${pad2(Math.floor(Math.abs(o) / 60))}:${pad2(Math.abs(o) % 60)}`);
const fmtInt = (n) => n.toLocaleString();
const fmtFixed = (v, d) => v.toLocaleString(undefined, { minimumFractionDigits: d, maximumFractionDigits: d });
const fmtSigned = (v, d, unit) => `${v < 0 ? '−' : '+'}${fmtFixed(Math.abs(v), d)}${unit}`;
const fmtCoord = (v) => v.toFixed(6).replace('-', '−');

function fmtStamp(ms) {
  const p = parts(ms, state.offset);
  return `${p.y}-${pad2(p.mo + 1)}-${pad2(p.d)} ${pad2(p.h)}:${pad2(p.mi)}`;
}

function fmtSpan(ms) {
  if (ms == null) return '—';
  const sec = ms / 1000;
  if (sec < 60) return `${fmtFixed(sec, sec < 10 ? 2 : 1)} s`;
  const both = (a, ua, b, ub) => (b ? `${a} ${ua} ${b} ${ub}` : `${a} ${ua}`);
  const m = Math.floor(sec / 60);
  if (m < 60) return both(m, 'min', Math.round(sec % 60), 's');
  const hr = Math.floor(m / 60);
  if (hr < 24) return both(hr, 'h', m % 60, 'min');
  return both(Math.floor(hr / 24), 'd', hr % 24, 'h');
}

const top = (pairs, n = 3) => pairs.slice(0, n).map(([k, c]) => `${k} ${fmtInt(c)}`).join(' · ') + (pairs.length > n ? ` · +${pairs.length - n} more` : '');

function deviceFromName(name) {
  return /^(ACORN-[A-Z0-9]+)_(playback|environment)/i.exec(name)?.[1]?.toUpperCase() || null;
}

// ---- Summaries --------------------------------------------------------------------------

function spanTile(r) {
  const first = r[0].t;
  const last = r[r.length - 1].t;
  return { label: 'Time span', value: fmtSpan(last - first), sub: `${fmtStamp(first)} → ${fmtStamp(last)}` };
}

function batteryTile(r) {
  const b = r.filter((x) => x.batteryV != null);
  if (!b.length) return { label: 'Battery', value: '—', sub: 'No battery readings' };
  const first = b[0];
  const last = b[b.length - 1];
  const low = stats(b.map((x) => x.batteryV)).min;
  const pct = (x) => (x.batteryPct != null ? `${x.batteryPct}%` : '?');
  return {
    label: 'Battery', value: `${fmtFixed(last.batteryV, 2)} V`,
    sub: `${pct(first)} → ${pct(last)} · ${fmtSigned(last.batteryV - first.batteryV, 2, ' V')} · low ${fmtFixed(low, 2)} V`,
    tone: last.batteryPct != null && last.batteryPct <= 20 ? 'warn' : '',
  };
}

function siteTile(log) {
  const list = sites(log.records);
  if (!list.length) {
    return { label: 'Site', value: 'Not recorded', sub: log.hasSite ? 'Latitude/longitude cells are empty.' : 'This log has no latitude/longitude columns (older firmware).' };
  }
  const latest = list.reduce((a, b) => (b.last > a.last ? b : a));
  const where = `${fmtCoord(latest.lat)}, ${fmtCoord(latest.lon)}`;
  if (list.length === 1) return { label: 'Site', value: where, mono: true, sub: `on ${fmtInt(latest.count)} of ${fmtInt(log.records.length)} rows` };
  return { label: 'Site', value: `${list.length} locations`, sub: `latest ${where}` };
}

function rangeTile(label, values, unit, d) {
  const st = stats(values);
  if (!st) return { label, value: '—', sub: 'No readings' };
  return { label, value: `${fmtFixed(st.mean, d)} ${unit}`, sub: `mean · min ${fmtFixed(st.min, d)} · max ${fmtFixed(st.max, d)}` };
}

function playbackTiles(log) {
  const r = log.records;
  const n = r.length;
  const count = (k) => r.filter((x) => x.result === k).length;
  const ok = count('ok');
  const stopped = count('stopped');
  const errors = count('error');
  const span = r[n - 1].t - r[0].t;
  const sources = tally(r.map((x) => x.source || 'unknown'));
  const scheduled = r.filter((x) => x.source === 'scheduled').length;
  const programs = tally(r.filter((x) => x.programId).map((x) => (x.revision ? `${x.programId} (rev ${x.revision})` : x.programId)));
  const events = tally(r.filter((x) => x.eventId).map((x) => x.eventId));
  const audio = tally(r.filter((x) => x.audio).map((x) => x.audio.replace(/^\/audio\//, '')));
  const played = r.filter((x) => x.result !== 'error' && x.durationMs != null);
  const dur = stats(played.map((x) => x.durationMs));
  const total = played.reduce((a, x) => a + x.durationMs, 0);
  const late = stats(r.filter((x) => x.source === 'scheduled' && x.scheduled != null).map((x) => x.t - x.scheduled));
  const codes = tally(r.filter((x) => x.errorCode).map((x) => x.errorCode));
  const days = localDays(r).n;
  return [
    { label: 'Playbacks', value: fmtInt(n), sub: days > 1 ? `${fmtFixed(n / days, 1)} per day over ${days} days` : `in ${fmtSpan(span)}` },
    spanTile(r),
    {
      label: 'Completed', value: `${fmtFixed((ok / n) * 100, 1)}%`, tone: errors ? 'fault' : stopped ? 'warn' : '',
      sub: `${fmtInt(ok)} ok · ${fmtInt(stopped)} stopped · ${fmtInt(errors)} error${errors === 1 ? '' : 's'}`,
    },
    { label: 'Sources', value: `${fmtInt(scheduled)} scheduled`, sub: top(sources.filter(([k]) => k !== 'scheduled').map(([k, c]) => [SOURCE_LABEL[k] || k, c])) || 'No manual playback' },
    { label: 'Programs', value: fmtInt(programs.length), sub: top(programs, 2) || 'None recorded' },
    { label: 'Events', value: fmtInt(events.length), sub: top(events) || 'None recorded' },
    { label: 'Audio', value: `${fmtInt(audio.length)} file${audio.length === 1 ? '' : 's'}`, sub: top(audio) },
    { label: 'Duration', value: dur ? fmtSpan(dur.median) : '—', sub: `median · ${fmtSpan(total)} played in total` },
    late && { label: 'Start delay', value: fmtSigned(late.median / 1000, 2, ' s'), sub: `median for scheduled plays · max ${fmtSigned(late.max / 1000, 2, ' s')}` },
    codes.length && { label: 'Errors', value: fmtInt(codes.reduce((a, [, c]) => a + c, 0)), sub: top(codes), tone: 'fault' },
    batteryTile(r),
    rangeTile('Temperature at playback', r.map((x) => x.tempC), '°C', 1),
    siteTile(log),
  ].filter(Boolean);
}

function environmentTiles(log) {
  const r = log.records;
  const cad = cadence(r);
  const blanks = r.filter((x) => x.tempC == null || x.rh == null || x.lux == null).length;
  const light = stats(r.map((x) => x.lux));
  const longest = cad.gaps.reduce((a, g) => Math.max(a, g.ms), 0);
  return [
    { label: 'Samples', value: fmtInt(r.length), sub: cad.intervalMs ? `median interval ${fmtSpan(cad.intervalMs)}` : 'single sample' },
    spanTile(r),
    {
      label: 'Coverage', value: cad.coverage == null ? '—' : `${fmtFixed(cad.coverage * 100, 1)}%`, tone: cad.gaps.length ? 'warn' : '',
      sub: cad.gaps.length ? `${cad.gaps.length} gap${cad.gaps.length === 1 ? '' : 's'} · longest ${fmtSpan(longest)}` : 'No gaps',
    },
    rangeTile('Temperature', r.map((x) => x.tempC), '°C', 1),
    rangeTile('Humidity', r.map((x) => x.rh), '%', 1),
    { label: 'Light', value: light ? `${fmtInt(Math.round(light.max))} lux` : '—', sub: light ? `max · mean ${fmtInt(Math.round(light.mean))} lux` : 'No readings' },
    batteryTile(r),
    { label: 'Missing readings', value: fmtInt(blanks), sub: blanks ? 'rows with a blank sensor value' : 'All sensor values present', tone: blanks ? 'warn' : '' },
    siteTile(log),
  ];
}

const tilesFor = (log) => (log.kind === 'playback' ? playbackTiles(log) : environmentTiles(log));

function digest(log, tiles) {
  const device = deviceFromName(state.name);
  return [
    `ACORN ${KIND_LABEL[log.kind].toLowerCase()}: ${state.name}`,
    device && `Device: ${device}`,
    `Times: ${fmtOffset(state.offset)}`,
    '',
    ...tiles.map((t) => `${t.label}: ${t.value}${t.sub ? ` (${t.sub})` : ''}`),
    '',
    'Summarized with ACORN Log Plotter (neurotech-hub.github.io/ACORN/utilities/log-plotter/)',
  ].filter((l) => l !== null && l !== undefined && l !== false).join('\n');
}

async function copyText(text) {
  try {
    await navigator.clipboard.writeText(text);
    return true;
  } catch {
    const ta = h('textarea', { style: { position: 'fixed', opacity: '0' } });
    ta.value = text;
    document.body.append(ta);
    ta.select();
    const ok = document.execCommand('copy');
    ta.remove();
    return ok;
  }
}

// ---- Elements ---------------------------------------------------------------------------

const view = document.getElementById('view');
const fileInput = h('input', { type: 'file', accept: '.csv,.bak,text/csv', hidden: true });
const sourceBody = h('div');
const summaryBody = h('div');
const plotsBody = h('div');
const printMeta = h('p', { class: 'print-only muted small' });

const demoPlaybackBtn = button('Playback log', { size: 'sm', kind: 'ghost', iconName: 'audio', onclick: () => loadDemo('playback') });
const demoEnvBtn = button('Environment log', { size: 'sm', kind: 'ghost', iconName: 'data', onclick: () => loadDemo('environment') });
const copyBtn = button('Copy summary', { size: 'sm', kind: 'ghost', iconName: 'copy', title: 'Copy a text summary to the clipboard' });
const printBtn = button('Print / PDF', { size: 'sm', kind: 'ghost', iconName: 'print', title: 'Print, or save as PDF from the print dialog', onclick: () => window.print() });

const sourceCard = h('section', { class: 'card', 'aria-labelledby': 'source-title' },
  h('div', { class: 'card-head' }, h('span', { class: 'step' }, '01'), h('h3', { id: 'source-title' }, 'Log file')),
  sourceBody,
  h('div', { class: 'demo-row' }, h('span', { class: 'muted small' }, 'No log at hand? Try demo data:'), demoPlaybackBtn, demoEnvBtn));

const summaryCard = h('section', { class: 'card disabled', 'aria-labelledby': 'summary-title' },
  h('div', { class: 'card-head' }, h('span', { class: 'step' }, '02'), h('h3', { id: 'summary-title' }, 'Summary'),
    h('div', { class: 'actions' }, copyBtn, printBtn)),
  summaryBody);

const plotsCard = h('section', { class: 'card disabled', 'aria-labelledby': 'plots-title' },
  h('div', { class: 'card-head' }, h('span', { class: 'step' }, '03'), h('h3', { id: 'plots-title' }, 'Plots')),
  plotsBody);

replace(view,
  h('a', { class: 'back', href: '../' }, icon('back', 'sm'), 'All utilities'),
  h('div', { class: 'page-head' },
    h('h1', null, 'Log Plotter'),
    h('p', { class: 'sub' }, 'Summarize and plot an ACORN playback or environment log from the SD card. Copy the summary to share it, or print the page to PDF.'),
    printMeta),
  h('div', { class: 'row', style: { marginBottom: '16px' } },
    badge('playback.csv'), badge('environment.csv'), badge('CSV')),
  h('div', { class: 'stack' }, sourceCard, summaryCard, plotsCard),
  fileInput);

// ---- Source -----------------------------------------------------------------------------

function dropzone(message) {
  const dz = h('div', { class: 'dropzone', role: 'button', tabindex: 0 },
    icon('upload', 'lg'),
    h('div', null, h('b', null, message || 'Choose a log file'), ' — tap to browse or drop it here'),
    h('div', { class: 'small' }, h('code', null, '/logs/playback.csv'), ' or ', h('code', null, '/logs/environment.csv'), ', or a download from the device’s Data page'));
  dz.onclick = () => fileInput.click();
  dz.onkeydown = (e) => { if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); fileInput.click(); } };
  return dz;
}

function renderSource() {
  const { log, error, loading } = state;
  if (loading) {
    replace(sourceBody, h('div', { class: 'dropzone', 'aria-busy': 'true' }, icon('data', 'lg'), h('div', null, h('b', null, `Reading ${state.name}…`))));
    return;
  }
  if (error) {
    replace(sourceBody, h('div', { class: 'stack' }, notice('fault', `Could not read ${state.name}.`, error), dropzone('Choose another file')));
    return;
  }
  if (!log) { replace(sourceBody, dropzone()); return; }

  const tz = h('select', { class: 'select', id: 'tz' }, OFFSETS.map((o) => h('option', { value: o, selected: o === state.offset }, fmtOffset(o))));
  tz.onchange = () => { state.offset = +tz.value; renderResults(); };
  const browserOffset = -new Date().getTimezoneOffset();
  const device = deviceFromName(state.name);
  replace(sourceBody, h('div', { class: 'source' },
    h('div', { class: 'source-name' }, state.name, badge(KIND_LABEL[log.kind], 'ok', 'check')),
    h('dl', { class: 'kv' },
      device && h('dt', null, 'Device'), device && h('dd', { class: 'mono' }, device),
      h('dt', null, 'Rows'), h('dd', null, fmtInt(log.records.length)),
      h('dt', null, 'Columns'), h('dd', null, log.hasSite ? 'Current layout, with latitude/longitude' : 'Older layout, without latitude/longitude'),
      h('dt', null, 'Size'), h('dd', null, fmtBytes(state.size))),
    log.malformed > 0 && notice('warn', `${fmtInt(log.malformed)} row${log.malformed === 1 ? '' : 's'} skipped.`, 'They had no valid timestamp or too few columns.'),
    state.size > LARGE_FILE && notice('warn', `Large file (${fmtBytes(state.size)}).`, 'Plots may be slow to draw, especially on phones.'),
    h('div', { class: 'form-row' },
      h('div', { class: 'field' }, h('label', { for: 'tz' }, 'Show times in'), tz,
        h('div', { class: 'hint' }, 'Logs are stored in UTC. Pick the site’s UTC offset to read plots in local time',
          browserOffset !== state.offset ? ` (this browser is ${fmtOffset(browserOffset)}).` : '.'))),
    h('div', { class: 'row' }, button('Choose another file', { size: 'sm', iconName: 'upload', onclick: () => fileInput.click() }))));
}

async function loadText(name, size, readText, offset = 0) {
  Object.assign(state, { name, size, log: null, error: null, loading: true, offset });
  renderAll();
  try {
    const log = parseLog(await readText());
    if (!log.records.length) throw new LogError(`This ${KIND_LABEL[log.kind].toLowerCase()} has a header but no rows yet.`);
    state.log = log;
  } catch (e) {
    if (!(e instanceof LogError)) console.error(e);
    state.error = e instanceof LogError ? e.message : 'The file could not be read as text.';
  }
  state.loading = false;
  renderAll();
}

function loadFile(file) {
  if (!file) return;
  loadText(file.name, file.size, () => file.text());
}

async function loadDemo(kind) {
  const btn = kind === 'playback' ? demoPlaybackBtn : demoEnvBtn;
  await busy(btn, async () => {
    const demo = kind === 'playback' ? demoPlayback() : demoEnvironment();
    await loadText(demo.name, new Blob([demo.text]).size, async () => demo.text, DEMO_OFFSET_MIN);
  });
}

fileInput.onchange = () => { const f = fileInput.files[0]; fileInput.value = ''; loadFile(f); };

let dragDepth = 0;
const dzHighlight = (on) => sourceBody.querySelector('.dropzone')?.classList.toggle('over', on);
window.addEventListener('dragenter', (e) => { e.preventDefault(); dragDepth++; dzHighlight(true); });
window.addEventListener('dragleave', () => { if (--dragDepth <= 0) { dragDepth = 0; dzHighlight(false); } });
window.addEventListener('dragover', (e) => e.preventDefault());
window.addEventListener('drop', (e) => {
  e.preventDefault();
  dragDepth = 0;
  dzHighlight(false);
  const f = e.dataTransfer?.files?.[0];
  if (f) loadFile(f);
});

// ---- Summary ----------------------------------------------------------------------------

function renderSummary() {
  const { log } = state;
  summaryCard.classList.toggle('disabled', !log);
  if (!log) {
    replace(summaryBody, h('p', { class: 'muted' }, 'Load a log to see its key numbers.'));
    copyBtn.onclick = null;
    return;
  }
  const tiles = tilesFor(log);
  replace(summaryBody, h('div', { class: 'stats' }, tiles.map((t) =>
    h('div', { class: `stat ${t.tone || ''}` },
      h('div', { class: 'eyebrow' }, t.label),
      h('div', { class: `value ${t.mono ? 'mono' : ''}` }, t.value),
      t.sub && h('div', { class: 'sub' }, t.sub)))));
  copyBtn.onclick = async () => {
    if (await copyText(digest(log, tiles))) {
      toast('Summary copied to the clipboard.', 'ok');
      setButton(copyBtn, 'Copied', 'check');
      setTimeout(() => setButton(copyBtn, 'Copy summary', 'copy'), 1600);
    } else {
      toast('Could not copy. Select the summary and copy it manually.', 'fault');
    }
  };
}

// ---- Plots ------------------------------------------------------------------------------

function chartBlock(title, sub, draw, legendItems) {
  const host = h('div');
  const block = h('div', { class: 'chart-block' },
    h('div', { class: 'chart-head' }, h('h4', null, title), sub && h('span', { class: 'muted small' }, sub)),
    legendItems && legend(legendItems),
    host);
  return { block, start: () => charts.push(draw(host)) };
}

function hbars(title, pairs) {
  const max = Math.max(1, ...pairs.map(([, c]) => c));
  return h('div', { class: 'chart-block' },
    h('div', { class: 'chart-head' }, h('h4', null, title), h('span', { class: 'muted small' }, `${pairs.length} distinct`)),
    h('div', { class: 'hbars' }, pairs.slice(0, 12).map(([name, c]) =>
      h('div', { class: 'hbar' },
        h('span', { class: 'name mono', title: name }, name),
        h('span', { class: 'track' }, h('i', { style: { width: `${(c / max) * 100}%` } })),
        h('span', { class: 'n num' }, fmtInt(c))))),
    pairs.length > 12 && h('div', { class: 'muted small' }, `+${pairs.length - 12} more`));
}

const series = (r, key) => r.map((x) => ({ t: x.t, v: x[key] }));
const has = (r, key) => r.some((x) => x[key] != null);
const rangeSub = (r, key, unit, d) => {
  const st = stats(r.map((x) => x[key]));
  return st ? `min ${fmtFixed(st.min, d)} · max ${fmtFixed(st.max, d)} ${unit}` : '';
};

function localDays(r) {
  const off = state.offset * MIN;
  const first = Math.floor((r[0].t + off) / DAY);
  const last = Math.floor((r[r.length - 1].t + off) / DAY);
  return { first, n: last - first + 1, index: (t) => Math.floor((t + off) / DAY) - first, start: (i) => (first + i) * DAY - off };
}

function playbackPlots(log) {
  const r = log.records;
  const days = localDays(r);
  const results = [['ok', 'c-ok', 'ok'], ['stopped', 'c-warn', 'stopped'], ['error', 'c-fault', 'error']];
  const byDay = results.map(([key, cls, name]) => ({ name, cls, values: Array(days.n).fill(0) }));
  const other = { name: 'other', cls: 'c-muted', values: Array(days.n).fill(0) };
  for (const x of r) {
    const i = days.index(x.t);
    const k = results.findIndex(([key]) => key === x.result);
    (k >= 0 ? byDay[k] : other).values[i]++;
  }
  const dayStacks = [...byDay, other].filter((st) => st.values.some(Boolean));
  const scheduled = { name: 'scheduled', cls: 'c-ink', values: Array(24).fill(0) };
  const manual = { name: 'manual', cls: 'c-lux', values: Array(24).fill(0) };
  for (const x of r) (x.source === 'scheduled' ? scheduled : manual).values[parts(x.t, state.offset).h]++;
  const zone = fmtOffset(state.offset);

  const blocks = [
    chartBlock('Playbacks per day', zone, (host) => columns(host, {
      categories: Array.from({ length: days.n }, (_, i) => fmtDay(days.start(i), state.offset)),
      stacks: dayStacks, height: 190,
    }), dayStacks.length > 1 && dayStacks),
    chartBlock('Time of day', `hour (${zone})`, (host) => columns(host, {
      categories: Array.from({ length: 24 }, (_, i) => pad2(i)), labelEvery: 3, height: 170,
      stacks: [scheduled, manual].filter((st) => st.values.some(Boolean)),
      tipLabel: (i) => `${pad2(i)}:00–${pad2(i)}:59`,
    }), manual.values.some(Boolean) && [scheduled, manual]),
    has(r, 'batteryV') && chartBlock('Battery voltage', rangeSub(r, 'batteryV', 'V', 2), (host) => timeSeries(host, {
      series: [{ name: 'Battery', cls: 'c-batt', points: series(r, 'batteryV'), decimals: 2, unit: 'V' }],
      offsetMin: state.offset, gapMs: 3 * HOUR, height: 180,
    })),
    has(r, 'tempC') && chartBlock('Temperature at playback', rangeSub(r, 'tempC', '°C', 1), (host) => timeSeries(host, {
      series: [{ name: 'Temperature', cls: 'c-temp', points: series(r, 'tempC'), decimals: 1, unit: '°C' }],
      offsetMin: state.offset, gapMs: 3 * HOUR, height: 180,
    })),
  ].filter(Boolean);

  const events = tally(r.filter((x) => x.eventId).map((x) => x.eventId));
  const audio = tally(r.filter((x) => x.audio).map((x) => x.audio.replace(/^\/audio\//, '')));
  replace(plotsBody, h('div', { class: 'charts' },
    blocks.map((b) => b.block),
    h('div', { class: 'two-col' },
      events.length ? hbars('By event', events) : h('div', { class: 'muted small' }, 'No event IDs in this log.'),
      hbars('By audio file', audio))));
  blocks.forEach((b) => b.start());
}

function environmentPlots(log) {
  const r = log.records;
  const cad = cadence(r);
  const gapMs = cad.intervalMs ? cad.intervalMs * 2.5 : Infinity;
  const common = { offsetMin: state.offset, gapMs, height: 190 };
  const ts = (key, name, cls, unit, decimals) => ({ series: [{ name, cls, points: series(r, key), decimals, unit }], ...common });

  const hours = Array.from({ length: 24 }, () => []);
  for (const x of r) if (x.tempC != null) hours[parts(x.t, state.offset).h].push(x.tempC);
  const profile = hours.map((v, hr) => ({ hr, st: stats(v) })).filter((p) => p.st);

  const blocks = [
    has(r, 'tempC') && chartBlock('Temperature', rangeSub(r, 'tempC', '°C', 1), (host) => timeSeries(host, ts('tempC', 'Temperature', 'c-temp', '°C', 1))),
    has(r, 'rh') && chartBlock('Relative humidity', rangeSub(r, 'rh', '%', 1), (host) => timeSeries(host, { ...ts('rh', 'Humidity', 'c-rh', '%', 1) })),
    has(r, 'lux') && chartBlock('Ambient light', rangeSub(r, 'lux', 'lux', 0), (host) => timeSeries(host, { ...ts('lux', 'Light', 'c-lux', 'lux', 0), yMin: 0 })),
    has(r, 'batteryV') && chartBlock('Battery voltage', rangeSub(r, 'batteryV', 'V', 2), (host) => timeSeries(host, ts('batteryV', 'Battery', 'c-batt', 'V', 2))),
    profile.length > 1 && chartBlock('Daily temperature profile', `mean and range by hour (${fmtOffset(state.offset)})`, (host) => timeSeries(host, {
      x: 'hour', height: 190,
      series: [{ name: 'Mean', cls: 'c-temp', points: profile.map((p) => ({ t: p.hr + 0.5, v: p.st.mean })), decimals: 1, unit: '°C' }],
      band: { cls: 'c-temp', points: profile.map((p) => ({ t: p.hr + 0.5, lo: p.st.min, hi: p.st.max })) },
      fmtX: (t) => `${pad2(Math.floor(t))}:00–${pad2(Math.floor(t))}:59`,
    })),
  ].filter(Boolean);

  replace(plotsBody, h('div', { class: 'charts' },
    blocks.length ? blocks.map((b) => b.block) : h('p', { class: 'muted' }, 'No sensor or battery readings to plot.'),
    cad.gaps.length > 0 && h('p', { class: 'muted small' },
      `Breaks in the lines are gaps in the log (no sample for over ${fmtSpan(cad.intervalMs * 1.5)}), e.g. the device was off or the card was out. `,
      `Longest: ${fmtStamp(cad.gaps.reduce((a, g) => (g.ms > a.ms ? g : a)).from)} for ${fmtSpan(cad.gaps.reduce((a, g) => Math.max(a, g.ms), 0))}.`)));
  blocks.forEach((b) => b.start());
}

function renderPlots() {
  charts.forEach((c) => c.destroy());
  charts = [];
  const { log } = state;
  plotsCard.classList.toggle('disabled', !log);
  if (!log) { replace(plotsBody, h('p', { class: 'muted' }, 'Plots appear here once a log is loaded.')); return; }
  if (log.kind === 'playback') playbackPlots(log); else environmentPlots(log);
}

function renderResults() {
  const now = new Date();
  printMeta.textContent = state.log
    ? `${state.name} · times in ${fmtOffset(state.offset)} · printed ${now.toISOString().slice(0, 16).replace('T', ' ')} UTC`
    : '';
  renderSummary();
  renderPlots();
}

function renderAll() {
  demoPlaybackBtn.disabled = demoEnvBtn.disabled = state.loading;
  renderSource();
  renderResults();
}

renderAll();
