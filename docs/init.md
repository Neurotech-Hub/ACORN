# ACORN Firmware & Configuration Interface Specification

**Project:** ACORN — Autonomous Configurable Outdoor Replay Node  
**Document:** Firmware Architecture and Configuration Interface Specification  
**Target:** ESP32-S3 / Arduino framework  
**Status:** Initial implementation specification  
**Version:** 0.1

---

## 1. Purpose

This document defines the initial firmware architecture, scheduling model, storage model, logging behavior, device API, and browser-based configuration interface for ACORN.

ACORN is an autonomous outdoor audio playback device intended primarily for wildlife research and related field applications.

The initial client use case is:

> During a 12-hour active period each day, play a single audio recording every 10 minutes. Remain inactive for the other 12 hours.

The firmware must support this use case while providing a sufficiently general scheduling architecture for other daily playback experiments.

This specification intentionally separates:

1. UI structure
2. Hardware abstraction
3. Core scheduler/logger/application architecture
4. UI/API integration

The web interface must be treated as a **client of the firmware API**, not as the firmware's internal control architecture.

Future clients may include:

- replacement web interfaces
- serial commands
- automated test tools
- remote/network interfaces
- other control software

Core device functionality must therefore live below the API/UI layer.

---

# 2. Out of Scope

The following are explicitly outside this specification:

- visual branding
- colors
- typography
- logo usage
- detailed responsive layout
- final enclosure interaction/state machine
- cloud services
- remote fleet management
- user accounts
- internet connectivity
- automatic daylight-saving-time handling
- weekly/7-day scheduling
- sunrise/sunset scheduling
- playlists
- simultaneous audio playback
- TRIG_IN behavior
- final Hall/lid behavior
- final button behavior beyond the initial definitions below

A separate brand document will be supplied to the UI implementation agent.

A complete hardware netlist will also be supplied. **Do not infer hardware pins from this document except where explicitly specified.**

---

# 3. Design Principles

## 3.1 Event-driven, low-power operation

The ESP32-S3 should spend the majority of deployed time in deep sleep.

The firmware should:

1. determine the next required event,
2. configure the appropriate wake source,
3. enter deep sleep,
4. wake,
5. perform the event,
6. log the result,
7. determine the next event,
8. return to sleep.

Do not implement deployed operation as a continuously running timer loop.

---

## 3.2 UTC internally

The RTC shall operate in UTC.

All persistent log timestamps shall use UTC.

Recommended representation:

```text
2026-09-27T18:10:00Z
```

Daily program schedules are expressed in **local wall-clock time** because this is the natural way for users to describe wildlife playback schedules.

The device shall store a fixed UTC offset:

```text
utc_offset_minutes
```

Example:

```text
UTC-5 -> -300
UTC+9:30 -> +570
```

No automatic DST logic is required in v1.

---

## 3.3 SD card contains experimental artifacts

Anything related to an experiment or subsequent analysis should live on the SD card.

Anything intrinsic to the physical device should live in NVS.

### NVS examples

- device name
- device ID-related settings
- UTC offset
- battery calibration/model
- sensor calibration
- device preferences
- active program ID
- hardware calibration
- configuration schema version

### SD examples

- WAV files
- program JSON files
- playback logs
- environmental logs

---

## 3.4 API-first architecture

The browser UI must not directly control hardware.

For example:

```text
BAD

Web page
   -> GPIO
   -> DAC
   -> RTC
```

Use:

```text
Web UI
   |
   v
Device API
   |
   v
Application Services
   |
   v
Hardware Abstraction
   |
   v
Hardware
```

The same application services should eventually be callable from other interfaces.

---

# 4. Implementation Phases

Development should proceed in four phases.

---

## Phase 1 — UI Skeleton

Build the configuration interface as a static/mock frontend.

Use mocked API data.

Establish:

- information hierarchy
- navigation
- program editor workflow
- audio workflow
- schedule editor
- validation presentation
- status dashboard
- log access
- settings
- OTA workflow

Do not couple Phase 1 to actual hardware.

The separate ACORN brand document should drive visual design.

---

## Phase 2 — Hardware Abstraction and Helpers

Using the supplied hardware netlist, implement and test:

- RTC
- UTC time handling
- SD card
- audio output
- DAC power control
- amplifier control
- battery measurement
- temperature/humidity
- ambient light
- LEDs
- buttons
- Wi-Fi AP
- deep sleep
- wake sources
- OTA

Pin mappings must come from the supplied netlist.

Do not guess pins that are not explicitly specified.

---

## Phase 3 — Core Application

Implement:

- `DeviceConfig`
- `DeviceState`
- `ProgramStore`
- `ProgramValidator`
- `Scheduler`
- `PlaybackService`
- `PlaybackLogger`
- `EnvironmentService`
- `EnvironmentLogger`
- `BatteryService`
- `TimeService`
- `SleepManager`
- REST/API layer

Scheduler logic should be deterministic and unit-testable independently of hardware.

---

## Phase 4 — Integration

Connect:

```text
Web UI
   ↓
API
   ↓
Services
   ↓
Hardware
```

Complete:

- program editing
- program validation
- schedule preview
- audio upload/selection
- audio testing
- volume testing
- device status
- environmental settings
- logs
- time configuration
- OTA
- sleep/wake integration

---

# 5. Proposed Firmware Modules

Suggested architecture:

