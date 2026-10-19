// Canvas waveform with draggable start/end trim handles and a playhead.
// Pressing on the waveform grabs the nearest handle. Handles are focusable sliders:
// arrows move 10 ms (Shift: 100 ms), PageUp/PageDown 1 s.

import { h, fmtTime, fmtTick } from './ui-helpers.js';

const BUCKETS = 4096;
const MIN_GAP = 0.01;

/** Min/max envelope of `samples` in BUCKETS columns. */
function envelope(samples) {
  const n = Math.min(BUCKETS, samples.length);
  const min = new Float32Array(n);
  const max = new Float32Array(n);
  const per = samples.length / n;
  for (let b = 0; b < n; b++) {
    const from = Math.floor(b * per);
    const to = Math.max(from + 1, Math.floor((b + 1) * per));
    let lo = 1, hi = -1;
    for (let i = from; i < to; i++) {
      const v = samples[i];
      if (v < lo) lo = v;
      if (v > hi) hi = v;
    }
    min[b] = lo;
    max[b] = hi;
  }
  return { min, max, n };
}

export function createWaveform({ onTrim } = {}) {
  const canvas = h('canvas', { 'aria-hidden': 'true' });
  const shadeL = h('div', { class: 'wave-shade left' });
  const shadeR = h('div', { class: 'wave-shade right' });
  const playhead = h('div', { class: 'wave-playhead', hidden: true });
  const startHandle = h('div', { class: 'wave-handle', role: 'slider', tabindex: 0, 'aria-label': 'Trim start' });
  const endHandle = h('div', { class: 'wave-handle', role: 'slider', tabindex: 0, 'aria-label': 'Trim end' });
  const wave = h('div', { class: 'wave' }, canvas, shadeL, shadeR, playhead, startHandle, endHandle);
  const ticks = h('div', { class: 'wave-ticks', 'aria-hidden': 'true' });
  const el = h('div', null, wave, ticks);

  let env = null;
  let duration = 0;
  let start = 0;
  let end = 0;

  function cssVar(name) {
    return getComputedStyle(wave).getPropertyValue(name).trim();
  }

  function draw() {
    const dpr = window.devicePixelRatio || 1;
    const w = Math.max(1, Math.round(wave.clientWidth * dpr));
    const ht = Math.max(1, Math.round(wave.clientHeight * dpr));
    if (canvas.width !== w) canvas.width = w;
    if (canvas.height !== ht) canvas.height = ht;
    const ctx = canvas.getContext('2d');
    ctx.clearRect(0, 0, w, ht);
    const mid = ht / 2;
    ctx.fillStyle = cssVar('--line-strong');
    ctx.fillRect(0, Math.floor(mid), w, Math.max(1, Math.round(dpr)));
    if (!env) return;
    ctx.fillStyle = cssVar('--ink');
    const amp = mid * 0.92;
    for (let x = 0; x < w; x++) {
      const b0 = Math.floor((x / w) * env.n);
      const b1 = Math.max(b0 + 1, Math.floor(((x + 1) / w) * env.n));
      let lo = 1, hi = -1;
      for (let b = b0; b < b1 && b < env.n; b++) {
        if (env.min[b] < lo) lo = env.min[b];
        if (env.max[b] > hi) hi = env.max[b];
      }
      if (hi < lo) continue;
      const top = mid - Math.min(1, hi) * amp;
      const bottom = mid - Math.max(-1, lo) * amp;
      ctx.fillRect(x, top, 1, Math.max(1, bottom - top));
    }
  }

  function pct(t) {
    return duration > 0 ? `${(t / duration) * 100}%` : '0%';
  }

  function layout() {
    startHandle.style.left = pct(start);
    endHandle.style.left = pct(end);
    shadeL.style.width = pct(start);
    shadeR.style.width = duration > 0 ? `${(1 - end / duration) * 100}%` : '0%';
    for (const [handle, value, lo, hi] of [[startHandle, start, 0, end - MIN_GAP], [endHandle, end, start + MIN_GAP, duration]]) {
      handle.setAttribute('aria-valuemin', Math.max(0, lo).toFixed(3));
      handle.setAttribute('aria-valuemax', Math.max(0, hi).toFixed(3));
      handle.setAttribute('aria-valuenow', value.toFixed(3));
      handle.setAttribute('aria-valuetext', fmtTime(value));
    }
  }

  function renderTicks() {
    ticks.replaceChildren(...[0, 0.25, 0.5, 0.75, 1].map((f) =>
      h('span', { style: { left: `${f * 100}%` } }, fmtTick(f * duration, duration))));
  }

  function set(which, t, notify = true) {
    if (which === 'start') start = Math.max(0, Math.min(t, end - MIN_GAP));
    else end = Math.min(duration, Math.max(t, start + MIN_GAP));
    layout();
    if (notify && onTrim) onTrim(start, end);
  }

  function timeAt(clientX) {
    const r = wave.getBoundingClientRect();
    return Math.max(0, Math.min(1, (clientX - r.left) / r.width)) * duration;
  }

  let dragging = null;
  wave.addEventListener('pointerdown', (e) => {
    if (!env || e.button > 0) return;
    const t = timeAt(e.clientX);
    if (e.target === startHandle) dragging = 'start';
    else if (e.target === endHandle) dragging = 'end';
    else dragging = Math.abs(t - start) <= Math.abs(t - end) ? 'start' : 'end';
    (dragging === 'start' ? startHandle : endHandle).focus({ preventScroll: true });
    wave.setPointerCapture(e.pointerId);
    set(dragging, t);
    e.preventDefault();
  });
  wave.addEventListener('pointermove', (e) => { if (dragging) set(dragging, timeAt(e.clientX)); });
  const stopDrag = () => { dragging = null; };
  wave.addEventListener('pointerup', stopDrag);
  wave.addEventListener('pointercancel', stopDrag);

  for (const [handle, which] of [[startHandle, 'start'], [endHandle, 'end']]) {
    handle.addEventListener('keydown', (e) => {
      if (!env) return;
      const value = which === 'start' ? start : end;
      const step = e.shiftKey ? 0.1 : 0.01;
      const moves = {
        ArrowLeft: value - step, ArrowDown: value - step,
        ArrowRight: value + step, ArrowUp: value + step,
        PageDown: value - 1, PageUp: value + 1,
        Home: which === 'start' ? 0 : start + MIN_GAP,
        End: which === 'start' ? end - MIN_GAP : duration,
      };
      if (!(e.key in moves)) return;
      e.preventDefault();
      set(which, moves[e.key]);
    });
  }

  new ResizeObserver(() => draw()).observe(wave);

  return {
    el,
    /** New audio (mono samples at `sampleRate`); resets the trim to the whole clip. */
    setData(samples, sampleRate) {
      env = samples.length ? envelope(samples) : null;
      duration = samples.length / sampleRate;
      start = 0;
      end = duration;
      layout();
      renderTicks();
      draw();
    },
    /** Applies a trim (seconds), clamped to the clip; returns the values used. */
    setTrim(s, e) {
      end = Math.min(duration, Math.max(e, MIN_GAP));
      start = Math.max(0, Math.min(s, end - MIN_GAP));
      layout();
      return { start, end };
    },
    get duration() { return duration; },
    setPlayhead(t) {
      playhead.hidden = t == null;
      if (t != null) playhead.style.left = pct(t);
    },
    redraw: draw,
  };
}
