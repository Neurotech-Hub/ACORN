// Program editor. Edits are staged in the browser and sent as one object on Save & Apply.
// Validation and schedule preview come from the device; the UI never computes schedules.

import { api } from '../api.js';
import { refresh, deviceNow, offsetMinutes } from '../store.js';
import {
  h, clear, replace, icon, badge, button, busy, field, section, skeleton, errorState, toast, toastError,
  confirmDialog, confirmDiscard, notice, normalizeTime, fmtClock, fmtUtcClock, fmtDuration, dayLabel,
  toLocalIso, clone,
} from '../ui.js';

const ID_RE = /^[a-z0-9][a-z0-9-]{0,31}$/;
const DAY_S = 86400;

let pending = null;
/** Hand a draft (upload / duplicate) to the next "#/programs/new" mount. */
export function setPendingDraft(program, note) {
  pending = { program, note };
}

const TEMPLATE = {
  schema_version: 1, id: '', name: '', description: '',
  events: [{
    event_id: 'daytime-playback', type: 'range', start: '06:00:00', end: '18:00:00',
    audio: '/audio/playback.wav', volume: 80, recurrence: { type: 'fixed', interval_seconds: 600 },
  }],
};

const slugify = (s) => s.toLowerCase().replace(/[^a-z0-9]+/g, '-').replace(/^-+|-+$/g, '').slice(0, 32);
const secs = (hms) => { const t = normalizeTime(hms); if (!t) return null; const [a, b, c] = t.split(':').map(Number); return a * 3600 + b * 60 + c; };
const hms = (s) => { s = ((s % DAY_S) + DAY_S) % DAY_S; return [s / 3600 | 0, (s % 3600) / 60 | 0, s % 60].map((n) => String(n).padStart(2, '0')).join(':'); };

function cleanEvent(ev) {
  const base = { event_id: ev.event_id, type: ev.type };
  if (ev.type === 'point') return { ...base, time: ev.time, audio: ev.audio, volume: ev.volume };
  const r = ev.recurrence || {};
  return {
    ...base, start: ev.start, end: ev.end, audio: ev.audio, volume: ev.volume,
    recurrence: r.type === 'random'
      ? { type: 'random', min_interval_seconds: r.min_interval_seconds, max_interval_seconds: r.max_interval_seconds }
      : { type: 'fixed', interval_seconds: r.interval_seconds },
  };
}

function serialize(d) {
  return {
    schema_version: d.schema_version ?? 1, id: d.id, name: d.name, description: d.description || '',
    events: d.events.map(cleanEvent),
  };
}

/** Make arbitrary uploaded JSON renderable. Semantic problems are left for the device validator. */
function normalizeDraft(p) {
  const d = p && typeof p === 'object' ? clone(p) : {};
  d.schema_version ??= 1;
  d.id = typeof d.id === 'string' ? d.id : '';
  d.name = typeof d.name === 'string' ? d.name : '';
  d.description = typeof d.description === 'string' ? d.description : '';
  d.events = Array.isArray(d.events) ? d.events.filter((e) => e && typeof e === 'object') : [];
  for (const ev of d.events) {
    if (ev.type !== 'point') {
      ev.type = 'range';
      ev.recurrence = ev.recurrence && typeof ev.recurrence === 'object' ? ev.recurrence : { type: 'fixed', interval_seconds: 600 };
    }
  }
  return d;
}

