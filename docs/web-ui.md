# ACORN Web UI — Development & Porting Guide

Phase 1 deliverable (see `docs/init.md` §4). The configuration UI is a static, dependency-free
frontend that talks only to `/api/v1/*`. During development a Node mock stands in for the
device, so the UI can be built and inspected without an ESP32.

---

## 1. Layout

```text
web/                      UI source — served as-is in dev, embedded in firmware for release
  index.html              shell, inline SVG sprites (logo + icons), theme bootstrap
  favicon.svg
  css/app.css             all styles (light + dark themes)
  js/api.js               the only module that calls the device (fetch/XHR -> /api/v1)
  js/store.js             polls /api/v1/status; views subscribe
  js/ui.js                DOM builder, dialogs, toasts, formatters
  js/app.js               router (hash-based), header, connection/session banners
  js/views/*.js           status, programs, editor, audio, data, device

dev/
  server.mjs              static server + mock API (no npm install)
  mock/state.mjs          in-memory mock device + fault scenarios
  mock/api.mjs            /api/v1 route handlers (the working API contract)
  mock/scheduler.mjs      MOCK scheduler (preview/next event) — not the firmware scheduler
  mock/validator.mjs      MOCK validator — not the firmware validator
  mock/binary.mjs         multipart, WAV header and ESP32 image inspection

tools/build-web.mjs       gzip + C header generator for firmware embedding
brand/svg/                vectorized logo, icon mark and wordmark (currentColor)
```

The mock scheduler and validator exist only so the UI receives realistic data. The
authoritative implementations are firmware `Scheduler` / `ProgramValidator` (Phase 3). They
should reproduce the same semantics (`init.md` §12–18) but must not be ported from this JS.

---

## 2. Running the UI without hardware

Requires Node 18+. No dependencies.

```bash
npm run dev            # http://127.0.0.1:8080/   (same as: node dev/server.mjs)
npm run dev:lan        # bind 0.0.0.0 to test on a phone on the same network
```

Options: `--port 8080`, `--host 127.0.0.1`, `--scenario <name>`, `--latency <ms>` (default 150,
simulates the ESP32 round-trip), `--dist` (serve the gzipped firmware build, see §5).

### Fault scenarios

Open **http://127.0.0.1:8080/__dev** to switch the mock device into a scenario (resets mock state):

| Scenario | What it exercises |
|---|---|
| `normal` | Healthy, armed, default 12-hour program active |
| `sd-missing` | `SD_NOT_PRESENT`: blocked playback; tone test and OTA still work |
| `sd-full` | `SD_FULL` blocking fault |
| `wav-missing` | Active program references a missing `/audio/playback.wav` |
| `program-invalid` | Active program on SD has overlapping ranges |
| `no-program` | Fresh card, no programs (`PROGRAM_NOT_FOUND`) |
| `rtc-invalid` | `TIME_NOT_SET`; fix via Device → Sync clock |
| `low-battery` | ~5% battery warning |
| `sensor-fail` | Non-blocking `SENSOR_READ_FAILED` |

The mock also accepts real uploads: WAV headers are parsed (16-bit mono 44.1/48 kHz accepted;
stereo etc. rejected with `WAV_UNSUPPORTED`), and firmware `.bin` files are checked for the
ESP32 image magic byte (`0xE9`), then a ~5 s reboot is simulated.

---

## 3. UI structure (information architecture)

Five sections; bottom tab bar on phones, sidebar at ≥900 px.

| Section | Contents (`init.md` §44) |
|---|---|
| **Status** | Armed / Not-armed hero with blocking faults and fix links, next playback + countdown, active/quiet window, active program, last playback, battery, SD, environment, device time, connection |
| **Programs** | Program library (activate, duplicate, JSON upload/download, delete) and the program editor: events (RANGE/POINT, fixed/random), 24 h timeline, device-side validation, schedule preview, Save & Apply |
| **Audio** | First-class field test player (source, volume, Play/Stop, built-in tone), WAV library with metadata, upload, guarded delete |
| **Data** | Environmental readings, logging enable + interval (presets and custom minutes), playback/environment CSV download and recent-rows preview |
| **Device** | Identity (ID, name), time (sync from browser, UTC offset), battery voltage→percent model, firmware OTA, diagnostics |

Principles carried from the spec:

- **UI is an API client.** No schedule is computed in the browser; preview, next playback,
  active/quiet window and validation all come from the device. The editor timeline only draws
  the event definitions.
