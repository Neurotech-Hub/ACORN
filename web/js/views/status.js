// Status dashboard: armed/fault state first, then next playback, then device telemetry.

import { deviceNow } from '../store.js';
import {
  h, clear, replace, icon, mark, badge, skeleton, fmtBytes, fmtClock, fmtUtcClock, fmtOffset, fmtRelative,
  dayLabel, toLocalIso, fmtDuration, fmtDateTime, batteryKind,
} from '../ui.js';

const FAULT_LINKS = {
  device: ['#/device', 'Set time'],
  programs: ['#/programs', 'Open programs'],
  data: ['#/data', 'Open data'],
  audio: ['#/audio', 'Open audio'],
};

function faultLink(f) {
  if (f.area === 'programs' && f.program_id) return ['#/programs/' + encodeURIComponent(f.program_id), 'Fix program'];
  return FAULT_LINKS[f.area];
}

function hero(st) {
  const blocking = st.faults.filter((f) => f.blocking);
  const armed = blocking.length === 0;
  return h('section', { class: `hero ${armed ? '' : 'fault'}` },
    mark('mark'),
    h('div', { class: 'eyebrow' }, 'Operational state'),
    h('h1', null, armed ? 'Armed' : 'Not armed'),
    h('p', null, armed
      ? 'Scheduled playback continues when you disconnect. Viewing this page does not change the schedule.'
      : 'Scheduled playback is blocked until the faults below are resolved. Configuration, diagnostics, the audio test tone and firmware updates remain available.'),
    !armed && h('ul', { class: 'fault-list' }, blocking.map((f) => {
      const link = faultLink(f);
      return h('li', null,
        h('div', null, h('code', null, f.code), h('div', null, f.message)),
        link && h('a', { href: link[0] }, link[1] + ' →'));
    })),
  );
}

function warnings(st) {
  const list = st.faults.filter((f) => !f.blocking).map((f) => [f.code, f.message]);
  if (st.battery && st.battery.valid && st.battery.percent <= 15) {
    list.push(['BATTERY_LOW', `Battery estimated at ${st.battery.percent}% (${st.battery.voltage.toFixed(2)} V). Replace soon.`]);
  }
  if (!list.length) return null;
  return h('ul', { class: 'warn-list' }, list.map(([code, msg]) =>
    h('li', null, icon('alert', 'sm'), h('div', null, h('b', null, code), ' ', h('span', null, msg)))));
}