```text
/src
    /api
        ApiServer
        ApiModels

    /audio
        PlaybackService
        WavReader
        AudioHardware

    /config
        DeviceConfig
        NvsStore

    /hardware
        BatteryHardware
        ButtonHardware
        LedHardware
        RtcHardware
        SensorHardware
        SdHardware
        AudioHardware

    /logging
        PlaybackLogger
        EnvironmentLogger

    /program
        Program
        ProgramStore
        ProgramValidator

    /scheduler
        Scheduler
        ScheduleGenerator

    /services
        BatteryService
        EnvironmentService
        TimeService
        DeviceStateService

    /power
        SleepManager
        WakeManager

    /web
        static UI assets

    /ota
        OtaService
```

Exact file organization is flexible. Separation of responsibilities is not.

---

# 6. SD Card Structure

Initial structure:

```text
/
├── audio/
│   ├── playback.wav
│   ├── squirrel.wav
│   └── hawk.wav
│
├── programs/
│   ├── default.json
│   ├── squirrel-study.json
│   └── ...
│
└── logs/
    ├── playback.csv
    └── environment.csv
```

No first-class "deployment" directory or deployment workflow is required in v1.

A common interaction with ACORN will simply be:

```text
open device
-> connect
-> inspect status
-> perhaps test audio
-> disconnect
-> device resumes operation
```

Entering configuration mode must therefore **not** imply starting or ending an experiment.

---

# 7. SD Card Requirement

The SD card is required for normal deployed operation.

If any of the following occur:

- SD absent
- SD cannot mount
- SD corrupt
- SD full
- active program unavailable
- active program invalid
- required WAV unavailable

the device shall enter a **FAULT / NOT ARMED** condition.

In this condition the device may still provide:

- configuration UI
- status
- diagnostics
- battery measurement
- environmental readings
- OTA
- built-in audio hardware test
- other functions not dependent on SD

It shall **not perform normal scheduled playback**.

The UI/API must clearly expose the blocking fault.

---

# 8. Program Model

A Program represents a complete daily playback schedule.

Multiple Program JSON files may exist on the SD card.

Exactly **one Program is active at a time** in v1.

The selected active program ID is device state and may therefore be stored in NVS.

The Program itself remains on SD.

---

## 8.1 Program metadata

Recommended fields:

```json
{
  "schema_version": 1,
  "id": "squirrel-study",
  "name": "Squirrel Study",
  "description": "Playback treatment",
  "modified_utc": "2026-09-27T18:00:00Z",
  "events": []
}
```

Each event shall have a stable `event_id`.

Example:

```json
{
  "event_id": "morning-playback"
}
```

The firmware should calculate a **program revision/content hash** from the relevant canonical Program contents.

This revision should be written into playback logs.

Purpose:

> A researcher should later be able to identify exactly which Program definition generated a logged playback.

The hash should not depend on the `revision` field itself.

---

# 9. Playback Event Types

v1 supports two playback event types:

1. `point`
2. `range`

Each event is associated with exactly **one audio file**.

No playlist semantics are required in v1.

---

# 10. POINT Events

A POINT event means:

> Play this sound at this wall-clock time every day.

It does **not** mean:

> Play once on a particular calendar date.

Example:

```json
{
  "event_id": "daily-noon-call",
  "type": "point",
  "time": "12:00:00",
  "audio": "/audio/playback.wav",
  "volume": 80
}
```

This generates:

```text
12:00:00 every local day
```

---

# 11. RANGE Events

A RANGE defines:

- local start time
- local end time
- audio file
- volume
- recurrence rule

Example:

```json
{
  "event_id": "daytime-calls",
  "type": "range",
  "start": "06:00:00",
  "end": "18:00:00",
  "audio": "/audio/playback.wav",
  "volume": 80,
  "recurrence": {
    "type": "fixed",
    "interval_seconds": 600
  }
}
```

---

# 12. Range Boundary Semantics

Ranges use:

```text
[start, end)
```

Start is inclusive.

End is exclusive.

Therefore:

```text
06:00–18:00
every 10 minutes
```

generates:

```text
06:00
06:10
06:20
...
17:40
17:50
```

It does **not** generate an 18:00 playback.

---

# 13. Fixed Recurrence

Fixed recurrence is anchored to the start of the range.

Do not schedule based on the completion time of the previous playback.

Example:

```text
Range:
06:00–18:00

Interval:
10 minutes
```

means:

```text
06:00
06:10
06:20
06:30
...
```

regardless of WAV duration.

This prevents schedule drift.

---

# 14. Random Recurrence

Random recurrence specifies:

```json
{
  "type": "random",
  "min_interval_seconds": 480,
  "max_interval_seconds": 720
}
```

For:

```text
06:00–18:00
random 8–12 minutes
```

the first playback occurs exactly at:

```text
06:00
```

Subsequent intervals are randomized between 8 and 12 minutes.

No generated event may occur at or after the range end.

---

# 15. Deterministic Random Scheduling

Random schedules should remain stable across:

- deep sleep
- reboot
- battery replacement
- configuration check-in

A reboot must not silently redraw an experiment's random schedule.

Recommended strategy:

Generate the random sequence deterministically from:

```text
program revision
+ event ID
+ local date associated with the range start
```

For example:

```text
seed = HASH(program_revision + event_id + range_start_local_date)
```

A deterministic PRNG then generates the interval sequence.

Benefits:

- reboot does not change the day's schedule
- no large schedule needs to be stored
- schedule preview matches actual playback
- schedule can potentially be reconstructed during analysis

For a range crossing midnight, use the **local calendar date on which the range started** as the schedule-instance date.

Example:

```text
20:00 Sep 27 → 04:00 Sep 28
```

belongs to the:

```text
2026-09-27 range instance
```

---

# 16. Cross-Midnight Ranges

Ranges may cross midnight.

Example:

