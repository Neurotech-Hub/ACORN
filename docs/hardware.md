# ACORN Hardware Reference

**Target:** ESP32-S3-WROOM-1-N16R2 (16 MB flash, 2 MB PSRAM)  
**Framework:** Arduino / ESP32 (arduino-esp32 core 3.x)  
**Purpose:** Pin, polarity and part reference for firmware development.

The schematic/netlist is authoritative for electrical details. Application code accesses hardware only through the `AcornHardware` library (`arduino/libraries/AcornHardware`); the single source of pin numbers and polarities in code is `src/Pins.h`.

All pull-ups/pull-downs are in hardware. Firmware leaves internal pulls disabled, including during deep sleep.

## Pin Map

| GPIO | Signal | Purpose | Polarity / notes |
|---:|---|---|---|
| 1 | `VBAT_ADC` | Battery voltage ADC | 1M (top) / 249k (bottom) divider, 100nF filter; Vbat = Vadc × 1249/249 |
| 2 | `LED_R` | Red LED (D3, D4) | Battery indication (BTN_02). OFF during sleep |
| 4 | `I2C_SDA` | I2C data | RTC + environmental sensors |
| 5 | `I2C_SCL` | I2C clock | RTC + environmental sensors |
| 6 | `RTC_MFP` | MCP7940N alarm | **Active LOW** (open drain, 100k pull-up); deep-sleep wake |
| 7 | `BTN_01` | Audio test button | **LOW = pressed**; deep-sleep wake |
| 8 | `BTN_02` | Battery status button | **LOW = pressed**; deep-sleep wake |
| 9 | `TRIG_IN` | External trigger | Active LOW, 100k pull-up; unused in v1 |
| 10 | `SD_CS` | microSD chip select | HIGH when inactive (held HIGH in sleep) |
| 11 | `MAG_OUT` | Hall sensor (DRV5032FA) | Push-pull. **HIGH = enclosure door open**; deep-sleep wake. Door open starts the configuration AP (see below) |
| 12 | `SD_DETECT` | SD card detect | **LOW = card inserted** (hardware pull-up) |
| 13 | `AUX_IO` | Auxiliary I/O | Reserved |
| 14 | `SD_CLK` | microSD SPI clock | 33R series; 100k pulldown |
| 15 | `SD_MOSI` | microSD MOSI/CMD | 100k pull-up |
| 16 | `SD_MISO` | microSD MISO/DAT0 | 100k pull-up |
| 17 | `3V3_SW_EN` | Switched 3.3 V (DAC/audio) | **LOW = rail on**. AO3401A P-FET, gate through 1k, 100k pulldown: gate LOW turns the rail on, so the pulldown leaves it on until firmware drives HIGH. Sleep holds this pin HIGH |
| 18 | `DAC_XSMT` | PCM5102A soft mute | **LOW = mute**; 100k pulldown |
| 35 | `I2S_BCK` | I2S bit clock | PCM5102A (no MCLK; DAC PLL from BCK) |
| 36 | `I2S_LRCK` | I2S word clock | PCM5102A |
| 37 | `I2S_DOUT` | I2S data | PCM5102A |
| 38 | `AMP_SHDN` | TPA3138D2 `SD/FAULT` (pin 2) via 5.1k series | **HIGH = enabled, LOW = shutdown (outputs Hi-Z)**. 100k pulldown on SD/FAULT: GPIO high-Z = amp off (boot, reset, sleep). Firmware: OFF = `INPUT`, ON = `OUTPUT` HIGH. The amp also pulls SD/FAULT LOW on OT/OC/DC fault |
| 48 | `LED_G` | Green LED (D2) | ON exactly while the Wi-Fi AP is running (“broadcasting”); OFF during sleep |

`AMP_MUTE` (formerly GPIO39) is **not populated**: muting is done with `DAC_XSMT` and `AMP_SHDN`.

Avoid repurposing ESP32-S3 strapping pins GPIO0, GPIO3, GPIO45 and GPIO46. Firmware assumes USB-C (J2) is wired to the native USB pins (GPIO19/20) for programming and serial (USB CDC on boot).

## Resolved behavior

These were open items in `docs/init.md` section 69:

- **LED_G:** "broadcasting" means the Wi-Fi configuration AP is running. LED_G is on exactly while the AP is up and always off in sleep. Only `WifiService` drives it.
- **Hall / door:** opening the door (MAG_OUT HIGH) wakes the device and starts the configuration AP. Closing the door or 10 minutes without client activity ends the session. See "Configuration mode" below.
- **TRIG_IN:** unused in v1.
- **Audio SPL calibration:** deferred until the prototype arrives. Volume stays in 0–100 UI units.

## I2C devices

Shared bus: GPIO4 = SDA, GPIO5 = SCL.

