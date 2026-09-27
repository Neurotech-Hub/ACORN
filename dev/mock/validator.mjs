// MOCK ProgramValidator for UI development only (see scheduler.mjs note).

import { parseHms, DAY_MS } from './scheduler.mjs';

export const LIMITS = {
  MAX_PROGRAMS: 16,
  MAX_EVENTS: 32,
  MAX_NAME: 48,
  MAX_DESCRIPTION: 200,
  MAX_AUDIO_NAME: 32,
  SCHEDULE_GUARD_MS: 2000,
};

const ID_RE = /^[a-z0-9][a-z0-9-]{0,31}$/;

export function validateProgram(p, audioByPath) {
  const errors = [];
  const err = (code, message, field) => errors.push({ code, message, field });

  if (!p || typeof p !== 'object' || Array.isArray(p)) {
    err('PROGRAM_JSON_INVALID', 'Program must be a JSON object.', '');
    return errors;
  }
  if (p.schema_version !== 1) {
    err('PROGRAM_SCHEMA_UNSUPPORTED', `schema_version ${p.schema_version} is not supported (expected 1).`, 'schema_version');
  }
  if (typeof p.id !== 'string' || !ID_RE.test(p.id)) {
    err('PROGRAM_JSON_INVALID', 'ID must be 1–32 characters: lowercase letters, digits and hyphens.', 'id');
  }
  if (typeof p.name !== 'string' || !p.name.trim()) err('PROGRAM_JSON_INVALID', 'Name is required.', 'name');
  else if (p.name.length > LIMITS.MAX_NAME) err('PROGRAM_LIMIT_EXCEEDED', `Name exceeds ${LIMITS.MAX_NAME} characters.`, 'name');
  if (p.description != null && typeof p.description !== 'string') err('PROGRAM_JSON_INVALID', 'Description must be text.', 'description');
  else if ((p.description || '').length > LIMITS.MAX_DESCRIPTION) err('PROGRAM_LIMIT_EXCEEDED', `Description exceeds ${LIMITS.MAX_DESCRIPTION} characters.`, 'description');

  if (!Array.isArray(p.events)) {
    err('PROGRAM_JSON_INVALID', 'events must be an array.', 'events');
    return errors;
  }
  if (p.events.length > LIMITS.MAX_EVENTS) {
    err('PROGRAM_LIMIT_EXCEEDED', `A program may contain at most ${LIMITS.MAX_EVENTS} events.`, 'events');
  }

  const seen = new Set();
  const ranges = [];
  const points = [];

  p.events.forEach((ev, i) => {
    const f = (k) => `events[${i}]${k ? '.' + k : ''}`;
    if (!ev || typeof ev !== 'object') { err('PROGRAM_JSON_INVALID', 'Event must be an object.', f()); return; }
    if (typeof ev.event_id !== 'string' || !ID_RE.test(ev.event_id)) {
      err('PROGRAM_JSON_INVALID', 'Event ID must be 1–32 characters: lowercase letters, digits and hyphens.', f('event_id'));
    } else if (seen.has(ev.event_id)) {
      err('PROGRAM_JSON_INVALID', `Event ID '${ev.event_id}' is used more than once.`, f('event_id'));
    } else seen.add(ev.event_id);

    if (!Number.isInteger(ev.volume) || ev.volume < 0 || ev.volume > 100) {
      err('PROGRAM_JSON_INVALID', 'Volume must be an integer from 0 to 100.', f('volume'));
    }

    let durationMs = 0;
    const audio = audioByPath(ev.audio);
    if (!audio) err('PROGRAM_AUDIO_MISSING', `Audio file '${ev.audio || ''}' was not found on the SD card.`, f('audio'));
    else if (!audio.valid) err('PROGRAM_AUDIO_INVALID', `Audio file '${ev.audio}' is not a supported WAV (${audio.error.message}).`, f('audio'));
    else durationMs = audio.duration_ms;

    if (ev.type === 'point') {
      const t = parseHms(ev.time);
      if (t == null) err('PROGRAM_JSON_INVALID', 'Time must be HH:MM or HH:MM:SS.', f('time'));
      else points.push({ i, t, d: durationMs, id: ev.event_id });
    } else if (ev.type === 'range') {
      const s = parseHms(ev.start), e = parseHms(ev.end);
      if (s == null) err('PROGRAM_RANGE_INVALID', 'Start must be HH:MM or HH:MM:SS.', f('start'));
      if (e == null) err('PROGRAM_RANGE_INVALID', 'End must be HH:MM or HH:MM:SS.', f('end'));
      if (s != null && e != null && s === e) err('PROGRAM_RANGE_INVALID', 'Start and end cannot be equal.', f('end'));
      const r = ev.recurrence || {};
      if (r.type === 'fixed') {
        if (!Number.isInteger(r.interval_seconds) || r.interval_seconds < 1) {
          err('PROGRAM_JSON_INVALID', 'Interval must be a positive whole number of seconds.', f('recurrence.interval_seconds'));
        } else if (durationMs && r.interval_seconds * 1000 <= durationMs + LIMITS.SCHEDULE_GUARD_MS) {
          err('PROGRAM_INTERVAL_TOO_SHORT',
            `Interval (${r.interval_seconds} s) must exceed the audio duration (${(durationMs / 1000).toFixed(1)} s) plus a ${LIMITS.SCHEDULE_GUARD_MS / 1000} s guard.`,
            f('recurrence.interval_seconds'));
        }
      } else if (r.type === 'random') {
        const mn = r.min_interval_seconds, mx = r.max_interval_seconds;
        if (!Number.isInteger(mn) || !Number.isInteger(mx) || mn < 1 || mx < mn) {
          err('PROGRAM_RANDOM_RANGE_INVALID', 'Random interval requires whole-second minimum ≥ 1 and maximum ≥ minimum.', f('recurrence.min_interval_seconds'));
        } else if (durationMs && mn * 1000 <= durationMs + LIMITS.SCHEDULE_GUARD_MS) {
          err('PROGRAM_INTERVAL_TOO_SHORT',
            `Minimum interval (${mn} s) must exceed the audio duration (${(durationMs / 1000).toFixed(1)} s) plus a ${LIMITS.SCHEDULE_GUARD_MS / 1000} s guard.`,
            f('recurrence.min_interval_seconds'));
        }
      } else {
        err('PROGRAM_JSON_INVALID', "Recurrence type must be 'fixed' or 'random'.", f('recurrence.type'));
      }
      if (s != null && e != null && s !== e) ranges.push({ i, s, e: e <= s ? e + DAY_MS : e, id: ev.event_id });
    } else {
      err('PROGRAM_JSON_INVALID', "Event type must be 'point' or 'range'.", f('type'));
    }
  });

  const intersects = (a0, a1, b0, b1) => a0 < b1 && b0 < a1;
  for (let a = 0; a < ranges.length; a++) {
    for (let b = a + 1; b < ranges.length; b++) {
      const A = ranges[a], B = ranges[b];
      const hit = [-DAY_MS, 0, DAY_MS].some((k) => intersects(A.s, A.e, B.s + k, B.e + k));
      if (hit) err('PROGRAM_RANGE_OVERLAP', `Range '${B.id}' overlaps range '${A.id}'.`, `events[${B.i}]`);
    }
  }
  for (const P of points) {
    for (const R of ranges) {
      if ([0, DAY_MS].some((k) => P.t + k >= R.s && P.t + k < R.e)) {
        err('PROGRAM_EVENT_COLLISION', `Point '${P.id}' falls inside range '${R.id}'.`, `events[${P.i}].time`);
      }
    }
  }
  for (let a = 0; a < points.length; a++) {
    for (let b = a + 1; b < points.length; b++) {
      const A = points[a], B = points[b];
      const ab = (B.t - A.t + DAY_MS) % DAY_MS, ba = (A.t - B.t + DAY_MS) % DAY_MS;
      if (ab < A.d + LIMITS.SCHEDULE_GUARD_MS || ba < B.d + LIMITS.SCHEDULE_GUARD_MS) {
        err('PROGRAM_EVENT_COLLISION', `Point '${B.id}' would overlap playback of point '${A.id}'.`, `events[${B.i}].time`);
      }
    }
  }
  return errors;
}