- **Staged edits, one apply.** Program, environment, name, offset and battery model edits
  are held in the browser until *Save & Apply*; leaving with unsaved edits asks first.
- **Viewing is non-destructive.** Nothing mutates on page load.
- **Device clock, not browser clock**, drives countdowns (`store.deviceNow()`).
- **Offline-safe.** No CDNs, web fonts or external requests: the AP has no internet.

Brand: the logo/icon are traced from `brand/ACORN_logo.png` (unaltered geometry, one color).
Neutral warm paper/ink palette with black/white brand marks; color is reserved for device state
(green armed, amber warning, red fault). Dark theme for night fieldwork.

---

## 4. API contract (as implemented by the mock)

All JSON responses use the `init.md` §47 envelope: `{ "ok": true, "data": … }` or
`{ "ok": false, "error": { "code", "message", "field?", "details" } }`.
Program rejections (HTTP 422) carry the full list in `error.details.errors[]`, each
`{ code, message, field }` where `field` is a path such as `events[1].recurrence.interval_seconds`.
The UI maps these paths onto form fields.

| Method & route | Notes |
|---|---|
| `GET /status` | Aggregate dashboard model (see fields below). Polled every 5 s (1 s while playing). |
| `POST /session/extend` | *Added.* Resets the AP inactivity timer ("Stay connected"). |
| `GET/PUT /device` | `{ id, name, firmware_version, battery_model: { name, points: [[volts, pct], …] } }` |
| `GET/PUT /time` | PUT `{ utc?: "…Z", utc_offset_minutes?: int }`; returns `{ utc, local, utc_offset_minutes, rtc_valid }` |
| `GET /audio` | `{ files: [{ name, path, size_bytes, duration_ms, sample_rate, bits_per_sample, channels, valid, error?, referenced_by[], in_active_program }], rules }` |
| `POST /audio` | multipart field `file`; validates WAV header |
| `GET /audio/{file}` | metadata for one file |
| `DELETE /audio/{file}` | `409 AUDIO_IN_USE` if used by the active program |
| `POST /audio/test` | `{ source: "file"|"tone", file?, volume }`; logged as `manual_ui` |
| `POST /audio/stop` | |
| `GET /programs` | `{ active_id, limits, programs: [summary] }` |
| `POST /programs` | create; `409 PROGRAM_ID_EXISTS` |
| `GET /programs/{id}` | `{ program, summary, errors }` |
| `PUT /programs/{id}` | validate-then-atomic-save; `applied: true` when it is the active program |
| `DELETE /programs/{id}` | `409 PROGRAM_ACTIVE` for the active program |
| `POST /programs/{id}/validate` | body optional: validates a **draft** if provided, else the stored file |
| `GET /programs/{id}/preview?count=N` | stored program |
| `POST /programs/{id}/preview?count=N` | *Added.* Preview an unsaved draft (editor needs this before saving) |
| `POST /programs/{id}/activate` | full validation before switching |
| `GET/PUT /environment` | `{ enabled, interval_minutes, min_interval_minutes, readings, next_sample_utc }` |
| `GET /logs` | `{ playback: { path, size_bytes, rows, modified_utc }, environment: {…} }` |
| `GET /logs/{playback|environment}` | CSV download; `?tail=N` returns header + last N rows inline |
| `GET /firmware` | *Added.* `{ version, build_utc, running_partition, next_partition, slot_size_bytes }` |
| `POST /firmware` | multipart `file`; responds before reboot with `{ rebooting: true }` |

`GET /status` fields: `device{id,name,firmware_version}`, `mode`, `armed`,
`faults[{code,message,blocking,area,program_id?,field?}]`, `time{utc,local,utc_offset_minutes,rtc_valid}`,
`reset_reason`, `wake_reason`, `uptime_s`, `battery{voltage,percent,valid,model}`,
`sd{present,mounted,total_bytes,used_bytes,free_bytes}`,
`environment{temperature_c,relative_humidity_pct,ambient_light_lux,ok,sampled_utc,logging_enabled,interval_minutes}`,
`program{id,name,revision,valid,loaded,window{state:"active"|"quiet"|"points_only",event_id,until_local,until_utc}}`,
`playback{state,source,audio,volume,duration_ms,position_ms,started_utc}`, `last_playback`,
`next_playback{event_id,scheduled_utc,scheduled_local,audio,volume}`, `next_env_sample_utc`,
`wifi{mode,ssid,security,clients,idle_timeout_s,idle_remaining_s}`.