```json
{
  "start": "20:00:00",
  "end": "04:00:00"
}
```

means:

```text
20:00 today through 03:59:59 tomorrow
```

The scheduler and validator must normalize these correctly.

---

# 17. Overlap Rules

v1 should favor simple, conservative validation.

### RANGE vs RANGE

Two RANGE windows may not overlap.

Reject the Program.

This applies even if their generated playback timestamps would theoretically not collide.

### POINT vs POINT

POINT events must not create overlapping audio.

### POINT vs RANGE

The validator must prevent possible playback collisions.

For v1, it is acceptable to conservatively reject POINT events that fall within a RANGE window rather than attempting to prove that their generated timestamps will never collide.

The objective is:

> A valid Program should never intentionally require two audio files to play simultaneously.

Do not solve schedule conflicts by queuing one scheduled playback behind another.

---

# 18. Audio Duration Validation

The Program validator should inspect referenced WAV metadata.

For a fixed range:

```text
interval > playback duration + guard
```

For a random range:

```text
minimum random interval > playback duration + guard
```

Use a small implementation-defined scheduling guard.

If the schedule cannot guarantee that one playback finishes before another begins, reject the Program.

---

# 19. Missed Events

Never catch up missed playback.

Example:

```text
Scheduled:
08:00
08:10
08:20

Power removed:
08:05

Power restored:
08:17
```

Next playback:

```text
08:20
```

Do not play the missed 08:10 event.

General rule:

```text
next_event.timestamp > now
```

except when waking specifically for an event whose scheduled timestamp is within an implementation-defined RTC/wake tolerance.

---

# 20. Scheduler Interface

The scheduler should expose a small deterministic interface.

Conceptually:

```cpp
NextEvent Scheduler::getNextEvent(DateTime nowUtc);
```

Also provide:

```cpp
SchedulePreview Scheduler::preview(
    Program program,
    DateTime fromUtc,
    size_t count
);
```

The scheduler must be the single authoritative interpreter of Program schedules.

The following should all use the same scheduler:

- RTC wake scheduling
- status API
- "next playback" UI
- BTN_01 audio selection
- schedule preview
- validation tests

Do not duplicate scheduling logic in the web layer.

---

# 21. Schedule Preview

Provide an API capability to return the next N generated events.

Example:

```text
next 100 events
```

Each preview event should include:

```json
{
  "event_id": "daytime-calls",
  "scheduled_utc": "2026-09-28T11:00:00Z",
  "scheduled_local": "2026-09-28T06:00:00",
  "audio": "/audio/playback.wav",
  "volume": 80
}
```

This serves two purposes:

1. user validation
2. automated scheduler testing

The Program editor should allow a user to inspect a schedule before activating it.

---

# 22. Initial Client Program

The initial client requirement can be represented as:

```json
{
  "schema_version": 1,
  "id": "default",
  "name": "12 Hour Playback",
  "description": "Play playback.wav every 10 minutes from 06:00 to 18:00.",
  "events": [
    {
      "event_id": "daytime-playback",
      "type": "range",
      "start": "06:00:00",
      "end": "18:00:00",
      "audio": "/audio/playback.wav",
      "volume": 80,
      "recurrence": {
        "type": "fixed",
        "interval_seconds": 600
      }
    }
  ]
}
```

---

# 23. Random Range Example

```json
{
  "event_id": "random-morning",
  "type": "range",
  "start": "06:00:00",
  "end": "12:00:00",
  "audio": "/audio/squirrel.wav",
  "volume": 75,
  "recurrence": {
    "type": "random",
    "min_interval_seconds": 480,
    "max_interval_seconds": 720
  }
}
```

---

# 24. Daily Point Example

```json
{
  "event_id": "noon-call",
  "type": "point",
  "time": "12:00:00",
  "audio": "/audio/hawk.wav",
  "volume": 70
}
```

---

# 25. Program Editor UX Requirements

When adding a new event after an existing event, default sensible fields from the previous event.

In particular:

- audio file
- volume
- recurrence type
- interval

This is a UI convenience only.

The resulting JSON must remain explicit.

Do not introduce implicit inheritance into the Program file format.

---

# 26. Audio Format

v1 shall support uncompressed PCM WAV.

Recommended initial constraints:

```text
container: WAV
encoding: linear PCM
bit depth: 16-bit
channels: mono
sample rate: 44.1 kHz or 48 kHz
```

The WAV parser should inspect and validate the file header rather than assuming the file is valid.

Unsupported WAV files should produce a clear validation error.

Compressed formats such as:

- MP3
- AAC
- OGG

are out of scope for v1.

---

# 27. Audio Library

Audio files live under:

```text
/audio/
```

Default fallback:

```text
/audio/playback.wav
```

The UI should support:

- list audio files
- inspect basic WAV metadata
- upload WAV
- delete WAV where safe
- select WAV for event
- manually play
- stop
- test volume

Deletion should be rejected or strongly guarded if an audio file is referenced by the active Program.

---

# 28. PlaybackService

Scheduling and audio playback must be separate concerns.

Conceptually:

```cpp
PlaybackResult PlaybackService::play(
    AudioSource source,
    Volume volume
);
```

The scheduler determines **what should happen and when**.

`PlaybackService` determines **how audio is played**.

PlaybackService responsibilities include:

- audio rail startup
- DAC initialization
- amplifier initialization
- WAV parsing
- I2S streaming
- volume application
- playback completion
- stop
- mute/shutdown
- error reporting

---

# 29. Manual Playback

Manual playback from either the web UI or BTN_01 must be logged as distinct from scheduled playback.

Example source:

