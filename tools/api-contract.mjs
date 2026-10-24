#!/usr/bin/env node
// ACORN /api/v1 contract test. Holds the mock (dev/mock) and the firmware to
// the same routes, envelopes, status codes, error codes and shapes
// (docs/web-ui.md section 4), plus the embedded web UI (gzip, ETag, 304) and
// the captive-portal redirect when run against the device AP.
//
//   node tools/api-contract.mjs --base http://127.0.0.1:8080   # mock
//   node tools/api-contract.mjs --base http://192.168.4.1      # device AP
//   add --readonly to skip checks that create/delete temporary files,
//   write settings back unchanged, or play a short quiet tone.
//
// Temporary resources (program "contract-tmp", audio "contract-tmp.wav")
// are always removed again. Time and device name are never changed.

import { preview, programRevision } from '../dev/mock/scheduler.mjs';

const args = process.argv.slice(2);
const opt = (name, def) => {
  const i = args.indexOf(`--${name}`);
  return i < 0 ? def : args[i + 1] && !args[i + 1].startsWith('--') ? args[i + 1] : true;
};
const BASE = String(opt('base', 'http://127.0.0.1:8080')).replace(/\/$/, '');
const READONLY = !!opt('readonly', false);
const API = `${BASE}/api/v1`;

let passed = 0;
let failed = 0;
let skipped = 0;
const failures = [];

function check(name, cond, detail = '') {
  if (cond) {
    passed++;
    console.log(`  ok    ${name}`);
  } else {
    failed++;
    failures.push(name);
    console.log(`  FAIL  ${name}${detail ? `\n        ${detail}` : ''}`);
  }
  return cond;
}
function skip(name, why) {
  skipped++;
  console.log(`  skip  ${name} (${why})`);
}
const section = (t) => console.log(`\n${t}`);

async function call(method, route, { json, body, headers = {} } = {}) {
  const init = { method, headers: { ...headers } };
  if (json !== undefined) {
    init.body = typeof json === 'string' ? json : JSON.stringify(json);
    init.headers['Content-Type'] = 'application/json';
  } else if (body !== undefined) {
    init.body = body;
  }
  const res = await fetch(`${API}${route}`, init);
  const text = await res.text();
  let parsed = null;
  try { parsed = JSON.parse(text); } catch { /* CSV or text */ }
  return { status: res.status, type: res.headers.get('content-type') || '', text, json: parsed };
}

const isObj = (v) => v !== null && typeof v === 'object' && !Array.isArray(v);
const hasKeys = (o, keys) => isObj(o) && keys.every((k) => k in o);
const missing = (o, keys) => (isObj(o) ? keys.filter((k) => !(k in o)).join(', ') : 'not an object');
const isIsoZ = (s) => typeof s === 'string' && /^\d{4}-\d\d-\d\dT\d\d:\d\d:\d\dZ$/.test(s);
const isIsoLocal = (s) => typeof s === 'string' && /^\d{4}-\d\d-\d\dT\d\d:\d\d:\d\d$/.test(s);

function okEnvelope(name, r, status = 200) {
  return check(`${name} -> ${status} ok envelope`, r.status === status && r.json?.ok === true && 'data' in r.json,
    `got ${r.status} ${r.text.slice(0, 200)}`);
}
function errEnvelope(name, r, status, code, field) {
  const e = r.json?.error;
  const good = r.status === status && r.json?.ok === false && e?.code === code && typeof e?.message === 'string' &&
    isObj(e?.details) && (field === undefined || e.field === field);
  return check(`${name} -> ${status} ${code}${field !== undefined ? ` (field ${field})` : ''}`, good,
    `got ${r.status} ${r.text.slice(0, 240)}`);
}

function multipart(fieldName, filename, data, type = 'application/octet-stream') {
  const boundary = `----acorn${Math.random().toString(16).slice(2)}`;
  const head = Buffer.from(`--${boundary}\r\nContent-Disposition: form-data; name="${fieldName}"; filename="${filename}"\r\n` +
    `Content-Type: ${type}\r\n\r\n`);
  const tail = Buffer.from(`\r\n--${boundary}--\r\n`);
  return { body: Buffer.concat([head, data, tail]), headers: { 'Content-Type': `multipart/form-data; boundary=${boundary}` } };
}

