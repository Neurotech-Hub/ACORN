// Audio converter: any browser-decodable file -> ACORN WAV (16-bit PCM, mono, 44.1/48 kHz),
// with channel selection, trimming and preview. Everything runs locally.

import {
  h, replace, icon, button, setButton, busy, badge, notice, toast, initTheme, onThemeChange,
  fmtBytes, fmtTime, parseTime, fmtRate, fmtDb, downloadBlob,
} from '../shared/js/ui-helpers.js';
import { ACORN_WAV, encodeWav, wavByteLength } from '../shared/js/wav-encoder.js';
import {
  readWavInfo, isAcornCompatible, pickSampleRate, decodeAudio, channelsOf, mixToMono,
  trimRange, measure, stereoAdvice, outputName, cleanFileName,
} from '../shared/js/audio-processor.js';
import { createWaveform } from '../shared/js/waveform.js';

const DEVICE_UPLOAD_LIMIT = 16 * 1024 * 1024;  // firmware app::MAX_UPLOAD_BYTES
const LARGE_FILE = 50 * 1024 * 1024;
const ACCEPT = 'audio/*,.wav,.wave,.mp3,.m4a,.aac,.flac,.ogg,.oga,.opus,.aif,.aiff,.caf,.webm';
const MODE_LABELS = { combine: 'Combine', left: 'Left', right: 'Right' };

initTheme();

const state = {
  file: null,
  bytes: null,
  header: null,
  loading: false,
  error: null,
  rate: 44100,
  mode: 'combine',
  channels: [],
  mono: null,
  trimStart: 0,
  trimEnd: 0,
  fileName: '',
};

// ---- Elements ---------------------------------------------------------------------------

const view = document.getElementById('view');
const fileInput = h('input', { type: 'file', accept: ACCEPT, hidden: true });

const sourceBody = h('div');
const waveform = createWaveform({ onTrim: (s, e) => setTrim(s, e, { fromWave: true }) });

const startInput = h('input', { class: 'input mono', id: 'trim-start', inputmode: 'decimal', autocomplete: 'off', spellcheck: 'false' });
const endInput = h('input', { class: 'input mono', id: 'trim-end', inputmode: 'decimal', autocomplete: 'off', spellcheck: 'false' });
const lengthInput = h('input', { class: 'input mono', id: 'trim-length', readonly: true, tabindex: -1 });
const resetTrimBtn = button('Select all', { size: 'sm', kind: 'ghost', iconName: 'refresh' });

const playBtn = button('Play preview', { kind: 'primary', size: 'lg', iconName: 'play' });
const stopBtn = button('Stop', { size: 'lg', iconName: 'stop', disabled: true });
const progress = h('div', { class: 'meter' }, h('i', { style: { width: '0%' } }));
const progressText = h('div', { class: 'progress-line' });
const volOut = h('output', { for: 'preview-volume' }, '50');
const volRange = h('input', { id: 'preview-volume', type: 'range', min: 0, max: 100, step: 1, value: 50 });

const modeButtons = Object.fromEntries(Object.entries(MODE_LABELS).map(([mode, label]) =>
  [mode, h('button', { type: 'button', role: 'radio', 'aria-checked': 'false', dataset: { mode } }, label)]));
const modeSeg = h('div', { class: 'seg', role: 'radiogroup', 'aria-label': 'Channels' }, Object.values(modeButtons));
const modeHint = h('div', { class: 'hint' });
const rateSelect = h('select', { class: 'select', id: 'out-rate' },
  ACORN_WAV.sampleRates.map((r) => h('option', { value: r }, fmtRate(r))));
const rateHint = h('div', { class: 'hint' });
const nameInput = h('input', { class: 'input mono', id: 'out-name', autocomplete: 'off', spellcheck: 'false' });
const outNotices = h('div', { class: 'notices' });
const summary = h('dl', { class: 'kv' });
const exportBtn = button('Download WAV', { kind: 'primary', size: 'lg', iconName: 'download' });

