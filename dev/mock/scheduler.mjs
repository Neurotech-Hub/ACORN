// MOCK scheduler for UI development only.
// The authoritative Scheduler is implemented in firmware (Phase 3). This file exists so the
// UI can be exercised against realistic preview / next-event data without hardware.

export const DAY_MS = 86_400_000;

export function parseHms(s) {
  const m = /^(\d{2}):(\d{2})(?::(\d{2}))?$/.exec(s || '');
  if (!m) return null;
  const h = +m[1], mi = +m[2], se = +(m[3] || 0);
  if (h > 23 || mi > 59 || se > 59) return null;
  return (h * 3600 + mi * 60 + se) * 1000;
}

export function fnv1a(str) {
  let h = 0x811c9dc5;
  for (let i = 0; i < str.length; i++) {
    h ^= str.charCodeAt(i);
    h = Math.imul(h, 0x01000193) >>> 0;
  }
  return h >>> 0;
}

function mulberry32(a) {
  return () => {
    a |= 0; a = (a + 0x6d2b79f5) | 0;
    let t = Math.imul(a ^ (a >>> 15), 1 | a);
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

export function canonical(v) {
  if (Array.isArray(v)) return '[' + v.map(canonical).join(',') + ']';
  if (v && typeof v === 'object') {
    return '{' + Object.keys(v).sort().map((k) => JSON.stringify(k) + ':' + canonical(v[k])).join(',') + '}';
  }
  return JSON.stringify(v);
}

export function programRevision(p) {
  return fnv1a(canonical({ schema_version: p.schema_version, events: p.events || [] }))
    .toString(16).padStart(8, '0');
}

const isoUtc = (ms) => new Date(ms).toISOString().replace(/\.\d{3}Z$/, 'Z');
const isoLocal = (localMs) => new Date(localMs).toISOString().slice(0, 19);
const localDate = (localMs) => new Date(localMs).toISOString().slice(0, 10);

// Local wall-clock times are represented as "local epoch" ms (UTC epoch shifted by the offset).
function rangeBounds(ev, dayMs) {
  const s = dayMs + parseHms(ev.start);
  let e = dayMs + parseHms(ev.end);
  if (e <= s) e += DAY_MS;
  return [s, e];
}

function occurrences(ev, dayMs, revision) {
  if (ev.type === 'point') return [dayMs + parseHms(ev.time)];
  const [s, e] = rangeBounds(ev, dayMs);
  const out = [];
  const r = ev.recurrence;
  if (r.type === 'fixed') {
    const iv = r.interval_seconds * 1000;
    for (let n = 0; ; n++) {
      const t = s + n * iv;
      if (t >= e) break;
      out.push(t);
    }
  } else {
    const rng = mulberry32(fnv1a(`${revision}|${ev.event_id}|${localDate(dayMs)}`));
    const mn = r.min_interval_seconds, mx = r.max_interval_seconds;
    for (let t = s; t < e; t += (mn + Math.floor(rng() * (mx - mn + 1))) * 1000) out.push(t);
  }
  return out;
}

function toEntry(ev, t, off) {
  return {
    event_id: ev.event_id,
    type: ev.type,
    scheduled_utc: isoUtc(t - off),
    scheduled_local: isoLocal(t),
    audio: ev.audio,
    volume: ev.volume,
  };
}

/** Events with fromUtc <= t < toUtc, ascending. */
export function eventsBetween(program, offsetMin, fromUtcMs, toUtcMs) {
  const rev = programRevision(program);
  const off = offsetMin * 60_000;
  const from = fromUtcMs + off, to = toUtcMs + off;
  const out = [];
  for (let day = Math.floor(from / DAY_MS) * DAY_MS - DAY_MS; day < to; day += DAY_MS) {
    for (const ev of program.events) {
      for (const t of occurrences(ev, day, rev)) if (t >= from && t < to) out.push([t, ev]);
    }
  }
  out.sort((a, b) => a[0] - b[0]);
  return out.map(([t, ev]) => toEntry(ev, t, off));
}

/** Next `count` events strictly after now. */
export function preview(program, offsetMin, nowUtcMs, count) {
  if (!program.events.length) return [];
  const rev = programRevision(program);
  const off = offsetMin * 60_000;
  const now = nowUtcMs + off;
  const pending = [];
  const out = [];
  const firstDay = Math.floor(now / DAY_MS) * DAY_MS - DAY_MS;
  for (let day = firstDay; day < firstDay + 400 * DAY_MS && out.length < count; day += DAY_MS) {
    for (const ev of program.events) {
      for (const t of occurrences(ev, day, rev)) if (t > now) pending.push([t, ev]);
    }
    pending.sort((a, b) => a[0] - b[0]);
    const boundary = day + DAY_MS;
    while (pending.length && pending[0][0] < boundary && out.length < count) {
      const [t, ev] = pending.shift();
      out.push(toEntry(ev, t, off));
    }
  }
  return out;
}

/** Whether "now" falls inside a RANGE window, and until when / when the next begins. */
export function windowState(program, offsetMin, nowUtcMs) {
  const off = offsetMin * 60_000;
  const now = nowUtcMs + off;
  const ranges = program.events.filter((e) => e.type === 'range');
  if (!ranges.length) return { state: 'points_only' };
  const today = Math.floor(now / DAY_MS) * DAY_MS;
  let nextStart = Infinity, nextEvent = null;
  for (const ev of ranges) {
    for (const day of [today - DAY_MS, today, today + DAY_MS]) {
      const [s, e] = rangeBounds(ev, day);
      if (s <= now && now < e) {
        return { state: 'active', event_id: ev.event_id, until_local: isoLocal(e), until_utc: isoUtc(e - off) };
      }
      if (s > now && s < nextStart) { nextStart = s; nextEvent = ev; }
    }
  }
  return {
    state: 'quiet',
    event_id: nextEvent.event_id,
    until_local: isoLocal(nextStart),
    until_utc: isoUtc(nextStart - off),
  };
}

export { isoUtc, isoLocal };