function localCheck(d, isNew) {
  const errs = [];
  const add = (field, message) => errs.push({ code: 'FORMAT', field, message });
  if (!d.name.trim()) add('name', 'Name is required.');
  if (isNew && !ID_RE.test(d.id)) add('id', 'Use 1–32 lowercase letters, digits or hyphens.');
  if (!d.events.length) add('events', 'Add at least one event.');
  const seen = new Set();
  d.events.forEach((ev, i) => {
    const f = (k) => `events[${i}].${k}`;
    if (!ID_RE.test(ev.event_id || '')) add(f('event_id'), 'Use lowercase letters, digits or hyphens.');
    else if (seen.has(ev.event_id)) add(f('event_id'), 'Event IDs must be unique.');
    seen.add(ev.event_id);
    if (!ev.audio) add(f('audio'), 'Choose an audio file.');
    if (!Number.isInteger(ev.volume) || ev.volume < 0 || ev.volume > 100) add(f('volume'), 'Volume 0–100.');
    if (ev.type === 'point') {
      if (!normalizeTime(ev.time)) add(f('time'), 'Enter a time.');
    } else {
      if (!normalizeTime(ev.start)) add(f('start'), 'Enter a start time.');
      if (!normalizeTime(ev.end)) add(f('end'), 'Enter an end time.');
      const r = ev.recurrence;
      if (r.type === 'fixed' && !(Number.isInteger(r.interval_seconds) && r.interval_seconds > 0)) add(f('recurrence.interval_seconds'), 'Enter a positive interval.');
      if (r.type === 'random') {
        const ok = Number.isInteger(r.min_interval_seconds) && r.min_interval_seconds > 0 && Number.isInteger(r.max_interval_seconds);
        if (!ok) add(f('recurrence.min_interval_seconds'), 'Enter positive minimum and maximum intervals.');
        else if (r.max_interval_seconds < r.min_interval_seconds) add(f('recurrence.max_interval_seconds'), 'Maximum must be ≥ minimum.');
      }
    }
  });
  return errs;
}

