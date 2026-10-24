// Device: identity, time, site location, battery model, firmware (OTA) and diagnostics.

import { api } from '../api.js';
import { store, refresh, deviceNow } from '../store.js';
import {
  h, svg, clear, replace, icon, badge, button, busy, field, pageHead, section, skeleton, errorState, toast, toastError,
  confirmDialog, confirmDiscard, notice, fmtBytes, fmtOffset, fmtDuration, toLocalIso, clone, batteryKind,
} from '../ui.js';

const OFFSETS = Array.from({ length: (840 + 720) / 15 + 1 }, (_, i) => -720 + i * 15);

export function mount(el) {
  let dev = null;
  let fw = null;
  const staged = { name: null, offset: null, site: null };
  const saved = { name: null, offset: null, site: null };
  const dirtyOf = (k) => JSON.stringify(staged[k]) !== JSON.stringify(saved[k]);
  const anyDirty = () => ['name', 'offset', 'site'].some(dirtyOf);

  const identityHost = h('div');
  const timeHost = h('div');
  const siteHost = h('div');
  let site = null;  // GET /site data, or null when it could not be read
  let siteError = null;
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

  // ---- site location (/config/site.json) ------------------------------------------------
  const SITE_SOURCES = { phone_gps: 'Phone GPS', manual: 'Manual entry', prestage: 'Prestaged on SD' };

  // Staged form values: coordinates as typed text so partial input is kept while editing.
  const siteForm = (s) => (s && s.set
    ? { latitude: String(s.latitude), longitude: String(s.longitude), accuracy_m: s.accuracy_m, source: s.source, note: s.note || '' }
    : { latitude: '', longitude: '', accuracy_m: null, source: null, note: '' });

  const coord = (text, limit) => {
    const t = text.trim();
    if (!/^[-+]?\d+(\.\d+)?$/.test(t)) return null;
    const v = +t;
    return Math.abs(v) <= limit ? v : null;
  };

  function locate(btn) {
    return busy(btn, () => new Promise((resolve) => {
      navigator.geolocation.getCurrentPosition((pos) => {
        const { latitude, longitude, accuracy } = pos.coords;
        staged.site = {
          ...staged.site, latitude: latitude.toFixed(6), longitude: longitude.toFixed(6),
          accuracy_m: Number.isFinite(accuracy) ? Math.round(accuracy * 10) / 10 : null, source: 'phone_gps',
        };
        toast(`Location found (±${Math.round(accuracy)} m). Save & Apply to store it.`, 'ok');
        renderSite();
        resolve();
      }, (e) => {
        const msg = e.code === e.PERMISSION_DENIED ? 'Location permission was denied. Enter coordinates manually.'
          : e.code === e.TIMEOUT ? 'No GPS fix yet. Move to open sky and try again, or enter coordinates manually.'
            : 'Location is unavailable on this phone. Enter coordinates manually.';
        toast(msg, 'fault');
        resolve();
      }, { enableHighAccuracy: true, timeout: 30000, maximumAge: 0 });
    }));
  }

  function renderSite() {
    if (siteError) {
      replace(siteHost, notice('warn', 'Site location unavailable', siteError));
      return;
    }
    if (!site) return;
    const s = staged.site;
    const geoOk = !!navigator.geolocation && window.isSecureContext;
    const lat = h('input', { class: 'input mono', value: s.latitude, inputmode: 'decimal', placeholder: '45.123456', autocomplete: 'off' });
    const lon = h('input', { class: 'input mono', value: s.longitude, inputmode: 'decimal', placeholder: '-93.654321', autocomplete: 'off' });
    const note = h('input', { class: 'input', value: s.note, maxlength: 120, placeholder: 'Optional, e.g. north plot' });
    const refreshActions = () => { actions.replaceWith(actions = siteActions()); };
    const typed = (key, input) => () => {
      // Typing a coordinate replaces a phone fix: its accuracy no longer describes the value.
      staged.site = { ...staged.site, [key]: input.value, ...(key === 'note' ? {} : { accuracy_m: null, source: 'manual' }) };
      refreshActions();
    };
    lat.oninput = typed('latitude', lat);
    lon.oninput = typed('longitude', lon);
    note.oninput = typed('note', note);

    function siteActions() {
      const cur = staged.site;
      const valid = coord(cur.latitude, 90) !== null && coord(cur.longitude, 180) !== null;
      const row = saveRow('site', saveSite);
      const save = row.lastChild;
      save.disabled = !dirtyOf('site') || !valid;
      return row;
    }
    let actions = siteActions();

    const locBtn = button('Use phone location', { iconName: 'pin', disabled: !geoOk });
    locBtn.onclick = () => locate(locBtn);
    const clearBtn = site.set && button('Clear', { kind: 'ghost', size: 'sm', iconName: 'trash' });
    if (clearBtn) clearBtn.onclick = () => clearSite(clearBtn);

    const summary = site.set
      ? h('dl', { class: 'kv' },
        h('dt', null, 'Latitude'), h('dd', { class: 'mono' }, site.latitude.toFixed(6)),
        h('dt', null, 'Longitude'), h('dd', { class: 'mono' }, site.longitude.toFixed(6)),
        h('dt', null, 'Accuracy'), h('dd', null, site.accuracy_m != null ? `±${site.accuracy_m} m` : '—'),
        h('dt', null, 'Source'), h('dd', null, SITE_SOURCES[site.source] || '—'),
        h('dt', null, 'Fixed'), h('dd', { class: 'mono' }, site.fixed_utc || '—'),
        site.note && h('dt', null, 'Note'), site.note && h('dd', null, site.note))
      : h('p', { class: 'muted' }, 'Not set. Log rows are written with empty latitude/longitude cells; playback is not affected.');

    replace(siteHost, h('div', { class: 'grid two' },
      h('div', { class: 'card stack', style: { alignContent: 'start' } },
        h('div', { class: 'card-head', style: { marginBottom: 0 } },
          h('div', { class: 'eyebrow' }, 'Stored on SD card'), clearBtn && h('div', { class: 'actions' }, clearBtn)),
        summary,
        h('p', { class: 'muted small' }, 'Stamped into every new playback and environment log row. Earlier rows keep the location that was current when they were written.')),
      h('div', { class: 'card form' },
        h('div', { class: 'stack', style: { gap: '6px' } },
          h('div', { class: 'row' }, locBtn,
            staged.site.source === 'phone_gps' && staged.site.accuracy_m != null && dirtyOf('site') &&
              h('span', { class: 'muted small' }, `±${staged.site.accuracy_m} m fix staged`)),
          h('div', { class: 'muted small' }, geoOk
            ? 'Phone GPS works without internet but needs open sky; the first fix can take a minute.'
            : 'This browser only shares location over a secure connection, which the device Wi-Fi is not. Read the coordinates from your phone’s maps or compass app and enter them below.')),
        h('div', { class: 'form-row' },
          field('Latitude', lat, { key: 'latitude', hint: 'Decimal degrees, -90 to 90 (north positive).' }),
          field('Longitude', lon, { key: 'longitude', hint: 'Decimal degrees, -180 to 180 (east positive).' })),
        field('Note', note),
        actions)));
  }

  async function saveSite() {
    const s = staged.site;
    const body = { latitude: coord(s.latitude, 90), longitude: coord(s.longitude, 180), source: s.source || 'manual', note: s.note.trim() };
    if (s.accuracy_m != null) body.accuracy_m = s.accuracy_m;
    try {
      site = await api.updateSite(body);
      saved.site = staged.site = siteForm(site);
      toast('Site location saved. New log rows carry it.', 'ok');
      refresh();
      renderSite();
    } catch (e) { toastError(e); }
  }

  async function clearSite(btn) {
    const ok = await confirmDialog({
      title: 'Clear site location?',
      body: [
        h('p', null, 'Deletes /config/site.json from the SD card.'),
        h('p', { class: 'muted' }, 'New log rows will have empty latitude/longitude. Existing rows are not changed.'),
      ],
      confirm: 'Clear', danger: true,
    });
    if (!ok) return;
    await busy(btn, async () => {
      try {
        site = await api.clearSite();
        saved.site = staged.site = siteForm(site);
        toast('Site location cleared.', 'ok');
        refresh();
        renderSite();
      } catch (e) { toastError(e); }
    });
  }

  async function loadSite() {
    try {
      site = await api.getSite();
      siteError = null;
    } catch (e) {
      site = null;
      siteError = e.code === 'SD_NOT_PRESENT' || e.code === 'SD_MOUNT_FAILED'
        ? 'The site location is stored on the SD card. Insert a card to view or set it.'
        : e.message;
    }
    saved.site = staged.site = siteForm(site);
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
  /** Discharge curve: voltage (y) against charge remaining, 100% at left to 0% at right. */
  function batteryPlot(points, b) {
    const W = 320, H = 180, L = 38, R = 12, T = 14, B = 28;
    const volts = points.map((p) => p[0]).concat(b && b.valid ? [b.voltage] : []);
    const vMin = Math.floor(Math.min(...volts) * 2) / 2;
    const vMax = Math.ceil(Math.max(...volts) * 2) / 2;
    const x = (pct) => L + ((100 - pct) / 100) * (W - L - R);
    const y = (v) => T + ((vMax - v) / (vMax - vMin || 1)) * (H - T - B);

    const grid = [];
    for (let v = vMin; v <= vMax + 1e-9; v += 0.5) {
      grid.push(svg('line', { class: 'grid', x1: L, x2: W - R, y1: y(v), y2: y(v) }),
        svg('text', { class: 'tick', x: L - 6, y: y(v) + 3.5, 'text-anchor': 'end' }, document.createTextNode(v.toFixed(1))));
    }
    for (const pct of [100, 75, 50, 25, 0]) {
      grid.push(svg('text', { class: 'tick', x: x(pct), y: H - B + 14, 'text-anchor': pct === 100 ? 'start' : pct === 0 ? 'end' : 'middle' },
        document.createTextNode(`${pct}%`)));
    }

    const curve = svg('polyline', { class: 'curve', points: points.map(([v, p]) => `${x(p)},${y(v)}`).join(' ') });
    let now = null;
    if (b && b.valid) {
      const cx = x(b.percent), cy = y(b.voltage);
      const anchor = b.percent > 70 ? 'start' : b.percent < 30 ? 'end' : 'middle';
      now = svg('g', { class: `now ${batteryKind(b.percent)}` },
        svg('line', { x1: cx, x2: cx, y1: T, y2: H - B }),
        svg('circle', { cx, cy, r: 4.5 }),
        svg('text', { x: cx + (anchor === 'start' ? 6 : anchor === 'end' ? -6 : 0), y: T - 3, 'text-anchor': anchor },
          document.createTextNode(`Now · ${b.voltage.toFixed(2)} V · ${b.percent}%`)));
    }

    return svg('svg', { class: 'batt-plot', viewBox: `0 0 ${W} ${H}`, role: 'img',
      'aria-label': b && b.valid ? `Battery at ${b.voltage.toFixed(2)} volts, about ${b.percent} percent` : 'Battery voltage model' },
    grid, svg('line', { class: 'axis', x1: L, x2: W - R, y1: H - B, y2: H - B }), curve, now);
  }

  function renderBattery() {
    const st = store.status;
    const b = st && st.battery;
    const model = dev.battery_model;
    const top = model.points[0], bottom = model.points[model.points.length - 1];

    replace(batteryHost, h('div', { class: 'grid two' },
      h('div', { class: 'card tile' },
        h('div', { class: 'eyebrow' }, 'Battery now'),
        b && b.valid ? h('div', { class: 'value' }, b.voltage.toFixed(2), h('small', null, 'V')) : h('div', { class: 'value muted' }, '—'),
        b && b.valid && h('div', { class: 'muted' }, `≈ ${b.percent}% estimated`),
        h('p', { class: 'foot' }, 'Voltage is the authoritative measurement; percent is an estimate from the model. BTN_02 flashes LED_R once per 10%.')),
      h('div', { class: 'card' },
        h('div', { class: 'card-head', style: { marginBottom: '8px' } }, h('h3', null, 'Discharge model'), h('span', { class: 'muted small' }, model.name)),
        batteryPlot(model.points, b),
        h('p', { class: 'muted small', style: { marginTop: '6px' } },
          `${top[0].toFixed(1)} V = ${top[1]}% down to ${bottom[0].toFixed(1)} V = ${bottom[1]}%. Fixed in firmware.`))));
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
    renderSite();
    renderBattery();
    renderFirmware();
    renderDiag();
  }

  async function load() {
    replace(el, pageHead('Device'), skeleton([140, 200, 200]));
    try {
      [dev, fw] = await Promise.all([api.device(), api.firmware(), loadSite()]);
      const st = store.status || (await refresh());
      saved.name = staged.name = dev.name;
      saved.offset = staged.offset = st.time.utc_offset_minutes;
      replace(el, 
        pageHead('Device'),
        section('Identity'), identityHost,
        section('Time'), timeHost,
        section('Site location'), siteHost,
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
  let lastSd = null;
  return {
    onStatus(st) {
      renderDiag();
      if (dev && lastRtc !== null && lastRtc !== st.time.rtc_valid) renderTime();
      lastRtc = st.time.rtc_valid;
      // SD inserted or removed: reload the site unless the operator has unsaved edits.
      if (dev && lastSd !== null && lastSd !== st.sd.mounted && !dirtyOf('site')) loadSite().then(renderSite);
      lastSd = st.sd.mounted;
      if (dev) renderBattery();
    },
    isDirty: anyDirty,
    canLeave: async () => !anyDirty() || confirmDiscard(),
    unmount: () => clearInterval(tick),
  };
}