const trimCard = h('section', { class: 'card disabled', 'aria-labelledby': 'trim-title' },
  h('div', { class: 'card-head' }, h('span', { class: 'step' }, '02'), h('h3', { id: 'trim-title' }, 'Trim & preview'),
    h('div', { class: 'actions' }, resetTrimBtn)),
  h('div', { class: 'form' },
    h('div', null, waveform.el,
      h('div', { class: 'hint small muted', style: { marginTop: '6px' } },
        'Drag the handles, or press on the waveform to move the nearest one. Arrow keys nudge a focused handle by 10 ms (Shift: 100 ms).')),
    h('div', { class: 'form-row' },
      h('div', { class: 'field' }, h('label', { for: 'trim-start' }, 'Start'), startInput),
      h('div', { class: 'field' }, h('label', { for: 'trim-end' }, 'End'), endInput),
      h('div', { class: 'field' }, h('label', { for: 'trim-length' }, 'Length'), lengthInput)),
    h('div', { class: 'transport' }, playBtn, stopBtn),
    h('div', null, progress, progressText),
    h('div', { class: 'field' }, h('label', { for: 'preview-volume' }, 'Preview volume'),
      h('div', { class: 'range-wrap' }, volRange, volOut),
      h('div', { class: 'hint' }, 'Only changes what you hear here. The exported file keeps its original level.'))));

const outputCard = h('section', { class: 'card disabled', 'aria-labelledby': 'out-title' },
  h('div', { class: 'card-head' }, h('span', { class: 'step' }, '03'), h('h3', { id: 'out-title' }, 'Output')),
  h('div', { class: 'form' },
    h('div', { class: 'form-row' },
      h('div', { class: 'field' }, h('span', { class: 'label' }, 'Channels'), modeSeg, modeHint),
      h('div', { class: 'field' }, h('label', { for: 'out-rate' }, 'Sample rate'), rateSelect, rateHint)),
    h('div', { class: 'field' }, h('label', { for: 'out-name' }, 'File name'), nameInput,
      h('div', { class: 'hint' }, 'The device keeps this name on upload. Name it ', h('code', null, 'playback.wav'),
        ' to use it as the BTN_01 fallback sound.')),
    outNotices,
    summary,
    h('div', { class: 'export-bar' }, exportBtn)));

const sourceCard = h('section', { class: 'card', 'aria-labelledby': 'source-title' },
  h('div', { class: 'card-head' }, h('span', { class: 'step' }, '01'), h('h3', { id: 'source-title' }, 'Source file')),
  sourceBody);

replace(view,
  h('a', { class: 'back', href: '../' }, icon('back', 'sm'), 'All utilities'),
  h('div', { class: 'page-head' },
    h('h1', null, 'Audio converter'),
    h('p', { class: 'sub' }, 'Turn any audio file into a WAV that ACORN can play. Trim it, pick the channel, preview it, then download.')),
  h('div', { class: 'row', style: { marginBottom: '16px' } },
    badge('16-bit PCM'), badge('Mono'), badge('44.1 or 48 kHz'), badge('WAV')),
  h('div', { class: 'stack' }, sourceCard, trimCard, outputCard),
  fileInput);

// ---- Source -----------------------------------------------------------------------------

function describeType(file, header) {
  if (header) {
    const depth = header.bitsPerSample ? `${header.bitsPerSample}-bit ` : '';
    return `WAV · ${depth}${header.formatName}`;
  }
  const ext = /\.([^.]+)$/.exec(file.name)?.[1];
  return ext ? ext.toUpperCase() : file.type || 'Unknown';
}

function channelLabel(n) {
  return n === 1 ? 'Mono' : n === 2 ? 'Stereo' : `${n} channels`;
}

function dropzone(message) {
  const dz = h('div', { class: 'dropzone', role: 'button', tabindex: 0 },
    icon('upload', 'lg'),
    h('div', null, h('b', null, message || 'Choose an audio file'), ' — tap to browse or drop it here'),
    h('div', { class: 'small' }, 'WAV, MP3, M4A/AAC, FLAC, OGG and more, depending on your browser'));
  dz.onclick = () => fileInput.click();
  dz.onkeydown = (e) => { if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); fileInput.click(); } };
  return dz;
}

