// ACORN SD card logs (docs/init.md §§35–36): CSV parsing, log type detection and statistics.
// Columns are looked up by name, so logs from firmware without the trailing
// latitude,longitude columns (or their .bak copies) read the same way.

export const PLAYBACK_COLUMNS = [
  'actual_utc', 'scheduled_utc', 'program_id', 'program_revision', 'event_id', 'source', 'audio', 'volume',
  'duration_ms', 'result', 'error_code', 'battery_voltage', 'battery_percent', 'temperature_c',
  'relative_humidity_pct', 'ambient_light_lux',
];
export const ENVIRONMENT_COLUMNS = [
  'timestamp_utc', 'temperature_c', 'relative_humidity_pct', 'ambient_light_lux', 'battery_voltage', 'battery_percent',
];
export const SITE_COLUMNS = ['latitude', 'longitude'];

export class LogError extends Error {}

/** RFC 4180-style CSV: quoted fields, doubled quotes, CRLF or LF line endings. */
export function parseCsv(text) {
  const rows = [];
  let row = [];
  let cell = '';
  let quoted = false;
  for (let i = 0; i < text.length; i++) {
    const c = text[i];
    if (quoted) {
      if (c === '"' && text[i + 1] === '"') { cell += '"'; i++; }
      else if (c === '"') quoted = false;
      else cell += c;
    } else if (c === '"') quoted = true;
    else if (c === ',') { row.push(cell); cell = ''; }
    else if (c === '\n' || c === '\r') {
      if (c === '\r' && text[i + 1] === '\n') i++;
      row.push(cell); rows.push(row); row = []; cell = '';
    } else cell += c;
  }
  if (cell !== '' || row.length) { row.push(cell); rows.push(row); }
  return rows;
}

const sameColumns = (header, cols) =>
  (header.length === cols.length || (header.length === cols.length + 2 && SITE_COLUMNS.every((c, i) => header[cols.length + i] === c))) &&
  cols.every((c, i) => header[i] === c);

const num = (s) => {
  if (s == null || s.trim() === '') return null;
  const v = Number(s);
  return Number.isFinite(v) ? v : null;
};
const time = (s) => {
  if (!s) return null;
  const t = Date.parse(s);
  return Number.isFinite(t) ? t : null;
};

/** Parses a playback or environment log. Throws LogError for anything else. */
export function parseLog(text) {
  const rows = parseCsv(String(text).replace(/^\uFEFF/, ''));
  if (!rows.length || (rows.length === 1 && rows[0].join('') === '')) throw new LogError('The file is empty.');
  const header = rows[0].map((s) => s.trim());
  const kind = sameColumns(header, PLAYBACK_COLUMNS) ? 'playback' : sameColumns(header, ENVIRONMENT_COLUMNS) ? 'environment' : null;
  if (!kind) {
    throw new LogError(header[0] === 'actual_utc' || header[0] === 'timestamp_utc'
      ? 'The columns do not match an ACORN log. The file may have been edited in a spreadsheet; try the original from the SD card.'
      : 'This is not an ACORN log. Choose playback.csv or environment.csv from the card’s /logs folder, or a download from the device’s Data page.');
  }
  const hasSite = header.length > (kind === 'playback' ? PLAYBACK_COLUMNS : ENVIRONMENT_COLUMNS).length;
  const at = Object.fromEntries(header.map((c, i) => [c, i]));
  const records = [];
  let malformed = 0;
  for (const r of rows.slice(1)) {
    if (r.length === 1 && r[0].trim() === '') continue;
    const get = (c) => (at[c] == null ? '' : (r[at[c]] ?? ''));
    const t = time(get(kind === 'playback' ? 'actual_utc' : 'timestamp_utc'));
    if (t == null || r.length < header.length - (hasSite ? 2 : 0)) { malformed++; continue; }
    const base = {
      t,
      batteryV: num(get('battery_voltage')), batteryPct: num(get('battery_percent')),
      tempC: num(get('temperature_c')), rh: num(get('relative_humidity_pct')), lux: num(get('ambient_light_lux')),
      lat: num(get('latitude')), lon: num(get('longitude')),
    };
    records.push(kind === 'environment' ? base : {
      ...base,
      scheduled: time(get('scheduled_utc')),
      programId: get('program_id'), revision: get('program_revision'), eventId: get('event_id'),
      source: get('source'), audio: get('audio'), volume: num(get('volume')), durationMs: num(get('duration_ms')),
      result: get('result'), errorCode: get('error_code'),
    });
  }
  records.sort((a, b) => a.t - b.t);
  return { kind, hasSite, records, malformed };
}

// ---- Statistics -------------------------------------------------------------------------

export function stats(values) {
  const v = values.filter((x) => x != null && Number.isFinite(x));
  if (!v.length) return null;
  const sorted = [...v].sort((a, b) => a - b);
  const mid = sorted.length >> 1;
  return {
    n: v.length,
    min: sorted[0],
    max: sorted[sorted.length - 1],
    mean: v.reduce((a, b) => a + b, 0) / v.length,
    median: sorted.length % 2 ? sorted[mid] : (sorted[mid - 1] + sorted[mid]) / 2,
  };
}

/** Counts of each value, most frequent first: [[value, count], …]. */
export function tally(values) {
  const m = new Map();
  for (const v of values) m.set(v, (m.get(v) || 0) + 1);
  return [...m.entries()].sort((a, b) => b[1] - a[1] || String(a[0]).localeCompare(String(b[0])));
}

/** Distinct site fixes in the order they first appear. */
export function sites(records) {
  const out = new Map();
  for (const r of records) {
    if (r.lat == null || r.lon == null) continue;
    const key = `${r.lat.toFixed(6)},${r.lon.toFixed(6)}`;
    const s = out.get(key);
    if (s) { s.count++; s.last = r.t; } else out.set(key, { lat: r.lat, lon: r.lon, count: 1, first: r.t, last: r.t });
  }
  return [...out.values()];
}

/** Sampling interval and gaps (an interval longer than 1.5× the median). */
export function cadence(records) {
  const d = [];
  for (let i = 1; i < records.length; i++) d.push(records[i].t - records[i - 1].t);
  const s = stats(d);
  if (!s) return { intervalMs: null, gaps: [], coverage: null };
  const limit = s.median * 1.5;
  const gaps = [];
  for (let i = 1; i < records.length; i++) {
    const dt = records[i].t - records[i - 1].t;
    if (dt > limit) gaps.push({ from: records[i - 1].t, to: records[i].t, ms: dt });
  }
  const span = records[records.length - 1].t - records[0].t;
  const missing = gaps.reduce((a, g) => a + g.ms - s.median, 0);
  return { intervalMs: s.median, gaps, coverage: span > 0 ? Math.max(0, 1 - missing / span) : 1 };
}