function nextCard(st) {
  const n = st.next_playback;
  const pb = st.playback;
  const playing = pb && pb.state === 'playing';
  const today = st.time.local;
  let body;
  if (n) {
    body = [
      h('div', { class: 'eyebrow' }, 'Next playback'),
      h('div', { class: 'bigtime' }, fmtClock(n.scheduled_local), h('small', null, dayLabel(n.scheduled_local, today))),
      playing
        ? h('div', { class: 'countdown' }, 'PLAYING')
        : h('div', { class: 'countdown', 'data-until': n.scheduled_utc }, ''),
      h('div', { class: 'muted small', style: { marginTop: '6px' } },
        h('span', { class: 'mono' }, n.event_id), ' · ', n.audio.replace('/audio/', ''), ` · vol ${n.volume} · `, fmtUtcClock(n.scheduled_utc)),
    ];
  } else if (playing) {
    // Manual / test tone with nothing scheduled: same card layout, no giant filename.
    const localStart = toLocalIso(Date.parse(pb.started_utc), st.time.utc_offset_minutes);
    body = [
      h('div', { class: 'eyebrow' }, 'Next playback'),
      h('div', { class: 'bigtime' }, fmtClock(localStart), h('small', null, dayLabel(localStart, today))),
      h('div', { class: 'countdown' }, 'PLAYING'),
      h('div', { class: 'muted small', style: { marginTop: '6px' } },
        (pb.audio || 'Test tone').replace('/audio/', ''), ` · ${pb.source} · vol ${pb.volume}`),
    ];
  } else {
    const reason = !st.armed ? 'Blocked by faults above.' : !st.program ? 'No active program.' : 'Program has no upcoming events.';
    body = [
      h('div', { class: 'eyebrow' }, 'Next playback'),
      h('div', { class: 'bigtime muted' }, '—'),
      h('div', { class: 'countdown muted' }, reason),
    ];
  }

  const w = st.program && st.program.window;
  let windowLine = null;
  if (w && st.armed) {
    windowLine = w.state === 'active'
      ? h('div', { class: 'window-line' }, h('span', { class: 'led ok' }), h('b', null, 'Active window'),
        h('span', { class: 'muted' }, `until ${fmtClock(w.until_local)} · ${w.event_id}`))
      : w.state === 'quiet'
        ? h('div', { class: 'window-line' }, h('span', { class: 'led' }), h('b', null, 'Quiet period'),
          h('span', { class: 'muted' }, `next window ${fmtClock(w.until_local)} ${dayLabel(w.until_local, today).toLowerCase()}`))
        : h('div', { class: 'window-line' }, h('span', { class: 'led' }), h('span', { class: 'muted' }, 'Point events only'));
  }

  const lp = st.last_playback;
  const prog = st.program;
  return h('div', { class: 'next' },
    h('div', { class: 'card' }, body, windowLine),
    h('div', { class: 'card stack' },
      h('div', null,
        h('div', { class: 'eyebrow' }, 'Active program'),
        prog && prog.loaded === false
          ? h('div', { class: 'row', style: { marginTop: '6px' } },
            h('b', { class: 'mono', style: { fontSize: '16px' } }, prog.id), badge('Not loaded', 'fault', 'alert'))
          : prog
            ? h('div', { class: 'row', style: { marginTop: '6px' } },
              h('a', { href: '#/programs/' + encodeURIComponent(prog.id), style: { fontWeight: 650, fontSize: '17px' } }, prog.name),
              prog.valid ? badge('Valid', 'ok', 'check') : badge('Invalid', 'fault', 'alert'))
            : h('div', { class: 'muted', style: { marginTop: '6px' } }, 'None — ', h('a', { href: '#/programs' }, 'choose a program')),
        prog && h('div', { class: 'muted small mono' }, prog.loaded === false ? 'Program file is on the SD card' : `${prog.id} · rev ${prog.revision}`)),
      h('hr', { class: 'divider', style: { margin: '2px 0' } }),
      h('div', null,
        h('div', { class: 'eyebrow' }, 'Last playback'),
        lp
          ? h('div', { style: { marginTop: '6px' } },
            h('div', { class: 'row' }, h('b', null, fmtDateTime(toLocalIso(Date.parse(lp.actual_utc), st.time.utc_offset_minutes))),
              badge(lp.source.replace('_', ' '), lp.source === 'scheduled' ? '' : 'ink'),
              lp.result !== 'ok' && badge(lp.result, 'warn')),
            h('div', { class: 'muted small' }, (lp.audio || '').replace('/audio/', ''), lp.event_id ? ` · ${lp.event_id}` : '', ` · vol ${lp.volume}`))
          : h('div', { class: 'muted', style: { marginTop: '6px' } }, st.sd.present ? 'No playback logged yet.' : 'Unavailable without SD card.')),
    ),
  );
}