function renderSource() {
  const { file, header, loading, error } = state;
  if (!file) { replace(sourceBody, dropzone()); return; }
  if (loading) {
    replace(sourceBody, h('div', { class: 'dropzone', 'aria-busy': 'true' },
      icon('audio', 'lg'),
      h('div', null, h('b', null, `Decoding ${file.name}…`)),
      h('div', { class: 'small' }, fmtBytes(file.size))));
    return;
  }
  if (error) {
    const unsupportedWav = header && (header.format !== 1 || header.bitsPerSample > 16);
    replace(sourceBody, h('div', { class: 'stack' },
      notice('fault', `Could not decode ${file.name}.`,
        unsupportedWav
          ? `This browser cannot read ${describeType(file, header)} files. Safari in particular rejects 24-bit and floating-point WAV; try Chrome or Firefox, or export a 16-bit WAV or MP3 from your editor.`
          : 'This browser does not support this file type, or the file is damaged. Try exporting it as WAV or MP3 from your audio editor.'),
      dropzone('Choose another file')));
    return;
  }

  const n = state.channels.length;
  const duration = state.mono.length / state.rate;
  const compatible = isAcornCompatible(header);
  const changeBtn = button('Choose another file', { size: 'sm', iconName: 'upload', onclick: () => fileInput.click() });
  replace(sourceBody, h('div', { class: 'source' },
    h('div', { class: 'source-name' }, file.name,
      compatible ? badge('ACORN-ready', 'ok', 'check') : badge('Needs conversion', 'warn')),
    h('dl', { class: 'kv' },
      h('dt', null, 'Type'), h('dd', null, describeType(file, header)),
      h('dt', null, 'Channels'), h('dd', null, channelLabel(n)),
      h('dt', null, 'Sample rate'), h('dd', null, header ? fmtRate(header.sampleRate) : 'Set by the file (compressed)'),
      h('dt', null, 'Length'), h('dd', null, fmtTime(duration)),
      h('dt', null, 'Size'), h('dd', null, fmtBytes(file.size))),
    compatible && notice('ok', 'Already in the ACORN format.', 'Upload it as is, or trim it here and export a new copy.'),
    file.size > LARGE_FILE && notice('warn', `Large file (${fmtBytes(file.size)}).`,
      'Decoding uses a lot of memory and may be slow, especially on phones.'),
    h('div', { class: 'row' }, changeBtn)));
}

async function loadFile(file) {
  if (!file) return;
  stopPreview();
  Object.assign(state, { file, bytes: null, header: null, loading: true, error: null, channels: [], mono: null, mode: 'combine', trimStart: 0, trimEnd: 0 });
  setEnabled(false);
  renderSource();
  try {
    state.bytes = await file.arrayBuffer();
    state.header = readWavInfo(state.bytes);
    state.rate = pickSampleRate(state.header?.sampleRate);
    rateSelect.value = String(state.rate);
    await decodeAt(state.rate);
  } catch (e) {
    console.error(e);
    state.loading = false;
    state.error = e;
    renderSource();
    return;
  }
  state.loading = false;
  state.fileName = outputName(file.name);
  nameInput.value = state.fileName;
  remix();
  setTrim(0, waveform.duration);
  renderSource();
  setEnabled(true);
}

async function decodeAt(rate) {
  const decoded = await decodeAudio(state.bytes, rate);
  state.rate = rate;
  state.channels = channelsOf(decoded);
  if (state.channels.length < 2) state.mode = 'combine';
}

fileInput.onchange = () => { const f = fileInput.files[0]; fileInput.value = ''; loadFile(f); };

// Drop a file anywhere on the page.
let dragDepth = 0;
const dzHighlight = (on) => sourceBody.querySelector('.dropzone')?.classList.toggle('over', on);
window.addEventListener('dragenter', (e) => { e.preventDefault(); dragDepth++; dzHighlight(true); });
window.addEventListener('dragleave', () => { if (--dragDepth <= 0) { dragDepth = 0; dzHighlight(false); } });
window.addEventListener('dragover', (e) => e.preventDefault());
window.addEventListener('drop', (e) => {
  e.preventDefault();
  dragDepth = 0;
  dzHighlight(false);
  const f = e.dataTransfer?.files?.[0];
  if (f) loadFile(f);
});

function setEnabled(on) {
  trimCard.classList.toggle('disabled', !on);
  outputCard.classList.toggle('disabled', !on);
  for (const el of [trimCard, outputCard]) {
    if (on) el.removeAttribute('inert'); else el.setAttribute('inert', '');
  }
}

// ---- Mix & trim -------------------------------------------------------------------------

/** Rebuilds the mono signal for the current channel mode and redraws the waveform. */
function remix() {
  state.mono = mixToMono(state.channels, state.mode);
  const { trimStart, trimEnd } = state;
  waveform.setData(state.mono, state.rate);
  if (trimEnd > 0) setTrim(trimStart, trimEnd);
  renderMode();
}

function setTrim(s, e, { fromWave = false } = {}) {
  const t = fromWave ? { start: s, end: e } : waveform.setTrim(s, e);
  state.trimStart = t.start;
  state.trimEnd = t.end;
  if (document.activeElement !== startInput) startInput.value = fmtTime(t.start);
  if (document.activeElement !== endInput) endInput.value = fmtTime(t.end);
  startInput.classList.remove('invalid');
  endInput.classList.remove('invalid');
  lengthInput.value = fmtTime(t.end - t.start);
  stopPreview();
  scheduleOutput();
}

