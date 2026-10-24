# ACORN

**Autonomous Configurable Outdoor Replay Node**: an ESP32-S3 field device that plays audio on
a daily schedule for wildlife research, logs playback and environmental data to SD, and is
configured from a phone over its own Wi-Fi access point.

- Firmware & interface specification: `[docs/init.md](docs/init.md)`
- Hardware pin reference: `[docs/hardware.md](docs/hardware.md)`
- Web UI development & porting guide: `[docs/web-ui.md](docs/web-ui.md)`
- Scheduling algorithms & parity testing: `[docs/scheduling.md](docs/scheduling.md)`
- ACORN Utilities, browser tools such as the audio converter: [neurotech-hub.github.io/ACORN/utilities](https://neurotech-hub.github.io/ACORN/utilities/) (source and setup in `[docs/web-ui.md](docs/web-ui.md)` §6)
- Brand assets: `[brand/](brand/)` (vector versions in `[brand/svg/](brand/svg/)`)



## Quick start (UI, no hardware needed)

Requires Node 18+. There are no dependencies to install.

```bash
npm run dev          # UI + mock device at http://127.0.0.1:8080/
                     # fault scenarios at  http://127.0.0.1:8080/__dev
npm run build:web    # gzip + generate web_assets.h (build/web/)
npm run build:web:firmware   # regenerate the header embedded in the firmware
node dev/server.mjs --proxy http://192.168.4.1   # local UI against a real device
```



## Repository layout

```text
web/        configuration UI (static HTML/CSS/JS, served by the device)
dev/        mock device API + dev server for building the UI without hardware
tools/      build tooling (web asset packaging), API contract test, mock golden check
arduino/    firmware (Phase 2+)
hardware/   hardware design files
docs/       specification and guides; docs/utilities/ is the GitHub Pages utilities site
brand/      logo and brand kit
```

## Flash Settings

| Setting          | Value                                                         |
| ---------------- | ------------------------------------------------------------- |
| Board            | ESP32S3 Dev Module (esp32 board library)                      |
| Flash Size       | 16MB                                                          |
| PSRAM            | QSPI PSRAM                                                    |
| USB CDC On Boot  | Enabled                                                       |
| Partition Scheme | Default (the sketch's `partitions.csv` is used automatically) |

### Open items carried from the spec (not decided)

Extra button behaviors (long-press / multi-function) · final audio calibration (volume units vs. SPL,
deferred until the prototype arrives).

## Plan: site lat/long on SD + stamped into logs

Not implemented yet. Execute this plan end-to-end (docs, firmware, mock, UI, tests). Goal: every
pulled card’s playback and environment CSVs are self-contained for analysis, while the current
site fix remains a single editable/prestageable file.

### Design decisions (locked)

- **Current site** lives on SD at `/config/site.json` (experiment artifact per `docs/init.md` §3.3).
  Not NVS. Prestage by copying the file onto the card before deploy.
- **Every new** `/logs/playback.csv` and `/logs/environment.csv` row includes `latitude,longitude`
  (decimal degrees). These are the last two columns. Blank cells when unset (same rule as other
  optional telemetry — never fake zeros).
- **Accuracy, source, and timestamps are not CSV columns.** Keep them only in `/config/site.json`
  and the API/UI so the operator can see how the fix was obtained.
- Changing site mid-deployment only affects rows written **after** the change. Historical rows keep
  whatever lat/long was current when they were appended.
- Missing site does **not** block armed/scheduled operation.
- Phone GNSS does not need internet; the config AP is still internet-less. Geolocation can work
  offline with sky view; cold starts may be slower without A-GPS. Manual entry is the fallback.

### `/config/site.json` shape

```json
{
  "latitude": 45.123456,
  "longitude": -93.654321,
  "accuracy_m": 12.5,
  "source": "phone_gps",
  "fixed_utc": "2026-10-24T13:15:00Z",
  "note": ""
}
```

- Required when present: `latitude`, `longitude` (finite numbers; lat ∈ [-90, 90], lon ∈ [-180, 180]).
- Optional: `accuracy_m` (≥ 0), `source` (`phone_gps` | `manual` | `prestage`), `fixed_utc` (ISO-Z),
  `note` (short string).
- Absent file or cleared site ⇒ unset. Prefer **deleting** `/config/site.json` on clear so prestaged
  absence and UI clear behave the same.

### CSV header changes (breaking)

Append to both headers (order matters; match firmware `Csv.h`, mock, docs, contract tests):

```text
playback:    …,ambient_light_lux,latitude,longitude
environment: …,battery_percent,latitude,longitude
```

**Existing non-empty logs:** on `CsvFile::ensure`, if the on-disk first line is not exactly the new
header, rename to `playback.csv.bak` / `environment.csv.bak` (overwrite prior `.bak` if needed) and
create a fresh file with the new header. Do not append new-schema rows under an old header — the
`tail` API overlays the canonical header and would misalign columns.

Update docs examples in `docs/init.md` §§35–36, SD layout §6 (add `/config/`), host-test goldens in
`arduino/host-tests/test_core.cpp`, mock builders in `dev/mock/state.mjs`, and
`tools/api-contract.mjs` header prefix checks.

### Firmware

1. **`AppConfig.h`** — `CONFIG_DIR = "/config"`, `SITE_PATH = "/config/site.json"`.
2. **`SiteStore` (new, under `arduino/acorn/src/`)** — load/cache/save/clear site JSON; mkdir
   `/config`; atomic write like `ProgramStore` (`.tmp` → validate → rename). Expose
   `hasFix()`, `latitude()`, `longitude()` for loggers. Reload on SD remount / boot with other
   SD-backed state.
3. **`Csv` (`arduino/libraries/AcornCore/src/Csv.{h,cpp}`)** — extend row builders to accept
   optional lat/lon; format with enough precision for field GPS (e.g. 6 decimal places); empty
   cells when no fix.
4. **`PlaybackLogger` / `EnvironmentLogger` / `Runtime`** — when building a row, read current site
   from `SiteStore` and stamp it.
5. **`CsvFile::ensure`** — header mismatch → rotate to `.bak` then write new header (see above).
6. **API** (`ApiServer.cpp` + client / `docs/web-ui.md`):
   - `GET /site` → site object or unset shape (`latitude`/`longitude` null).
   - `PUT /site` → body `{ latitude, longitude, accuracy_m?, source?, note? }`; server sets
     `fixed_utc` from RTC when valid, else omit/null; validate ranges; persist; return saved object.
   - `DELETE /site` → delete file / clear cache.
   - Optionally surface a short summary on `GET /status` (lat/lon or null) for the status page;
     keep full metadata on `/site`.
7. Error codes — e.g. `SITE_INVALID`; reuse existing `SD_NOT_PRESENT` / log-write faults where
   appropriate.

### Mock (`dev/mock/`)

Mirror `/site` GET/PUT/DELETE, persist in mock state, stamp lat/lon on synthesized playback and
environment rows, and serve updated CSV headers. Seed one scenario with a fix and one without.

### Web UI (`web/js/views/device.js` + `api.js`)

Add a **Site location** card on Device (same staged Save & Apply / Discard pattern as name/offset):

- Show current lat/lon, accuracy, source, and `fixed_utc` when set; muted “Not set” when unset.
- **Use phone location** — `navigator.geolocation.getCurrentPosition` (enable high accuracy); on
  success stage lat/lon/`accuracy_m`/`source: phone_gps`; toast on denial/timeout/unavailable.
- **Manual entry** — two numeric inputs; `source: manual`.
- **Clear** — confirm, then `DELETE /site`.
- **Save & Apply** — `PUT /site` with staged values.
- Offline-safe: no maps, no external tiles. Hint text may note GPS works without internet but needs
  sky view; manual entry always available.
- Wire `api.getSite` / `updateSite` / `clearSite`. Rebuild embedded assets (`npm run build:web:firmware`)
  when shipping firmware.

### Tests / verification

- Host tests: row builders with and without a fix; header string equality.
- API contract: `/site` round-trip, validation 422s, CSV header prefixes include `latitude,longitude`.
- Manual: mock UI capture → save → download CSV and confirm columns; clear → blank cells on new rows.
- Optional field check: prestaged `/config/site.json` on a card is loaded after mount without using
  the UI.

### Out of scope

- Onboard GPS hardware.
- Capturing location before joining the AP (nice follow-up if captive-portal Geolocation proves
  flaky on some phones; same `PUT /site` either way).
- Per-deployment “session” objects beyond this single current site file.
- Sunrise/sunset or schedule logic keyed off coordinates.
