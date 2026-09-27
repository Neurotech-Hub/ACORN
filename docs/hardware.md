# ACORN Hardware Reference

**Target:** ESP32-S3-WROOM-1-N16R2  
**Framework:** Arduino / ESP32  
**Purpose:** Minimal hardware/pin reference for firmware development.

The schematic/netlist is authoritative for electrical details. Application code should access hardware through helper/service functions rather than manipulating GPIOs throughout the codebase.

## Pin Map

| GPIO | Signal | Purpose | Notes |
|---:|---|---|---|
| 1 | `VBAT_ADC` | Battery voltage ADC | 1M/249k divider, 100nF filter |
| 4 | `I2C_SDA` | I2C data | RTC + environmental sensors |
| 5 | `I2C_SCL` | I2C clock | RTC + environmental sensors |
| 6 | `RTC_MFP` | RTC alarm/wake | Active LOW, 100k pull-up |
| 7 | `BTN_01` | Audio test button | Active LOW; wake source |
| 8 | `BTN_02` | Battery status button | Active LOW; wake source |
| 9 | `TRIG_IN` | External trigger | Active LOW, 100k pull-up; behavior TBD |
| 10 | `SD_CS` | microSD chip select | HIGH when inactive |
| 11 | `MAG_OUT` | Hall sensor | DRV5032FA push-pull; wake source |
| 12 | `SD_DETECT` | SD card detect | Input |
| 13 | `AUX_IO` | Auxiliary I/O | Reserved |
| 14 | `SD_CLK` | microSD SPI clock | 33R series; 100k pulldown |
| 15 | `SD_MOSI` | microSD MOSI/CMD | 100k pull-up |
| 16 | `SD_MISO` | microSD MISO/DAT0 | 100k pull-up |
| 17 | `3V3_SW_EN` | DAC/audio 3.3V power | Hide electrical polarity behind helper |
| 18 | `DAC_XSMT` | PCM5102A mute | LOW = mute; 100k pulldown |
| 35 | `I2S_BCK` | I2S bit clock | PCM5102A |
| 36 | `I2S_LRCK` | I2S LR/word clock | PCM5102A |
| 37 | `I2S_DOUT` | I2S audio data | PCM5102A |
| 38 | `AMP_SHDN` | Amplifier shutdown | Hide polarity behind helper |
| 39 | `AMP_MUTE` | Amplifier mute/control | Hide polarity behind helper |
| 48 | `LED_G` | Green LED | OFF during sleep |

`LED_R` is used for battery indication; use its assignment from the final netlist rather than assuming a GPIO here.

Avoid repurposing ESP32-S3 strapping pins GPIO3, GPIO45, and GPIO46.

## I2C Hardware

Shared bus:

```text
GPIO4 = SDA
GPIO5 = SCL