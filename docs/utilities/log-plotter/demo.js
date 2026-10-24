// Demo logs generated in memory, in the exact column layout the firmware writes
// (arduino/libraries/AcornCore/src/Csv.h). Seeded, so every visitor sees the same data.

import { PLAYBACK_COLUMNS, ENVIRONMENT_COLUMNS, SITE_COLUMNS } from '../shared/js/acorn-logs.js';

const MIN = 60_000;
const HOUR = 60 * MIN;
const DAY = 24 * HOUR;
const OFFSET_MIN = -300;  // the demo site runs on UTC−05:00
const SITE = ['44.977753', '-93.265011'];
const DEVICE = 'ACORN-DEMO01';
// Firmware discharge model (BatteryModel.cpp): volts -> percent, linear between points.
const MODEL = [[12.0, 100], [11.7, 90], [11.5, 80], [11.3, 70], [11.1, 60], [10.9, 50], [10.7, 40], [10.5, 30], [10.3, 20], [10.0, 10], [9.4, 0]];

function rng(seed) {
  return () => {
    seed = (seed + 0x6d2b79f5) | 0;
    let t = Math.imul(seed ^ (seed >>> 15), 1 | seed);
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

const iso = (ms) => new Date(ms).toISOString().replace(/\.\d{3}Z$/, 'Z');
const localHour = (ms) => (((ms / HOUR + OFFSET_MIN / 60) % 24) + 24) % 24;

function percent(v) {
  if (v >= MODEL[0][0]) return 100;
  for (let i = 1; i < MODEL.length; i++) {
    const [v1, p1] = MODEL[i - 1];
    const [v0, p0] = MODEL[i];
    if (v >= v0) return Math.round(p0 + ((v - v0) / (v1 - v0)) * (p1 - p0));
  }
  return 0;
}

// Weather: a slow day-to-day drift plus the daily cycle; cloudier days are cooler and darker.
function weather(rand, days) {
  return Array.from({ length: days + 2 }, () => ({ base: 4 + rand() * 6, swing: 4 + rand() * 4, cloud: rand() }));
}

function conditions(ms, start, wx, rand) {
  const f = Math.max(0, (ms - start) / DAY);
  const i = Math.min(wx.length - 2, Math.floor(f));
  const k = f - i;
  const day = Object.fromEntries(Object.keys(wx[i]).map((key) => [key, wx[i][key] * (1 - k) + wx[i + 1][key] * k]));
  const hr = localHour(ms);
  const cyc = Math.sin(((hr - 9) / 24) * 2 * Math.PI);
  const sun = Math.max(0, Math.sin(((hr - 6.5) / 12.5) * Math.PI));
  const temp = day.base + day.swing * cyc * (1 - day.cloud * 0.5) + (rand() - 0.5) * 0.6;
  const rh = Math.min(99, Math.max(20, 68 - 18 * cyc + day.cloud * 15 + (rand() - 0.5) * 3));
  const lux = sun > 0 ? sun * (22000 - day.cloud * 17000) * (0.85 + rand() * 0.3) : 0.2 + rand() * 0.4;
  return { temp, rh, lux };
}

const csv = (cols, rows) => [[...cols, ...SITE_COLUMNS].join(','), ...rows.map((r) => [...r, ...SITE].join(','))].join('\n') + '\n';

/** Four days of the squirrel-study program: random morning calls, a noon hawk call, a few manual tests. */
export function demoPlayback(now = Date.now()) {
  const rand = rng(7);
  const days = 4;
  const end = Math.floor(now / DAY) * DAY - OFFSET_MIN * MIN;  // local midnight today
  const start = end - days * DAY;
  const wx = weather(rand, days);
  const rows = [];
  let volts = 12.08;
  const push = (actual, scheduled, eventId, source, audio, volume, duration, result = 'ok', error = '') => {
    volts -= 0.0011 + rand() * 0.0006;
    const c = conditions(actual, start, wx, rand);
    rows.push([
      iso(actual), scheduled ? iso(scheduled) : '', 'squirrel-study', 'a81f92c1', eventId, source, audio, volume,
      duration, result, error, volts.toFixed(2), percent(volts), c.temp.toFixed(1), c.rh.toFixed(1), c.lux.toFixed(1),
    ]);
  };
  for (let d = 0; d < days; d++) {
    const midnight = start + d * DAY;
    // random-morning: 06:00–12:00 local, every 8–12 minutes
    for (let t = midnight + 6 * HOUR; t < midnight + 12 * HOUR; t += (8 + rand() * 4) * MIN) {
      const sched = Math.round(t / 1000) * 1000;
      const late = rand() < 0.02 ? 1000 + Math.round(rand() * 1500) : Math.round(rand() * 400);
      const fail = rand() < 0.006;
      push(sched + late, sched, 'random-morning', 'scheduled', '/audio/squirrel.wav', 75, fail ? 0 : 3410, fail ? 'error' : 'ok', fail ? 'PLAYBACK_IO_ERROR' : '');
    }
    const noon = midnight + 12 * HOUR;
    push(noon + 120, noon, 'noon-call', 'scheduled', '/audio/hawk.wav', 70, 6120);
  }
  // Field visit on day 2: a tone check, a test of the program sound and one stopped early.
  const visit = start + DAY + 15 * HOUR + 12 * MIN;
  push(visit, null, '', 'manual_ui', 'builtin:sine', 60, 2000);
  push(visit + 95_000, null, 'random-morning', 'manual_test', '/audio/squirrel.wav', 75, 3410);
  push(visit + 160_000, null, '', 'manual_ui', '/audio/hawk.wav', 70, 2380, 'stopped');
  rows.sort((a, b) => (a[0] < b[0] ? -1 : 1));
  return { name: `${DEVICE}_playback.csv`, text: csv(PLAYBACK_COLUMNS, rows) };
}

/** Seven days of 10-minute samples, with a 3-hour gap (card removed) and a few sensor blanks. */
export function demoEnvironment(now = Date.now()) {
  const rand = rng(11);
  const days = 7;
  const iv = 10 * MIN;
  const end = Math.floor(now / iv) * iv;
  const start = end - days * DAY;
  const wx = weather(rand, days);
  const gapFrom = start + 3 * DAY + 13 * HOUR;
  const gapTo = gapFrom + 3 * HOUR;
  const rows = [];
  let volts = 12.1;
  for (let t = start; t <= end; t += iv) {
    if (t >= gapFrom && t < gapTo) continue;
    const c = conditions(t, start, wx, rand);
    volts -= 0.00018 + rand() * 0.00008;
    const sag = c.temp < 3 ? (3 - c.temp) * 0.004 : 0;  // cold lowers the loaded voltage a little
    const v = volts - sag;
    const blank = rand() < 0.004;
    rows.push([
      iso(t), blank ? '' : c.temp.toFixed(1), blank ? '' : c.rh.toFixed(1), c.lux.toFixed(1), v.toFixed(2), percent(v),
    ]);
  }
  return { name: `${DEVICE}_environment.csv`, text: csv(ENVIRONMENT_COLUMNS, rows) };
}

export const DEMO_OFFSET_MIN = OFFSET_MIN;
