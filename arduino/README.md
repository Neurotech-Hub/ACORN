# ACORN Firmware (Arduino)

```text
arduino/
  acorn/                     firmware sketch + partitions.csv
    src/                     application services (see "Firmware layout")
  libraries/AcornCore/       pure C++17 logic: scheduler, validator, JSON, time, CSV, battery model
  libraries/AcornHardware/   hardware abstraction layer (HAL)
  libraries/ArduinoJson/     vendored ArduinoJson 7.4.2 (MIT)
  host-tests/                AcornCore unit tests + golden previews, built with the host compiler
  tests/test_*/              isolated per-component test sketches
  compile-tests.sh           host tests, regenerate the embedded UI, compile everything with arduino-cli
```

Target: **ESP32-S3-WROOM-1-N16R2** on the arduino-esp32 core **3.x** (tested with 3.3.11). The only third-party library is the vendored ArduinoJson. Everything else uses core libraries (`Wire`, `SPI`, `SD`, `ESP_I2S`, `WiFi`, `WebServer`, `Update`, `Preferences`) and first-party drivers for the MCP7940N, SHT40 and VEML6031X00.

Layering: `acorn/src` services use `AcornHardware` for hardware and `AcornCore` for logic. API handlers call services only. `AcornCore` has no `Arduino.h` and is the single implementation of the scheduler, the program validator and UTC conversion, shared with the host tests. Algorithms are described in [`docs/scheduling.md`](../docs/scheduling.md).

Pin numbers and polarities live only in `libraries/AcornHardware/src/Pins.h`; see [`docs/hardware.md`](../docs/hardware.md).

## Board settings

| Setting | Value |
|---|---|
| Board | ESP32S3 Dev Module |
| USB CDC On Boot | Enabled |
| Flash Size | 16MB |
| PSRAM | QSPI PSRAM |
| Partition Scheme | leave default; `acorn/partitions.csv` is picked up automatically |

FQBN: `esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=16M,PSRAM=enabled`

## Arduino IDE

Set **File → Preferences → Sketchbook location** to this `arduino/` folder so `libraries/AcornHardware` is found, then open any sketch in `acorn/` or `tests/`. Serial Monitor: 115200 baud.

## arduino-cli

```bash
./arduino/compile-tests.sh                 # host tests, firmware + all test sketches
./arduino/compile-tests.sh test_hardware   # host tests + one sketch
ACORN_SKIP_HOST=1 ./arduino/compile-tests.sh acorn   # skip host tests

FQBN=esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=16M,PSRAM=enabled
arduino-cli compile --fqbn $FQBN --libraries arduino/libraries \
  --upload -p /dev/cu.usbmodem* arduino/tests/test_hardware
arduino-cli monitor -p /dev/cu.usbmodem* -c baudrate=115200
```

The default partition scheme's size limit (1.3 MB) does not apply to sketches with `partitions.csv`; `compile-tests.sh` passes the real 6.25 MB slot size for those. When compiling `acorn` by hand, add `--build-property upload.maximum_size=6553600`.

## Isolated tests

Sketches print `[PASS]` / `[FAIL]` / `[INFO]` lines. On a cold boot, `test_hardware` ends its automated checks with `==== RESULT: PASS|FAIL ====`. It has passed on the prototype: LEDs, buttons, Hall, battery, RTC, SHT40, light, SD, and the 5 s RTC wake. Run them in this order on a new board:

| Sketch | Checks | Operator action | Pass |
|---|---|---|---|
| `test_hardware` | LEDs, buttons, Hall edges, VBAT, RTC (UTC, tick, MFP alarm), SHT40, VEML6031 lux, SD mount/round-trip/WAV headers, 5 s RTC deep-sleep wake (Hall line unarmed) | Watch each LED; open/close the door; `cal <volts>`; `set YYYY-MM-DDTHH:MM:SSZ`; cover the light sensor; insert the card; BTN_01 toggles both LEDs; BTN_02 press then release sleeps 5 s on the RTC alarm only | 10/19/2026 |
| `test_i2c_scan` | 0x6F RTC, 0x44 SHT40, 0x29 VEML6031 | — | 10/19/2026 |
| `test_audio` | volume curve, power sequence, tones, WAV from SD | Listen for tone and pops; `vol`, `tone <hz>`, `wav <id>` | 10/19/2026 |
| `test_wifi_ap` | `ACORN-xxxxxx` open AP, client fetch, idle timeout (2 min in test) | Join AP, open `http://192.168.4.1/` | 10/19/2026 |
| `test_ota` | partition layout, upload + verify + reboot into other slot | Upload a `.bin` at `http://192.168.4.1/` | — |

Sensors that fail report *invalid* (never zeros), matching the spec's `SENSOR_READ_FAILED` / `BATTERY_READING_INVALID` codes.

