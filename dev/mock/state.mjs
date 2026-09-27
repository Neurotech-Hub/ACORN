// In-memory mock of an ACORN device. Stands in for firmware services during UI development.

import { preview, windowState, eventsBetween, programRevision, isoUtc, DAY_MS } from './scheduler.mjs';
import { validateProgram, LIMITS } from './validator.mjs';

export const SCENARIOS = {
  normal: { label: 'Normal', description: 'Healthy device, default program active, armed.' },
  'sd-missing': { label: 'SD card missing', description: 'No SD card detected. Scheduled playback blocked; tone test and OTA still available.' },
  'sd-full': { label: 'SD card full', description: 'SD mounted but no free space; logging cannot be guaranteed.' },
  'wav-missing': { label: 'Required WAV missing', description: '/audio/playback.wav referenced by the active program is absent.' },
  'program-invalid': { label: 'Active program invalid', description: 'Active program on SD was hand-edited and now has overlapping ranges.' },
  'no-program': { label: 'No active program', description: 'Fresh SD card: no programs, only playback.wav.' },
  'rtc-invalid': { label: 'RTC not set', description: 'RTC lost power; time must be synchronized before arming.' },
  'low-battery': { label: 'Low battery', description: 'Battery at ~5%.' },
  'sensor-fail': { label: 'Sensor read failure', description: 'Environmental sensors not responding (non-blocking).' },
};

const DEFAULT_BATTERY_MODEL = {
  name: 'M12 Li-ion (default)',
  points: [[12.6, 100], [12.3, 90], [12.1, 80], [11.9, 70], [11.7, 60], [11.5, 50], [11.3, 40], [11.1, 30], [10.9, 20], [10.6, 10], [10.0, 0]],
};

const wav = (duration_ms, extra = {}) => ({
  sample_rate: 48000, bits_per_sample: 16, channels: 1, duration_ms,
  size_bytes: Math.round((duration_ms / 1000) * 48000 * 2) + 44, valid: true, ...extra,
});

function seedAudio() {
  return new Map([
    ['playback.wav', wav(5032)],
    ['squirrel.wav', wav(3410, { sample_rate: 44100, size_bytes: Math.round(3.41 * 44100 * 2) + 44 })],
    ['hawk.wav', wav(6120)],
    ['owl-hoot.wav', wav(2240)],
    ['chickadee-stereo.wav', {
      ...wav(4100, { channels: 2, size_bytes: Math.round(4.1 * 48000 * 4) + 44 }),
      valid: false, error: { code: 'WAV_UNSUPPORTED', message: 'Unsupported: stereo' },
    }],
  ]);
}

function seedPrograms(nowIso) {
  return new Map([
    ['default', {
      schema_version: 1, id: 'default', name: '12 Hour Playback',
      description: 'Play playback.wav every 10 minutes from 06:00 to 18:00.', modified_utc: nowIso,
      events: [{
        event_id: 'daytime-playback', type: 'range', start: '06:00:00', end: '18:00:00',
        audio: '/audio/playback.wav', volume: 80, recurrence: { type: 'fixed', interval_seconds: 600 },
      }],
    }],
    ['squirrel-study', {
      schema_version: 1, id: 'squirrel-study', name: 'Squirrel Study',
      description: 'Randomized morning squirrel calls with a daily noon hawk call.', modified_utc: nowIso,
      events: [
        {
          event_id: 'random-morning', type: 'range', start: '06:00:00', end: '12:00:00',
          audio: '/audio/squirrel.wav', volume: 75, recurrence: { type: 'random', min_interval_seconds: 480, max_interval_seconds: 720 },
        },
        { event_id: 'noon-call', type: 'point', time: '12:00:00', audio: '/audio/hawk.wav', volume: 70 },
      ],
    }],
    ['night-owl', {
      schema_version: 1, id: 'night-owl', name: 'Night Owl',
      description: 'Owl hoots every 15 minutes overnight (crosses midnight).', modified_utc: nowIso,
      events: [{
        event_id: 'overnight', type: 'range', start: '20:00:00', end: '04:00:00',
        audio: '/audio/owl-hoot.wav', volume: 65, recurrence: { type: 'fixed', interval_seconds: 900 },
      }],
    }],
  ]);
}

const PLAYBACK_HEADER = 'actual_utc,scheduled_utc,program_id,program_revision,event_id,source,audio,volume,duration_ms,result,error_code,battery_voltage,battery_percent,temperature_c,relative_humidity_pct,ambient_light_lux';
const ENV_HEADER = 'timestamp_utc,temperature_c,relative_humidity_pct,ambient_light_lux,battery_voltage,battery_percent';

