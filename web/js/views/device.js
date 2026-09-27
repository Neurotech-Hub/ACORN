// Device: identity, time, battery model, firmware (OTA) and diagnostics.

import { api } from '../api.js';
import { store, refresh, deviceNow } from '../store.js';
import {
  h, clear, replace, icon, badge, button, busy, field, pageHead, section, skeleton, errorState, toast, toastError,
  confirmDialog, confirmDiscard, notice, fmtBytes, fmtOffset, fmtDuration, toLocalIso, clone,
} from '../ui.js';

const OFFSETS = Array.from({ length: (840 + 720) / 15 + 1 }, (_, i) => -720 + i * 15);

export function mount(el) {
  let dev = null;
  let fw = null;
  const staged = { name: null, offset: null, model: null };
  const saved = { name: null, offset: null, model: null };
  const dirtyOf = (k) => JSON.stringify(staged[k]) !== JSON.stringify(saved[k]);
  const anyDirty = () => ['name', 'offset', 'model'].some(dirtyOf);

  const identityHost = h('div');
  const timeHost = h('div');
  const batteryHost = h('div');
  const firmwareHost = h('div');
  const diagHost = h('div');
  let clockEls = null;

  function saveRow(key, onSave) {
    const btn = button('Save & Apply', { kind: 'primary', iconName: 'check', disabled: !dirtyOf(key) });
    btn.onclick = () => busy(btn, onSave);
    const discard = dirtyOf(key) && button('Discard', {
      onclick: () => { staged[key] = clone(saved[key]); renderAll(); },
    });
    return h('div', { class: 'row', style: { justifyContent: 'flex-end' } }, discard, btn);
  }

  // ---- identity -------------------------------------------------------------------------
  function renderIdentity() {
    const name = h('input', { class: 'input mono', value: staged.name, maxlength: 32, autocapitalize: 'characters', spellcheck: 'false' });
    name.oninput = () => { staged.name = name.value.trim(); actions.replaceWith(actions = saveRow('name', saveName)); };
    let actions = saveRow('name', saveName);
    const copy = button('', {
      size: 'sm', kind: 'ghost', iconName: 'copy', title: 'Copy device ID',
      onclick: async () => { try { await navigator.clipboard.writeText(dev.id); toast('Device ID copied.', 'ok'); } catch { /* insecure context */ } },
    });
    replace(identityHost, h('div', { class: 'card form' },
      h('div', { class: 'form-row' },
        h('div', { class: 'field' }, h('div', { class: 'label' }, 'Device ID'),
          h('div', { class: 'row' }, h('span', { class: 'mono', style: { fontSize: '17px', fontWeight: 650 } }, dev.id), copy),
          h('div', { class: 'hint' }, 'Permanent, derived from the ESP32-S3 MAC. Also the Wi-Fi network name.')),
        field('Device name', name, { key: 'name', hint: 'Human/experimental role, e.g. SQUIRREL_SITE_07. Letters, digits, _ and -.' })),
      actions));
  }

  async function saveName() {
    try {
      dev = await api.updateDevice({ name: staged.name });
      saved.name = staged.name = dev.name;
      toast('Device name saved.', 'ok');
      refresh();
      renderIdentity();
    } catch (e) { toastError(e); }
  }

  // ---- time -----------------------------------------------------------------------------
  function renderTime() {
    const st = store.status;
    const rtcValid = st ? st.time.rtc_valid : true;
    const browserOffset = -new Date().getTimezoneOffset();
    const sel = h('select', { class: 'select', id: 'utc-offset' },
      OFFSETS.map((o) => h('option', { value: o, selected: o === staged.offset }, fmtOffset(o))));
    sel.onchange = () => { staged.offset = +sel.value; renderTime(); };

    clockEls = {
      local: h('div', { class: 'value num', style: { fontSize: '30px' } }),
      localDate: h('div', { class: 'muted small num' }),
      utc: h('dd', { class: 'mono' }),
      browser: h('dd', { class: 'mono' }),
      drift: h('dd'),
    };

    const syncBtn = button('Sync to this browser’s clock', { kind: rtcValid ? '' : 'primary', iconName: 'refresh' });
    syncBtn.onclick = async () => {
      const ok = await confirmDialog({
        title: 'Set device clock?',
        body: [
          h('p', null, 'The device RTC will be set to this browser’s current UTC time.'),
          h('p', { class: 'muted' }, 'The schedule and next wake are recalculated. Events skipped by a forward jump are not replayed.'),
        ],
        confirm: 'Set clock',
      });
      if (!ok) return;
      await busy(syncBtn, async () => {
        try {
          await api.setTime({ utc: new Date().toISOString() });
          toast('Device clock synchronized.', 'ok');
          await refresh();
          renderTime();
        } catch (e) { toastError(e); }
      });
    };

    const offsetDirty = dirtyOf('offset');
    replace(timeHost, 
      !rtcValid && notice('fault', 'Clock not set', 'Scheduled playback is blocked until the device clock is synchronized.'),
      !rtcValid && h('div', { style: { height: '12px' } }),
      h('div', { class: 'grid two' },
        h('div', { class: 'card stack' },
          h('div', { class: 'eyebrow' }, 'Device local time'),
          clockEls.local, clockEls.localDate,
          h('dl', { class: 'kv' },
            h('dt', null, 'Device UTC'), clockEls.utc,
            h('dt', null, 'This browser'), clockEls.browser,
            h('dt', null, 'Difference'), clockEls.drift),
          syncBtn),
        h('div', { class: 'card form' },
          field('UTC offset', sel, { hint: 'Fixed offset applied to program wall-clock times. No automatic daylight-saving changes.' }),
          browserOffset !== staged.offset && h('div', { class: 'row small' },
            h('span', { class: 'muted' }, `This browser is ${fmtOffset(browserOffset)}.`),
            OFFSETS.includes(browserOffset) && button('Use it', { size: 'sm', onclick: () => { staged.offset = browserOffset; renderTime(); } })),
          offsetDirty && notice('warn', null, `Program times will map to new UTC instants (e.g. 06:00 local → ${toLocalIso(Date.UTC(2000, 0, 1, 6, 0) - staged.offset * 60000, 0).slice(11, 16)}Z). Logs stay in UTC.`),
          saveRow('offset', saveOffset))),
    );
    tickClock();
  }

  async function saveOffset() {
    try {
      const t = await api.setTime({ utc_offset_minutes: staged.offset });
      saved.offset = staged.offset = t.utc_offset_minutes;
      toast(`UTC offset set to ${fmtOffset(t.utc_offset_minutes)}. Schedule recalculated.`, 'ok');
      await refresh();
      renderTime();
    } catch (e) { toastError(e); }
  }

  function tickClock() {
    if (!clockEls || !store.status) return;
    const now = deviceNow();
    const st = store.status;
    const local = toLocalIso(now, st.time.utc_offset_minutes);
    if (st.time.rtc_valid) {
      clockEls.local.textContent = local.slice(11, 19);
      clockEls.localDate.textContent = `${local.slice(0, 10)} · ${fmtOffset(st.time.utc_offset_minutes)}`;
    } else {
      clockEls.local.textContent = 'Not set';
      clockEls.local.style.color = 'var(--fault)';
      clockEls.localDate.textContent = '';
    }
    clockEls.utc.textContent = new Date(now).toISOString().slice(0, 19) + 'Z';
    clockEls.browser.textContent = new Date().toISOString().slice(0, 19) + 'Z';
    const drift = (now - Date.now()) / 1000;
    const abs = Math.abs(drift);
    replace(clockEls.drift, 
      abs < 2 ? badge('In sync', 'ok', 'check')
        : badge(abs > 86400 ? 'Way off' : `${abs < 60 ? abs.toFixed(1) + ' s' : fmtDuration(abs * 1000)} ${drift > 0 ? 'ahead' : 'behind'}`, abs > 30 ? 'fault' : 'warn'));
  }

  // ---- battery --------------------------------------------------------------------------
  function renderBattery() {
    const st = store.status;
    const b = st && st.battery;
    const pts = staged.model.points;
    const rows = pts.map((p, i) => {
      const v = h('input', { class: 'input num', type: 'number', step: 0.01, min: 0, value: p[0], 'aria-label': `Voltage ${i + 1}` });
      const pc = h('input', { class: 'input num', type: 'number', step: 1, min: 0, max: 100, value: p[1], 'aria-label': `Percent ${i + 1}` });
      v.oninput = () => { p[0] = parseFloat(v.value); refreshModelActions(); };
      pc.oninput = () => { p[1] = parseInt(pc.value, 10); refreshModelActions(); };
      return h('tr', null,
        h('td', null, v), h('td', null, pc),
        h('td', null, button('', {
          size: 'sm', kind: 'ghost', iconName: 'x', title: 'Remove point', disabled: pts.length <= 2,
          onclick: () => { pts.splice(i, 1); renderBattery(); },
        })));
    });
    let modelActions = saveRow('model', saveModel);
    function refreshModelActions() { const n = saveRow('model', saveModel); modelActions.replaceWith(n); modelActions = n; }

    replace(batteryHost, h('div', { class: 'grid two' },
      h('div', { class: 'card tile' },
        h('div', { class: 'eyebrow' }, 'Battery now'),
        b && b.valid ? h('div', { class: 'value' }, b.voltage.toFixed(2), h('small', null, 'V')) : h('div', { class: 'value muted' }, '—'),
        b && b.valid && h('div', { class: 'muted' }, `≈ ${b.percent}% estimated`),
        h('p', { class: 'foot' }, 'Voltage is the authoritative measurement; percent is an estimate from the model. BTN_02 flashes LED_R once per 10%.')),
      h('div', { class: 'card form' },
        h('div', { class: 'card-head', style: { margin: 0 } }, h('h3', null, 'Voltage → percent model'), h('span', { class: 'muted small' }, staged.model.name)),
        h('div', { class: 'table-wrap' }, h('table', { class: 'table' },
          h('thead', null, h('tr', null, h('th', null, 'Volts'), h('th', null, 'Percent'), h('th'))),
          h('tbody', null, rows))),
        h('div', { class: 'row' },
          button('Add point', {
            size: 'sm', iconName: 'plus', disabled: pts.length >= 16,
            onclick: () => { const last = pts[pts.length - 1]; pts.push([+(last[0] - 0.2).toFixed(2), Math.max(0, last[1] - 10)]); renderBattery(); },
          }),
          h('span', { class: 'small muted' }, 'Highest voltage first; 2–16 points.')),
        modelActions)));
  }

  async function saveModel() {
    try {
      const name = dirtyOf('model') && staged.model.name.includes('(default)') ? 'Custom' : staged.model.name;
      dev = await api.updateDevice({ battery_model: { name, points: staged.model.points } });
      saved.model = clone(dev.battery_model);
      staged.model = clone(dev.battery_model);
      toast('Battery model saved.', 'ok');
      refresh();
      renderBattery();
    } catch (e) { toastError(e); }
  }

  // ---- firmware -------------------------------------------------------------------------
  let ota = { phase: 'idle' };

  function renderFirmware() {
    const fileInput = h('input', { type: 'file', accept: '.bin,application/octet-stream', hidden: true });
    fileInput.onchange = () => { const f = fileInput.files[0]; fileInput.value = ''; if (f) startOta(f); };

    let otaBody;
    if (ota.phase === 'idle') {
      otaBody = [
        h('p', { class: 'small muted' }, 'Select an ACORN application image (.bin). It is written to the inactive slot, verified, and the device reboots into it. No internet is required.'),
        h('div', { class: 'row' }, button('Choose firmware file…', { iconName: 'upload', onclick: () => fileInput.click() })),
      ];
    } else if (ota.phase === 'uploading') {
      const pct = Math.round(ota.pct * 100);
      otaBody = [
        h('div', { class: 'row' }, h('b', null, ota.name), h('span', { class: 'spacer' }), h('span', { class: 'num' }, pct < 100 ? `${pct}%` : 'Verifying…')),
        h('div', { class: 'meter' }, h('i', { style: { width: `${pct}%` } })),
        notice('warn', 'Keep this page open', 'Stay connected and do not remove power until the update completes.'),
      ];
    } else if (ota.phase === 'rebooting') {
      otaBody = [
        h('div', { class: 'row' }, h('b', null, 'Installed. Rebooting…'), h('span', { class: 'spacer' }), h('span', { class: 'small muted num' }, `${ota.elapsed || 0} s`)),
        h('div', { class: 'meter' }, h('i', { style: { width: '100%' } })),
        h('p', { class: 'small muted' }, 'Waiting for ACORN to come back. If your phone drops the Wi-Fi, reconnect to ', h('b', null, dev.id), '.'),
      ];
    } else if (ota.phase === 'done') {
      otaBody = [notice('', `Firmware ${fw.version} is running`, `Booted from ${fw.running_partition}.`),
        h('div', { class: 'row' }, button('Done', { onclick: () => { ota = { phase: 'idle' }; renderFirmware(); } }))];
    } else if (ota.phase === 'error') {
      otaBody = [notice('fault', ota.error.message, ota.error.code),
        h('div', { class: 'row' }, button('Try again', { onclick: () => { ota = { phase: 'idle' }; renderFirmware(); } }))];
    }

    replace(firmwareHost, h('div', { class: 'grid two' },
      h('div', { class: 'card' },
        h('div', { class: 'eyebrow' }, 'Running firmware'),
        h('div', { class: 'value', style: { fontSize: '26px', fontWeight: 650, margin: '6px 0 10px' } }, fw.version),
        h('dl', { class: 'kv' },
          h('dt', null, 'Built'), h('dd', null, fw.build_utc),
          h('dt', null, 'Running slot'), h('dd', { class: 'mono' }, fw.running_partition),
          h('dt', null, 'Update slot'), h('dd', { class: 'mono' }, `${fw.next_partition} · ${fmtBytes(fw.slot_size_bytes)}`))),
      h('div', { class: 'card stack' }, h('h3', null, 'Update firmware'), otaBody, fileInput)));
  }

  async function startOta(file) {
    if (!/\.bin$/i.test(file.name)) { toast('Choose a .bin firmware image.', 'fault', { code: 'OTA_INVALID' }); return; }
    const ok = await confirmDialog({
      title: 'Install firmware?',
      body: [
        h('p', null, h('b', null, file.name), ` · ${fmtBytes(file.size)}`),
        h('p', { class: 'muted' }, 'The device will reboot after installation. Logs, programs and settings are kept. The schedule resumes after reboot; missed events are not replayed.'),
      ],
      confirm: 'Install',
    });
    if (!ok) return;
    ota = { phase: 'uploading', name: file.name, pct: 0 };
    renderFirmware();
    try {
      await api.uploadFirmware(file, (p) => { ota.pct = p; renderFirmware(); });
    } catch (e) {
      ota = { phase: 'error', error: e };
      renderFirmware();
      return;
    }
    ota = { phase: 'rebooting', elapsed: 0 };
    renderFirmware();
    const t0 = Date.now();
    await new Promise((r) => setTimeout(r, 1500));
    while (Date.now() - t0 < 120000) {
      try {
        fw = await api.firmware();
        ota = { phase: 'done' };
        toast(`Firmware ${fw.version} installed.`, 'ok');
        refresh();
        renderFirmware();
        return;
      } catch {
        ota.elapsed = Math.round((Date.now() - t0) / 1000);
        renderFirmware();
        await new Promise((r) => setTimeout(r, 1500));
      }
    }
    ota = { phase: 'error', error: { code: 'OTA_TIMEOUT', message: 'ACORN did not come back within 2 minutes. Reconnect to its Wi-Fi and reload this page.' } };
    renderFirmware();
  }

  // ---- diagnostics ----------------------------------------------------------------------
  function renderDiag() {
    const st = store.status;
    if (!st) return;
    replace(diagHost, h('div', { class: 'card' }, h('dl', { class: 'kv' },
      h('dt', null, 'Mode'), h('dd', null, st.mode.toUpperCase()),
      h('dt', null, 'Reset reason'), h('dd', { class: 'mono' }, st.reset_reason),
      h('dt', null, 'Wake reason'), h('dd', { class: 'mono' }, st.wake_reason),
      h('dt', null, 'Uptime'), h('dd', null, fmtDuration(st.uptime_s * 1000)),
      h('dt', null, 'Wi-Fi'), h('dd', null, `${st.wifi.ssid} · ${st.wifi.security} · closes after ${Math.round(st.wifi.idle_timeout_s / 60)} min idle`),
      h('dt', null, 'SD card'), h('dd', null, st.sd.mounted ? `Mounted · ${fmtBytes(st.sd.total_bytes)}` : 'Not present'))));
  }

  function renderAll() {
    renderIdentity();
    renderTime();
    renderBattery();
    renderFirmware();
    renderDiag();
  }

  async function load() {
    replace(el, pageHead('Device'), skeleton([140, 200, 200]));
    try {
      [dev, fw] = await Promise.all([api.device(), api.firmware()]);
      const st = store.status || (await refresh());
      saved.name = staged.name = dev.name;
      saved.offset = staged.offset = st.time.utc_offset_minutes;
      saved.model = clone(dev.battery_model);
      staged.model = clone(dev.battery_model);
      replace(el, 
        pageHead('Device', 'Identity, clock, power and firmware.'),
        section('Identity'), identityHost,
        section('Time'), timeHost,
        section('Battery'), batteryHost,
        section('Firmware'), firmwareHost,
        section('Diagnostics'), diagHost,
      );
      renderAll();
    } catch (e) {
      replace(el, pageHead('Device'), errorState(e, load));
    }
  }

  load();
  const tick = setInterval(tickClock, 1000);
  let lastRtc = null;
  return {
    onStatus(st) {
      renderDiag();
      if (dev && lastRtc !== null && lastRtc !== st.time.rtc_valid) renderTime();
      lastRtc = st.time.rtc_valid;
      if (dev && !dirtyOf('model') && !batteryHost.contains(document.activeElement)) renderBattery();
    },
    isDirty: anyDirty,
    canLeave: async () => !anyDirty() || confirmDiscard(),
    unmount: () => clearInterval(tick),
  };
}
