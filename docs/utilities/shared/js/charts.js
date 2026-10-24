// Small SVG charts for the utilities pages: time series (with gaps and an optional min–max
// band) and stacked columns. No dependencies. Colors come from CSS classes (.c0–.c5) so the
// charts follow the theme and the print stylesheet without re-rendering.

import { h, replace } from './ui-helpers.js';

const NS = 'http://www.w3.org/2000/svg';
const MONTHS = ['Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec'];
const MIN = 60_000;
const HOUR = 60 * MIN;
const DAY = 24 * HOUR;
const TIME_STEPS = [15 * MIN, 30 * MIN, HOUR, 2 * HOUR, 3 * HOUR, 6 * HOUR, 12 * HOUR, DAY, 2 * DAY, 7 * DAY, 14 * DAY, 28 * DAY];
const M = { top: 12, right: 14, bottom: 26, left: 48 };

function s(tag, attrs = {}, ...children) {
  const el = document.createElementNS(NS, tag);
  for (const [k, v] of Object.entries(attrs)) if (v != null && v !== false) el.setAttribute(k, v);
  for (const c of children.flat()) if (c != null && c !== false) el.append(c instanceof Node ? c : String(c));
  return el;
}

const pad2 = (n) => String(n).padStart(2, '0');
const minOf = (a) => a.reduce((m, v) => (v < m ? v : m), Infinity);
const maxOf = (a) => a.reduce((m, v) => (v > m ? v : m), -Infinity);

/** Local calendar parts of a UTC instant shifted by a fixed offset (minutes). */
export function parts(ms, offsetMin) {
  const d = new Date(ms + offsetMin * MIN);
  return { y: d.getUTCFullYear(), mo: d.getUTCMonth(), d: d.getUTCDate(), h: d.getUTCHours(), mi: d.getUTCMinutes() };
}
export const fmtDay = (ms, off) => { const p = parts(ms, off); return `${MONTHS[p.mo]} ${p.d}`; };
export const fmtClock = (ms, off) => { const p = parts(ms, off); return `${pad2(p.h)}:${pad2(p.mi)}`; };

function niceStep(span, target) {
  const raw = span / Math.max(1, target);
  const p = 10 ** Math.floor(Math.log10(raw));
  const f = raw / p;
  return (f < 1.5 ? 1 : f < 3 ? 2 : f < 7 ? 5 : 10) * p;
}

function linearTicks(min, max, target) {
  if (!(max > min)) { const c = Number.isFinite(min) ? min : 0; min = c - 1; max = c + 1; }
  const step = niceStep(max - min, target);
  const lo = Math.floor(min / step + 1e-9) * step;
  const hi = Math.ceil(max / step - 1e-9) * step;
  const ticks = [];
  for (let v = lo; v <= hi + step / 2; v += step) ticks.push(+v.toPrecision(12));
  return { lo, hi, ticks, decimals: Math.max(0, -Math.floor(Math.log10(step) + 1e-9)) };
}

function timeTicks(a, b, offsetMin, target) {
  const step = TIME_STEPS.find((st) => (b - a) / st <= target + 1) || TIME_STEPS[TIME_STEPS.length - 1];
  const off = offsetMin * MIN;
  const ticks = [];
  for (let t = Math.ceil((a + off) / step) * step - off; t <= b; t += step) ticks.push(t);
  const label = (t) => {
    const p = parts(t, offsetMin);
    if (step >= DAY || (p.h === 0 && p.mi === 0)) return `${MONTHS[p.mo]} ${p.d}`;
    return `${pad2(p.h)}:${pad2(p.mi)}`;
  };
  return { ticks, label };
}

const fmtNum = (v, decimals) => v.toLocaleString(undefined, { minimumFractionDigits: decimals, maximumFractionDigits: decimals });

