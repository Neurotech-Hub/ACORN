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
simulates the ESP32 round-trip), `--dist` (serve the gzipped firmware build with ETag/304, see §5),
`--proxy http://192.168.4.1` (send `/api/*` to a real device instead of the mock, including
uploads; join the device AP first). With `--proxy` you can iterate on `web/` against real
hardware without reflashing:

```bash
node dev/server.mjs --proxy http://192.168.4.1   # UI from web/, API from the device
```

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
| `no-site` | No `/config/site.json`; log rows have empty latitude/longitude (all other scenarios carry a fix) |

The mock also accepts real uploads: WAV headers are parsed (16-bit mono 44.1/48 kHz accepted;
stereo etc. rejected with `WAV_UNSUPPORTED`), and firmware `.bin` files are checked for the
ESP32 image magic byte (`0xE9`), then a ~5 s reboot is simulated.

---

## 3. UI structure (information architecture)

Five sections; bottom tab bar on phones, sidebar at ≥900 px.

| Section | Contents (`init.md` §44) |
|---|---|
| **Status** | Armed / Not-armed hero with blocking faults and fix links, next playback + countdown, active/quiet window, active program, last playback, battery, SD, environment, device time, connection |
| **Programs** | Program library (activate/deactivate, duplicate, JSON upload/download, delete) and the program editor: events (RANGE/POINT, fixed/random), 24 h timeline, device-side validation, schedule preview, Save & Apply |
| **Audio** | First-class field test player (source, volume, Play/Stop, built-in tone), WAV library with metadata, upload, guarded delete |
| **Data** | Environmental readings, logging enable + interval (presets and custom minutes), playback/environment CSV download and recent-rows preview |
| **Device** | Identity (ID, name), time (sync from browser, UTC offset), site location (phone GPS or manual entry, stored in `/config/site.json`), battery discharge plot (read-only voltage→percent model with the current reading marked), firmware OTA, diagnostics |

Principles carried from the spec:

- **UI is an API client.** No schedule is computed in the browser; preview, next playback,
  active/quiet window and validation all come from the device. The editor timeline only draws
  the event definitions.
- **Staged edits, one apply.** Program, environment, name, offset and site location edits
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
| `GET/PUT /device` | `{ id, name, firmware_version, battery_model: { name, points: [[volts, pct], …] } }`. PUT accepts `name` only; the battery model is fixed in firmware (12.0 V = 100%) and a `battery_model` field is rejected with 422 `BATTERY_MODEL_INVALID`. |
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
| `POST /programs/{id}/deactivate` | clears the active program if it is `{id}` (device not armed); returns status |
| `GET/PUT /environment` | `{ enabled, interval_minutes, min_interval_minutes, readings, next_sample_utc }` |
| `GET/PUT/DELETE /site` | *Added.* Site location in `/config/site.json`: `{ set, latitude, longitude, accuracy_m, source, fixed_utc, note }`, all `null` when unset. PUT `{ latitude, longitude, accuracy_m?, source?: "phone_gps"|"manual"|"prestage", note? }` (default source `manual`); the device sets `fixed_utc` from its clock when the clock is valid. Bad values → 422 `SITE_INVALID` with `field`. DELETE removes the file. All three need the SD card (503 otherwise). |
| `GET /logs` | `{ playback: { path, size_bytes, rows, modified_utc }, environment: {…} }` |
| `GET /logs/{playback|environment}` | CSV download; `?tail=N` returns header + last N rows inline. Both logs end with `latitude,longitude` (6 decimals, blank when no site is set). |
| `GET /firmware` | *Added.* `{ version, build_utc, running_partition, next_partition, slot_size_bytes }` |
| `POST /firmware` | multipart `file`; responds before reboot with `{ rebooting: true }` |

`GET /status` fields: `device{id,name,firmware_version}`, `mode`, `armed`,
`faults[{code,message,blocking,area,program_id?,field?}]`, `time{utc,local,utc_offset_minutes,rtc_valid}`,
`reset_reason`, `wake_reason`, `uptime_s`, `battery{voltage,percent,valid,model}`,
`sd{present,mounted,total_bytes,used_bytes,free_bytes}`, `site` (`null` or `{latitude,longitude,accuracy_m,source,fixed_utc,note}`),
`environment{temperature_c,relative_humidity_pct,ambient_light_lux,ok,sampled_utc,logging_enabled,interval_minutes}`,
`program{id,name,revision,valid,loaded,window{state:"active"|"quiet"|"points_only",event_id,until_local,until_utc}}`,
`playback{state,source,audio,volume,duration_ms,position_ms,started_utc}`, `last_playback`,
`next_playback{event_id,scheduled_utc,scheduled_local,audio,volume}`, `next_env_sample_utc`,
`wifi{mode,ssid,security,clients,idle_timeout_s,idle_remaining_s}`.