function tiles(st) {
  const b = st.battery, sd = st.sd, env = st.environment, t = st.time;
  const bk = batteryKind(b.percent);
  const sdPct = sd.mounted ? (sd.used_bytes / sd.total_bytes) * 100 : 0;
  const sdKind = !sd.mounted ? 'fault' : sdPct > 95 ? 'fault' : sdPct > 80 ? 'warn' : 'ok';
  const envOk = env.ok !== false;

  return h('div', { class: 'grid' },
    h('div', { class: 'card tile' },
      h('div', { class: 'label eyebrow' }, 'Battery'),
      b.valid
        ? h('div', { class: 'value' }, b.voltage.toFixed(2), h('small', null, 'V'))
        : h('div', { class: 'value muted' }, '—'),
      b.valid && h('div', { class: `meter ${bk}` }, h('i', { style: { width: `${b.percent}%` } })),
      h('div', { class: 'foot' }, b.valid ? `≈ ${b.percent}% estimated · ${b.model}` : 'Reading invalid')),

    h('div', { class: 'card tile' },
      h('div', { class: 'label eyebrow' }, 'SD card'),
      sd.mounted
        ? h('div', { class: 'value' }, fmtBytes(sd.free_bytes), h('small', null, 'free'))
        : h('div', { class: 'value', style: { color: 'var(--fault)' } }, 'Not present'),
      sd.mounted && h('div', { class: `meter ${sdKind}` }, h('i', { style: { width: `${Math.max(1, sdPct)}%` } })),
      h('div', { class: 'foot' }, sd.mounted ? `${fmtBytes(sd.used_bytes)} of ${fmtBytes(sd.total_bytes)} used` : 'Scheduled playback blocked')),

    h('div', { class: 'card tile' },
      h('div', { class: 'label eyebrow' }, 'Environment'),
      envOk
        ? h('div', { class: 'value' }, env.temperature_c.toFixed(1), h('small', null, '°C'))
        : h('div', { class: 'value', style: { color: 'var(--warn)' } }, 'No reading'),
      envOk && h('div', { class: 'muted small num' }, `${env.relative_humidity_pct.toFixed(0)}% RH · ${env.ambient_light_lux.toLocaleString(undefined, { maximumFractionDigits: 0 })} lux`),
      h('a', { class: 'foot', href: '#/data' }, !env.logging_enabled ? 'Logging off' :
        st.next_env_sample_utc ? h('span', null, `Every ${env.interval_minutes} min · next `, h('span', { 'data-until': st.next_env_sample_utc })) : `Every ${env.interval_minutes} min`)),

    h('div', { class: 'card tile' },
      h('div', { class: 'label eyebrow' }, 'Device time'),
      t.rtc_valid
        ? h('div', { class: 'value', 'data-clock': '' }, fmtClock(t.local, { seconds: true }))
        : h('div', { class: 'value', style: { color: 'var(--fault)' } }, 'Not set'),
      h('div', { class: 'muted small num', 'data-utc': '' }, t.rtc_valid ? `${t.utc.slice(0, 10)} · ${fmtUtcClock(t.utc)}` : ''),
      h('a', { class: 'foot', href: '#/device' }, `Local ${fmtOffset(t.utc_offset_minutes)} · logs in UTC`)),

    h('div', { class: 'card tile' },
      h('div', { class: 'label eyebrow' }, 'Connection'),
      h('div', { class: 'value', style: { fontSize: '20px' } }, st.wifi.ssid),
      h('div', { class: 'muted small' }, `Open access point · ${st.wifi.clients} client${st.wifi.clients === 1 ? '' : 's'}`),
      h('div', { class: 'foot' }, `Firmware ${st.device.firmware_version} · up ${fmtDuration(st.uptime_s * 1000)}`)),
  );
}

export function mount(el) {
  el.append(skeleton([150, 180, 130]));
  let tick = null;

  function updateTicks() {
    const now = deviceNow();
    el.querySelectorAll('[data-until]').forEach((n) => {
      n.textContent = fmtRelative(Date.parse(n.dataset.until) - now);
    });
    const off = lastStatus ? lastStatus.time.utc_offset_minutes : 0;
    const clock = el.querySelector('[data-clock]');
    if (clock) clock.textContent = toLocalIso(now, off).slice(11, 19);
  }

  let lastStatus = null;
  return {
    onStatus(st) {
      lastStatus = st;
      replace(el, hero(st), warnings(st) || '', nextCard(st), h('div', { style: { height: '12px' } }), tiles(st));
      updateTicks();
      clearInterval(tick);
      tick = setInterval(updateTicks, 1000);
    },
    unmount() { clearInterval(tick); },
  };
}