function bindTimeInput(input, which) {
  const commit = () => {
    const t = parseTime(input.value);
    if (t == null) { input.classList.add('invalid'); return; }
    if (which === 'start') setTrim(t, state.trimEnd); else setTrim(state.trimStart, t);
    input.value = fmtTime(which === 'start' ? state.trimStart : state.trimEnd);
  };
  input.addEventListener('change', commit);
  input.addEventListener('keydown', (e) => { if (e.key === 'Enter') { e.preventDefault(); commit(); input.select(); } });
  input.addEventListener('blur', () => {
    input.value = fmtTime(which === 'start' ? state.trimStart : state.trimEnd);
    input.classList.remove('invalid');
  });
}
bindTimeInput(startInput, 'start');
bindTimeInput(endInput, 'end');
resetTrimBtn.onclick = () => setTrim(0, waveform.duration);

// ---- Output options ---------------------------------------------------------------------

function renderMode() {
  const n = state.channels.length;
  for (const [mode, btn] of Object.entries(modeButtons)) {
    btn.setAttribute('aria-checked', String(state.mode === mode));
    btn.tabIndex = state.mode === mode ? 0 : -1;
    btn.disabled = n < 2 && mode !== 'combine';
  }
  modeHint.textContent = n < 2
    ? 'The source is already mono.'
    : state.mode === 'combine'
      ? n > 2 ? `Averages all ${n} channels into one.` : 'Averages left and right into one channel.'
      : `Keeps only the ${state.mode} channel.`;
}

function selectMode(mode) {
  if (mode === state.mode || modeButtons[mode].disabled) return;
  stopPreview();
  state.mode = mode;
  remix();
}

for (const [mode, btn] of Object.entries(modeButtons)) btn.onclick = () => selectMode(mode);
modeSeg.addEventListener('keydown', (e) => {
  const modes = Object.keys(modeButtons).filter((m) => !modeButtons[m].disabled);
  const dir = { ArrowRight: 1, ArrowDown: 1, ArrowLeft: -1, ArrowUp: -1 }[e.key];
  if (!dir || modes.length < 2) return;
  e.preventDefault();
  const next = modes[(modes.indexOf(state.mode) + dir + modes.length) % modes.length];
  selectMode(next);
  modeButtons[next].focus();
});

rateSelect.onchange = async () => {
  const rate = Number(rateSelect.value);
  if (rate === state.rate || !state.bytes) return;
  stopPreview();
  rateSelect.disabled = true;
  rateHint.textContent = 'Resampling…';
  try {
    await decodeAt(rate);
    remix();
  } catch (e) {
    toast(e.message, 'fault');
    rateSelect.value = String(state.rate);
  } finally {
    rateSelect.disabled = false;
    scheduleOutput();
  }
};

function renderRateHint() {
  const src = state.header?.sampleRate;
  rateHint.textContent = !src
    ? 'Your browser decodes the file at this rate.'
    : src === state.rate ? 'Same as the source (no resampling).' : `Resampled from ${fmtRate(src)} by your browser.`;
}

nameInput.addEventListener('change', () => {
  state.fileName = cleanFileName(nameInput.value);
  nameInput.value = state.fileName;
  scheduleOutput();
});

// ---- Output summary ---------------------------------------------------------------------

let outputFrame = 0;
function scheduleOutput() {
  if (!outputFrame) outputFrame = requestAnimationFrame(() => { outputFrame = 0; renderOutput(); });
}