### Bring-up notes

- **LED polarity** (confirmed by `test_hardware`): `level::LED_ON = HIGH` in `Pins.h` (GPIO → resistor → LED → GND).
- **AMP_SHDN drive** (confirmed hardware: 5.1k series, 100k pulldown on SD/FAULT): OFF = `INPUT` (the pulldown shuts the amp down, including during boot and sleep), ON = `OUTPUT` HIGH. Amp faults cannot be read back through GPIO38, so `test_audio` relies on listening.
- **Volume curve**: quadratic 0–100 → gain; SPL calibration is deferred until the prototype arrives.

## Host tests (no hardware)

```bash
./arduino/host-tests/run.sh                  # build + run AcornCore tests (c++ -std=c++17)
./arduino/host-tests/run.sh --write-golden   # regenerate host-tests/golden/*.json
node tools/check-mock-golden.mjs             # the dev mock must match the same golden files
```

`test_core.cpp` covers the `init.md` §67 vectors (fixed and random recurrence, cross-midnight, missed events, overlaps, WAV-duration guards, missing audio, UTC offset and RTC changes), CSV formatting, battery flash counts, env alignment, the wake planner and JSON round trips. Only regenerate the golden files for an intentional algorithm change; the mock must then be updated to match.

## Firmware layout (`acorn/src`)

| Folder | Contents |
|---|---|
| `AppConfig.*` | Firmware version, `ACORN_STAY_AWAKE`, SD paths, upload/body limits, AP idle timeout |
| `config/` | `DeviceConfig`: NVS (`Preferences`) settings: name, UTC offset, battery model/calibration, env enable/interval, active program |
| `services/` | `TimeService` (RTC + `esp_timer`), `BatteryService`, `EnvironmentService`, `LedService`, `WifiService` (only AP start/stop path; drives LED_G), `FirmwareService` (OTA) |
| `program/` | `AudioLibrary` (WAV index, uploads), `ProgramStore` (atomic save), `ActiveProgram` (§49 activation, scheduler) |
| `audio/` | `PlaybackService`: FreeRTOS playback task (file or tone) |
| `logging/` | `PlaybackLogger`, `EnvironmentLogger` on top of `CsvFile` |
| `state/` | `DeviceState` (SD monitoring, faults, armed, `/status` model), `JsonUtil` |
| `power/` | `SleepManager`: §42 sleep preparation with `WakePlanner`, RTC alarm 2 s early, door-close wake |
| `api/` | `ApiServer`: every `/api/v1` route of the dev mock on the core `WebServer`; `WebUi`: embedded UI + captive portal |
| `services/ConfigSession` | Door-driven configuration session: AP + DNS + web server start/stop, idle timeout |
| `web/web_assets.h` | Generated gzipped UI (`npm run build:web:firmware`; committed) |
| `Runtime.*` | Boot self-check, SERVICE / CONFIG loop, sleep decision |

### Runtime behavior

Default build (`ACORN_STAY_AWAKE 0`): event-driven (init.md sections 40, 56).

- **Wake → SERVICE.** Runs whatever is due, then sleeps once nothing is pending:
  - scheduled playback at its scheduled second, within ±10 s (no catch-up);
  - environmental samples on UTC-aligned boundaries (deferred while audio plays);
  - BTN_01: play the next event's WAV, else `/audio/playback.wav`, else a 1 kHz tone (ignored while playing);
  - BTN_02: battery flashes on LED_R.
  The device stays awake while audio plays, LEDs flash, or the next wake is under 3 s away. Wi-Fi is never started on these wakes.
- **Door → CONFIG.** A door-open wake, a door-open edge while awake, or a reset with the door open starts a configuration session: AP `ACORN-xxxxxx`, captive portal, web UI at `http://192.168.4.1/`, LED_G on. Scheduled playback and sampling continue. The session ends on door close or after 10 minutes without API activity. A door left open is armed to wake the device when it closes (see [`docs/hardware.md`](../docs/hardware.md)).
- **Console access.** After a reset the device stays awake for 30 s, and every serial keystroke extends this by 30 s, so you can type commands before it sleeps. On deep-sleep wakes it does not wait for a USB host.

Bench build: `ACORN_STAY_AWAKE=1` keeps a permanent session (AP, UI and API always up; never sleeps unless you type `sleep`). Set it in `AppConfig.h`, or with arduino-cli:

```bash
arduino-cli compile --fqbn $FQBN --libraries arduino/libraries --build-property upload.maximum_size=6553600 \
  --build-property "compiler.cpp.extra_flags=-DACORN_STAY_AWAKE=1" arduino/acorn
```

Serial commands (115200 baud, newline-terminated):