export class MockDevice {
  constructor(scenario = 'normal') {
    this.scenario = SCENARIOS[scenario] ? scenario : 'normal';
    const now = Date.now();
    this.bootMs = now;
    this.clockOffsetMs = 0;
    this.rtcValid = true;
    this.utcOffsetMinutes = -300;
    this.device = { id: 'ACORN-A3F29C', name: 'SQUIRREL_SITE_07' };
    this.firmware = { version: '0.1.0-dev', build_utc: '2026-09-27T21:00:00Z', running_partition: 'app0', next_partition: 'app1', slot_size_bytes: 6_553_600 };
    this.batteryVoltage = 11.84;
    this.batteryModel = structuredClone(DEFAULT_BATTERY_MODEL);
    this.sd = { present: true, total_bytes: 31_914_983_424, used_bytes: 1_274_019_840 };
    this.audio = seedAudio();
    this.programs = seedPrograms(isoUtc(now - 3 * DAY_MS));
    this.activeId = 'default';
    this.env = { enabled: true, interval_minutes: 10 };
    this.sensorFail = false;
    this.playback = null;
    this.manualLog = [];
    this.lastActivityMs = now;
    this.idleTimeoutS = 600;
    this.rebootUntil = 0;
    this.applyScenario();
    this.historyPlayback = this.buildPlaybackHistory(now - 36 * 3600_000, now);
  }

  applyScenario() {
    switch (this.scenario) {
      case 'sd-missing': this.sd.present = false; break;
      case 'sd-full': this.sd.used_bytes = this.sd.total_bytes - 2048; break;
      case 'wav-missing': this.audio.delete('playback.wav'); break;
      case 'program-invalid':
        this.programs.get('default').events.push({
          event_id: 'afternoon-extra', type: 'range', start: '15:00:00', end: '20:00:00',
          audio: '/audio/hawk.wav', volume: 70, recurrence: { type: 'fixed', interval_seconds: 1200 },
        });
        break;
      case 'no-program':
        this.programs.clear();
        this.activeId = null;
        for (const k of [...this.audio.keys()]) if (k !== 'playback.wav') this.audio.delete(k);
        break;
      case 'rtc-invalid':
        this.rtcValid = false;
        this.clockOffsetMs = Date.parse('2000-01-01T00:00:00Z') - Date.now();
        break;
      case 'low-battery': this.batteryVoltage = 10.28; break;
      case 'sensor-fail': this.sensorFail = true; break;
    }
  }

  // ---- time -------------------------------------------------------------------------------
  nowMs() { return Date.now() + this.clockOffsetMs; }
  localIso(utcMs) { return new Date(utcMs + this.utcOffsetMinutes * 60_000).toISOString().slice(0, 19); }
  touch() { this.lastActivityMs = Date.now(); }
  timeInfo() {
    const now = this.nowMs();
    return { utc: isoUtc(now), local: this.localIso(now), utc_offset_minutes: this.utcOffsetMinutes, rtc_valid: this.rtcValid };
  }

  // ---- battery / sensors ------------------------------------------------------------------
  batteryPercent(v = this.batteryVoltage) {
    const pts = this.batteryModel.points;
    if (v >= pts[0][0]) return pts[0][1];
    for (let i = 1; i < pts.length; i++) {
      const [v1, p1] = pts[i - 1], [v0, p0] = pts[i];
      if (v >= v0) return Math.round(p0 + ((v - v0) / (v1 - v0)) * (p1 - p0));
    }
    return pts[pts.length - 1][1];
  }
  battery() { return { voltage: this.batteryVoltage, percent: this.batteryPercent(), valid: true, model: this.batteryModel.name }; }

  readings(atUtcMs = this.nowMs()) {
    if (this.sensorFail) return { temperature_c: null, relative_humidity_pct: null, ambient_light_lux: null, ok: false };
    const h = ((atUtcMs / 3600_000 + this.utcOffsetMinutes / 60) % 24 + 24) % 24;
    const day = Math.max(0, Math.sin(((h - 6) / 12) * Math.PI));
    return {
      temperature_c: +(6 + 7 * Math.sin(((h - 9) / 24) * 2 * Math.PI)).toFixed(1),
      relative_humidity_pct: +(64 - 14 * Math.sin(((h - 9) / 24) * 2 * Math.PI)).toFixed(1),
      ambient_light_lux: +(day * 18000 + 0.4).toFixed(1),
      ok: true,
    };
  }