/** Keeps the first, min and max point per pixel column so dense series keep their shape. */
function decimate(points, xOf, width) {
  if (points.length <= width * 3) return points;
  const out = [];
  let col = null;
  let bucket = [];
  const flush = () => {
    if (!bucket.length) return;
    let lo = bucket[0];
    let hi = bucket[0];
    for (const p of bucket) { if (p.v < lo.v) lo = p; if (p.v > hi.v) hi = p; }
    const keep = [bucket[0], lo, hi, bucket[bucket.length - 1]].filter((p, i, arr) => arr.indexOf(p) === i).sort((x, y) => x.t - y.t);
    out.push(...keep);
    bucket = [];
  };
  for (const p of points) {
    const c = Math.floor(xOf(p.t));
    if (c !== col) { flush(); col = c; }
    bucket.push(p);
  }
  flush();
  return out;
}

function nearest(points, t) {
  let lo = 0;
  let hi = points.length - 1;
  if (hi < 0) return null;
  while (hi - lo > 1) {
    const mid = (lo + hi) >> 1;
    if (points[mid].t < t) lo = mid; else hi = mid;
  }
  return Math.abs(points[lo].t - t) <= Math.abs(points[hi].t - t) ? points[lo] : points[hi];
}

/**
 * Renders a chart into `host` and re-renders it when the host is resized (including for print).
 * `draw(width)` returns { svg, onMove?(x) -> tip text | null }.
 */
function mount(host, draw, height) {
  host.classList.add('chart');
  const tip = h('div', { class: 'chart-tip', hidden: true });
  let last = 0;
  let current = null;
  const render = (w) => {
    const width = Math.max(240, Math.round(w));
    if (width === last) return;
    last = width;
    current = draw(width);
    current.svg.setAttribute('viewBox', `0 0 ${width} ${height}`);
    current.svg.setAttribute('width', '100%');
    current.svg.setAttribute('height', height);
    current.svg.setAttribute('role', 'img');
    replace(host, current.svg, tip);
  };
  host.addEventListener('pointermove', (e) => {
    if (!current?.onMove) return;
    const box = host.getBoundingClientRect();
    const scale = last / box.width;
    const res = current.onMove((e.clientX - box.left) * scale);
    if (!res) { tip.hidden = true; current.guide?.setAttribute('visibility', 'hidden'); return; }
    replace(tip, res.lines.map((l, i) => h('div', { class: i ? '' : 'tip-head' }, l)));
    tip.hidden = false;
    const x = res.x / scale;
    tip.style.left = `${Math.min(Math.max(0, x + 12), box.width - tip.offsetWidth)}px`;
  });
  host.addEventListener('pointerleave', () => { tip.hidden = true; current?.guide?.setAttribute('visibility', 'hidden'); });
  const ro = new ResizeObserver((entries) => render(entries[0].contentRect.width));
  ro.observe(host);
  render(host.clientWidth || 640);
  return { destroy: () => ro.disconnect() };
}

/**
 * Time series.
 * opts: { series: [{ name, cls, points: [{ t, v }], decimals, unit }], band?: { cls, points: [{ t, lo, hi }] },
 *         height, offsetMin, gapMs, x: 'time' | 'hour', yMin, yMax, fmtX?(t) }
 * Points must be sorted by t. A gap longer than gapMs (or a null value) breaks the line.
 */