function wav({ ms = 100, rate = 48000, channels = 1, bits = 16 } = {}) {
  const frames = Math.round((rate * ms) / 1000);
  const bytes = frames * channels * (bits / 8);
  const b = Buffer.alloc(44 + bytes);
  b.write('RIFF', 0); b.writeUInt32LE(36 + bytes, 4); b.write('WAVE', 8);
  b.write('fmt ', 12); b.writeUInt32LE(16, 16); b.writeUInt16LE(1, 20); b.writeUInt16LE(channels, 22);
  b.writeUInt32LE(rate, 24); b.writeUInt32LE(rate * channels * (bits / 8), 28);
  b.writeUInt16LE(channels * (bits / 8), 32); b.writeUInt16LE(bits, 34);
  b.write('data', 36); b.writeUInt32LE(bytes, 40);
  return b;
}

// ---------------------------------------------------------------------------

async function main() {
  console.log(`ACORN API contract test against ${API}${READONLY ? ' (read-only)' : ''}`);

  section('status / session');
  const st = await call('GET', '/status');
  okEnvelope('GET /status', st);
  const s = st.json?.data || {};
  const statusKeys = ['device', 'mode', 'armed', 'faults', 'time', 'reset_reason', 'wake_reason', 'uptime_s', 'battery',
    'sd', 'environment', 'program', 'playback', 'last_playback', 'next_playback', 'next_env_sample_utc', 'wifi'];
  check('status has all top-level fields', hasKeys(s, statusKeys), `missing: ${missing(s, statusKeys)}`);
  check('status.device {id,name,firmware_version}', hasKeys(s.device, ['id', 'name', 'firmware_version']));
  check('status.armed is boolean', typeof s.armed === 'boolean');
  check('status.faults entries {code,message,blocking,area}',
    Array.isArray(s.faults) && s.faults.every((f) => hasKeys(f, ['code', 'message', 'blocking', 'area'])));
  check('status.armed == no blocking faults', s.armed === !(s.faults || []).some((f) => f.blocking));
  check('status.time {utc,local,utc_offset_minutes,rtc_valid}',
    hasKeys(s.time, ['utc', 'local', 'utc_offset_minutes', 'rtc_valid']) && isIsoZ(s.time.utc) && isIsoLocal(s.time.local));
  check('status.battery {voltage,percent,valid,model}', hasKeys(s.battery, ['voltage', 'percent', 'valid', 'model']));
  check('status.sd {present,mounted}', hasKeys(s.sd, ['present', 'mounted']));
  check('status.site null or {latitude,longitude,...}', s.site === null ||
    (hasKeys(s.site, ['latitude', 'longitude', 'accuracy_m', 'source', 'fixed_utc', 'note']) && typeof s.site.latitude === 'number'),
    JSON.stringify(s.site));
  check('status.environment shape', hasKeys(s.environment,
    ['temperature_c', 'relative_humidity_pct', 'ambient_light_lux', 'ok', 'sampled_utc', 'logging_enabled', 'interval_minutes']));
  check('status.program null or {id,name,revision,valid,loaded,window}',
    s.program === null || hasKeys(s.program, ['id', 'name', 'revision', 'valid', 'loaded', 'window']));
  check('status.playback.state', ['idle', 'playing'].includes(s.playback?.state));
  check('status.next_playback null or scheduled event', s.next_playback === null ||
    (hasKeys(s.next_playback, ['event_id', 'scheduled_utc', 'scheduled_local', 'audio', 'volume']) && isIsoZ(s.next_playback.scheduled_utc)));
  check('status.wifi shape', hasKeys(s.wifi, ['mode', 'ssid', 'security', 'clients', 'idle_timeout_s', 'idle_remaining_s']));
  const ext = await call('POST', '/session/extend');
  okEnvelope('POST /session/extend', ext);
  check('session/extend idle_remaining_s', typeof ext.json?.data?.idle_remaining_s === 'number');

  section('device / time');
  const dev = await call('GET', '/device');
  okEnvelope('GET /device', dev);
  const d = dev.json?.data;
  check('device {id,name,firmware_version,battery_model{name,points}}',
    hasKeys(d, ['id', 'name', 'firmware_version', 'battery_model']) && Array.isArray(d.battery_model?.points));
  errEnvelope('PUT /device bad name', await call('PUT', '/device', { json: { name: 'bad name!' } }), 422, 'DEVICE_NAME_INVALID', 'name');
  errEnvelope('PUT /device bad battery model', await call('PUT', '/device', { json: { battery_model: { name: 'x', points: [[10, 0], [12, 100]] } } }),
    422, 'BATTERY_MODEL_INVALID', 'battery_model');
  const tm = await call('GET', '/time');
  okEnvelope('GET /time', tm);
  check('time shape', hasKeys(tm.json?.data, ['utc', 'local', 'utc_offset_minutes', 'rtc_valid']));
  errEnvelope('PUT /time bad utc', await call('PUT', '/time', { json: { utc: '2026-09-28 10:00' } }), 422, 'TIME_INVALID', 'utc');
  errEnvelope('PUT /time bad offset', await call('PUT', '/time', { json: { utc_offset_minutes: 7 } }), 422, 'TIME_OFFSET_INVALID', 'utc_offset_minutes');
  errEnvelope('PUT /time malformed JSON', await call('PUT', '/time', { json: '{"utc":' }), 400, 'JSON_INVALID');

  section('environment');
  const env = await call('GET', '/environment');
  okEnvelope('GET /environment', env);
  const e = env.json?.data;
  check('environment shape', hasKeys(e, ['enabled', 'interval_minutes', 'min_interval_minutes', 'readings', 'next_sample_utc']) &&
    hasKeys(e.readings, ['temperature_c', 'relative_humidity_pct', 'ambient_light_lux', 'ok']));
  errEnvelope('PUT /environment interval 0', await call('PUT', '/environment', { json: { interval_minutes: 0 } }), 422, 'ENV_INTERVAL_INVALID', 'interval_minutes');
  errEnvelope('PUT /environment enabled "yes"', await call('PUT', '/environment', { json: { enabled: 'yes' } }), 422, 'ENV_CONFIG_INVALID', 'enabled');
  if (READONLY) skip('PUT /environment unchanged values', 'read-only');
  else if (e) {
    const put = await call('PUT', '/environment', { json: { enabled: e.enabled, interval_minutes: e.interval_minutes } });
    okEnvelope('PUT /environment unchanged values', put);
    check('environment unchanged', put.json?.data?.enabled === e.enabled && put.json?.data?.interval_minutes === e.interval_minutes);
    if (e.enabled && s.time?.rtc_valid) {
      const iv = e.interval_minutes * 60_000;
      const expect = new Date(Math.floor(Date.parse(put.json.data.next_sample_utc) / iv) * iv).toISOString().replace('.000', '');
      check('next_sample_utc is epoch-aligned', put.json.data.next_sample_utc === expect, put.json.data.next_sample_utc);
    }
  }

  section('firmware');
  const fw = await call('GET', '/firmware');
  okEnvelope('GET /firmware', fw);
  check('firmware shape', hasKeys(fw.json?.data, ['version', 'build_utc', 'running_partition', 'next_partition', 'slot_size_bytes']));
  errEnvelope('POST /firmware without multipart', await call('POST', '/firmware', { json: {} }), 400, 'OTA_INVALID');
  const badImg = multipart('file', 'bad.bin', Buffer.alloc(512, 0));
  errEnvelope('POST /firmware bad magic', await call('POST', '/firmware', badImg), 422, 'OTA_INVALID', 'file');

  section('routing');
  errEnvelope('GET unknown route', await call('GET', '/nope'), 404, 'NOT_FOUND');

  section('audio test / stop');
  errEnvelope('POST /audio/test volume 101', await call('POST', '/audio/test', { json: { source: 'tone', volume: 101 } }), 422, 'VOLUME_INVALID', 'volume');
  errEnvelope('POST /audio/test bad source', await call('POST', '/audio/test', { json: { source: 'x', volume: 10 } }), 422, 'PLAYBACK_START_FAILED', 'source');
  if (READONLY) skip('tone play/stop', 'read-only');
  else {
    const play = await call('POST', '/audio/test', { json: { source: 'tone', volume: 5 } });
    okEnvelope('POST /audio/test tone', play);
    check('tone playback shape', play.json?.data?.state === 'playing' && play.json.data.kind === 'tone' &&
      play.json.data.source === 'manual_ui' && play.json.data.audio === null, JSON.stringify(play.json?.data));
    const stop = await call('POST', '/audio/stop');
    okEnvelope('POST /audio/stop', stop);
    check('stop -> idle', stop.json?.data?.state === 'idle');
  }

  if (!s.sd?.present || !s.sd?.mounted) {
    section('SD-dependent routes (no SD)');
    errEnvelope('GET /programs without SD', await call('GET', '/programs'), 503, s.sd?.present ? 'SD_MOUNT_FAILED' : 'SD_NOT_PRESENT');
    errEnvelope('GET /audio without SD', await call('GET', '/audio'), 503, s.sd?.present ? 'SD_MOUNT_FAILED' : 'SD_NOT_PRESENT');
    return;
  }

  section('audio library');
  const au = await call('GET', '/audio');
  okEnvelope('GET /audio', au);
  const files = au.json?.data?.files || [];
  const fileKeys = ['name', 'path', 'size_bytes', 'valid', 'referenced_by', 'in_active_program'];
  check('audio files shape', Array.isArray(files) && files.every((f) => hasKeys(f, fileKeys) && (f.valid || hasKeys(f.error, ['code', 'message']))));
  check('audio rules', hasKeys(au.json?.data?.rules, ['bits_per_sample', 'channels', 'sample_rates']));
  errEnvelope('GET /audio/missing', await call('GET', '/audio/contract-none.wav'), 404, 'PROGRAM_AUDIO_MISSING');
  errEnvelope('POST /audio/test missing file', await call('POST', '/audio/test', { json: { source: 'file', file: 'contract-none.wav', volume: 10 } }),
    404, 'PROGRAM_AUDIO_MISSING', 'file');
  const inUse = files.find((f) => f.in_active_program);
  if (inUse) {
    const del = await call('DELETE', `/audio/${encodeURIComponent(inUse.name)}`);
    errEnvelope('DELETE audio used by active program', del, 409, 'AUDIO_IN_USE');
    check('AUDIO_IN_USE details.referenced_by', Array.isArray(del.json?.error?.details?.referenced_by));
  } else skip('DELETE audio used by active program', 'no such file');

  if (READONLY) skip('audio upload/delete', 'read-only');
  else {
    const up = await call('POST', '/audio', multipart('file', 'Contract-TMP.wav', wav({ ms: 100 }), 'audio/wav'));
    check('POST /audio new file -> 201', up.status === 201 && up.json?.ok === true, `got ${up.status} ${up.text.slice(0, 200)}`);
    const f = up.json?.data;
    check('uploaded entry sanitized + metadata', f?.name === 'contract-tmp.wav' && f.valid === true && f.duration_ms === 100 &&
      f.sample_rate === 48000 && f.channels === 1 && f.bits_per_sample === 16, JSON.stringify(f));
    const again = await call('POST', '/audio', multipart('file', 'contract-tmp.wav', wav({ ms: 100 }), 'audio/wav'));
    check('POST /audio replace -> 200', again.status === 200 && again.json?.ok === true, `got ${again.status}`);
    const stereo = await call('POST', '/audio', multipart('file', 'contract-stereo.wav', wav({ ms: 50, channels: 2 }), 'audio/wav'));
    errEnvelope('POST /audio stereo', stereo, 422, 'WAV_UNSUPPORTED', 'file');
    check('stereo rejection details', stereo.json?.error?.details?.valid === false && stereo.json.error.details.channels === 2);
    errEnvelope('POST /audio non-wav name', await call('POST', '/audio', multipart('file', 'notes.txt', Buffer.from('hi'))), 422, 'WAV_UNSUPPORTED', 'file');
    errEnvelope('POST /audio corrupt', await call('POST', '/audio', multipart('file', 'contract-bad.wav', Buffer.alloc(64, 1))), 422, 'WAV_CORRUPT', 'file');
    errEnvelope('POST /audio without multipart', await call('POST', '/audio', { json: {} }), 400, 'UPLOAD_INVALID');
    const one = await call('GET', '/audio/contract-tmp.wav');
    okEnvelope('GET /audio/contract-tmp.wav', one);
    okEnvelope('DELETE /audio/contract-tmp.wav', await call('DELETE', '/audio/contract-tmp.wav'));
    errEnvelope('DELETE deleted audio', await call('DELETE', '/audio/contract-tmp.wav'), 404, 'PROGRAM_AUDIO_MISSING');
  }

  section('programs');
  const pr = await call('GET', '/programs');
  okEnvelope('GET /programs', pr);
  const pd = pr.json?.data || {};
  check('programs {active_id,limits,programs}', hasKeys(pd, ['active_id', 'limits', 'programs']));
  check('limits keys', hasKeys(pd.limits, ['MAX_PROGRAMS', 'MAX_EVENTS', 'MAX_NAME', 'MAX_DESCRIPTION', 'MAX_AUDIO_NAME', 'SCHEDULE_GUARD_MS']));
  const sumKeys = ['id', 'name', 'description', 'modified_utc', 'event_count', 'revision', 'valid', 'error_count', 'active'];
  check('program summaries shape', (pd.programs || []).every((p) => hasKeys(p, sumKeys)));
  errEnvelope('GET /programs/missing', await call('GET', '/programs/contract-none'), 404, 'PROGRAM_NOT_FOUND');
  errEnvelope('POST validate malformed JSON', await call('POST', '/programs/contract-none/validate', { json: '{"id":' }), 400, 'JSON_INVALID');

  const validAudio = files.find((f) => f.valid);
  const draft = (events) => ({ schema_version: 1, id: 'contract-tmp', name: 'Contract test', description: '', events });
  const range = (id, start, end, audio, interval = 3600) => ({
    event_id: id, type: 'range', start, end, audio, volume: 10, recurrence: { type: 'fixed', interval_seconds: interval },
  });

  const missingAudio = await call('POST', '/programs/contract-tmp/validate', { json: draft([range('a', '06:00:00', '07:00:00', '/audio/contract-none.wav')]) });
  okEnvelope('validate draft with missing audio', missingAudio);
  check('validate reports PROGRAM_AUDIO_MISSING at events[0].audio', missingAudio.json?.data?.valid === false &&
    missingAudio.json.data.errors.some((x) => x.code === 'PROGRAM_AUDIO_MISSING' && x.field === 'events[0].audio'));

  if (validAudio) {
    const overlap = draft([range('a', '20:00:00', '04:00:00', validAudio.path), range('b', '03:00:00', '05:00:00', validAudio.path)]);
    const ov = await call('POST', '/programs/contract-tmp/validate', { json: overlap });
    check('validate cross-midnight overlap', ov.json?.data?.valid === false &&
      ov.json.data.errors.some((x) => x.code === 'PROGRAM_RANGE_OVERLAP' && x.field === 'events[1]'), JSON.stringify(ov.json?.data?.errors));
    check('validate revision matches mock definition', ov.json?.data?.revision === programRevision(overlap), ov.json?.data?.revision);
  } else skip('draft validation with a valid WAV', 'no valid WAV on SD');

  const active = pd.active_id && (pd.programs || []).find((p) => p.id === pd.active_id);
  if (active?.valid && s.time?.rtc_valid) {
    const got = await call('GET', `/programs/${active.id}`);
    okEnvelope(`GET /programs/${active.id}`, got);
    check('program item {program,summary,errors}', hasKeys(got.json?.data, ['program', 'summary', 'errors']));
    const prog = got.json?.data?.program;
    const pv = await call('GET', `/programs/${active.id}/preview?count=25`);
    okEnvelope('GET preview?count=25', pv);
    const pvd = pv.json?.data || {};
    check('preview shape', hasKeys(pvd, ['revision', 'from_utc', 'utc_offset_minutes', 'events']) && pvd.events.length > 0 &&
      pvd.events.every((x) => hasKeys(x, ['event_id', 'type', 'scheduled_utc', 'scheduled_local', 'audio', 'volume'])));
    if (prog && pvd.events) {
      const norm = { ...prog, schema_version: prog.schema_version ?? 1, events: prog.events ?? [] };
      check('preview revision == mock programRevision', pvd.revision === programRevision(norm), `${pvd.revision} vs ${programRevision(norm)}`);
      const ref = preview(norm, pvd.utc_offset_minutes, Date.parse(pvd.from_utc), 25);
      const same = ref.length === pvd.events.length && ref.every((x, i) =>
        x.event_id === pvd.events[i].event_id && x.scheduled_utc === pvd.events[i].scheduled_utc && x.scheduled_local === pvd.events[i].scheduled_local);
      check('preview events == mock scheduler (same program, offset, from_utc)', same,
        `first device ${JSON.stringify(pvd.events[0])} mock ${JSON.stringify(ref[0])}`);
    }
    if (!READONLY) errEnvelope('DELETE active program', await call('DELETE', `/programs/${active.id}`), 409, 'PROGRAM_ACTIVE');
  } else skip('active program preview parity', 'no valid active program or time not set');

  if (READONLY || !validAudio) skip('program create/update/delete', READONLY ? 'read-only' : 'no valid WAV on SD');
  else {
    await call('DELETE', '/programs/contract-tmp');  // leftover from an interrupted run
    // HH:MM on input: both implementations store HH:MM:SS.
    const body = draft([range('morning', '06:00', '07:00', validAudio.path)]);
    const created = await call('POST', '/programs', { json: body });
    okEnvelope('POST /programs', created, 201);
    check('created program normalized + summary', created.json?.data?.program?.events?.[0]?.start === '06:00:00' &&
      created.json.data.program.events[0].end === '07:00:00' &&
      created.json.data.summary?.revision === programRevision(created.json.data.program) &&
      isIsoZ(created.json.data.program.modified_utc) && created.json.data.summary?.id === 'contract-tmp' &&
      created.json.data.summary.active === false, JSON.stringify(created.json?.data));
    errEnvelope('POST /programs duplicate', await call('POST', '/programs', { json: body }), 409, 'PROGRAM_ID_EXISTS', 'id');
    errEnvelope('PUT /programs id change', await call('PUT', '/programs/contract-tmp', { json: { ...body, id: 'contract-other' } }), 422, 'PROGRAM_JSON_INVALID', 'id');
    const bad = await call('PUT', '/programs/contract-tmp', { json: draft([range('a', '06:00:00', '07:00:00', validAudio.path, 1)]) });
    errEnvelope('PUT /programs interval too short', bad, 422, 'PROGRAM_INTERVAL_TOO_SHORT', 'events[0].recurrence.interval_seconds');
    check('422 carries details.errors[]', Array.isArray(bad.json?.error?.details?.errors) && bad.json.error.details.errors.length > 0);
    const upd = await call('PUT', '/programs/contract-tmp', { json: { ...body, name: 'Contract test 2' } });
    okEnvelope('PUT /programs', upd);
    check('PUT applied=false for inactive program', upd.json?.data?.applied === false && upd.json.data.program?.name === 'Contract test 2');
    const val = await call('POST', '/programs/contract-tmp/validate');
    check('validate stored program', val.json?.data?.valid === true && val.json.data.revision === created.json?.data?.summary?.revision,
      JSON.stringify(val.json?.data));
    if (s.time?.rtc_valid) {
      const pvDraft = await call('POST', '/programs/contract-tmp/preview?count=3', { json: body });
      okEnvelope('POST draft preview?count=3', pvDraft);
      check('draft preview count', pvDraft.json?.data?.events?.length === 3);
    }
    okEnvelope('DELETE /programs/contract-tmp', await call('DELETE', '/programs/contract-tmp'));
    errEnvelope('GET deleted program', await call('GET', '/programs/contract-tmp'), 404, 'PROGRAM_NOT_FOUND');
  }

  section('site');
  const siteKeys = ['set', 'latitude', 'longitude', 'accuracy_m', 'source', 'fixed_utc', 'note'];
  const site = await call('GET', '/site');
  okEnvelope('GET /site', site);
  const st0 = site.json?.data;
  check('site shape', hasKeys(st0, siteKeys) && typeof st0.set === 'boolean' &&
    (st0.set ? typeof st0.latitude === 'number' && typeof st0.longitude === 'number' : st0.latitude === null && st0.longitude === null),
    JSON.stringify(st0));
  check('status.site matches /site', st0?.set ? s.site?.latitude === st0.latitude && s.site?.longitude === st0.longitude : s.site === null);
  errEnvelope('PUT /site latitude 91', await call('PUT', '/site', { json: { latitude: 91, longitude: 0 } }), 422, 'SITE_INVALID', 'latitude');
  errEnvelope('PUT /site longitude missing', await call('PUT', '/site', { json: { latitude: 45 } }), 422, 'SITE_INVALID', 'longitude');
  errEnvelope('PUT /site latitude as string', await call('PUT', '/site', { json: { latitude: '45', longitude: 0 } }), 422, 'SITE_INVALID', 'latitude');
  errEnvelope('PUT /site accuracy -1', await call('PUT', '/site', { json: { latitude: 45, longitude: 0, accuracy_m: -1 } }), 422, 'SITE_INVALID', 'accuracy_m');
  errEnvelope('PUT /site bad source', await call('PUT', '/site', { json: { latitude: 45, longitude: 0, source: 'satellite' } }), 422, 'SITE_INVALID', 'source');
  errEnvelope('PUT /site malformed JSON', await call('PUT', '/site', { json: '{"latitude":' }), 400, 'JSON_INVALID');
  if (READONLY) skip('PUT/DELETE /site round-trip', 'read-only');
  else if (st0) {
    const put = await call('PUT', '/site', { json: { latitude: 45.1234567, longitude: -93.7654321, accuracy_m: 12, source: 'manual', note: 'contract test' } });
    okEnvelope('PUT /site', put);
    const p = put.json?.data;
    check('PUT /site stored at 6 decimals', p?.set === true && p.latitude === 45.123457 && p.longitude === -93.765432 &&
      p.accuracy_m === 12 && p.source === 'manual' && p.note === 'contract test', JSON.stringify(p));
    check('PUT /site fixed_utc from device clock', !s.time?.rtc_valid || isIsoZ(p?.fixed_utc), p?.fixed_utc);
    const got = (await call('GET', '/site')).json?.data;
    check('GET /site after PUT', got?.latitude === 45.123457 && got?.longitude === -93.765432);
    const envAfter = await call('GET', '/logs/environment?tail=1');
    check('header still current after site change', envAfter.text.startsWith('timestamp_utc,') && envAfter.text.includes(',latitude,longitude\n'));
    const del = await call('DELETE', '/site');
    okEnvelope('DELETE /site', del);
    check('DELETE /site clears', del.json?.data?.set === false && del.json?.data?.latitude === null);
    if (st0.set) {
      const { set, fixed_utc, ...prev } = st0;
      okEnvelope('PUT /site restore original', await call('PUT', '/site', { json: Object.fromEntries(Object.entries(prev).filter(([, v]) => v !== null)) }));
    }
  }

  section('logs');
  const logs = await call('GET', '/logs');
  okEnvelope('GET /logs', logs);
  const infoKeys = ['path', 'size_bytes', 'rows', 'modified_utc'];
  check('logs shape', hasKeys(logs.json?.data?.playback, infoKeys) && hasKeys(logs.json?.data?.environment, infoKeys));
  const tail = await call('GET', '/logs/playback?tail=3');
  check('GET /logs/playback?tail=3 is CSV with header', tail.status === 200 && tail.type.startsWith('text/csv') &&
    tail.text.startsWith('actual_utc,scheduled_utc,program_id,program_revision,event_id,source,audio,volume,duration_ms,result,error_code,') &&
    tail.text.trim().split('\n').length <= 4, tail.text.slice(0, 120));
  const envTail = await call('GET', '/logs/environment?tail=2');
  check('GET /logs/playback header ends with latitude,longitude',
    tail.text.split('\n')[0] === 'actual_utc,scheduled_utc,program_id,program_revision,event_id,source,audio,volume,duration_ms,result,error_code,' +
      'battery_voltage,battery_percent,temperature_c,relative_humidity_pct,ambient_light_lux,latitude,longitude', tail.text.split('\n')[0]);
  check('GET /logs/environment?tail=2 header', envTail.status === 200 &&
    envTail.text.startsWith('timestamp_utc,temperature_c,relative_humidity_pct,ambient_light_lux,battery_voltage,battery_percent,latitude,longitude\n'));
  const coordCells = (row) => row.split(',').slice(-2);
  const coordOk = ([la, lo]) => (la === '' && lo === '') || (/^-?\d+\.\d{6}$/.test(la) && /^-?\d+\.\d{6}$/.test(lo));
  const envRows = envTail.text.trim().split('\n').slice(1);
  check('environment rows end with 6-decimal or empty lat/lon', envRows.every((r) => coordOk(coordCells(r))), envRows[0]);

  section('web ui');
  const index = await fetch(`${BASE}/`);
  const indexText = await index.text();
  check('GET / -> 200 text/html', index.status === 200 && (index.headers.get('content-type') || '').startsWith('text/html') &&
    indexText.includes('<html'), `got ${index.status} ${index.headers.get('content-type')}`);
  const gz = index.headers.get('content-encoding') === 'gzip';
  if (!gz) skip('gzip / ETag / 304', 'server sends uncompressed files (use a device or dev/server.mjs --dist)');
  else {
    const etag = index.headers.get('etag');
    check('GET / has ETag + Cache-Control no-cache', !!etag && index.headers.get('cache-control') === 'no-cache');
    const again = await fetch(`${BASE}/`, { headers: { 'If-None-Match': etag || '' } });
    check('GET / with If-None-Match -> 304', again.status === 304, `got ${again.status}`);
    const js = await fetch(`${BASE}/js/app.js`);
    await js.text();
    check('GET /js/app.js -> text/javascript gzip', js.status === 200 &&
      (js.headers.get('content-type') || '').startsWith('text/javascript') && js.headers.get('content-encoding') === 'gzip');
  }
  if (new URL(BASE).hostname === '192.168.4.1') {
    const probe = await fetch(`${BASE}/generate_204`, { redirect: 'manual' });
    check('captive probe /generate_204 -> 302 to UI', probe.status === 302 && probe.headers.get('location') === 'http://192.168.4.1/',
      `got ${probe.status} ${probe.headers.get('location')}`);
  } else skip('captive portal probe', 'only checked against the device AP (192.168.4.1)');
}

main()
  .catch((err) => {
    failed++;
    console.error(`\nERROR ${err.message}`);
  })
  .finally(() => {
    console.log(`\n==== ${passed} passed, ${failed} failed, ${skipped} skipped ====`);
    if (failures.length) console.log(`failed: ${failures.join('; ')}`);
    process.exit(failed ? 1 : 0);
  });