  // ---- SD / audio -------------------------------------------------------------------------
  sdInfo() {
    if (!this.sd.present) return { present: false, mounted: false };
    return { present: true, mounted: true, total_bytes: this.sd.total_bytes, used_bytes: this.sd.used_bytes, free_bytes: this.sd.total_bytes - this.sd.used_bytes };
  }
  audioByPath = (p) => {
    const m = /^\/audio\/([^/]+)$/.exec(p || '');
    return m ? this.audio.get(m[1]) : undefined;
  };
  audioList() {
    const active = this.activeProgram();
    return [...this.audio.entries()].sort((a, b) => a[0].localeCompare(b[0])).map(([name, meta]) => {
      const path = `/audio/${name}`;
      const referenced_by = [...this.programs.values()].filter((p) => p.events.some((e) => e.audio === path)).map((p) => p.id);
      return { name, path, ...meta, referenced_by, in_active_program: !!active && referenced_by.includes(active.id) };
    });
  }

  // ---- programs ---------------------------------------------------------------------------
  activeProgram() { return this.activeId ? this.programs.get(this.activeId) || null : null; }
  validate(p) { return validateProgram(p, this.audioByPath); }
  programSummary(p) {
    const errors = this.sd.present ? this.validate(p) : [];
    return {
      id: p.id, name: p.name, description: p.description || '', modified_utc: p.modified_utc,
      event_count: p.events.length, revision: programRevision(p), valid: errors.length === 0,
      error_count: errors.length, active: p.id === this.activeId,
    };
  }

  // ---- faults -----------------------------------------------------------------------------
  faults() {
    const f = [];
    const add = (code, message, blocking, extra = {}) => f.push({ code, message, blocking, ...extra });
    if (!this.rtcValid) add('TIME_NOT_SET', 'Device clock is not set. Synchronize time from this browser.', true, { area: 'device' });
    if (!this.sd.present) add('SD_NOT_PRESENT', 'No SD card detected. Insert a FAT32-formatted microSD card.', true, { area: 'status' });
    else {
      if (this.sd.total_bytes - this.sd.used_bytes < 1_048_576) add('SD_FULL', 'SD card is full. Logs cannot be written reliably.', true, { area: 'data' });
      const p = this.activeProgram();
      if (!p) add('PROGRAM_NOT_FOUND', 'No active program. Create or activate a program.', true, { area: 'programs' });
      else for (const e of this.validate(p)) add(e.code, `Active program '${p.id}': ${e.message}`, true, { area: 'programs', program_id: p.id, field: e.field });
    }
    if (this.sensorFail) add('SENSOR_READ_FAILED', 'Temperature/humidity and light sensors did not respond. Environmental values will be blank.', false, { area: 'data' });
    return f;
  }
  armed() { return !this.faults().some((x) => x.blocking); }

  // ---- playback ---------------------------------------------------------------------------
  currentPlayback() {
    if (!this.playback) return { state: 'idle' };
    const pos = Date.now() - this.playback.startedReal;
    if (pos >= this.playback.duration_ms) { this.finishPlayback('ok'); return { state: 'idle' }; }
    const { startedReal, ...rest } = this.playback;
    return { state: 'playing', ...rest, position_ms: pos };
  }
  startPlayback({ source, file, volume }) {
    if (this.playback) this.finishPlayback('stopped');
    let duration_ms = 2000, audio = null;
    if (source === 'file') { audio = `/audio/${file}`; duration_ms = this.audio.get(file).duration_ms; }
    this.playback = {
      source: 'manual_ui', kind: source, audio, volume,
      duration_ms, started_utc: isoUtc(this.nowMs()), startedReal: Date.now(),
    };
    return this.currentPlayback();
  }
  finishPlayback(result) {
    const pb = this.playback;
    this.playback = null;
    if (!pb || !this.sd.present) return;
    const p = this.activeProgram();
    const r = this.readings();
    this.manualLog.push([
      pb.started_utc, '', p?.id || '', p ? programRevision(p) : '', '', pb.source, pb.audio || 'builtin:sine', pb.volume,
      result === 'ok' ? pb.duration_ms : Date.now() - pb.startedReal, result, '',
      this.batteryVoltage.toFixed(2), this.batteryPercent(), r.temperature_c ?? '', r.relative_humidity_pct ?? '', r.ambient_light_lux ?? '',
    ].join(','));
  }

