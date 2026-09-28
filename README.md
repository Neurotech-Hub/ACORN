# ACORN

**Autonomous Configurable Outdoor Replay Node**: an ESP32-S3 field device that plays audio on
a daily schedule for wildlife research, logs playback and environmental data to SD, and is
configured from a phone over its own Wi-Fi access point.

- Firmware & interface specification: [`docs/init.md`](docs/init.md)
- Hardware pin reference: [`docs/hardware.md`](docs/hardware.md)
- Web UI development & porting guide: [`docs/web-ui.md`](docs/web-ui.md)
- Scheduling algorithms & parity testing: [`docs/scheduling.md`](docs/scheduling.md)
- Brand assets: [`brand/`](brand/) (vector versions in [`brand/svg/`](brand/svg/))

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
docs/       specification and guides
brand/      logo and brand kit
```

## Progress

### Phase 1: UI skeleton ✅

- [x] Static, dependency-free frontend in `web/` (~44 KB gzipped), light and dark themes, phone-first responsive layout
- [x] Mock `/api/v1` device (`dev/`) with realistic data, simulated latency, and nine fault scenarios
- [x] Information hierarchy and navigation: Status · Programs · Audio · Data · Device
- [x] Status dashboard: armed/not-armed state, blocking and non-blocking faults with fix links, next playback, window state, telemetry
- [x] Program editor workflow: RANGE/POINT events, fixed/random recurrence, defaults from previous event, 24 h timeline, staged Save & Apply, activate, duplicate, JSON upload/download
- [x] Validation presentation: device error codes mapped to fields, validation panel, timeline conflict highlighting
- [x] Schedule preview (device-generated, draft or saved)
- [x] Audio workflow: first-class test player (file or built-in tone), WAV library with metadata, upload with progress, guarded delete
- [x] Log access: playback/environment CSV download and recent-rows preview
- [x] Environment settings: enable, presets, and custom integer-minute intervals
- [x] Device settings: name, ID, time sync from browser, UTC offset, battery voltage→percent model
- [x] OTA workflow: upload with progress, verify, reboot and reconnect handling
- [x] Porting path: `tools/build-web.mjs` → gzipped assets + `web_assets.h`; `--dist` mode to verify the packaged build

### Phase 2: Hardware abstraction and helpers (code complete, board bring-up pending)

`AcornHardware` library plus one isolated test sketch per component, all compiling with `arduino-cli` (see [`arduino/README.md`](arduino/README.md)). Each item still needs its test sketch to pass on real hardware.

- [x] RTC, UTC time handling: MCP7940N driver, battery-backed "time set" marker, alarm on MFP, epoch/ISO conversion (`test_rtc`)
- [x] SD card: SPI mount, detect, capacity, write check, `SD_*` error codes, WAV header validation (`test_sd`)
- [x] Audio output: 3V3_SW_EN / I2S / DAC_XSMT / AMP_SHDN power sequence (high-Z = off via 100k pulldown, driven HIGH = on), tone generator (`test_audio`)
- [x] Battery measurement: calibrated ADC, divider, invalid detection (`test_battery`)
- [x] Temperature/humidity (SHT40, CRC checked) and ambient light (VEML6031X00, auto-range) (`test_sht40`, `test_light`, `test_i2c_scan`)
- [x] LEDs, buttons, Hall sensor (`test_leds`, `test_buttons`, `test_hall`)
- [x] Wi-Fi AP: open `ACORN-xxxxxx`, idle-timeout helper (`test_wifi_ap`)
- [x] Deep sleep, wake sources: EXT0 Hall HIGH + EXT1 ANY_LOW buttons/RTC, GPIO hold (`test_sleep_wake`)
- [x] OTA partition layout (2 × 6.25 MB slots) and update (`test_ota`)
- [ ] On-board verification of every test sketch (confirm LED polarity)
- [x] Spec resolved: LED_G "broadcasting" = Wi-Fi AP running (on exactly while the AP is up)
- [x] Spec resolved: Hall door open starts the configuration AP; door close or 10 min idle ends it (implemented in Phase 4)
- [x] Spec resolved: TRIG_IN unused in v1
- [ ] Spec deferred: volume-to-SPL calibration (after the prototype arrives)

### Phase 3: Core application ✅ (code complete, hardware testing in Phase 4)

`AcornCore` (pure C++, host-tested) + firmware services + `/api/v1` in `arduino/acorn/src`, runnable in
a stay-awake dev mode (see [`arduino/README.md`](arduino/README.md)). Algorithms: [`docs/scheduling.md`](docs/scheduling.md).

- [x] `AcornCore`: limits, error codes, program JSON + revision, `ProgramValidator`, `Scheduler`, env schedule, battery model, wake planner, CSV, UTC time; vendored ArduinoJson 7
- [x] Host unit tests covering `init.md` §67 plus golden previews shared with the mock (`arduino/host-tests`, `tools/check-mock-golden.mjs`)
- [x] `DeviceConfig` (NVS), `DeviceState` (faults, armed, status), `ProgramStore` (atomic save), `ActiveProgram` (§49 activation), `AudioLibrary`
- [x] `PlaybackService` (FreeRTOS task), `PlaybackLogger`, `EnvironmentService`, `EnvironmentLogger`
- [x] `BatteryService`, `TimeService`, `LedService`, `WifiService`, `FirmwareService` (OTA), `SleepManager`
- [x] REST/API layer matching the mock (`docs/web-ui.md` §4), with streamed audio and OTA uploads
- [x] Dev-mode runtime: boot self-check, scheduled playback, env sampling, BTN_01/BTN_02, serial console
- [x] API contract test for mock and device (`tools/api-contract.mjs`)

### Phase 4: Integration (code complete, hardware acceptance pending)

- [x] Embed the UI: `npm run build:web:firmware` → `arduino/acorn/src/web/web_assets.h`, served from the core `WebServer` with gzip, ETag and 304; `docs/web-ui.md` §5.2 updated
- [x] Captive portal: wildcard DNS + OS probe redirects, so phones open the UI on joining the AP
- [x] `dev/server.mjs --proxy http://192.168.4.1` to iterate on the UI against a real device
- [x] Event-driven boot/sleep (§40/§56) as the default: wake-reason dispatch (RTC event, env sample, BTN_01, BTN_02), then prepare-sleep with the next wake and RTC alarm (2 s lead; playback starts at the scheduled second); fault mode still sleeps and wakes, with scheduled playback blocked; `ACORN_STAY_AWAKE=1` bench build
- [x] Hall configuration mode: door open (EXT0 wake, reset, or while awake) starts AP + DNS + server and keeps the device awake (LED_G on); scheduled events and env samples still run; session ends on door close or 10 min without API activity (`GET /status` polling and connected phones are not activity, `POST /session/extend` is); a door left open at sleep time arms EXT0 to wake on door close
- [x] Recalculation hooks: time/offset changes clear the RTC alarm; the next wake and alarm are recomputed at every sleep, with no replay (§50); executed-event and env markers survive deep sleep
- [x] Mock parity: the mock normalizes programs on save like the firmware; `api-contract.mjs` checks normalization and the embedded UI, and passes against the mock, `--dist` and `--proxy`
- [ ] Hardware acceptance (`init.md` §68 items 1–16, procedure in [`arduino/README.md`](arduino/README.md)):
  - [ ] 1–3 RTC in UTC, SD mounts, `playback.wav` validated and played
  - [ ] 4–5 06:00–18:00 every 10 min program, 72 events 06:00 … 17:50
  - [ ] 6, 9 wakes only as needed, returns to sleep, sleep current recorded
  - [ ] 7–8 72 scheduled plays/day logged, env row every 10 min
  - [ ] 10–11 UI shows next playback, program edit/validation
  - [ ] 12–13 BTN_01 hierarchy, BTN_02 flashes (awake and from sleep)
  - [ ] 14 missing SD blocks cleanly
  - [ ] 15 OTA from the UI
  - [ ] 16 Hall config mode (open, close, idle timeout with door open, close wake, reopen)
  - [ ] API contract test against the device (`node tools/api-contract.mjs --base http://192.168.4.1`)
- [x] Docs: config mode and door-close wake in `docs/hardware.md`, dispatch timing in `docs/scheduling.md`, runtime and acceptance in `arduino/README.md`

### Open items carried from the spec (not decided)

Extra button behaviors (long-press / multi-function) · final audio calibration (volume units vs. SPL,
deferred until the prototype arrives).
