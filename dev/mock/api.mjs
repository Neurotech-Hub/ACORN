// Mock /api/v1 routes. Response shapes here are the working contract for firmware (docs/web-ui.md).

import { parseMultipart, inspectWav, inspectFirmware } from './binary.mjs';
import { LIMITS, programRevision, preview } from './state.mjs';
import { isoUtc, parseHms } from './scheduler.mjs';

const MAX_UPLOAD = 16 * 1024 * 1024;

function send(res, status, obj) {
  const body = JSON.stringify(obj);
  res.writeHead(status, { 'Content-Type': 'application/json; charset=utf-8', 'Cache-Control': 'no-store' });
  res.end(body);
}
const ok = (res, data = {}, status = 200) => send(res, status, { ok: true, data });
const fail = (res, status, code, message, field, details = {}) =>
  send(res, status, { ok: false, error: { code, message, ...(field != null ? { field } : {}), details } });

function readBody(req) {
  return new Promise((resolve, reject) => {
    const chunks = [];
    let size = 0;
    req.on('data', (c) => {
      size += c.length;
      if (size > MAX_UPLOAD) { reject(new Error('too large')); req.destroy(); return; }
      chunks.push(c);
    });
    req.on('end', () => resolve(Buffer.concat(chunks)));
    req.on('error', reject);
  });
}

async function readJson(req) {
  const buf = await readBody(req);
  if (!buf.length) return undefined;
  return JSON.parse(buf.toString('utf8'));
}

function requireSd(dev, res) {
  if (!dev.sd.present) { fail(res, 503, 'SD_NOT_PRESENT', 'No SD card detected.'); return false; }
  return true;
}

function programInvalid(res, errors) {
  const first = errors[0];
  return fail(res, 422, first.code, first.message, first.field, { errors });
}

function normalizeProgram(p) {
  return { ...p, schema_version: p.schema_version ?? 1, description: p.description ?? '', events: p.events ?? [] };
}

const hms = (s) => {
  const ms = parseHms(s);
  if (ms == null) return s;
  const t = ms / 1000;
  return [Math.floor(t / 3600), Math.floor((t % 3600) / 60), t % 60].map((n) => String(n).padStart(2, '0')).join(':');
};

// The form the firmware writes on save (ProgramJson::toJson): times as HH:MM:SS,
// known fields only. Applied after validation, so every field is well-formed.
function storedProgram(p) {
  return {
    schema_version: p.schema_version, id: p.id, name: p.name, description: p.description,
    events: p.events.map((e) => (e.type === 'point'
      ? { event_id: e.event_id, type: 'point', time: hms(e.time), audio: e.audio, volume: e.volume }
      : {
        event_id: e.event_id, type: 'range', start: hms(e.start), end: hms(e.end), audio: e.audio, volume: e.volume,
        recurrence: e.recurrence.type === 'random'
          ? { type: 'random', min_interval_seconds: e.recurrence.min_interval_seconds, max_interval_seconds: e.recurrence.max_interval_seconds }
          : { type: 'fixed', interval_seconds: e.recurrence.interval_seconds },
      })),
  };
}