export function timeSeries(host, opts) {
  const { series, band, height = 200, offsetMin = 0, gapMs = Infinity, x = 'time' } = opts;
  const all = series.flatMap((sr) => sr.points);
  const vals = all.map((p) => p.v).concat(band ? band.points.flatMap((p) => [p.lo, p.hi]) : []).filter((v) => v != null);
  const ts = all.map((p) => p.t).concat(band ? band.points.map((p) => p.t) : []);
  const x0 = x === 'hour' ? 0 : minOf(ts);
  const x1 = x === 'hour' ? 24 : maxOf(ts);
  const fmtX = opts.fmtX || ((t) => (x === 'hour' ? `${pad2(Math.floor(t))}:${pad2(Math.round((t % 1) * 60))}` : `${fmtDay(t, offsetMin)} ${fmtClock(t, offsetMin)}`));

  return mount(host, (width) => {
    const iw = width - M.left - M.right;
    const ih = height - M.top - M.bottom;
    const span = x1 - x0 || 1;
    const xOf = (t) => M.left + ((t - x0) / span) * iw;
    const y = linearTicks(opts.yMin ?? minOf(vals), opts.yMax ?? maxOf(vals), Math.max(3, Math.round(ih / 40)));
    const yOf = (v) => M.top + ih - ((v - y.lo) / (y.hi - y.lo)) * ih;

    const grid = s('g', { class: 'grid' });
    for (const v of y.ticks) {
      grid.append(s('line', { x1: M.left, x2: M.left + iw, y1: yOf(v), y2: yOf(v) }));
      grid.append(s('text', { x: M.left - 8, y: yOf(v) + 4, 'text-anchor': 'end', class: 'tick' }, fmtNum(v, y.decimals)));
    }
    const xt = x === 'hour'
      ? { ticks: [0, 3, 6, 9, 12, 15, 18, 21, 24].filter((hr) => iw / 8 > 34 || hr % 6 === 0), label: (hr) => pad2(hr) }
      : timeTicks(x0, x1, offsetMin, Math.max(3, Math.floor(iw / 80)));
    for (const t of xt.ticks) {
      grid.append(s('line', { x1: xOf(t), x2: xOf(t), y1: M.top, y2: M.top + ih, class: 'v' }));
      grid.append(s('text', { x: xOf(t), y: height - 8, 'text-anchor': 'middle', class: 'tick' }, xt.label(t)));
    }

    const layers = [];
    if (band && band.points.length) {
      const pts = band.points.filter((p) => p.lo != null && p.hi != null);
      const d = pts.map((p, i) => `${i ? 'L' : 'M'}${xOf(p.t).toFixed(1)},${yOf(p.hi).toFixed(1)}`).join('') +
        pts.slice().reverse().map((p) => `L${xOf(p.t).toFixed(1)},${yOf(p.lo).toFixed(1)}`).join('') + 'Z';
      layers.push(s('path', { d, class: `band ${band.cls || 'c0'}` }));
    }
    for (const sr of series) {
      const pts = decimate(sr.points.filter((p) => p.v != null), xOf, iw);
      let d = '';
      let prev = null;
      const dots = [];
      for (let i = 0; i < pts.length; i++) {
        const p = pts[i];
        const brk = !prev || p.t - prev.t > gapMs;
        d += `${brk ? 'M' : 'L'}${xOf(p.t).toFixed(1)},${yOf(p.v).toFixed(1)}`;
        const next = pts[i + 1];
        if (brk && (!next || next.t - p.t > gapMs)) dots.push(p);
        prev = p;
      }
      layers.push(s('path', { d, class: `line ${sr.cls || 'c0'}` }));
      for (const p of dots) layers.push(s('circle', { cx: xOf(p.t), cy: yOf(p.v), r: 2, class: `dot ${sr.cls || 'c0'}` }));
    }

    const guide = s('g', { visibility: 'hidden' });
    const guideLine = s('line', { y1: M.top, y2: M.top + ih, class: 'guide' });
    const marks = series.map((sr) => s('circle', { r: 3.5, class: `mark ${sr.cls || 'c0'}` }));
    guide.append(guideLine, ...marks);

    const svg = s('svg', { class: 'chart-svg' }, grid, ...layers, guide);
    const onMove = (px) => {
      if (px < M.left - 4 || px > M.left + iw + 4) return null;
      const t = x0 + ((px - M.left) / iw) * span;
      const hits = series.map((sr) => nearest(sr.points.filter((p) => p.v != null), t));
      const ref = hits.find(Boolean);
      if (!ref) return null;
      const gx = xOf(ref.t);
      guideLine.setAttribute('x1', gx);
      guideLine.setAttribute('x2', gx);
      hits.forEach((p, i) => {
        if (!p) { marks[i].setAttribute('visibility', 'hidden'); return; }
        marks[i].setAttribute('visibility', 'visible');
        marks[i].setAttribute('cx', xOf(p.t));
        marks[i].setAttribute('cy', yOf(p.v));
      });
      guide.setAttribute('visibility', 'visible');
      const bandHit = band && nearest(band.points, ref.t);
      return {
        x: gx,
        lines: [
          fmtX(ref.t),
          ...hits.map((p, i) => p && `${series.length > 1 ? `${series[i].name}: ` : ''}${fmtNum(p.v, series[i].decimals ?? 1)}${series[i].unit ? ` ${series[i].unit}` : ''}`),
          bandHit && `range ${fmtNum(bandHit.lo, series[0].decimals ?? 1)} – ${fmtNum(bandHit.hi, series[0].decimals ?? 1)}${series[0].unit ? ` ${series[0].unit}` : ''}`,
        ].filter(Boolean),
      };
    };
    return { svg, onMove, guide };
  }, height);
}