```text
manual_ui
manual_test
scheduled
```

Manual playback must **not alter the scheduled timeline**.

Example:

```text
next scheduled event = 10:00
BTN_01 pressed = 09:58
```

The manual test plays.

The 10:00 event remains scheduled.

---

# 30. BTN_01

Known mapping:

```text
GPIO7 -> BTN_01
```

Initial behavior:

```text
press/wake
    |
    v
determine next programmed playback
    |
    +-- available -> play its WAV using its configured volume
    |
    +-- unavailable -> /audio/playback.wav
    |
    +-- unavailable -> generated built-in sine wave
```

Priority:

1. audio from next scheduled playback
2. `/audio/playback.wav`
3. generated sine tone

The sine tone must not require the SD card.

BTN_01 must therefore remain useful for basic audio hardware testing even when the SD card has failed.

If SD is available, log the action as:

```text
manual_test
```

If SD is unavailable, perform the test but obviously no SD log can be written.

After completion:

- shut down audio hardware appropriately
- recompute next scheduled device event
- return to normal operation/sleep

---

# 31. BTN_02

Known mapping:

```text
GPIO8 -> BTN_02
```

Purpose:

> Allow someone standing at the device to quickly estimate battery state without connecting to Wi-Fi.

Behavior:

1. wake device
2. measure battery voltage
3. convert voltage to estimated battery percentage
4. convert percentage to 1–10 flashes where battery is valid/nonempty
5. flash `LED_R`
6. use approximately 100 ms between flashes
7. turn LED off
8. return to normal scheduler/sleep state

Suggested count calculation:

```text
count = ceil(battery_percent / 10)
```

then clamp appropriately.

Example:

```text
93% -> 10 flashes
74% -> 8 flashes
51% -> 6 flashes
22% -> 3 flashes
```

A valid/nonempty battery should not silently produce zero flashes.

Invalid ADC/battery readings should have a separately recognizable error behavior rather than masquerading as 0%.

Exact error flash pattern may be defined during hardware integration.

---

# 32. LEDs

Create a centralized LED service.

Conceptually:

```cpp
LedService::setGreen(bool);
LedService::setRed(bool);
LedService::flashRed(...);
LedService::allOff();
```

Do not allow unrelated application modules to manipulate LED GPIO directly.

Both LEDs must be OFF during deep sleep.

### LED_G

The requirement currently states:

> LED_G should be on when the device is "broadcasting."

The exact meaning of **broadcasting** remains TBD:

- audio playback, or
- Wi-Fi AP broadcast

Do not infer this in v1 implementation until clarified.

**Resolved (see section 69):** broadcasting means the Wi-Fi AP is running.

---

# 33. Battery Service

BatteryService should expose at least:

```cpp
float getVoltage();
uint8_t getEstimatedPercent();
```

Battery voltage is the authoritative measurement.

Battery percentage is an estimate for user convenience.

Use a configurable M12 voltage-to-percentage model/lookup table rather than presenting percentage as a precise state-of-charge measurement.

Calibration/model parameters belong in NVS.

Both voltage and percentage should be available to:

- API
- dashboard
- playback logger
- environmental logger
- BTN_02

---

# 34. Environmental Logging

Environmental logging is:

```text
enabled by default
```

Default interval:

```text
10 minutes
```

Minimum interval:

```text
1 minute
```

Internally allow arbitrary integer-minute intervals.

The UI may provide convenient presets such as:

```text
1
5
10
15
30
60 minutes
```

but should not require the firmware data model to use only these values.

Environmental logging should occur independently of whether the current playback Program is inside an active RANGE.

---

# 35. Environmental Log

File:

```text
/logs/environment.csv
```

Recommended header:

```csv
timestamp_utc,temperature_c,relative_humidity_pct,ambient_light_lux,battery_voltage,battery_percent
```

Example:

```csv
2026-09-27T18:10:00Z,4.8,63.2,127.4,11.84,76
```

Environmental values should use sensible sensor-level precision rather than excessive decimal places.

---

# 36. Playback Log

File:

```text
/logs/playback.csv
```

Recommended header:

```csv
actual_utc,scheduled_utc,program_id,program_revision,event_id,source,audio,volume,duration_ms,result,error_code,battery_voltage,battery_percent,temperature_c,relative_humidity_pct,ambient_light_lux
```

Example scheduled playback:

```csv
2026-09-27T18:10:00Z,2026-09-27T18:10:00Z,squirrel-study,a81f92c1,daytime-playback,scheduled,/audio/playback.wav,80,5032,ok,,11.84,76,4.8,63.2,127.4
```

Example manual playback:

```csv
2026-09-27T18:12:41Z,,squirrel-study,a81f92c1,daytime-playback,manual_test,/audio/playback.wav,80,5032,ok,,11.83,76,4.8,63.3,126.9
```

Environmental fields may be blank if a valid measurement cannot be obtained.

---

# 37. Log Integrity

Logging should prioritize recoverability under unexpected power loss.

Recommended pattern:

```text
open
append complete CSV row
flush
close
```

for relatively infrequent events.

Do not keep important experimental log data buffered in RAM for long periods merely to reduce SD writes.

At boot:

- validate log availability
- create file/header if missing
- never duplicate the header during normal append
- detect write failures
- expose write failures as device faults/status

Normal deployed operation should be blocked if required logging cannot be performed reliably.

---

# 38. Device Identity

Generate an immutable short device ID from the ESP32-S3 MAC.

Example:

```text
ACORN-A3F29C
```

Also support a user-editable device name.

Example:

```text
SQUIRREL_SITE_07
```

