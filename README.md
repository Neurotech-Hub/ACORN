# ACORN

**Autonomous Configurable Outdoor Replay Node**: an ESP32-S3 field device that plays audio on
a daily schedule for wildlife research, logs playback and environmental data to SD, and is
configured from a phone over its own Wi-Fi access point.

- Firmware & interface specification: [`docs/init.md`](docs/init.md)
- Hardware pin reference: [`docs/hardware.md`](docs/hardware.md)
- Web UI development & porting guide: [`docs/web-ui.md`](docs/web-ui.md)
- Brand assets: [`brand/`](brand/) (vector versions in [`brand/svg/`](brand/svg/))

## Quick start (UI, no hardware needed)

Requires Node 18+. There are no dependencies to install.

```bash
npm run dev          # UI + mock device at http://127.0.0.1:8080/
                     # fault scenarios at  http://127.0.0.1:8080/__dev
npm run build:web    # gzip + generate web_assets.h for firmware embedding
```

## Repository layout

```text
web/        configuration UI (static HTML/CSS/JS, served by the device)
dev/        mock device API + dev server for building the UI without hardware
tools/      build tooling (web asset packaging for firmware)
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

### Phase 2: Hardware abstraction and helpers

- [ ] RTC, UTC time handling
- [ ] SD card
- [ ] Audio output: DAC power, PCM5102A, amplifier control
- [ ] Battery measurement
- [ ] Temperature/humidity, ambient light
- [ ] LEDs, buttons
- [ ] Wi-Fi AP
- [ ] Deep sleep, wake sources
- [ ] OTA partition layout and update

### Phase 3: Core application

- [ ] `DeviceConfig`, `DeviceState`, `ProgramStore`, `ProgramValidator`
- [ ] `Scheduler` (host unit tests per `init.md` §67)
- [ ] `PlaybackService`, `PlaybackLogger`, `EnvironmentService`, `EnvironmentLogger`
- [ ] `BatteryService`, `TimeService`, `SleepManager`
- [ ] REST/API layer matching `docs/web-ui.md` §4

### Phase 4: Integration

- [ ] Embed UI in firmware; connect UI → API → services → hardware
- [ ] Decide whether status polling counts as AP activity (see `docs/web-ui.md` §4)

### Open items carried from the spec (not decided)

LED_G "broadcasting" meaning · Hall/enclosure → config-mode behavior · TRIG_IN · extra button
behaviors · final audio calibration (volume units vs. SPL).