| Command | Action |
|---|---|
| `status` | Pretty-printed `GET /api/v1/status` JSON |
| `faults` | Blocking and non-blocking faults |
| `wake` | Next planned wake time and reason |
| `sleep` | End any session and enter deep sleep now (refused if the next event is under 3 s away; restarts if sleep fails) |
| `session` | Start a configuration session, or end the current one |
| `door` | Door state and which Hall wake (open or close) sleep would arm |
| `btn1` / `btn2` | Simulate a button press |

### Hardware acceptance (init.md section 68)

Run on the prototype with the default build, a charged battery, an SD card holding `/audio/playback.wav` (16-bit mono, 44.1 or 48 kHz), and the serial monitor open. Record each result in the Phase 4 checklist in the root `README.md`.

1. **RTC in UTC.** Open the door, join `ACORN-xxxxxx` (the UI should open by itself), then use Device → Sync clock. After a reset, `status` shows the same UTC time and `rtc_valid: true`.
2. **SD mounts.** The boot self-check prints `sd present=1 mounted=1`, and there are no SD faults.
3. **WAV validated and played.** Audio lists `playback.wav` as valid; Test playback plays it.
4. **Program 06:00–18:00 every 10 min.** Create it in the editor (range, fixed 600 s, `playback.wav`), then Save & Apply and activate it.
5. **Exactly 06:00 … 17:50.** The editor preview shows 72 events per day, the first at 06:00:00 and the last at 17:50:00, with none at 18:00.
6. **Wakes only as needed.** Close the door. The serial log shows `sleep: next wake …` with the next event or env boundary, and the device wakes about 2 s before it.
7. **Playback logged.** After a day, `/logs/playback.csv` has 72 `scheduled` rows with `result=ok`. `actual_utc` should equal `scheduled_utc`, or be at most a second later.
8. **Env every 10 min.** `/logs/environment.csv` has one row per :00/:10/:20… UTC boundary and no gaps (samples during playback may be deferred but keep their boundary timestamp).
9. **Returns to sleep.** Measure the battery current while asleep (LED_G and LED_R off, audio rail off) and record it. Confirm the device sleeps again after each scheduled wake.
10. **UI reports next playback.** Status shows the next playback and matches `wake`.
11. **Edit and validate.** Create an overlapping event and confirm the editor shows the validation error and blocks saving. Then fix it and save.
12. **BTN_01 hierarchy.** With the program active, BTN_01 plays the next event's WAV. With no program, it plays `playback.wav`. With `playback.wav` removed, it plays the tone. Repeat from sleep (the button wakes the device).
13. **BTN_02 battery.** Flash count equals `ceil(percent / 10)`. Disconnect the battery sense (or use an invalid reading) to see the single long flash. Repeat from sleep.
14. **Missing SD blocks cleanly.** Remove the card: status shows `SD_NOT_PRESENT` (blocking), no scheduled playback runs, the device still sleeps and wakes, and the UI, tone test and OTA still work.
15. **Browser OTA.** Upload a new `acorn.ino.bin` from Device → Firmware. The device reboots into the other slot and the UI reconnects with the new version.
16. **Hall config mode.**
    - Open the door: the AP appears and LED_G turns on.
    - Close it: the AP stops, LED_G turns off, and the device sleeps.
    - Leave it open for 10 minutes without using the UI: the session times out, the log shows `door open: wake on close`, and the device sleeps.
    - Close the door: the log shows `door closed while asleep`, and the device goes back to sleep.
    - Open it again: a new session starts.

### API contract

```bash
node tools/api-contract.mjs --base http://127.0.0.1:8080   # mock (npm run dev)
node tools/api-contract.mjs --base http://192.168.4.1      # device
node tools/api-contract.mjs --base http://192.168.4.1 --readonly
```

The contract test checks routes, envelopes, status codes and error codes, and compares the device's schedule preview and revision with the mock scheduler. It also checks the embedded UI (gzip, ETag, 304) and, against `192.168.4.1`, the captive-portal redirect. It creates and removes `contract-tmp` (program) and `contract-tmp.wav`; `--readonly` skips anything that writes. Run it during a configuration session (door open) or with a bench build.

## Web UI

The UI from `web/` is embedded in the firmware (`acorn/src/web/web_assets.h`, about 44 KB gzipped). After changing `web/`, run `npm run build:web:firmware` (or `compile-tests.sh`, which regenerates it) and reflash or OTA. To iterate on the UI against a real device without reflashing, run `node dev/server.mjs --proxy http://192.168.4.1`. See [`docs/web-ui.md`](../docs/web-ui.md).

## Partition table

`acorn/partitions.csv` (16 MB): `nvs` 20 KB, `otadata`, `app0`/`app1` 6.25 MB each (two-slot OTA), `spiffs` 3.4 MB (unused, reserved), `coredump` 64 KB. `tests/test_ota/partitions.csv` must stay identical (`compile-tests.sh` checks).