The immutable ID identifies the physical device.

The editable name identifies its human/experimental role.

---

# 39. Device Status Model

The API should expose at least:

- device ID
- device name
- firmware version
- RTC UTC time
- local displayed time
- UTC offset
- boot/reset reason
- wake reason
- battery voltage
- estimated battery %
- SD mounted
- SD total capacity
- SD used/free space
- temperature
- relative humidity
- ambient light
- active Program ID/name
- Program revision
- Program validity
- current playback state
- current active/quiet schedule state
- last playback
- next playback
- next environmental sample
- blocking faults
- Wi-Fi/AP state

The UI designer may determine the exact visual presentation.

---

# 40. Device States

Recommended high-level state model:

```text
BOOT
 |
 v
SELF_CHECK
 |
 +---- blocking fault ----> FAULT
 |
 v
READY
 |
 +---- configuration requested ----> CONFIG
 |
 +---- playback due -------------> PLAYBACK
 |
 +---- env sample due -----------> SAMPLE_ENV
 |
 +---- BTN_01 -------------------> MANUAL_TEST
 |
 +---- BTN_02 -------------------> BATTERY_CHECK
 |
 v
PREPARE_SLEEP
 |
 v
DEEP_SLEEP
```

`FAULT` does not necessarily mean the CPU remains awake.

A faulting device may still sleep/wake as appropriate while normal scheduled playback remains blocked.

Configuration/diagnostic access must remain possible.

---

# 41. Unified Next Device Event

Playback is not the only reason the device may wake.

SleepManager should conceptually merge:

```text
next playback
next environmental sample
other future scheduled maintenance event
```

and program the earliest required wake.

Conceptually:

```cpp
DeviceWakeEvent nextWake =
    min(
        scheduler.nextPlayback(),
        environmentLogger.nextSample(),
        ...
    );
```

The RTC/wake layer should not contain scheduling policy.

---

# 42. Deep Sleep Preparation

Before entering deep sleep:

1. ensure playback has stopped
2. mute/shutdown amplifier
3. shut down DAC/audio rail as required by hardware
4. stop I2S
5. ensure logs are flushed/closed
6. leave SD/SPI in its required safe state
7. turn off Wi-Fi
8. turn off `LED_R`
9. turn off `LED_G`
10. calculate next required wake
11. configure RTC alarm/wake inputs
12. enter deep sleep

Exact electrical sequencing comes from the hardware implementation phase/netlist.

---

# 43. Wi-Fi Configuration Mode

ACORN provides a local Wi-Fi AP.

v1 AP security:

```text
OPEN
```

No internet connection is required.

Wi-Fi should normally be OFF during deployed operation.

Configuration AP timeout:

```text
10 minutes of inactivity
```

Active UI/API activity may extend the session.

The exact mechanism by which opening the enclosure/Hall sensor starts configuration mode is **TBD**.

Do not permanently couple Hall behavior to the web server until the enclosure activation state machine is defined.

**Resolved (see section 69):** door open starts the AP; door close or the 10-minute idle timeout ends it (Phase 4).

---

# 44. Web UI Information Architecture

The initial UI should provide logical areas for:

### Dashboard

- device identity
- battery
- SD
- time
- sensor status
- active Program
- last playback
- next playback
- next environmental sample
- faults

### Programs

- list Programs
- create Program
- edit Program
- duplicate Program
- upload Program JSON
- download Program JSON
- delete Program
- validate Program
- preview generated schedule
- activate Program

### Audio

- list WAV files
- upload WAV
- inspect WAV metadata
- select file
- Play
- Stop
- volume test
- central audio configuration/test tools

### Environment

- enable/disable logging
- logging interval
- current readings

### Logs / Data

- playback log information/download
- environmental log information/download

### Device / Time

- device name
- device ID
- UTC
- local time
- UTC offset
- battery calibration/configuration as appropriate

### Firmware

- firmware version
- browser-based OTA upload
- update status/error

Visual design is explicitly outside this document.

---

# 45. Save & Apply Semantics

UI edits should be staged in the browser.

Do not update live configuration field-by-field.

User workflow:

```text
edit
  ↓
Save & Apply
  ↓
validate entire object
  ↓
reject OR atomically save
  ↓
reload
  ↓
recalculate schedule
  ↓
return resulting status
```

There must not be a state where half of a Program update has been applied.

For SD JSON writes, prefer:

```text
write temporary file
validate/read back if appropriate
rename/replace target
```

rather than overwriting the only valid Program in-place.

---

# 46. API

Use a versioned REST-like API.

Suggested namespace:

```text
/api/v1/
```

Recommended resources:

```text
GET  /api/v1/status

GET  /api/v1/device
PUT  /api/v1/device

GET  /api/v1/time
PUT  /api/v1/time

GET  /api/v1/audio
POST /api/v1/audio
GET  /api/v1/audio/{file}
DELETE /api/v1/audio/{file}
POST /api/v1/audio/test
POST /api/v1/audio/stop

GET  /api/v1/programs
POST /api/v1/programs
GET  /api/v1/programs/{id}
PUT  /api/v1/programs/{id}
DELETE /api/v1/programs/{id}

POST /api/v1/programs/{id}/validate
GET  /api/v1/programs/{id}/preview
POST /api/v1/programs/{id}/activate

GET  /api/v1/environment
PUT  /api/v1/environment

GET  /api/v1/logs
GET  /api/v1/logs/playback
GET  /api/v1/logs/environment

POST /api/v1/firmware
```

The exact routes may change if the developer identifies a cleaner consistent hierarchy.

The separation of resources should remain.

---

# 47. Standard API Error Shape

