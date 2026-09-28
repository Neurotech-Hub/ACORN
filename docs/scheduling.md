# ACORN Scheduling Reference

This is the reference for how ACORN turns a program into playback times.
The firmware (`arduino/libraries/AcornCore`) and the dev mock
(`dev/mock/scheduler.mjs`) implement the same algorithms, and they must
produce the same results bit for bit. `docs/init.md` holds the requirements;
this file describes the implementation that satisfies them.

All scheduling code lives in AcornCore. AcornCore is plain C++17 with no
`Arduino.h`, so the same sources compile on the device and in the host tests.

## Time model

- The device keeps UTC: the DS3231 RTC, extended between reads by `esp_timer`.
- A program's times are local wall-clock times (`HH:MM[:SS]`). Local time is
  UTC plus the configured fixed offset in minutes (`utc_offset_minutes`, in
  the range -720 to +840, in multiples of 15). DST is not applied.
- Internally, schedules are built in *local epoch seconds*
  (`utc + offset * 60`). A local day starts at `floorDiv(local, 86400) * 86400`.
- A `ScheduledEvent` carries the event index, `utc` and `local`.

## Program revision

A revision identifies a program's schedule-relevant content:

```
revision = hex8( fnv1a32( canonical({ schema_version, events }) ) )
```

- `canonical` is compact JSON with object keys sorted by byte order (strcmp),
  arrays kept in order, and strings escaped the way `JSON.stringify` escapes
  them (control characters become `\u00xx`).
- Name, description, id and `modified_utc` are excluded, so renaming a
  program does not change its revision or its random sequences.
- `ProgramJson::revision(Program)` hashes the *normalized* program (times
  written as `HH:MM:SS`). `ProgramJson::rawRevision(json)` hashes the JSON
  as given (`schema_version` defaults to 1 and `events` to `[]`); this is how
  the mock computes a revision for a stored program.
- Both the firmware and the mock normalize programs when saving them (times
  as `HH:MM:SS`, known fields only), and both hash valid drafts sent to
  validate or preview in that normalized form, so revisions and random
  sequences match. Only an invalid draft's revision is computed from the
  JSON as sent.

## Event occurrences

`Scheduler::occurrences(event, dayStart, lo, hi, cap)` generates one day
instance of an event, keeping times in `[lo, hi)`.

- **Point:** a single time, `dayStart + time`.
- **Range:** from `s = dayStart + start` to `e = dayStart + end`. If
  `e <= s`, the window crosses midnight and `e += 86400`. A range therefore
  belongs to the day on which it *starts*. Occurrences stop before
  `min(e, hi)`.
  - **Fixed recurrence:** anchored at the window start,
    `t = s + n * interval_seconds` for n = 0, 1, 2, ... The first n that
    reaches `lo` is computed directly, so the times never drift.
  - **Random recurrence:** the first occurrence is at `s`. Each next one is
    `t += min + floor(rng() * (max - min + 1))`. `rng` is mulberry32, seeded
    with `fnv1a32(revision + "|" + event_id + "|" + YYYY-MM-DD)`, where the
    date is the local date of the window start. Because the seed depends only
    on those values, the sequence for a given day is the same across reboots,
    in preview, and when the device wakes in the middle of a window.

The mulberry32 step is:

```
a += 0x6d2b79f5
t  = a
t  = (t ^ (t >> 15)) * (1 | t)
t  = (t + (t ^ (t >> 7)) * (61 | t)) ^ t
return (t ^ (t >> 14)) / 2^32
```

The C++ version (in `Hash.h`) uses uint32 arithmetic, which matches the
JavaScript version's `Math.imul` and `>>> 0`.

## Preview and next event

`Scheduler::preview(fromUtc, count)` returns the next `count` occurrences
strictly after `fromUtc`:

1. Start one local day before the current day, so ranges that cross midnight
   from yesterday are included.
2. For each day, collect every event's occurrences after `now` (up to the
   remaining count), then sort by `(local, event index)`.
3. Emit the sorted times that fall before the end of that day. Keep later
   ones pending, because a range crossing midnight can produce times after
   the next day's first events.
4. Stop at `count` results or after `PREVIEW_HORIZON_DAYS` (400) days.

`nextEvent(now)` is `preview(now, 1)`. It is used for status, for the wake
planner, and for the BTN_01 manual-play choice.

`between(fromUtc, toUtc)` returns every occurrence in `[from, to)`, sorted
the same way.

`windowState(now)` reports whether a range window is active (and until when),
the program is quiet until the next window start, or the program has only
point events. It drives the Home screen summary.

## Dispatch

The runtime checks the schedule at least once per second:

```
dueEvent(now, lastExecutedUtc, tolerance = 10 s)
  = first e in between(now - tol, now + tol + 1) with e.utc > lastExecutedUtc
```