export async function handleApi(req, res, url, ctx) {
  const dev = ctx.device;
  const route = url.pathname.replace(/^\/api\/v1/, '') || '/';
  const m = req.method;
  let match;

  if (!(m === 'GET' && route === '/status')) dev.touch();

  try {
    // ---- status / session ---------------------------------------------------------------
    if (m === 'GET' && route === '/status') return ok(res, dev.status());
    if (m === 'POST' && route === '/session/extend') return ok(res, { idle_remaining_s: dev.idleTimeoutS });

    // ---- device -------------------------------------------------------------------------
    if (route === '/device') {
      if (m === 'GET') return ok(res, { ...dev.device, firmware_version: dev.firmware.version, battery_model: dev.batteryModel });
      if (m === 'PUT') {
        const b = (await readJson(req)) || {};
        if (b.name !== undefined && !/^[A-Za-z0-9_-]{1,32}$/.test(b.name)) {
          return fail(res, 422, 'DEVICE_NAME_INVALID', 'Name must be 1–32 characters: letters, digits, _ or -.', 'name');
        }
        if (b.battery_model !== undefined) {
          const pts = b.battery_model?.points;
          const valid = Array.isArray(pts) && pts.length >= 2 && pts.length <= 16 &&
            pts.every((p, i) => Array.isArray(p) && Number.isFinite(p[0]) && Number.isInteger(p[1]) && p[1] >= 0 && p[1] <= 100 &&
              (i === 0 || (p[0] < pts[i - 1][0] && p[1] <= pts[i - 1][1])));
          if (!valid) {
            return fail(res, 422, 'BATTERY_MODEL_INVALID', 'Battery model needs 2–16 points with voltage strictly decreasing and percent 0–100 non-increasing.', 'battery_model');
          }
        }
        if (b.name !== undefined) dev.device.name = b.name;
        if (b.battery_model !== undefined) dev.batteryModel = { name: b.battery_model.name || 'Custom', points: b.battery_model.points };
        return ok(res, { ...dev.device, firmware_version: dev.firmware.version, battery_model: dev.batteryModel });
      }
    }

    // ---- time ---------------------------------------------------------------------------
    if (route === '/time') {
      if (m === 'GET') return ok(res, dev.timeInfo());
      if (m === 'PUT') {
        const b = (await readJson(req)) || {};
        if (b.utc !== undefined) {
          const t = Date.parse(b.utc);
          if (!Number.isFinite(t) || !/Z$/.test(b.utc)) return fail(res, 422, 'TIME_INVALID', 'utc must be an ISO-8601 UTC timestamp ending in Z.', 'utc');
        }
        if (b.utc_offset_minutes !== undefined) {
          const o = b.utc_offset_minutes;
          if (!Number.isInteger(o) || o < -720 || o > 840 || o % 15 !== 0) {
            return fail(res, 422, 'TIME_OFFSET_INVALID', 'UTC offset must be a multiple of 15 minutes between −12:00 and +14:00.', 'utc_offset_minutes');
          }
        }
        if (b.utc !== undefined) { dev.clockOffsetMs = Date.parse(b.utc) - Date.now(); dev.rtcValid = true; }
        if (b.utc_offset_minutes !== undefined) dev.utcOffsetMinutes = b.utc_offset_minutes;
        return ok(res, dev.timeInfo());
      }
    }

    // ---- audio --------------------------------------------------------------------------
    if (route === '/audio') {
      if (!requireSd(dev, res)) return;
      if (m === 'GET') return ok(res, { files: dev.audioList(), rules: { bits_per_sample: 16, channels: 1, sample_rates: [44100, 48000] } });
      if (m === 'POST') {
        let buf;
        try { buf = await readBody(req); } catch { return fail(res, 413, 'UPLOAD_TOO_LARGE', `Upload exceeds ${MAX_UPLOAD / 1048576} MB.`); }
        const part = parseMultipart(buf, req.headers['content-type'])?.find((p) => p.filename);
        if (!part) return fail(res, 400, 'UPLOAD_INVALID', 'Expected a multipart upload with a file field.');
        const name = part.filename.replace(/^.*[\\/]/, '').toLowerCase().replace(/[^a-z0-9._-]/g, '-');
        if (!/\.wav$/.test(name)) return fail(res, 422, 'WAV_UNSUPPORTED', 'Only .wav files are supported.', 'file');
        if (name.length > LIMITS.MAX_AUDIO_NAME) return fail(res, 422, 'AUDIO_NAME_TOO_LONG', `File name exceeds ${LIMITS.MAX_AUDIO_NAME} characters.`, 'file');
        const info = inspectWav(part.data);
        if (!info.valid) return fail(res, 422, info.error.code, info.error.message, 'file', info);
        const free = dev.sd.total_bytes - dev.sd.used_bytes;
        if (part.data.length > free) return fail(res, 507, 'SD_FULL', 'Not enough free space on the SD card.');
        const prev = dev.audio.get(name);
        dev.sd.used_bytes += part.data.length - (prev?.size_bytes || 0);
        dev.audio.set(name, { ...info, size_bytes: part.data.length });
        return ok(res, dev.audioList().find((f) => f.name === name), prev ? 200 : 201);
      }
    }
    if (m === 'POST' && route === '/audio/test') {
      const b = (await readJson(req)) || {};
      const volume = b.volume;
      if (!Number.isInteger(volume) || volume < 0 || volume > 100) return fail(res, 422, 'VOLUME_INVALID', 'Volume must be 0–100.', 'volume');
      if (b.source === 'file') {
        if (!requireSd(dev, res)) return;
        const meta = dev.audio.get(b.file);
        if (!meta) return fail(res, 404, 'PROGRAM_AUDIO_MISSING', `Audio file '${b.file}' not found.`, 'file');
        if (!meta.valid) return fail(res, 422, meta.error.code, meta.error.message, 'file');
      } else if (b.source !== 'tone') {
        return fail(res, 422, 'PLAYBACK_START_FAILED', "source must be 'file' or 'tone'.", 'source');
      }
      return ok(res, dev.startPlayback(b));
    }
    if (m === 'POST' && route === '/audio/stop') {
      if (dev.playback) dev.finishPlayback('stopped');
      return ok(res, { state: 'idle' });
    }
    if ((match = /^\/audio\/([^/]+)$/.exec(route))) {
      if (!requireSd(dev, res)) return;
      const name = decodeURIComponent(match[1]);
      const entry = dev.audioList().find((f) => f.name === name);
      if (!entry) return fail(res, 404, 'PROGRAM_AUDIO_MISSING', `Audio file '${name}' not found.`);
      if (m === 'GET') return ok(res, entry);
      if (m === 'DELETE') {
        if (entry.in_active_program) {
          return fail(res, 409, 'AUDIO_IN_USE', `'${name}' is used by the active program and cannot be deleted.`, null, { referenced_by: entry.referenced_by });
        }
        dev.audio.delete(name);
        dev.sd.used_bytes -= entry.size_bytes;
        return ok(res, { deleted: name });
      }
    }

    // ---- programs -----------------------------------------------------------------------
    if (route === '/programs') {
      if (!requireSd(dev, res)) return;
      if (m === 'GET') {
        return ok(res, { active_id: dev.activeId, limits: LIMITS, programs: [...dev.programs.values()].map((p) => dev.programSummary(p)) });
      }
      if (m === 'POST') {
        let p;
        try { p = normalizeProgram(await readJson(req)); } catch { return fail(res, 400, 'PROGRAM_JSON_INVALID', 'Body is not valid JSON.'); }
        if (dev.programs.has(p.id)) return fail(res, 409, 'PROGRAM_ID_EXISTS', `A program with ID '${p.id}' already exists.`, 'id');
        if (dev.programs.size >= LIMITS.MAX_PROGRAMS) return fail(res, 422, 'PROGRAM_LIMIT_EXCEEDED', `At most ${LIMITS.MAX_PROGRAMS} programs can be stored.`);
        const errors = dev.validate(p);
        if (errors.length) return programInvalid(res, errors);
        p = storedProgram(p);
        p.modified_utc = isoUtc(dev.nowMs());
        dev.programs.set(p.id, p);
        return ok(res, { program: p, summary: dev.programSummary(p) }, 201);
      }
    }
    if ((match = /^\/programs\/([^/]+)(?:\/(validate|preview|activate))?$/.exec(route))) {
      if (!requireSd(dev, res)) return;
      const id = decodeURIComponent(match[1]);
      const action = match[2];
      const stored = dev.programs.get(id);

      if (action === 'validate' && m === 'POST') {
        const body = await readJson(req);
        const p = body ? normalizeProgram(body) : stored;
        if (!p) return fail(res, 404, 'PROGRAM_NOT_FOUND', `Program '${id}' not found.`);
        const errors = dev.validate(p);
        // Like the firmware: a valid program is hashed in its stored (normalized) form.
        return ok(res, { valid: errors.length === 0, revision: programRevision(errors.length ? p : storedProgram(p)), errors });
      }
      if (action === 'preview' && (m === 'GET' || m === 'POST')) {
        const body = m === 'POST' ? await readJson(req) : undefined;
        const p = body ? normalizeProgram(body) : stored;
        if (!p) return fail(res, 404, 'PROGRAM_NOT_FOUND', `Program '${id}' not found.`);
        if (!dev.rtcValid) return fail(res, 409, 'TIME_NOT_SET', 'Device time is not set; preview would be meaningless.');
        const errors = dev.validate(p);
        if (errors.length) return programInvalid(res, errors);
        const count = Math.min(500, Math.max(1, parseInt(url.searchParams.get('count') || '100', 10)));
        const norm = storedProgram(p);
        return ok(res, {
          revision: programRevision(norm), from_utc: isoUtc(dev.nowMs()), utc_offset_minutes: dev.utcOffsetMinutes,
          events: preview(norm, dev.utcOffsetMinutes, dev.nowMs(), count),
        });
      }
      if (action === 'activate' && m === 'POST') {
        if (!stored) return fail(res, 404, 'PROGRAM_NOT_FOUND', `Program '${id}' not found.`);
        const errors = dev.validate(stored);
        if (errors.length) return programInvalid(res, errors);
        dev.activeId = id;
        return ok(res, dev.status());
      }
      if (!action) {
        if (m === 'GET') {
          if (!stored) return fail(res, 404, 'PROGRAM_NOT_FOUND', `Program '${id}' not found.`);
          return ok(res, { program: stored, summary: dev.programSummary(stored), errors: dev.validate(stored) });
        }
        if (m === 'PUT') {
          if (!stored) return fail(res, 404, 'PROGRAM_NOT_FOUND', `Program '${id}' not found.`);
          let p;
          try { p = normalizeProgram(await readJson(req)); } catch { return fail(res, 400, 'PROGRAM_JSON_INVALID', 'Body is not valid JSON.'); }
          if (p.id !== id) return fail(res, 422, 'PROGRAM_JSON_INVALID', 'Program ID cannot be changed. Duplicate the program instead.', 'id');
          const errors = dev.validate(p);
          if (errors.length) return programInvalid(res, errors);
          p = storedProgram(p);
          p.modified_utc = isoUtc(dev.nowMs());
          dev.programs.set(id, p);
          return ok(res, { program: p, summary: dev.programSummary(p), applied: id === dev.activeId });
        }
        if (m === 'DELETE') {
          if (!stored) return fail(res, 404, 'PROGRAM_NOT_FOUND', `Program '${id}' not found.`);
          if (id === dev.activeId) return fail(res, 409, 'PROGRAM_ACTIVE', 'The active program cannot be deleted. Activate another program first.');
          dev.programs.delete(id);
          return ok(res, { deleted: id });
        }
      }
    }

    // ---- environment --------------------------------------------------------------------
    if (route === '/environment') {
      if (m === 'GET') {
        return ok(res, { ...dev.env, min_interval_minutes: 1, readings: dev.readings(), next_sample_utc: dev.nextEnvSample() });
      }
      if (m === 'PUT') {
        const b = (await readJson(req)) || {};
        if (b.enabled !== undefined && typeof b.enabled !== 'boolean') return fail(res, 422, 'ENV_CONFIG_INVALID', 'enabled must be true or false.', 'enabled');
        if (b.interval_minutes !== undefined && (!Number.isInteger(b.interval_minutes) || b.interval_minutes < 1 || b.interval_minutes > 1440)) {
          return fail(res, 422, 'ENV_INTERVAL_INVALID', 'Interval must be a whole number of minutes from 1 to 1440.', 'interval_minutes');
        }
        Object.assign(dev.env, b.enabled !== undefined ? { enabled: b.enabled } : {}, b.interval_minutes !== undefined ? { interval_minutes: b.interval_minutes } : {});
        return ok(res, { ...dev.env, min_interval_minutes: 1, readings: dev.readings(), next_sample_utc: dev.nextEnvSample() });
      }
    }

    // ---- logs ---------------------------------------------------------------------------
    if (m === 'GET' && route === '/logs') {
      if (!requireSd(dev, res)) return;
      return ok(res, { playback: dev.logInfo('playback'), environment: dev.logInfo('environment') });
    }
    if (m === 'GET' && (match = /^\/logs\/(playback|environment)$/.exec(route))) {
      if (!requireSd(dev, res)) return;
      const tail = parseInt(url.searchParams.get('tail') || '0', 10) || 0;
      res.writeHead(200, {
        'Content-Type': 'text/csv; charset=utf-8',
        'Cache-Control': 'no-store',
        ...(tail ? {} : { 'Content-Disposition': `attachment; filename="${dev.device.id}_${match[1]}.csv"` }),
      });
      return res.end(dev.logCsv(match[1], tail));
    }

    // ---- firmware -----------------------------------------------------------------------
    if (route === '/firmware') {
      if (m === 'GET') return ok(res, dev.firmware);
      if (m === 'POST') {
        let buf;
        try { buf = await readBody(req); } catch { return fail(res, 413, 'OTA_INVALID', 'Firmware image exceeds the application slot.'); }
        const part = parseMultipart(buf, req.headers['content-type'])?.find((p) => p.filename);
        if (!part) return fail(res, 400, 'OTA_INVALID', 'Expected a multipart upload with a firmware file.');
        if (part.data.length > dev.firmware.slot_size_bytes) return fail(res, 413, 'OTA_INVALID', 'Firmware image exceeds the application slot.');
        const info = inspectFirmware(part.data);
        if (!info.valid) return fail(res, 422, 'OTA_INVALID', info.message, 'file');
        const written = dev.firmware.next_partition;
        dev.firmware = {
          ...dev.firmware,
          version: info.version || dev.firmware.version,
          build_utc: isoUtc(Date.now()),
          running_partition: written,
          next_partition: dev.firmware.running_partition,
        };
        ctx.reboot();
        return ok(res, { bytes: part.data.length, partition: written, version: info.version, rebooting: true });
      }
    }

    return fail(res, 404, 'NOT_FOUND', `No route for ${m} ${url.pathname}`);
  } catch (e) {
    if (e instanceof SyntaxError) return fail(res, 400, 'JSON_INVALID', 'Request body is not valid JSON.');
    console.error(e);
    return fail(res, 500, 'INTERNAL', e.message);
  }
}