Use consistent machine-readable errors.

Example:

```json
{
  "ok": false,
  "error": {
    "code": "PROGRAM_RANGE_OVERLAP",
    "message": "Range 'morning' overlaps range 'daytime'.",
    "field": "events[2]",
    "details": {}
  }
}
```

Successful mutations should return:

```json
{
  "ok": true,
  "data": {}
}
```

where practical.

---

# 48. Validation/Error Taxonomy

At minimum distinguish:

```text
SD_NOT_PRESENT
SD_MOUNT_FAILED
SD_WRITE_FAILED
SD_FULL

PROGRAM_NOT_FOUND
PROGRAM_JSON_INVALID
PROGRAM_SCHEMA_UNSUPPORTED
PROGRAM_RANGE_INVALID
PROGRAM_RANGE_OVERLAP
PROGRAM_EVENT_COLLISION
PROGRAM_AUDIO_MISSING
PROGRAM_AUDIO_INVALID
PROGRAM_INTERVAL_TOO_SHORT
PROGRAM_RANDOM_RANGE_INVALID

WAV_UNSUPPORTED
WAV_CORRUPT

RTC_INVALID
TIME_NOT_SET

BATTERY_READING_INVALID

SENSOR_READ_FAILED

PLAYBACK_START_FAILED
PLAYBACK_IO_ERROR

OTA_INVALID
OTA_FAILED
```

Errors should be usable by both humans and API clients.

---

# 49. Program Activation

Activating a Program should:

1. load Program from SD
2. parse JSON
3. validate schema
4. validate all event definitions
5. validate range overlaps
6. validate referenced audio files
7. inspect WAV durations
8. validate collision constraints
9. calculate revision/hash
10. generate a schedule preview successfully
11. only then store active Program ID in NVS
12. recalculate next event

If any step fails, the previously active valid Program should remain selected where possible.

---

# 50. Time Synchronization

The configuration UI should allow device time synchronization.

Preferred workflow:

```text
browser knows current time
        ↓
API receives UTC timestamp
        ↓
RTC set in UTC
```

UTC offset is configured separately.

Logs remain UTC regardless of displayed local time.

Changing time or UTC offset must force:

- schedule recalculation
- RTC alarm recalculation
- next-event recalculation

Missed events created by the change are not replayed.

---

# 51. OTA

Browser-based OTA is required in v1.

OTA must remain available in SD fault mode.

The firmware project should reserve an OTA-compatible flash partition layout from the beginning.

OTA flow:

```text
select firmware binary
        ↓
upload
        ↓
validate
        ↓
write inactive application partition
        ↓
report success/failure
        ↓
reboot
```

Do not require internet access or an external update server.

---

# 52. UI Audio Test Workflow

Audio testing is a high-priority field workflow.

Someone physically standing at ACORN should be able to:

```text
open device
connect
open Audio
select WAV
set/test volume
Play
Stop
```

without navigating through Program configuration.

The Audio section should therefore be a first-class UI area rather than burying playback testing inside the Program editor.

---

# 53. Program Editor Requirements

For RANGE:

```text
Event type       RANGE
Start            HH:MM[:SS]
End              HH:MM[:SS]
Audio            [selector]
Volume           [control]

Playback
    Fixed
        Every [value] [minutes/seconds]

    Random
        Minimum [value]
        Maximum [value]
```

For POINT:

```text
Event type       POINT
Time             HH:MM[:SS]
Audio            [selector]
Volume           [control]
```

Provide immediate validation feedback where convenient, but authoritative validation occurs in firmware.

---

# 54. Schedule Preview UX

Program editor should provide something conceptually equivalent to:

```text
Preview Schedule
```

Result:

```text
Local       UTC                  Audio          Volume
06:00       11:00Z               playback.wav   80
06:10       11:10Z               playback.wav   80
06:20       11:20Z               playback.wav   80
...
```

For random schedules, this preview must use the same deterministic generation algorithm as actual playback.

---

# 55. Program Limits

Do not design the JSON schema around unlimited resources.

The implementation may impose explicit limits based on memory testing, such as:

- maximum Programs indexed
- maximum events per Program
- maximum JSON size
- maximum filename length

These limits should:

1. be centralized constants,
2. be exposed through validation errors,
3. not be silently exceeded,
4. not leak arbitrary implementation assumptions into the UI.

Do not introduce limits without documenting them.

---

# 56. Boot Sequence

Suggested boot flow:

```text
BOOT
 |
 +-> initialize minimum hardware
 |
 +-> determine reset/wake reason
 |
 +-> load NVS configuration
 |
 +-> initialize RTC/time
 |
 +-> measure battery
 |
 +-> mount SD
 |
 +-> load active Program
 |
 +-> validate Program/audio
 |
 +-> initialize logging
 |
 +-> inspect wake reason
       |
       +-> BTN_01 -> manual audio test
       |
       +-> BTN_02 -> battery indication
       |
       +-> RTC -> execute due event
       |
       +-> configuration trigger -> CONFIG
       |
       +-> other -> evaluate state
 |
 +-> calculate next event
 |
 +-> sleep
```

Avoid unnecessarily initializing Wi-Fi on ordinary scheduled wakes.

---

# 57. RTC Wake Tolerance

Exact RTC wakeup will have finite timing tolerance.

Define a small constant:

```text
EVENT_DUE_TOLERANCE
```

When waking from the RTC for an event:

```text
if abs(now - scheduled_time) <= tolerance:
    execute event
```

Otherwise:

```text
do not replay historical events
calculate next future event
```

Log actual UTC and scheduled UTC separately so timing performance can be measured.

