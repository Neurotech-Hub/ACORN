// Audio: field test player (first-class, no program editing needed) and the WAV library.

import { api } from '../api.js';
import { store, refresh } from '../store.js';
import {
  h, clear, replace, icon, badge, button, busy, pageHead, section, skeleton, toast, toastError, confirmDialog,
  notice, fmtBytes, fmtDuration, fmtClockMs,
} from '../ui.js';

const TONE = '__tone';
const prefs = { source: null, volume: 80 };

export function mount(el) {
  let files = null;
  let rules = null;
  let libError = null;
  let uploading = null;

  const rings = h('div', { class: 'rings', 'aria-hidden': 'true' }, h('i'), h('i'), h('i'));
  const sourceSel = h('select', { class: 'select', id: 'audio-source' });
  const volOut = h('output', { for: 'audio-volume' }, prefs.volume);
  const volRange = h('input', { id: 'audio-volume', type: 'range', min: 0, max: 100, step: 1, value: prefs.volume });
  const playBtn = button('Play', { kind: 'primary', size: 'lg', iconName: 'play' });
  const stopBtn = button('Stop', { size: 'lg', iconName: 'stop' });
  const progress = h('div', { class: 'meter' }, h('i', { style: { width: '0%' } }));
  const progressText = h('div', { class: 'progress-line' });
  const nowLine = h('div', { class: 'small muted' });
  const libHost = h('div');
  const fileInput = h('input', { type: 'file', accept: '.wav,audio/wav,audio/x-wav', hidden: true });

  volRange.oninput = () => { prefs.volume = +volRange.value; volOut.value = volRange.value; };
  sourceSel.onchange = () => { prefs.source = sourceSel.value; };

  playBtn.onclick = () => busy(playBtn, async () => {
    const src = sourceSel.value;
    try {
      await api.playTest(src === TONE ? { source: 'tone', volume: prefs.volume } : { source: 'file', file: src, volume: prefs.volume });
      await refresh();
    } catch (e) { toastError(e); }
  });
  stopBtn.onclick = () => busy(stopBtn, async () => {
    try { await api.stop(); await refresh(); } catch (e) { toastError(e); }
  });

  function renderSources() {
    const valid = (files || []).filter((f) => f.valid);
    if (files && (!prefs.source || (prefs.source !== TONE && !valid.some((f) => f.name === prefs.source)))) {
      prefs.source = valid.find((f) => f.name === 'playback.wav') ? 'playback.wav' : valid[0]?.name || TONE;
    }
    replace(sourceSel, 
      valid.length > 0 && h('optgroup', { label: 'SD card' }, valid.map((f) =>
        h('option', { value: f.name, selected: prefs.source === f.name }, `${f.name} · ${fmtDuration(f.duration_ms)}`))),
      h('optgroup', { label: 'Built-in' }, h('option', { value: TONE, selected: !prefs.source || prefs.source === TONE }, 'Test tone (sine, no SD needed)')));
  }

  function renderPlayback(st) {
    const pb = st && st.playback;
    const playing = pb && pb.state === 'playing';
    rings.classList.toggle('playing', !!playing);
    stopBtn.disabled = !playing;
    if (playing) {
      const pct = Math.min(100, (pb.position_ms / pb.duration_ms) * 100);
      progress.firstChild.style.width = `${pct}%`;
      progressText.replaceChildren(h('span', null, fmtClockMs(pb.position_ms)), h('span', null, fmtClockMs(pb.duration_ms)));
      nowLine.replaceChildren(h('b', null, 'Playing '), (pb.audio || 'built-in tone').replace('/audio/', ''), ` at volume ${pb.volume}`);
    } else {
      progress.firstChild.style.width = '0%';
      progressText.replaceChildren(h('span', null, '0:00'), h('span', null, '—'));
      nowLine.replaceChildren('Idle. Manual plays are logged as ', h('code', null, 'manual_ui'), ' and never shift the schedule.');
    }
  }

  function selectForTest(name) {
    prefs.source = name;
    renderSources();
    window.scrollTo({ top: 0, behavior: 'smooth' });
    sourceSel.focus({ preventScroll: true });
  }

  async function remove(f) {
    const others = f.referenced_by.filter(Boolean);
    const ok = await confirmDialog({
      title: `Delete ${f.name}?`,
      body: [
        h('p', null, `This permanently removes ${f.path} from the SD card.`),
        others.length > 0 && h('p', { style: { color: 'var(--warn)' } }, `Referenced by: ${others.join(', ')}. Those programs will fail validation until updated.`),
      ],
      confirm: 'Delete', danger: true,
    });
    if (!ok) return;
    try {
      await api.deleteAudio(f.name);
      toast(`Deleted ${f.name}.`, 'ok');
      load();
    } catch (e) { toastError(e); }
  }

  async function doUpload(file) {
    if (!file) return;
    if (!/\.wav$/i.test(file.name)) { toast('Only .wav files are supported.', 'fault', { code: 'WAV_UNSUPPORTED' }); return; }
    const exists = files && files.find((f) => f.name === file.name.toLowerCase());
    if (exists) {
      const ok = await confirmDialog({
        title: `Replace ${exists.name}?`,
        body: exists.in_active_program ? 'This file is used by the active program. The new file must still fit its schedule.' : 'The existing file will be overwritten.',
        confirm: 'Replace',
      });
      if (!ok) return;
    }
    uploading = { name: file.name, pct: 0 };
    renderLibrary();
    try {
      const res = await api.uploadAudio(file, (p) => { uploading.pct = p; renderUploadProgress(); });
      toast(`Uploaded ${res.name} · ${fmtDuration(res.duration_ms)}`, 'ok');
      uploading = null;
      load();
    } catch (e) {
      uploading = null;
      renderLibrary();
      toastError(e);
    }
  }
  fileInput.onchange = () => { const f = fileInput.files[0]; fileInput.value = ''; doUpload(f); };

  let progressEl = null;
  function renderUploadProgress() {
    if (!progressEl || !uploading) return;
    const pct = Math.round(uploading.pct * 100);
    progressEl.replaceChildren(
      h('div', { class: 'row' }, h('b', null, uploading.name), h('span', { class: 'spacer' }), h('span', { class: 'num' }, pct < 100 ? `${pct}%` : 'Validating…')),
      h('div', { class: 'meter' }, h('i', { style: { width: `${pct}%` } })));
  }

  function dropzone() {
    const dz = h('div', { class: 'dropzone', role: 'button', tabindex: 0 },
      icon('upload', 'lg'),
      h('div', null, h('b', null, 'Upload WAV'), ' — tap to choose or drop a file'),
      rules && h('div', { class: 'small' }, `${rules.bits_per_sample}-bit linear PCM · mono · ${rules.sample_rates.map((r) => r / 1000 + ' kHz').join(' or ')}`));
    dz.onclick = () => fileInput.click();
    dz.onkeydown = (e) => { if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); fileInput.click(); } };
    dz.ondragover = (e) => { e.preventDefault(); dz.classList.add('over'); };
    dz.ondragleave = () => dz.classList.remove('over');
    dz.ondrop = (e) => { e.preventDefault(); dz.classList.remove('over'); doUpload(e.dataTransfer.files[0]); };
    return dz;
  }

  function fileRow(f) {
    const meta = f.valid || f.sample_rate
      ? `${fmtDuration(f.duration_ms)} · ${f.sample_rate / 1000} kHz · ${f.bits_per_sample}-bit ${f.channels === 1 ? 'mono' : f.channels === 2 ? 'stereo' : f.channels + 'ch'} · ${fmtBytes(f.size_bytes)}`
      : fmtBytes(f.size_bytes);
    const selected = prefs.source === f.name;
    return h('li', { class: selected ? 'selected' : '' },
      h('div', { class: 'main' },
        h('div', { class: 'title' }, f.name,
          f.in_active_program && badge('Active program', 'ink'),
          !f.in_active_program && f.referenced_by.length > 0 && badge(`Used by ${f.referenced_by.length}`),
          !f.valid && badge(f.error.code === 'WAV_CORRUPT' ? 'Corrupt' : 'Unsupported', 'fault', 'alert')),
        h('div', { class: 'meta num' }, meta),
        !f.valid && h('div', { class: 'meta', style: { color: 'var(--fault)' } }, f.error.message)),
      h('div', { class: 'actions' },
        f.valid && button(selected ? 'Selected' : 'Test', { size: 'sm', iconName: 'play', onclick: () => selectForTest(f.name), disabled: selected }),
        button('', {
          size: 'sm', kind: 'ghost danger', iconName: 'trash',
          title: f.in_active_program ? 'Used by the active program' : 'Delete',
          disabled: f.in_active_program, onclick: () => remove(f),
        })));
  }

  function renderLibrary() {
    clear(libHost);
    if (libError) {
      libHost.append(notice('fault', libError.code === 'SD_NOT_PRESENT' ? 'SD card not available' : libError.message,
        libError.code === 'SD_NOT_PRESENT' ? 'The WAV library is on the SD card. The built-in test tone still works.' : libError.code));
      return;
    }
    if (!files) { libHost.append(skeleton([90, 200])); return; }
    progressEl = uploading ? h('div', { class: 'upload-progress card' }) : null;
    libHost.append(
      uploading ? progressEl : dropzone(),
      h('div', { style: { height: '12px' } }),
      files.length
        ? h('div', { class: 'card flush' }, h('ul', { class: 'list' }, files.map(fileRow)))
        : h('div', { class: 'card empty' }, 'No WAV files in /audio/.'));
    renderUploadProgress();
  }

  async function load() {
    try {
      const res = await api.audio();
      files = res.files;
      rules = res.rules;
      libError = null;
    } catch (e) {
      files = [];
      libError = e;
    }
    renderSources();
    renderLibrary();
  }

  replace(el, 
    pageHead('Audio', 'Test playback on the device speaker, and manage WAV files on the SD card.'),
    h('div', { class: 'card player' },
      rings,
      h('div', { class: 'player-controls' },
        h('div', { class: 'field' }, h('label', { for: 'audio-source' }, 'Source'), sourceSel),
        h('div', { class: 'field' }, h('label', { for: 'audio-volume' }, 'Volume'), h('div', { class: 'range-wrap' }, volRange, volOut),
          h('div', { class: 'hint' }, 'Device volume units 0–100 (not calibrated SPL).')),
        h('div', { class: 'transport' }, playBtn, stopBtn),
        h('div', null, progress, progressText),
        nowLine)),
    section('Library · /audio/'),
    libHost,
    fileInput,
  );
  renderSources();
  renderLibrary();
  renderPlayback(store.status);
  load();

  let tick = null;
  return {
    onStatus(st) {
      renderPlayback(st);
      clearInterval(tick);
      if (st.playback && st.playback.state === 'playing') {
        const t0 = performance.now(), base = st.playback.position_ms;
        tick = setInterval(() => {
          renderPlayback({ playback: { ...st.playback, position_ms: Math.min(st.playback.duration_ms, base + performance.now() - t0) } });
        }, 200);
      }
    },
    unmount() { clearInterval(tick); },
  };
}