function renderOutput() {
  if (!state.mono) return;
  renderRateHint();
  const { start, end } = trimRange(state.mono.length, state.rate, state.trimStart, state.trimEnd);
  const frames = end - start;
  const bytes = wavByteLength(frames);
  const { peak } = measure(state.mono, start, end);
  const advice = state.mode === 'combine' ? stereoAdvice(state.channels, start, end) : null;

  replace(outNotices,
    advice?.kind === 'silent' && notice('warn', `The ${advice.silent} channel is nearly silent.`,
      `Combine averages it in and makes the sound about 6 dB quieter. Choose ${advice.silent === 'left' ? 'Right' : 'Left'} to keep the full level.`),
    advice?.kind === 'phase' && notice('warn', 'Left and right partly cancel when combined.',
      'The channels are out of phase, so Combine sounds thin or quiet. Preview Left and Right and keep the one that sounds fuller.'),
    peak > 1 && notice('warn', 'Some peaks are above full scale.', 'They will be clipped in the 16-bit export. Lower the level in your editor first if you hear distortion.'),
    peak < 0.01 && notice('warn', peak > 0 ? 'This selection is nearly silent.' : 'This selection is silent.', `Peak level is ${fmtDb(peak)}.`),
    bytes > DEVICE_UPLOAD_LIMIT && notice('warn', `The file is larger than the ${fmtBytes(DEVICE_UPLOAD_LIMIT)} the ACORN web upload accepts.`,
      'Trim it shorter, or copy it straight into the /audio folder on the SD card.'));

  replace(summary,
    h('dt', null, 'Format'), h('dd', null, `WAV · 16-bit PCM · mono · ${fmtRate(state.rate)}`),
    h('dt', null, 'Channels'), h('dd', null, state.channels.length < 2 ? 'Mono source' : MODE_LABELS[state.mode]),
    h('dt', null, 'Length'), h('dd', null, `${fmtTime(frames / state.rate)} (${frames.toLocaleString()} samples)`),
    h('dt', null, 'File size'), h('dd', null, fmtBytes(bytes)),
    h('dt', null, 'Peak level'), h('dd', null, fmtDb(peak)));

  exportBtn.disabled = frames < 1;
}

exportBtn.onclick = () => busy(exportBtn, async () => {
  await new Promise((r) => setTimeout(r, 30));
  const { start, end } = trimRange(state.mono.length, state.rate, state.trimStart, state.trimEnd);
  const blob = encodeWav(state.mono.subarray(start, end), state.rate);
  downloadBlob(state.fileName, blob);
  toast(`Saved ${state.fileName} · ${fmtBytes(blob.size)}`, 'ok');
});

// ---- Preview ----------------------------------------------------------------------------

let audioCtx = null;
let preview = null;
let previewFrame = 0;

const previewGain = () => (Number(volRange.value) / 100) ** 2;

volRange.oninput = () => {
  volOut.value = volRange.value;
  if (preview) preview.gain.gain.setTargetAtTime(previewGain(), audioCtx.currentTime, 0.02);
};

function renderProgress(elapsed, length) {
  progress.firstChild.style.width = length > 0 ? `${Math.min(100, (elapsed / length) * 100)}%` : '0%';
  progressText.replaceChildren(h('span', null, fmtTime(elapsed)), h('span', null, length ? fmtTime(length) : '—'));
}

function tick() {
  if (!preview) return;
  const elapsed = Math.min(preview.length, audioCtx.currentTime - preview.t0);
  renderProgress(elapsed, preview.length);
  waveform.setPlayhead(preview.offset + elapsed);
  previewFrame = requestAnimationFrame(tick);
}

async function startPreview() {
  stopPreview();
  const { start, end } = trimRange(state.mono.length, state.rate, state.trimStart, state.trimEnd);
  if (end <= start) return;
  const Ctx = window.AudioContext || window.webkitAudioContext;
  audioCtx ||= new Ctx();
  if (audioCtx.state === 'suspended') await audioCtx.resume();
  const buffer = audioCtx.createBuffer(1, end - start, state.rate);
  buffer.getChannelData(0).set(state.mono.subarray(start, end));
  const src = audioCtx.createBufferSource();
  src.buffer = buffer;
  const gain = audioCtx.createGain();
  gain.gain.value = previewGain();
  src.connect(gain).connect(audioCtx.destination);
  src.onended = () => { if (preview && preview.src === src) stopPreview(); };
  src.start();
  preview = { src, gain, t0: audioCtx.currentTime, offset: start / state.rate, length: (end - start) / state.rate };
  stopBtn.disabled = false;
  setButton(playBtn, 'Restart', 'play');
  tick();
}

function stopPreview() {
  cancelAnimationFrame(previewFrame);
  if (preview) {
    const { src } = preview;
    preview = null;
    try { src.stop(); } catch { /* already stopped */ }
    src.disconnect();
  }
  stopBtn.disabled = true;
  setButton(playBtn, 'Play preview', 'play');
  waveform.setPlayhead(null);
  renderProgress(0, state.mono ? state.trimEnd - state.trimStart : 0);
}

playBtn.onclick = () => startPreview().catch((e) => toast(e.message || String(e), 'fault'));
stopBtn.onclick = stopPreview;

onThemeChange(() => waveform.redraw());

renderSource();
renderProgress(0, 0);
renderMode();
setEnabled(false);