---

# 58. Environmental Sampling Schedule

Environmental logging should avoid cumulative drift.

Recommended behavior:

- calculate deterministic future sample boundaries from the configured interval,
- rather than scheduling the next sample as `previous_sample_completion + interval`.

For a 10-minute interval, an implementation may naturally align samples to UTC boundaries:

```text
12:00
12:10
12:20
...
```

The exact alignment policy should be documented and unit tested.

---

# 59. Fault Philosophy

Differentiate:

### Blocking faults

Prevent normal deployed operation.

Examples:

- SD missing
- active Program invalid
- required WAV missing
- RTC invalid
- log cannot be written

### Nonblocking faults

Allow operation with degraded telemetry.

Examples may include:

- transient humidity read failure
- transient light sensor read failure

The API should report both.

A failed environmental sensor should generally not prevent playback unless future requirements explicitly require environmental gating.

---

# 60. Hardware Abstraction Rule

Application code should not know things such as:

```text
GPIO38 means amplifier shutdown
GPIO17 means audio rail
```

Instead:

```cpp
audioHardware.powerOn();
audioHardware.shutdown();

batteryService.read();

ledService.flashRed(...);
```

The hardware netlist supplied later is authoritative for pin definitions.

Known exceptions already defined:

```text
GPIO7 -> BTN_01
GPIO8 -> BTN_02
```

---

# 61. Scheduler Pseudocode

Conceptual implementation:

```cpp
NextEvent Scheduler::getNextEvent(UtcTime nowUtc)
{
    LocalTime nowLocal = timeService.utcToLocal(nowUtc);

    candidates = [];

    for each event in activeProgram.events:
        if event.type == POINT:
            candidate = nextPointOccurrence(event, nowLocal);

        else if event.type == RANGE:
            candidate = nextRangeOccurrence(event, nowLocal);

        if candidate exists:
            candidates.push(candidate);

    return earliest candidate converted to UTC;
}
```

Cross-midnight logic must consider both:

```text
range instance beginning today
range instance beginning yesterday
```

when appropriate.

---

# 62. Fixed Range Generation Pseudocode

```cpp
t = rangeStart;

while (t < rangeEnd) {
    emit(t);
    t = rangeStart + n * interval;
}
```

Use anchor-based calculation where possible rather than repeatedly adding elapsed playback time.

---

# 63. Random Range Generation Pseudocode

```cpp
seed = hash(
    programRevision,
    eventId,
    rangeStartLocalDate
);

rng.seed(seed);

t = rangeStart;
emit(t);

while (true) {
    interval = rng.uniform(minInterval, maxInterval);
    t += interval;

    if (t >= rangeEnd)
        break;

    emit(t);
}
```

The preview engine and live scheduler must use the same implementation.

---

# 64. Local/UTC Conversion

Programs store daily local wall-clock times.

Scheduler should:

1. determine applicable local date/range instance
2. generate event in local schedule space
3. apply fixed UTC offset
4. produce UTC timestamp
5. compare against current UTC
6. program RTC wake using UTC

Do not embed timezone/DST databases in v1.

---

# 65. API/Service Separation

Example:

```text
POST /api/v1/audio/test
```

should call:

```text
AudioController
    -> PlaybackService
        -> AudioHardware
```

It should not:

```text
HTTP handler
    -> digitalWrite(...)
```

Likewise:

```text
POST /api/v1/programs/x/validate
```

must call the same `ProgramValidator` used during Program activation.

---

# 66. Configuration Check-In

Opening the configuration interface is expected to be a common field action.

Therefore configuration mode should be non-destructive.

Entering configuration mode must not:

- reset Program timing
- redraw random schedules
- clear logs
- create a deployment
- mark missed events for catch-up
- change active Program
- alter RTC unless explicitly requested
- modify configuration merely by viewing it

---

# 67. Testing Requirements

The scheduler should be testable on a desktop/host environment if practical, without ESP32 hardware.

At minimum create automated test vectors for the following.

### Fixed range

```text
06:00–18:00
10-minute interval
```

Verify:

```text
first = 06:00
last = 17:50
count = 72
```

### End boundary

Verify:

```text
18:00 is not emitted
```

### Cross-midnight

```text
20:00–04:00
```

Verify correct events before and after midnight.

### Missed event

Current time:

```text
08:17
```

Schedule:

```text
08:00
08:10
08:20
```

Expected:

```text
08:20
```

### Random reproducibility

Generate random schedule.

Reinitialize scheduler.

Generate again with identical:

```text
program revision
event ID
local date
```

Expected:

```text
identical timestamps
```

### Random date change

Change schedule-instance date.

Expected:

```text
new deterministic sequence
```

### Program revision change

Change relevant Program content.

Expected:

```text
new revision
new deterministic random sequence
```

### Overlapping ranges

Expected:

```text
validation failure
```

### Cross-midnight overlap

Expected:

```text
validation failure
```

### Interval shorter than WAV

Expected:

```text
validation failure
```

### Random minimum shorter than WAV

Expected:

```text
validation failure
```

### Missing WAV

Expected:

```text
Program cannot activate
```

### Missing SD

Expected:

```text
scheduled operation blocked
configuration/diagnostics available
BTN_01 built-in sine still available
```

### Manual test

Press BTN_01 shortly before scheduled playback.

Expected:

```text
manual audio plays
scheduled event remains unchanged
```

### BTN_02

Test battery percentages across:

```text
0–100%
```

Verify expected flash count and clamping.

### RTC/time change

Change RTC forward/backward.

Expected:

```text
scheduler recalculates
historical events are not replayed
```