  // ---- schedule-derived status ------------------------------------------------------------
  nextPlayback() {
    const p = this.activeProgram();
    if (!p || !this.armed()) return null;
    return preview(p, this.utcOffsetMinutes, this.nowMs(), 1)[0] || null;
  }
  lastPlayback() {
    if (this.manualLog.length) {
      const c = this.manualLog[this.manualLog.length - 1].split(',');
      return { actual_utc: c[0], scheduled_utc: null, event_id: null, source: c[5], audio: c[6], volume: +c[7], result: c[9] };
    }
    const rows = this.playbackRows();
    if (!rows.length) return null;
    const c = rows[rows.length - 1].split(',');
    return { actual_utc: c[0], scheduled_utc: c[1] || null, event_id: c[4] || null, source: c[5], audio: c[6], volume: +c[7], result: c[9] };
  }
  nextEnvSample() {
    if (!this.env.enabled || !this.rtcValid) return null;
    const iv = this.env.interval_minutes * 60_000;
    return isoUtc(Math.floor(this.nowMs() / iv) * iv + iv);
  }

  status() {
    const now = this.nowMs();
    const p = this.activeProgram();
    const faults = this.faults();
    const armed = !faults.some((x) => x.blocking);
    const errors = p && this.sd.present ? this.validate(p) : [];
    return {
      device: { ...this.device, firmware_version: this.firmware.version },
      mode: 'config',
      armed,
      faults,
      time: this.timeInfo(),
      reset_reason: 'POWERON',
      wake_reason: 'UNDEFINED',
      uptime_s: Math.round((Date.now() - this.bootMs) / 1000),
      battery: this.battery(),
      sd: this.sdInfo(),
      environment: { ...this.readings(now), sampled_utc: isoUtc(now), logging_enabled: this.env.enabled, interval_minutes: this.env.interval_minutes },
      program: !p ? null : !this.sd.present ? {
        id: p.id, name: null, revision: null, valid: false, loaded: false, window: null,
      } : {
        id: p.id, name: p.name, revision: programRevision(p), valid: errors.length === 0, loaded: true,
        window: this.rtcValid && errors.length === 0 ? windowState(p, this.utcOffsetMinutes, now) : null,
      },
      playback: this.currentPlayback(),
      last_playback: this.sd.present ? this.lastPlayback() : null,
      next_playback: this.nextPlayback(),
      next_env_sample_utc: this.nextEnvSample(),
      wifi: {
        mode: 'ap', ssid: this.device.id, security: 'open', clients: 1,
        idle_timeout_s: this.idleTimeoutS,
        idle_remaining_s: Math.max(0, this.idleTimeoutS - Math.round((Date.now() - this.lastActivityMs) / 1000)),
      },
    };
  }

  // ---- logs -------------------------------------------------------------------------------
  buildPlaybackHistory(fromMs, toMs) {
    const p = this.activeProgram();
    if (!p || !this.rtcValid || this.validate(p).length) return [];
    const rev = programRevision(p);
    return eventsBetween(p, this.utcOffsetMinutes, fromMs, toMs).map((e) => {
      const t = Date.parse(e.scheduled_utc);
      const r = this.readings(t);
      const dur = this.audioByPath(e.audio)?.duration_ms || 0;
      return [
        e.scheduled_utc, e.scheduled_utc, p.id, rev, e.event_id, 'scheduled', e.audio, e.volume, dur, 'ok', '',
        (this.batteryVoltage + 0.04).toFixed(2), this.batteryPercent(this.batteryVoltage + 0.04), r.temperature_c ?? '', r.relative_humidity_pct ?? '', r.ambient_light_lux ?? '',
      ].join(',');
    });
  }
  playbackRows() {
    const live = this.buildPlaybackHistory(this.bootMs + this.clockOffsetMs, this.nowMs());
    return [...this.historyPlayback, ...live, ...this.manualLog].sort();
  }
  environmentRows() {
    if (!this.rtcValid) return [];
    const iv = this.env.interval_minutes * 60_000;
    const rows = [];
    const now = this.nowMs();
    for (let t = Math.floor((now - DAY_MS) / iv) * iv; t <= now; t += iv) {
      const r = this.readings(t);
      rows.push([isoUtc(t), r.temperature_c ?? '', r.relative_humidity_pct ?? '', r.ambient_light_lux ?? '', this.batteryVoltage.toFixed(2), this.batteryPercent()].join(','));
    }
    return rows;
  }
  logCsv(kind, tail) {
    const header = kind === 'playback' ? PLAYBACK_HEADER : ENV_HEADER;
    let rows = kind === 'playback' ? this.playbackRows() : this.environmentRows();
    if (tail) rows = rows.slice(-tail);
    return header + '\n' + rows.join('\n') + (rows.length ? '\n' : '');
  }
  logInfo(kind) {
    const csv = this.logCsv(kind);
    const rows = csv.trim().split('\n').length - 1;
    const last = rows ? csv.trim().split('\n').pop().split(',')[0] : null;
    return { path: `/logs/${kind}.csv`, size_bytes: Buffer.byteLength(csv), rows, modified_utc: last };
  }
}

export { LIMITS, programRevision, preview };