| Ref | Part | 7-bit address | Notes |
|---|---|---|---|
| U5 | MCP7940N RTC | `0x6F` (SRAM/RTCC) | 32.768 kHz crystal (Y2), BR2032 backup (BT1). MFP → GPIO6. Runs in UTC |
| U8 | SHT40-AD1F temperature/humidity | `0x44` | High-precision single shot (`0xFD`), CRC-8 |
| IC1 | VEML6031X00 ambient light | `0x29` | 16-bit ALS; auto-ranged in firmware up to ~172 klx |
| X3, X4 | STEMMA QT / Qwiic (JST-SH 4-pin) | — | Expansion on the same bus |

## Audio chain

```text
ESP32 I2S ──► PCM5102A DAC (U4) ──► TPA3138D2 class-D amp (U7) ──► speaker
              power: 3V3_SW_EN      shutdown: AMP_SHDN (SD/FAULT)
              mute:  DAC_XSMT
```

Power-up order: `3V3_SW_EN` LOW → settle → start I2S clocks (silence) → `DAC_XSMT` HIGH → `AMP_SHDN` HIGH (driven).  
Power-down order: `AMP_SHDN` high-Z (pulldown → LOW) → `DAC_XSMT` LOW → stop I2S → `3V3_SW_EN` HIGH.

```text
GPIO38 ──5.1k──┬── SD/FAULT (TPA3138D2 pin 2)
               └──100k── GND
```

TPA3138D2 notes for audio development ([datasheet](https://www.ti.com/lit/ds/symlink/tpa3138d2.pdf)): SD/FAULT must not float; hold it LOW until the inputs are stable (avoids nuisance DC-detect faults and pops); enter shutdown before removing supply. The 100k pulldown gives exactly this whenever GPIO38 is high-Z (boot, reset, deep sleep). SD/FAULT is also a fault output: the amp pulls it LOW while faulted and auto-recovers. The 5.1k series resistor limits the contention to about 0.65 mA when that happens while GPIO38 drives HIGH. Because GPIO38 reads back its own drive, firmware cannot detect amp faults (`AudioHardware::ampFaulted()` is always false). Gain is set in hardware via GAIN/SLV.

## Deep-sleep wake sources

Wake inputs have **mixed polarity**, so they need two different ESP32-S3 wake mechanisms:

| Source | GPIO | Asserted level | Wake mechanism |
|---|---:|---|---|
| Enclosure door (Hall) | 11 | HIGH | `EXT0`, level 1 |
| Door closed (door left open at sleep) | 11 | LOW | `EXT0`, level 0 (instead of the row above) |
| BTN_01 | 7 | LOW | `EXT1`, `ANY_LOW` |
| BTN_02 | 8 | LOW | `EXT1`, `ANY_LOW` |
| RTC alarm (MFP) | 6 | LOW | `EXT1`, `ANY_LOW` |
| Timer (fallback) | — | — | RTC timer |

Level-triggered sources re-wake immediately if they are already asserted at sleep time (door left open, button held, RTC alarm flag not cleared). `SleepHardware` therefore skips arming any source that is currently asserted and reports what it skipped; the RTC alarm flag must be cleared before sleeping. The door is the exception: with `SleepConfig::hallWakeOnClose` (always set by the firmware), an open door arms EXT0 at level 0, so the device wakes when the door closes and then re-arms for door open. No timer polling is needed.

A Hall wake reports `HALL` for either edge; firmware reads MAG_OUT at boot to tell door-open from door-close.

## Configuration mode

- **Start:** a door-open wake, a reset with the door open, or a door-open edge while awake starts the configuration session: open AP `ACORN-xxxxxx`, captive-portal DNS, embedded web UI and `/api/v1` at `http://192.168.4.1/`. LED_G is on for the whole session.
- **During:** the device stays awake. Scheduled playback and environmental samples keep running.
- **End:** the door closes, or 10 minutes pass without API activity. `GET /status` polling and a connected phone do not count as activity; any other request does, including `POST /session/extend` ("Stay connected"). The AP, DNS and web server stop, LED_G turns off, and the device returns to event-driven sleep.
- **Door left open:** if the session timed out with the door still open, the device sleeps with the door-close wake armed. Button and RTC wakes do not restart the AP; the door must be closed and opened again.
- **Bench mode:** building with `ACORN_STAY_AWAKE=1` keeps a permanent session (never times out, never sleeps).

All GPIO outputs that must stay in a defined state during sleep (LEDs OFF, `DAC_XSMT` LOW, `3V3_SW_EN` HIGH, `SD_CS` HIGH) are latched with GPIO hold before `esp_deep_sleep_start()` and released on the next boot. `3V3_SW_EN` is held HIGH because its gate pulldown would otherwise turn the rail on. `AMP_SHDN` is left high-Z instead: the 100k pulldown on SD/FAULT keeps the amplifier shut down.
