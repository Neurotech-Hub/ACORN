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

## Site location in logs (done)

The current site location lives on the SD card in `/config/site.json` (prestage it by copying the
file onto the card, or set it from **Device → Site location** with phone GPS or manual entry).
Every new `playback.csv` and `environment.csv` row ends with `latitude,longitude` (6 decimals,
blank when unset); a missing site never blocks operation. Logs written by older firmware are kept
as `*.csv.bak` when the header changes. Details: `docs/init.md` §§6, 35–36 and the `/site` API in
`docs/web-ui.md` §4.