/**
 * Stacked columns.
 * opts: { categories: [label], stacks: [{ name, cls, values: [n] }], height, labelEvery?, tipLabel?(i) }
 */
export function columns(host, opts) {
  const { categories, stacks, height = 180 } = opts;
  const totals = categories.map((_, i) => stacks.reduce((a, st) => a + (st.values[i] || 0), 0));
  return mount(host, (width) => {
    const iw = width - M.left - M.right;
    const ih = height - M.top - M.bottom;
    const y = linearTicks(0, Math.max(1, maxOf(totals)), Math.max(3, Math.round(ih / 40)));
    const yOf = (v) => M.top + ih - (v / y.hi) * ih;
    const n = categories.length;
    const slot = iw / n;
    const bw = Math.max(1, Math.min(42, slot * 0.72));
    const every = opts.labelEvery || Math.max(1, Math.ceil(n / Math.max(1, Math.floor(iw / 44))));

    const grid = s('g', { class: 'grid' });
    for (const v of y.ticks.filter((v) => Number.isInteger(v))) {
      grid.append(s('line', { x1: M.left, x2: M.left + iw, y1: yOf(v), y2: yOf(v) }));
      grid.append(s('text', { x: M.left - 8, y: yOf(v) + 4, 'text-anchor': 'end', class: 'tick' }, fmtNum(v, 0)));
    }
    const bars = s('g');
    const hot = s('rect', { y: M.top, height: ih, class: 'hot', visibility: 'hidden' });
    categories.forEach((label, i) => {
      const cx = M.left + slot * i + slot / 2;
      let acc = 0;
      for (const st of stacks) {
        const v = st.values[i] || 0;
        if (!v) continue;
        bars.append(s('rect', { x: cx - bw / 2, width: bw, y: yOf(acc + v), height: Math.max(0.5, yOf(acc) - yOf(acc + v)), class: `bar ${st.cls || 'c0'}` }));
        acc += v;
      }
      if (i % every === 0) grid.append(s('text', { x: cx, y: height - 8, 'text-anchor': 'middle', class: 'tick' }, label));
    });
    const svg = s('svg', { class: 'chart-svg' }, hot, grid, bars);
    const onMove = (px) => {
      const i = Math.floor((px - M.left) / slot);
      if (i < 0 || i >= n) { hot.setAttribute('visibility', 'hidden'); return null; }
      hot.setAttribute('x', M.left + slot * i);
      hot.setAttribute('width', slot);
      hot.setAttribute('visibility', 'visible');
      return {
        x: M.left + slot * i + slot / 2,
        lines: [opts.tipLabel ? opts.tipLabel(i) : categories[i], ...stacks.filter((st) => st.values[i]).map((st) => `${st.name}: ${st.values[i]}`), stacks.length > 1 && `total: ${totals[i]}`].filter(Boolean),
      };
    };
    return { svg, onMove, guide: hot };
  }, height);
}

/** HTML legend for multi-series charts. */
export function legend(items) {
  return h('div', { class: 'legend' }, items.map((it) => h('span', null, h('i', { class: `sw ${it.cls}` }), it.name)));
}
