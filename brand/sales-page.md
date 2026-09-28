ACORN · Autonomous Configurable Outdoor Replay Node

# Scheduled audio for wildlife fieldwork.

## Set the day on your phone. ACORN plays it, logs it, and sleeps until the next event.

## Summary

ACORN is a battery-powered outdoor player for wildlife research and related field experiments. Put a recording on the card, describe the day in local time, and leave the device on site. It wakes for each playback, writes a record of what played, and returns to sleep. Temperature, humidity, and light are sampled on an interval you choose. Setup is done from a phone joined to the device itself. The site does not need a network, a cell signal, or an account.

## A day you can describe in one sentence

Most playback protocols are simple. Call on a fixed interval through an active window, then stay quiet. ACORN is built for that day: one file, every ten minutes, from 06:00 to 18:00, and silent for the other twelve hours.

The same program can do more when the protocol asks for it. Place a single play at an exact time. Or scatter plays at random inside a window, when a perfectly even rhythm would bias the trial. Several programs can live on the card. One is active.

## See the schedule before you leave

Programs are written in local wall-clock time, the way a field protocol is written. ACORN shows the upcoming plays on the phone and refuses a schedule that could overlap itself. One playback is never stacked behind another. A missed play stays missed.

The clock is battery-backed and kept in UTC. You set the offset for local time, and the log uses that same timeline. Every saved program carries a revision, and that revision is written into the playback log, so a response in the field can be matched to the exact schedule that produced it.

## Open the door and join the device

Configuration starts when the enclosure opens. ACORN brings up its own Wi-Fi network and the setup page opens on your phone. A green light stays on while that radio is running.

From the page you set the clock, load or edit the program, choose a volume, and play the file once before you walk away. A button on the device can play a test as well. Another reports battery status. Close the door, or leave the session idle, and the radio shuts off. The device goes back to sleeping between events. Scheduled playback continues while you are standing there.

## The card is the dataset

Recordings, programs, and logs live on the microSD card. The playback log records what played and when. Environmental logging writes temperature, humidity, and ambient light on a preset interval or any whole number of minutes you set.

The device itself keeps its name, its clock offset, and its battery model. Pull the card when the deployment ends. The experiment comes with it.

## Awake only when there is work

Between events, ACORN sleeps. It calculates the next playback or sample, wakes for that job, writes the result, and sleeps again. Firmware can be updated from the phone while you are at the device, and the setup page is part of that update.

## In the field

1. Copy a WAV file onto the microSD card and seat the card.
2. Open the enclosure and join the ACORN network on your phone.
3. Set the clock and confirm local time.
4. Build the day’s program, or load one you saved earlier, and read the preview.
5. Play the file once at the volume you want.
6. Close the door.

## How long a pack lasts

ACORN runs from an M12 pack and sleeps between events. The figures below are estimates for a full pack, playing at about 90 dB at 1 meter (1 watt into the speaker), with an environmental sample every 10 minutes. Daily playback is the speaker’s total on-time, added up across the day. A louder recording uses more of the pack. A cold pack delivers less than it does at moderate temperature.

| Daily playback | 2.0 Ah M12 | 5.0 Ah M12 |
| --- | --- | --- |
| 5 minutes | About 30 days | About 80 days |
| 10 minutes | About 25 days | About 65 days |
| 30 minutes | About 15 days | About 35 days |

## Specifications

- Daily programs in local time: active windows and single points, with fixed or random spacing
- Audio: 16-bit mono WAV at 44.1 or 48 kHz, through a class-D amplifier. Volume is set on the device, from 0 to 100.
- Speaker: Visaton R 10 SC, 4 ohm. 90 dB at 1 meter from 1 watt, 100 Hz–13 kHz. Full volume from a 12-volt pack is about 103 dB at 1 meter.
- Weather: IP65. Operating range −25°C to 85°C (−13°F to 185°F).
- Power: 2.0 Ah or 5.0 Ah M12 pack. Voltage and percent are on the status page. Estimated runtime is in the table above.
- Logs: playback and environment, as CSV files on the card
- Sensors: temperature, relative humidity, and ambient light
- Clock: battery-backed real-time clock, with a fixed local-time offset
- Setup: a phone on the device’s own Wi-Fi network, with the page opening when you join
- Updates: firmware upload from the setup page
- Identity: a device name and id, stored on the device