Error codes used in addition to `init.md` §48: `AUDIO_IN_USE`, `AUDIO_NAME_TOO_LONG`,
`PROGRAM_ID_EXISTS`, `PROGRAM_ACTIVE`, `PROGRAM_LIMIT_EXCEEDED`, `DEVICE_NAME_INVALID`,
`BATTERY_MODEL_INVALID`, `TIME_INVALID`, `TIME_OFFSET_INVALID`, `ENV_CONFIG_INVALID`,
`ENV_INTERVAL_INVALID`, `SITE_INVALID`, `VOLUME_INVALID`, `UPLOAD_INVALID`, `UPLOAD_TOO_LARGE`, `NOT_FOUND`.

Mock limits (to be confirmed by memory testing in Phase 3, `init.md` §55): 16 programs,
32 events/program, name 48 chars, description 200, audio filename 32, scheduling guard 2 s.

**AP activity (decided in Phase 4):** neither `GET /status` polling nor a connected phone counts as
activity, on the mock or the device. Any other API request does, including `POST /session/extend`.
An idle open page therefore still times out after 10 min, with a 60 s "Stay connected" warning.
The device also ends the session when the enclosure door closes.

---

## 5. Porting to firmware

The UI ships inside the firmware image (not on the SD card), so it is available in SD-fault
mode and is updated together with firmware via OTA.

### 5.1 Build the assets

```bash
npm run build:web
# or write the header straight into the firmware tree:
npm run build:web:firmware   # = node tools/build-web.mjs --header arduino/acorn/src/web/web_assets.h
```

`arduino/acorn/src/web/web_assets.h` is committed, so the sketch builds without Node (for example,
from the Arduino IDE). `arduino/compile-tests.sh` regenerates it before every compile.

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

The firmware uses the arduino-esp32 core `WebServer`
([`arduino/acorn/src/api/WebUi.cpp`](../arduino/acorn/src/api/WebUi.cpp)). `/api/v1/*` goes to
`ApiServer`'s request handler, and every other request lands in the not-found handler, which
calls `WebUi::serve()`. The essential part:

```cpp
#include <WebServer.h>
#include "web/web_assets.h"

// Once, when the server is created: the core WebServer only keeps listed headers.
static const char *headers[] = {"If-None-Match"};
server.collectHeaders(headers, 1);

bool serveAsset(WebServer &s, const WebAsset &a) {
  s.sendHeader("ETag", a.etag);
  s.sendHeader("Cache-Control", "no-cache");  // revalidate via ETag; UI changes with firmware
  if (s.header("If-None-Match") == a.etag) {
    s.send(304);
    return true;
  }
  s.sendHeader("Content-Encoding", "gzip");
  s.send_P(200, a.mime, reinterpret_cast<const char *>(a.data), a.length);  // straight from flash
  return true;
}
```

`/` maps to `/index.html`. Unknown paths get a plain-text 404.

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
6. Captive portal: while the AP runs, a wildcard DNS server resolves every name to
   `192.168.4.1`. Operating-system connectivity probes (`/generate_204`, `/hotspot-detect.html`,
   `/connecttest.txt`, `/ncsi.txt`, `/canonical.html`, `/success.txt`, ...) and any non-API request
   with a foreign `Host` get `302 → http://192.168.4.1/`, so phones open the UI when they join
   `ACORN-xxxxxx`. `/api/v1/*` is never redirected.

### 5.3 Workflow

```text
edit web/ ──► npm run dev (mock) ──► iterate
         ├──► node dev/server.mjs --proxy http://192.168.4.1 (real device API)
         └──► npm run build:web ──► node dev/server.mjs --dist (verify)
                                └──► npm run build:web:firmware ──► build/flash or OTA
```

`node tools/api-contract.mjs --base <url>` checks the API, the gzip/ETag/304 asset handling and
(against `http://192.168.4.1`) the captive-portal redirect.

Treat `web_assets.h` as a build artifact: regenerate it rather than editing it.

---

## 6. ACORN Utilities (GitHub Pages)

Browser-based tools at <https://neurotech-hub.github.io/ACORN/utilities/>, served by GitHub
Pages from `docs/`. They are separate from the device UI: nothing in `docs/utilities/` is
embedded in the firmware, and the pages never talk to a device.

- **Audio converter** (`utilities/audio-converter/`): converts any audio file the browser can
  decode (WAV, MP3, M4A/AAC, FLAC, OGG, ...) to the WAV format the firmware accepts (16-bit
  linear PCM, mono, 44.1 or 48 kHz):
  - Stereo to mono: Combine (average of the channels, the default), Left, or Right. The page
    warns when Combine loses level (one channel nearly silent) or cancels (channels out of phase).
  - Trim with draggable handles on the waveform, typed start/end times, or the keyboard.
  - Preview of the trimmed clip with the selected channel, then download.