Error codes used in addition to `init.md` §48: `AUDIO_IN_USE`, `AUDIO_NAME_TOO_LONG`,
`PROGRAM_ID_EXISTS`, `PROGRAM_ACTIVE`, `PROGRAM_LIMIT_EXCEEDED`, `DEVICE_NAME_INVALID`,
`BATTERY_MODEL_INVALID`, `TIME_INVALID`, `TIME_OFFSET_INVALID`, `ENV_CONFIG_INVALID`,
`ENV_INTERVAL_INVALID`, `VOLUME_INVALID`, `UPLOAD_INVALID`, `UPLOAD_TOO_LARGE`, `NOT_FOUND`.

Mock limits (to be confirmed by memory testing in Phase 3, `init.md` §55): 16 programs,
32 events/program, name 48 chars, description 200, audio filename 32, scheduling guard 2 s.

**Open decision for Phase 4:** the mock does *not* count `GET /status` polling as AP activity,
so an idle open page still times out after 10 min (with a 60 s "Stay connected" warning).

---

## 5. Porting to firmware

The UI ships inside the firmware image (not on the SD card), so it is available in SD-fault
mode and is updated together with firmware via OTA.

### 5.1 Build the assets

```bash
npm run build:web
# or write the header straight into the firmware tree:
node tools/build-web.mjs --header arduino/acorn/src/web/web_assets.h
```

Output:

- `build/web/**.gz`: gzipped copy of every file in `web/`
- `web_assets.h`: `WEB_ASSETS[]` table of `{ path, mime, etag, data, length }` in PROGMEM

The script prints a size report and fails if the gzipped UI exceeds 150 KB (currently ~44 KB).

Verify the packaged build before flashing. This serves the exact gzipped bytes with
`Content-Encoding: gzip`, just as the device will:

```bash
npm run build:web && node dev/server.mjs --dist
```

### 5.2 Serve from the firmware

Sketch for ESPAsyncWebServer (adjust to the server library chosen in Phase 2; some forks
rename `beginResponse_P` to `beginResponse`):

```cpp
#include <ESPAsyncWebServer.h>
#include "web/web_assets.h"

static void sendAsset(AsyncWebServerRequest *req, const WebAsset &a) {
  if (req->hasHeader("If-None-Match") && req->header("If-None-Match") == a.etag) {
    req->send(304);
    return;
  }
  AsyncWebServerResponse *res = req->beginResponse_P(200, a.mime, a.data, a.length);
  res->addHeader("Content-Encoding", "gzip");
  res->addHeader("ETag", a.etag);
  res->addHeader("Cache-Control", "no-cache");  // revalidate via ETag; UI changes with firmware
  req->send(res);
}

void registerWebUi(AsyncWebServer &server) {
  for (size_t i = 0; i < WEB_ASSET_COUNT; i++) {
    const WebAsset *a = &WEB_ASSETS[i];
    server.on(a->path, HTTP_GET, [a](AsyncWebServerRequest *req) { sendAsset(req, *a); });
    if (strcmp(a->path, "/index.html") == 0) {
      server.on("/", HTTP_GET, [a](AsyncWebServerRequest *req) { sendAsset(req, *a); });
    }
  }
}
```

Requirements for the firmware side:

1. **MIME types matter.** `js/*.js` are ES modules and browsers refuse them unless served as
   `text/javascript`. The generated table already carries the right type.
2. **Always send `Content-Encoding: gzip`.** Every embedded body is gzip.
3. Register `/api/v1/*` handlers on the same server. Keep response shapes identical to §4
   (or update `web/js/api.js` and this document together).
4. Uploads (`POST /audio`, `POST /firmware`) arrive as `multipart/form-data` with field `file`.
   Stream to SD / the OTA partition; don't buffer whole files in RAM.
5. For `POST /firmware`, send the JSON response *before* restarting (e.g. restart from a
   short timer), so the browser receives `{ ok: true }` and starts its reconnect loop.
6. Optional: a captive-portal redirect for unknown hosts to `/` makes the UI open
   automatically on phones joining the AP (TBD with the Hall/config-mode design).

### 5.3 Workflow

```text
edit web/ ──► npm run dev (mock) ──► iterate
         └──► npm run build:web ──► node dev/server.mjs --dist (verify)
                                └──► web_assets.h into firmware ──► build/flash or OTA
```

Treat `web_assets.h` as a build artifact: regenerate it rather than editing it.