export function mount(el, { params }) {
  const isNewRoute = params.length === 0;
  let isNew = isNewRoute;
  let draft = null;
  let baseline = '';
  let activeId = null;
  let summary = null;
  let audioFiles = [];
  let errors = [];
  let validation = null;   // { valid, revision, errors, stale }
  let previewData = null;  // { events, revision, stale }
  let previewCount = 100;
  let idTouched = false;
  let note = null;
  const units = new WeakMap();

  const dirty = () => draft && JSON.stringify(serialize(draft)) !== baseline;

  // ---- loading --------------------------------------------------------------------------
  async function load() {
    replace(el, skeleton([48, 180, 220]));
    try {
      const [audio, programs, prog] = await Promise.all([
        api.audio(),
        api.programs(),
        isNewRoute ? null : api.program(params[0]),
      ]);
      audioFiles = audio.files;
      activeId = programs.active_id;
      if (isNewRoute) {
        const p = pending ? pending.program : TEMPLATE;
        note = pending ? pending.note : null;
        pending = null;
        draft = normalizeDraft(p);
        idTouched = !!draft.id;
        baseline = note ? '' : JSON.stringify(serialize(normalizeDraft(TEMPLATE)));
      } else {
        draft = normalizeDraft(prog.program);
        summary = prog.summary;
        baseline = JSON.stringify(serialize(draft));
        if (prog.errors.length) {
          errors = prog.errors;
          validation = { valid: false, errors: prog.errors, revision: prog.summary.revision };
        }
      }
      renderAll();
    } catch (e) {
      replace(el, h('a', { class: 'back', href: '#/programs' }, icon('back', 'sm'), 'Programs'), errorState(e, load));
    }
  }

  // ---- change handling ------------------------------------------------------------------
  let saveBar, timelineHost, jsonPre, validationHost, previewHost;

  function changed(fieldKey) {
    if (fieldKey) {
      errors = errors.filter((e) => e.field !== fieldKey && !(e.field || '').startsWith(fieldKey + '.'));
      applyErrors();
    }
    if (validation) validation.stale = true;
    if (previewData) previewData.stale = true;
    renderSaveBar();
    renderTimeline();
    renderValidation();
    renderPreview();
    if (jsonPre) jsonPre.textContent = JSON.stringify(serialize(draft), null, 2);
  }

  function structural() {
    if (validation) validation.stale = true;
    if (previewData) previewData.stale = true;
    renderAll();
  }

  // ---- errors ---------------------------------------------------------------------------
  function applyErrors() {
    el.querySelectorAll('.field .error').forEach((n) => n.remove());
    el.querySelectorAll('.has-error').forEach((n) => n.classList.remove('has-error'));
    for (const e of errors) {
      let key = e.field || '';
      let target = null;
      while (key && !(target = el.querySelector(`[data-field="${CSS.escape(key)}"]`))) {
        key = key.replace(/(\.[^.[\]]+|\[\d+\])$/, '');
      }
      if (!target) continue;
      target.classList.add('has-error');
      if (target.classList.contains('field')) target.append(h('div', { class: 'error' }, e.message));
      else {
        const slot = target.querySelector('.event-errors');
        if (slot) slot.append(h('div', { class: 'error' }, e.message));
      }
    }
    el.querySelectorAll('.event-card').forEach((card) => {
      const i = card.dataset.index;
      card.classList.toggle('has-error', errors.some((e) => (e.field || '').startsWith(`events[${i}]`)));
    });
  }

  function focusError(e) {
    let key = e.field || '';
    let target = null;
    while (key && !(target = el.querySelector(`[data-field="${CSS.escape(key)}"]`))) key = key.replace(/(\.[^.[\]]+|\[\d+\])$/, '');
    if (!target) return;
    target.scrollIntoView({ behavior: 'smooth', block: 'center' });
    const input = target.querySelector('input, select, textarea');
    if (input) setTimeout(() => input.focus({ preventScroll: true }), 300);
  }

  // ---- details --------------------------------------------------------------------------
  function detailsCard() {
    const name = h('input', { class: 'input', value: draft.name, maxlength: 48, placeholder: 'e.g. Squirrel Study' });
    const id = h('input', {
      class: 'input mono', value: draft.id, maxlength: 32, readonly: !isNew, placeholder: 'squirrel-study',
      autocapitalize: 'off', autocomplete: 'off', spellcheck: 'false',
    });
    const desc = h('textarea', { class: 'input', maxlength: 200, placeholder: 'Optional notes for researchers' }, draft.description);
    name.oninput = () => {
      draft.name = name.value;
      if (isNew && !idTouched) { draft.id = slugify(name.value); id.value = draft.id; changed('id'); }
      changed('name');
    };
    id.oninput = () => { idTouched = true; draft.id = id.value.trim(); changed('id'); };
    desc.oninput = () => { draft.description = desc.value; changed('description'); };
    return h('div', { class: 'card form' },
      h('div', { class: 'form-row' },
        field('Name', name, { key: 'name' }),
        field('Program ID', id, {
          key: 'id',
          hint: isNew ? `Saved as /programs/${draft.id || '…'}.json` : 'Fixed after creation. Duplicate to change it.',
        })),
      field('Description', desc, { key: 'description' }));
  }

  // ---- timeline -------------------------------------------------------------------------
  function renderTimeline() {
    if (!timelineHost) return;
    const bad = new Set(errors.filter((e) => /OVERLAP|COLLISION/.test(e.code)).map((e) => (/events\[(\d+)\]/.exec(e.field || '') || [])[1]));
    const pct = (s) => `${(s / DAY_S) * 100}%`;
    const nowS = Math.floor(((deviceNow() / 1000 + offsetMinutes() * 60) % DAY_S + DAY_S) % DAY_S);
    const lanes = draft.events.map((ev, i) => {
      const cls = bad.has(String(i)) ? ' bad' : '';
      const marks = [];
      if (ev.type === 'point') {
        const t = secs(ev.time);
        if (t != null) marks.push(h('i', { class: 'tl-pt' + cls, style: { left: pct(t) } }));
      } else {
        const s = secs(ev.start), e = secs(ev.end);
        if (s != null && e != null && s !== e) {
          if (e > s) marks.push(h('i', { class: 'tl-bar' + cls, style: { left: pct(s), width: pct(e - s) } }));
          else {
            marks.push(h('i', { class: 'tl-bar' + cls, style: { left: pct(s), width: pct(DAY_S - s) } }));
            marks.push(h('i', { class: 'tl-bar' + cls, style: { left: 0, width: pct(e) } }));
          }
        }
      }
      return h('div', { class: 'tl-lane' },
        h('div', { class: 'tl-label', title: ev.event_id }, ev.event_id || `event ${i + 1}`),
        h('div', { class: 'tl-track' }, marks, h('i', { class: 'tl-now', style: { left: pct(nowS) } })));
    });
    const axis = h('div', { class: 'tl-lane tl-axis' }, h('div'),
      h('div', { class: 'tl-ticks' }, [0, 3, 6, 9, 12, 15, 18, 21, 24].map((hr) =>
        h('span', { style: { left: `${(hr / 24) * 100}%` } }, String(hr).padStart(2, '0')))));
    replace(timelineHost, h('div', { class: 'tl' }, lanes.length ? lanes : h('div', { class: 'muted small' }, 'No events yet.'), axis));
  }

  // ---- events ---------------------------------------------------------------------------
  function audioSelect(ev, key) {
    const known = audioFiles.some((f) => f.path === ev.audio);
    const sel = h('select', { class: 'select' },
      !known && h('option', { value: ev.audio || '', selected: true }, ev.audio ? `${ev.audio.replace('/audio/', '')} (missing)` : 'Choose…'),
      audioFiles.map((f) => h('option', { value: f.path, selected: f.path === ev.audio, disabled: !f.valid },
        f.name + (f.valid ? ` · ${fmtDuration(f.duration_ms)}` : ' (unsupported)'))));
    sel.onchange = () => { ev.audio = sel.value; changed(key); };
    return sel;
  }

  function volumeControl(ev, key) {
    const out = h('output', null, ev.volume);
    const range = h('input', { type: 'range', min: 0, max: 100, step: 1, value: ev.volume, 'aria-label': 'Volume' });
    range.oninput = () => { ev.volume = +range.value; out.value = range.value; changed(key); };
    return h('div', { class: 'range-wrap' }, range, out);
  }

  /** 24-hour text entry; native time pickers show AM/PM in some locales. */
  function timeInput(value, onChange) {
    const show = (v) => { const t = normalizeTime(v); return t ? (t.endsWith(':00') ? t.slice(0, 5) : t) : v || ''; };
    const inp = h('input', {
      class: 'input mono', value: show(value), placeholder: 'HH:MM', maxlength: 8,
      inputmode: 'numeric', autocomplete: 'off', spellcheck: 'false', pattern: '[0-9:]*',
    });
    inp.oninput = () => onChange(normalizeTime(inp.value) || inp.value);
    inp.onblur = () => { if (normalizeTime(inp.value)) inp.value = show(inp.value); };
    return inp;
  }

  function unitFor(ev) {
    if (!units.has(ev)) {
      const r = ev.recurrence;
      const vals = r.type === 'random' ? [r.min_interval_seconds, r.max_interval_seconds] : [r.interval_seconds];
      units.set(ev, vals.every((v) => Number.isInteger(v) && v % 60 === 0) ? 60 : 1);
    }
    return units.get(ev);
  }

  function intervalInput(ev, prop, key, label) {
    const u = unitFor(ev);
    const v = ev.recurrence[prop];
    const inp = h('input', {
      class: 'input num', type: 'number', min: u === 60 ? 0.1 : 1, step: u === 60 ? 'any' : 1, inputmode: 'decimal',
      value: Number.isFinite(v) ? +(v / u).toFixed(2) : '', 'aria-label': `${label} (${u === 60 ? 'minutes' : 'seconds'})`,
    });
    inp.oninput = () => {
      const n = parseFloat(inp.value);
      ev.recurrence[prop] = Number.isFinite(n) ? Math.round(n * unitFor(ev)) : null;
      changed(key);
    };
    return inp;
  }

  function unitSelect(ev) {
    const sel = h('select', { class: 'select', 'aria-label': 'Units' },
      h('option', { value: 60, selected: unitFor(ev) === 60 }, 'minutes'),
      h('option', { value: 1, selected: unitFor(ev) === 1 }, 'seconds'));
    sel.onchange = () => { units.set(ev, +sel.value); structural(); };
    return sel;
  }

  function recurrenceBlock(ev, i) {
    const r = ev.recurrence;
    const f = (k) => `events[${i}].recurrence.${k}`;
    const seg = h('div', { class: 'seg', role: 'group', 'aria-label': 'Playback pattern' },
      ['fixed', 'random'].map((t) => h('button', {
        type: 'button', 'aria-pressed': String(r.type === t),
        onclick: () => {
          if (r.type === t) return;
          if (t === 'random') {
            const base = r.interval_seconds || 600;
            ev.recurrence = { type: 'random', min_interval_seconds: Math.round(base * 0.8), max_interval_seconds: Math.round(base * 1.2) };
          } else {
            ev.recurrence = { type: 'fixed', interval_seconds: r.min_interval_seconds || 600 };
          }
          units.delete(ev);
          structural();
        },
      }, t)));

    const body = r.type === 'fixed'
      ? h('div', { class: 'form-row' },
        h('div', { class: 'field', dataset: { field: f('interval_seconds') } },
          h('label', null, 'Play every'),
          h('div', { class: 'input-group' }, intervalInput(ev, 'interval_seconds', f('interval_seconds'), 'Play every'), unitSelect(ev)),
          h('div', { class: 'hint' }, 'Anchored to the range start; independent of audio length.')))
      : h('div', { class: 'form-row' },
        h('div', { class: 'field', dataset: { field: f('min_interval_seconds') } },
          h('label', null, 'Minimum gap'),
          h('div', { class: 'input-group' }, intervalInput(ev, 'min_interval_seconds', f('min_interval_seconds'), 'Minimum gap'), unitSelect(ev))),
        h('div', { class: 'field', dataset: { field: f('max_interval_seconds') } },
          h('label', null, 'Maximum gap'),
          h('div', { class: 'input-group' }, intervalInput(ev, 'max_interval_seconds', f('max_interval_seconds'), 'Maximum gap'),
            h('span', { class: 'btn ghost', style: { pointerEvents: 'none' } }, unitFor(ev) === 60 ? 'min' : 's'))),
        h('div', { class: 'hint', style: { gridColumn: '1 / -1' } }, 'First play at the range start. Random gaps are deterministic per program revision and date.'));

    return h('div', { class: 'recurrence' }, h('div', { class: 'row' }, h('span', { class: 'eyebrow' }, 'Playback'), seg), body);
  }

  function eventCard(ev, i) {
    const f = (k) => `events[${i}]${k ? '.' + k : ''}`;
    const idInput = h('input', { class: 'input mono', value: ev.event_id || '', maxlength: 32, autocapitalize: 'off', spellcheck: 'false' });
    idInput.oninput = () => { ev.event_id = idInput.value.trim(); changed(f('event_id')); };

    const typeSeg = h('div', { class: 'seg', role: 'group', 'aria-label': 'Event type' },
      ['range', 'point'].map((t) => h('button', {
        type: 'button', 'aria-pressed': String(ev.type === t),
        onclick: () => {
          if (ev.type === t) return;
          if (t === 'point') {
            ev.time = ev.start || '12:00:00';
            delete ev.start; delete ev.end; delete ev.recurrence;
          } else {
            const s = secs(ev.time) ?? 6 * 3600;
            ev.start = hms(s); ev.end = hms(s + 3600);
            ev.recurrence = { type: 'fixed', interval_seconds: 600 };
            delete ev.time;
          }
          ev.type = t;
          units.delete(ev);
          structural();
        },
      }, t)));

    const removeBtn = button('', {
      size: 'sm', kind: 'ghost danger', iconName: 'trash', title: 'Remove event',
      onclick: async () => {
        const ok = await confirmDialog({ title: `Remove “${ev.event_id || 'event'}”?`, body: 'This only changes the draft until you Save & Apply.', confirm: 'Remove', danger: true });
        if (!ok) return;
        draft.events.splice(i, 1);
        errors = [];
        structural();
      },
    });

    const audioMeta = audioFiles.find((a) => a.path === ev.audio);
    const timing = ev.type === 'point'
      ? h('div', { class: 'form-row' },
        field('Time (daily, local)', timeInput(ev.time, (v) => { ev.time = v; changed(f('time')); }), { key: f('time') }))
      : h('div', { class: 'form-row' },
        field('Start (inclusive)', timeInput(ev.start, (v) => { ev.start = v; changed(f('start')); }), { key: f('start') }),
        field('End (exclusive)', timeInput(ev.end, (v) => { ev.end = v; changed(f('end')); }), {
          key: f('end'),
          hint: secs(ev.end) != null && secs(ev.start) != null && secs(ev.end) < secs(ev.start) ? 'Crosses midnight into the next day.' : null,
        }));

    return h('article', { class: `card event-card ${ev.type}`, dataset: { field: f(), index: i } },
      h('div', { class: 'card-head' },
        h('span', { class: 'event-index' }, String(i + 1).padStart(2, '0')),
        typeSeg,
        h('div', { class: 'actions' }, removeBtn)),
      h('div', { class: 'form' },
        h('div', { class: 'event-errors field' }),
        timing,
        h('div', { class: 'form-row' },
          field('Audio', audioSelect(ev, f('audio')), {
            key: f('audio'),
            hint: audioMeta ? `${fmtDuration(audioMeta.duration_ms)} · ${audioMeta.sample_rate / 1000} kHz · ${audioMeta.bits_per_sample}-bit` : null,
          }),
          h('div', { class: 'field', dataset: { field: f('volume') } }, h('label', null, 'Volume'), volumeControl(ev, f('volume')))),
        ev.type === 'range' && recurrenceBlock(ev, i),
        field('Event ID', idInput, { key: f('event_id'), hint: 'Written to the playback log for every play of this event.' })),
    );
  }

  function addEvent() {
    const prev = draft.events[draft.events.length - 1];
    const ids = new Set(draft.events.map((e) => e.event_id));
    const type = prev ? prev.type : 'range';
    let n = draft.events.length + 1;
    while (ids.has(`${type}-${n}`)) n++;
    const ev = { event_id: `${type}-${n}`, type, audio: prev ? prev.audio : '/audio/playback.wav', volume: prev ? prev.volume : 80 };
    if (type === 'point') {
      ev.time = hms((prev ? secs(prev.time) ?? 43200 : 43200) + 3600);
    } else {
      const s = prev ? secs(prev.end) ?? 21600 : 21600;
      ev.start = hms(s);
      ev.end = hms(s + 3600);
      ev.recurrence = prev ? clone(prev.recurrence) : { type: 'fixed', interval_seconds: 600 };
      if (prev) units.set(ev, unitFor(prev));
    }
    draft.events.push(ev);
    structural();
    requestAnimationFrame(() => el.querySelector(`.event-card[data-index="${draft.events.length - 1}"]`)?.scrollIntoView({ behavior: 'smooth', block: 'center' }));
  }

  // ---- validation / preview -------------------------------------------------------------
  function renderValidation() {
    if (!validationHost) return;
    clear(validationHost);
    if (!validation) return;
    const stale = validation.stale ? h('span', { class: 'muted' }, ' · draft changed since check') : '';
    if (validation.valid) {
      validationHost.append(h('div', { class: 'validation ok' }, h('div', { class: 'row' }, icon('check'),
        h('b', null, 'Valid'), h('span', { class: 'mono small' }, `revision ${validation.revision}`), stale)));
    } else {
      validationHost.append(h('div', { class: 'validation fault' },
        h('div', { class: 'row' }, icon('alert'), h('b', null, `${validation.errors.length} problem${validation.errors.length === 1 ? '' : 's'}`), stale),
        h('ul', null, validation.errors.map((e) => h('li', { onclick: () => focusError(e) }, h('code', null, e.code), h('span', null, e.message))))));
    }
  }

  function renderPreview() {
    if (!previewHost) return;
    clear(previewHost);
    if (!previewData) return;
    const today = toLocalIso(deviceNow(), offsetMinutes());
    const rows = [];
    let lastDay = null;
    for (const ev of previewData.events) {
      const day = ev.scheduled_local.slice(0, 10);
      if (day !== lastDay) {
        rows.push(h('tr', { class: 'day-sep' }, h('td', { colspan: 5 }, `${dayLabel(ev.scheduled_local, today)} · ${day}`)));
        lastDay = day;
      }
      rows.push(h('tr', null,
        h('td', null, h('b', null, fmtClock(ev.scheduled_local, { seconds: true }))),
        h('td', { class: 'muted' }, fmtUtcClock(ev.scheduled_utc)),
        h('td', { class: 'mono' }, ev.event_id),
        h('td', null, ev.audio.replace('/audio/', '')),
        h('td', null, ev.volume)));
    }
    previewHost.append(h('div', { class: 'card flush', style: { opacity: previewData.stale ? 0.55 : 1 } },
      h('div', { class: 'card-head', style: { padding: '14px 16px 0' } },
        h('div', null,
          h('h3', null, `Next ${previewData.events.length} playbacks`),
          h('div', { class: 'muted small' }, previewData.stale
            ? 'Draft changed — run preview again.'
            : `Generated by the device scheduler · revision ${previewData.revision}`))),
      previewData.events.length
        ? h('div', { class: 'table-wrap scroll-y' }, h('table', { class: 'table' },
          h('thead', null, h('tr', null, ['Local', 'UTC', 'Event', 'Audio', 'Vol'].map((t) => h('th', null, t)))),
          h('tbody', null, rows)))
        : h('div', { class: 'empty' }, 'No upcoming events.')));
  }

  function precheck() {
    const local = localCheck(draft, isNew);
    if (local.length) {
      errors = local;
      applyErrors();
      focusError(local[0]);
      toast(`Fix ${local.length} field${local.length === 1 ? '' : 's'} first.`, 'fault');
      return false;
    }
    return true;
  }

  function takeServerErrors(e) {
    if (!e.details || (!e.details.errors && !e.field)) { toastError(e); return; }
    errors = e.errors;
    validation = { valid: false, errors, revision: null };
    applyErrors();
    renderValidation();
    renderTimeline();
    toast(`Device rejected the program: ${errors.length} problem${errors.length === 1 ? '' : 's'}.`, 'fault', { code: errors[0].code });
  }

  async function doValidate(btn) {
    if (!precheck()) return;
    await busy(btn, async () => {
      try {
        const res = await api.validateProgram(draft.id, serialize(draft));
        validation = res;
        errors = res.errors;
        applyErrors();
        renderValidation();
        renderTimeline();
      } catch (e) { toastError(e); }
    });
  }

  async function doPreview(btn) {
    if (!precheck()) return;
    await busy(btn, async () => {
      try {
        const res = await api.previewProgram(draft.id, serialize(draft), previewCount);
        previewData = res;
        validation = { valid: true, revision: res.revision, errors: [] };
        errors = [];
        applyErrors();
        renderValidation();
        renderPreview();
        renderTimeline();
        previewHost.scrollIntoView({ behavior: 'smooth', block: 'start' });
      } catch (e) { takeServerErrors(e); }
    });
  }

  // ---- save / activate ------------------------------------------------------------------
  async function doSave(btn) {
    if (!precheck()) return;
    await busy(btn, async () => {
      try {
        const body = serialize(draft);
        if (isNew) {
          await api.createProgram(body);
          baseline = JSON.stringify(body);
          isNew = false;
          note = null;
          toast(`Saved /programs/${body.id}.json`, 'ok');
          location.hash = '#/programs/' + encodeURIComponent(body.id);
          return;
        }
        const res = await api.saveProgram(draft.id, body);
        summary = res.summary;
        baseline = JSON.stringify(body);
        errors = [];
        validation = { valid: true, revision: res.summary.revision, errors: [] };
        toast(res.applied ? 'Saved & applied. Schedule recalculated.' : 'Saved.', 'ok');
        refresh();
        renderAll();
      } catch (e) { takeServerErrors(e); }
    });
  }

  async function doActivate(btn) {
    const ok = await confirmDialog({
      title: `Activate “${draft.name}”?`,
      body: 'The device validates the program, recalculates the schedule and uses it for all scheduled playback. Missed events are not replayed.',
      confirm: 'Activate',
    });
    if (!ok) return;
    await busy(btn, async () => {
      try {
        await api.activateProgram(draft.id);
        activeId = draft.id;
        toast(`“${draft.name}” is now active.`, 'ok');
        refresh();
        renderAll();
      } catch (e) { takeServerErrors(e); }
    });
  }

  async function doDiscard() {
    if (!(await confirmDiscard())) return;
    if (isNew) { location.hash = '#/programs'; return; }
    draft = normalizeDraft(JSON.parse(baseline));
    errors = [];
    validation = null;
    previewData = null;
    renderAll();
  }

  function renderSaveBar() {
    if (!saveBar) return;
    const isDirty = dirty();
    const isActive = !isNew && draft.id === activeId;
    saveBar.className = `save-bar ${isDirty || isNew ? '' : 'clean'}`;
    const saveBtn = button(isActive ? 'Save & Apply' : 'Save', { kind: 'primary', iconName: 'check', disabled: !isDirty && !isNew });
    saveBtn.onclick = () => doSave(saveBtn);
    const actBtn = button('Activate', { kind: 'primary', iconName: 'check' });
    actBtn.onclick = () => doActivate(actBtn);
    replace(saveBar, 
      h('div', { class: 'status-text' },
        isNew ? 'New program — not saved' : isDirty ? 'Unsaved changes' : isActive ? [icon('check', 'sm'), 'Saved · active program'] : 'Saved'),
      (isDirty || isNew) && button('Discard', { onclick: doDiscard }),
      !isDirty && !isNew && !isActive ? actBtn : saveBtn,
    );
  }

  // ---- full render ----------------------------------------------------------------------
  function renderAll() {
    const isActive = !isNew && draft.id === activeId;
    saveBar = h('div', { class: 'save-bar' });
    timelineHost = h('div');
    validationHost = h('div', { class: 'stack' });
    previewHost = h('div');
    jsonPre = h('pre', null, JSON.stringify(serialize(draft), null, 2));

    const valBtn = button('Validate', { iconName: 'check' });
    valBtn.onclick = () => doValidate(valBtn);
    const prevBtn = button('Preview schedule', { kind: 'primary', iconName: 'eye' });
    prevBtn.onclick = () => doPreview(prevBtn);
    const countSel = h('select', { class: 'select', 'aria-label': 'Preview length' },
      [25, 100, 250].map((n) => h('option', { value: n, selected: n === previewCount }, `${n} events`)));
    countSel.onchange = () => { previewCount = +countSel.value; };

    replace(el, 
      h('a', { class: 'back', href: '#/programs' }, icon('back', 'sm'), 'Programs'),
      h('div', { class: 'page-head' },
        h('h1', null, isNew ? 'New program' : 'Edit program'),
        isActive && badge('Active', 'ink'),
        summary && h('span', { class: 'muted small mono' }, `rev ${summary.revision}`)),
      note && notice('', null, note),
      note && h('div', { style: { height: '12px' } }),
      detailsCard(),
      section('Daily timeline'),
      h('div', { class: 'card' }, timelineHost,
        h('p', { class: 'muted small', style: { marginTop: '10px' } }, 'Local wall-clock time, repeating every day. Green line marks the device’s current time.')),
      section(`Events · ${draft.events.length}`),
      h('div', { dataset: { field: 'events' }, class: 'stack' },
        h('div', { class: 'event-errors field' }),
        draft.events.map(eventCard)),
      button('Add event', {
        kind: 'add-event', iconName: 'plus', onclick: addEvent,
        disabled: draft.events.length >= 32,
      }),
      section('Check'),
      h('p', { class: 'muted small', style: { marginBottom: '12px' } },
        'The device validates the whole program and generates the preview with the same scheduler used for playback.'),
      h('div', { class: 'check-actions' },
        valBtn,
        h('div', { class: 'group' }, countSel, prevBtn)),
      validationHost,
      h('div', { style: { height: '12px' } }),
      previewHost,
      h('details', { class: 'json' }, h('summary', null, 'Program JSON'), jsonPre),
      h('div', { class: 'save-bar-spacer' }),
      saveBar,
    );
    renderTimeline();
    renderValidation();
    renderPreview();
    renderSaveBar();
    applyErrors();
  }

  load();

  const nowTimer = setInterval(() => draft && renderTimeline(), 30000);
  return {
    isDirty: () => dirty() || (isNew && !!draft && !!note),
    canLeave: async () => !(dirty() || (isNew && draft && note)) || confirmDiscard(),
    unmount: () => clearInterval(nowTimer),
  };
}