- **Log Plotter** (`utilities/log-plotter/`): summarizes and plots one `playback.csv` or
  `environment.csv` from the SD card or the device's Data page. The log type is detected from
  the header, and logs from older firmware without `latitude,longitude` still load:
  - Summary tiles. Playback logs show counts by result, source, program, event and audio, plus
    duration, start delay, errors, battery, temperature and site. Environment logs show sample
    interval, coverage and gaps, sensor ranges, missing readings, battery and site.
  - Plots. Playback logs get playbacks per day by result, time of day (scheduled vs manual),
    battery and temperature at each playback. Environment logs get temperature, humidity,
    light, battery and a daily temperature profile. Gaps break the lines.
  - A UTC offset picker for local-time plots (logs are UTC), **Copy summary** (plain text) and
    **Print / PDF** (a print stylesheet with light colors and no controls).
  - Demo playback and environment logs, generated in the page, for trying it without a device.

Files are processed locally and never uploaded.

```text
docs/
  index.html                  redirects the site root to utilities/
  utilities/
    index.html                hub landing page (one card per tool)
    favicon.svg               copy of web/favicon.svg
    audio-converter/
      index.html              page shell
      converter.js            page logic
    log-plotter/
      index.html              page shell
      plotter.js              page logic: summary tiles, plots, copy, print
      demo.js                 seeded demo playback and environment logs
    shared/
      icons.svg               SVG sprite (logo, wordmark, icons from web/index.html)
      css/utilities.css       tokens and components adapted from web/css/app.css
      js/ui-helpers.js        DOM builder, theme toggle, toasts, formatting
      js/wav-encoder.js       canonical 44-byte header + 16-bit PCM samples
      js/audio-processor.js   decode/resample (Web Audio API), mix, trim, WAV header reading
      js/waveform.js          canvas waveform with trim handles and playhead
      js/acorn-logs.js        CSV parsing for both log layouts, stats, gaps
      js/charts.js            SVG time-series and column charts with tooltips
```

Like `web/`, there are no dependencies or build step.

### 6.1 Converter behavior

- **Resampling:** `decodeAudioData()` on an `OfflineAudioContext` running at the output rate
  decodes and resamples in one step. The default output rate is 48 kHz for 48 kHz-family
  sources (8, 16, 32, 48, 96 kHz ...) and 44.1 kHz for everything else, so 44.1 and 48 kHz
  files are not resampled.
- **Quantization:** positive samples are scaled by 32767 and negative ones by 32768, which
  matches Chromium's 16-bit decoder. An ACORN-ready file exported without changes is
  bit-identical to the source.
- **File names:** the default is `<source-name>-acorn.wav`. The name is lowercased, and anything
  outside `[a-z0-9._-]` becomes `-`, as in the firmware's `AudioLibrary::sanitizeName`, so the
  device keeps the name on upload.
- **Size:** the page warns above 16 MB (`app::MAX_UPLOAD_BYTES`, about 3 minutes of 44.1 kHz mono),
  the most the device web upload accepts. Larger files can still be copied to `/audio` on the SD card.
- **Browser support:** decodable formats depend on the browser. Safari rejects 24-bit and
  floating-point WAV, and the page then suggests Chrome or Firefox.

If the firmware's WAV rules change (`WavReader::isSupported`), update `ACORN_WAV` in
`wav-encoder.js` too. If the CSV columns change (`Csv::PLAYBACK_HEADER`,
`Csv::ENVIRONMENT_HEADER`), update the column lists in `acorn-logs.js`. Keep `utilities.css`
visually in step with `web/css/app.css`.

### 6.2 Local preview

ES modules do not load from `file://`, so serve the folder:

```bash
python3 -m http.server 8000 --directory docs   # http://127.0.0.1:8000/utilities/
```

### 6.3 Publishing

One-time setup by a repository admin:

1. On GitHub, open **Settings → Pages**.
2. Under **Build and deployment**, set **Source** to *Deploy from a branch*, choose the `main`
   branch and the `/docs` folder, then **Save**.
3. After the first deployment (a minute or two, shown under **Actions**), the site is at
   <https://neurotech-hub.github.io/ACORN/utilities/>. The site root redirects there.

After that, every push to `main` that changes `docs/` republishes the site. GitHub Pages also
renders the Markdown guides in `docs/` (for example `/ACORN/web-ui.html`). That is harmless,
but Jekyll treats double curly braces and curly-brace-percent pairs as Liquid template tags,
even inside code blocks, and a malformed tag fails the whole deployment. Keep them out of
`docs/*.md`, or add an empty `docs/.nojekyll` to publish the files as-is.