- An event plays only if the device is awake within ±10 s of its time
  (`EVENT_DUE_TOLERANCE_S`). Missed events are not caught up.
- An event found early (for example, after the RTC lead wake below) is not
  started until `now >= scheduled_utc`, so playback never begins before its
  scheduled second. It still counts as due, which is why a slightly early
  wake is valid.
- `lastExecutedUtc` is set to the scheduled time of the event that was
  started, so each occurrence plays at most once. It lives in RTC memory
  (`RTC_DATA_ATTR`), so it survives deep sleep and a quick re-wake cannot
  repeat an event.
- A scheduled start stops any manual playback in progress. If the start fails
  (for example, a missing file), a playback row with the error code is
  written.

## Time and program changes

The runtime tracks `TimeService::generation()` and
`ActiveProgram::generation()`. When either changes (the clock is set, the
offset changes, or a program is activated):

- Forward jumps need no special handling: `dueEvent` only looks inside the
  tolerance window, so skipped events are never replayed.
- If a large backward jump leaves `lastExecutedUtc` in the future, it is set
  back to `now - tolerance - 1`, so events at the new time play normally.
- The scheduler is rebuilt lazily. It is keyed by the program revision and
  the UTC offset.
- A clock or offset change also disables and clears the RTC alarm. The next
  alarm is always recomputed when the device goes to sleep, so changes made
  during a configuration session need no other hooks.

## Environmental sampling

`EnvSchedule` aligns samples to the UTC epoch:

- The boundaries are multiples of `interval_minutes * 60` counted from the
  epoch (1 to 1440 minutes, default 10). A 10-minute interval samples at
  :00, :10, :20 UTC, however long each wake takes.
- `nextSampleAfter(now)` returns the first boundary strictly after `now`.
  `sampleAtOrBefore(now)` returns the boundary that is due.
- Each row is stamped with its boundary time, not the time it was read.
- Sampling is deferred while audio is playing and resumes after playback.
- A time change or interval change recomputes the next boundary.
- The last sampled boundary is kept in RTC memory. When the next boundary is
  computed at boot, a boundary no more than 10 s in the past that has not been
  sampled is still taken, so a slow boot does not skip a sample. Otherwise
  the next future boundary is used.

## Wake planning

`WakePlanner::earliest(nextPlayback, nextEnvSample)` merges the two event
streams into one wake time. On a tie, playback wins, and the env sample runs
in the same wake. `isDue(now, t, tol)` applies the same ±tolerance rule. The
sleep layer only asks the planner for a time; it has no scheduling policy of
its own.

`SleepManager` programs the RTC alarm `WAKE_LEAD_S` (2 s) before the planned
time, so booting, mounting the SD card and powering up the audio chain finish
before the scheduled second. The device stays awake instead of sleeping if
the alarm would be less than `MIN_SLEEP_S` (3 s) away. Without a valid clock
no alarm is set, so only the buttons and the door can wake the device. When
the device is not armed (a blocking fault), `nextPlayback` is empty, so only
env samples schedule wakes.

## Limits

These are defined in `AcornLimits.h` and reported to clients by
`GET /api/v1/programs` under `limits`:

| Limit | Value |
|---|---|
| Programs | 16 |
| Events per program | 32 |
| Name / description | 48 / 200 chars |
| Audio file name | 32 chars |
| Audio files indexed | 64 |
| Program JSON | 16 KB |
| Preview count | 500 |
| Preview horizon | 400 days |
| Audio guard between events | 2000 ms |
| Due tolerance | 10 s |

## Validation

`ProgramValidator` is the only validator. It returns `{code, path, message}`
errors whose paths and messages match the mock (for example,
`events[2].recurrence.min_interval_seconds`). It checks structure, ids,
times, volume, recurrence bounds, audio presence and format (against the
audio library catalog), the limits above, and whether an event's audio
fits inside its interval plus the guard. The validator fills in the parsed
program even when it reports errors, so callers can still show a preview.

## Verifying parity

- `arduino/host-tests/run.sh` builds `test_core.cpp` with the host compiler
  and runs the §67 vectors, CSV, battery, wake planner, JSON round trip and
  golden previews. Run `run.sh --write-golden` to regenerate
  `host-tests/golden/*.json` after an intentional algorithm change.
- `node tools/check-mock-golden.mjs` checks the mock's `preview` and
  `programRevision` against the same golden files. If both pass, the device
  and the mock agree.
- `node tools/api-contract.mjs --base <url> [--readonly]` runs the API
  contract against the mock or a device, and compares the device's preview
  and revision with the mock scheduler.
- `arduino/compile-tests.sh` runs the host tests and the golden comparison
  before compiling (set `ACORN_SKIP_HOST=1` to skip them).