### UTC offset change

Expected:

```text
local daily schedule maps to new UTC timestamps
```

### Power loss

Simulate reboot between events.

Expected:

```text
next future event selected
random schedule unchanged
logs remain parseable
```

---

# 68. Acceptance Criteria for Initial Client

The first usable firmware milestone should demonstrate:

1. Device reads RTC in UTC.
2. SD mounts successfully.
3. `/audio/playback.wav` is validated and played.
4. A Program JSON can define:

```text
06:00–18:00
play every 10 minutes
```

5. Scheduler generates exactly:

```text
06:00 ... 17:50
```

6. Device wakes only as needed.
7. Scheduled playback is logged.
8. Environmental data logs every 10 minutes.
9. Device returns to deep sleep.
10. Web UI reports next playback.
11. Program can be edited and validated through API/UI.
12. BTN_01 performs the defined audio test hierarchy.
13. BTN_02 reports battery via LED_R.
14. Missing SD blocks deployed operation cleanly.
15. Browser-based OTA works.
16. Random recurrence and POINT events pass automated scheduler tests even if not emphasized in the first UI workflow.

---

# 69. Explicit TBDs

Do not silently decide these during implementation.

### RESOLVED — LED_G

Meaning of:

```text
"LED_G on while broadcasting"
```

Resolved: "broadcasting" means the Wi-Fi configuration AP is running. LED_G is on exactly while the AP is up and is always off in sleep. `WifiService` is the only code path that starts/stops the AP and the only caller of `LedService::setBroadcasting()`.

### RESOLVED — Hall / enclosure behavior

Final relationship between:

```text
lid open
Hall sensor
configuration AP
device activation
```

Resolved: opening the door (MAG_OUT HIGH, which also wakes the device from deep sleep) starts the configuration AP and web server. The session ends when the door closes or after 10 minutes without client activity, then the device returns to event-driven sleep. Implemented in Phase 4 (`ConfigSession`; behavior in `docs/hardware.md` "Configuration mode"). A door left open when the session ends is armed to wake the device on door close.

### RESOLVED — TRIG_IN

Unused in v1. No behavior is assigned; the pin is left as an input (its reset default) and the firmware does not otherwise touch it.

### TBD — Additional button state machine

BTN_01 and BTN_02 have initial functions, but additional long-press/multi-function behavior may be defined later.

### TBD — Exact hardware pins

All unspecified pin assignments come from the updated netlist.

### DEFERRED — Final audio calibration

The relationship between UI volume units and calibrated acoustic SPL may be expanded later.

Deferred until the hardware prototype arrives. Volume remains 0–100 UI units until then.

### TBD — Sunrise/sunset

Potential future schedule type.

Light sensor may eventually participate.

Do not implement in v1.

---

# 70. Future-Compatible Concepts

The architecture should not require these now, but should avoid making them unnecessarily difficult later:

- sunrise/sunset/light-triggered schedules
- external triggers
- calibrated SPL targets
- multi-device synchronization
- serial API
- remote API
- fleet management
- additional environmental sensors
- multiple schedule types
- deployment/session metadata
- program import/export tooling
- richer audio treatments

Do not implement speculative features merely because they appear in this list.

---

# 71. Core Architectural Summary

The intended hierarchy is:

```text
                    WEB UI
                       |
                       v
                  /api/v1/*
                       |
        +--------------+--------------+
        |              |              |
        v              v              v
   ProgramStore   DeviceState    Audio Control
        |              |              |
        v              |              v
 ProgramValidator      |       PlaybackService
        |              |              |
        v              |              v
    Scheduler          |        AudioHardware
        |
        v
   Next Event
        |
        v
   SleepManager
        |
        v
      RTC
```

Logging operates alongside these services:

```text
PlaybackService
      |
      +----> PlaybackLogger

EnvironmentService
      |
      +----> EnvironmentLogger
```

---

# 72. Most Important Implementation Constraint

There must be **one authoritative interpretation of a Program**.

The following must never independently calculate schedules:

- UI
- API handlers
- sleep code
- RTC code
- playback code
- button code

They should all ask the Scheduler.

Likewise, there should be:

- one Program validator
- one PlaybackService
- one TimeService
- one BatteryService
- one hardware abstraction layer

This is the primary architectural requirement intended to keep ACORN maintainable as its functionality expands.

---

# 73. Recommended First Development Slice

Before integrating all hardware, prove this vertical slice:

```text
Program JSON
    ↓
parse
    ↓
validate
    ↓
Scheduler
    ↓
next event
    ↓
schedule preview API
    ↓
mock UI
```

Use host/unit tests heavily here.

Then implement:

```text
RTC + SD + PlaybackService
```

and connect the tested scheduler to actual hardware.

This keeps the most complex part of ACORN—the scheduling semantics—independent from the complexity of audio, Wi-Fi, sensors, and deep sleep.

---

# 74. Definition of v1

ACORN v1 is successful when a researcher can:

1. place WAV files on or upload them to the SD card,
2. create/select a Program,
3. define daily POINT and RANGE playback events,
4. choose fixed or random RANGE intervals,
5. validate the schedule,
6. preview upcoming playback,
7. set/test volume,
8. configure environmental logging,
9. synchronize device time,
10. activate the Program,
11. disconnect,
12. leave ACORN operating autonomously,
13. later retrieve unambiguous playback/environment logs,
14. inspect battery/device state in the field,
15. update firmware locally without physically reprogramming the ESP32.

The implementation should optimize for **predictability, experimental reproducibility, field reliability, and understandable behavior** rather than adding unnecessary scheduling or networking complexity.