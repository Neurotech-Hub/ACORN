// Program library: list, activate/deactivate, duplicate, upload/download, delete.

import { api } from '../api.js';
import { refresh } from '../store.js';
import {
  h, clear, replace, icon, badge, button, busy, pageHead, skeleton, errorState, toast, toastError,
  confirmDialog, downloadText, mark, notice,
} from '../ui.js';
import { setPendingDraft } from './editor.js';

function summarizeEvents(n) {
  return `${n} event${n === 1 ? '' : 's'}`;
}

export function mount(el) {
  let data = null;
  const fileInput = h('input', { type: 'file', accept: '.json,application/json', hidden: true, onchange: onUpload });

  async function load() {
    replace(el, skeleton([60, 160, 160]));
    try {
      data = await api.programs();
      render();
    } catch (e) {
      replace(el, pageHead('Programs'), errorState(e, load));
    }
  }

  async function onUpload() {
    const file = fileInput.files[0];
    fileInput.value = '';
    if (!file) return;
    try {
      const obj = JSON.parse(await file.text());
      setPendingDraft(obj, `Loaded from ${file.name}. Review, then Save & Apply to store it on the device.`);
      location.hash = '#/programs/new';
    } catch {
      toast(`${file.name} is not valid JSON.`, 'fault', { code: 'PROGRAM_JSON_INVALID' });
    }
  }

  async function download(id, btn) {
    await busy(btn, async () => {
      try {
        const { program } = await api.program(id);
        downloadText(`${id}.json`, JSON.stringify(program, null, 2) + '\n');
      } catch (e) { toastError(e); }
    });
  }

  async function duplicate(id, btn) {
    await busy(btn, async () => {
      try {
        const { program } = await api.program(id);
        let newId = `${program.id}-copy`.slice(0, 32);
        const ids = new Set(data.programs.map((p) => p.id));
        for (let i = 2; ids.has(newId); i++) newId = `${program.id}-copy-${i}`.slice(0, 32);
        setPendingDraft({ ...program, id: newId, name: `${program.name} (copy)` }, `Duplicate of '${program.id}'. Not saved yet.`);
        location.hash = '#/programs/new';
      } catch (e) { toastError(e); }
    });
  }

  async function activate(p, btn) {
    const ok = await confirmDialog({
      title: `Activate “${p.name}”?`,
      body: [
        h('p', null, 'The device will validate this program, recalculate its schedule, and use it for all scheduled playback.'),
        h('p', { class: 'muted' }, 'Missed events are never replayed. If validation fails, the current program stays active.'),
      ],
      confirm: 'Activate',
    });
    if (!ok) return;
    await busy(btn, async () => {
      try {
        await api.activateProgram(p.id);
        toast(`“${p.name}” is now active.`, 'ok');
        refresh();
        load();
      } catch (e) { toastError(e); }
    });
  }

  async function deactivate(p, btn) {
    const ok = await confirmDialog({
      title: `Deactivate “${p.name}”?`,
      body: [
        h('p', null, 'The device will have no active program and will not be armed. No scheduled playback runs until a program is activated again.'),
        h('p', { class: 'muted' }, 'The program stays on the SD card. Environmental logging and manual playback still work.'),
      ],
      confirm: 'Deactivate',
    });
    if (!ok) return;
    await busy(btn, async () => {
      try {
        await api.deactivateProgram(p.id);
        toast(`“${p.name}” deactivated. Device is not armed.`, 'ok');
        refresh();
        load();
      } catch (e) { toastError(e); }
    });
  }

  async function remove(p) {
    const ok = await confirmDialog({
      title: `Delete “${p.name}”?`,
      body: `This removes /programs/${p.id}.json from the SD card. Logs that reference it are kept.`,
      confirm: 'Delete', danger: true,
    });
    if (!ok) return;
    try {
      await api.deleteProgram(p.id);
      toast('Program deleted.', 'ok');
      load();
    } catch (e) { toastError(e); }
  }

  function card(p) {
    const actBtn = button('Activate', { size: 'sm', iconName: 'check', disabled: !p.valid });
    actBtn.onclick = () => activate(p, actBtn);
    const deactBtn = button('Deactivate', { size: 'sm', iconName: 'x' });
    deactBtn.onclick = () => deactivate(p, deactBtn);
    const dupBtn = button('Duplicate', { size: 'sm', iconName: 'copy' });
    dupBtn.onclick = () => duplicate(p.id, dupBtn);
    const dlBtn = button('JSON', { size: 'sm', iconName: 'download', title: 'Download JSON' });
    dlBtn.onclick = () => download(p.id, dlBtn);

    return h('article', { class: `card program-card ${p.active ? 'active' : ''}` },
      h('div', { class: 'title' },
        h('h3', null, h('a', { href: '#/programs/' + encodeURIComponent(p.id), style: { textDecoration: 'none' } }, p.name)),
        p.active && badge('Active', 'ink'),
        p.valid ? badge('Valid', 'ok', 'check') : badge(`${p.error_count} error${p.error_count === 1 ? '' : 's'}`, 'fault', 'alert')),
      p.description && h('p', { class: 'muted' }, p.description),
      h('div', { class: 'muted small mono' }, `${p.id} · ${summarizeEvents(p.event_count)} · rev ${p.revision}`),
      h('div', { class: 'actions' },
        h('a', { class: 'btn sm', href: '#/programs/' + encodeURIComponent(p.id) }, icon('edit', 'sm'), 'Edit'),
        p.active ? deactBtn : actBtn,
        dupBtn, dlBtn,
        h('span', { class: 'spacer' }),
        button('', {
          size: 'sm', kind: 'ghost danger', iconName: 'trash', title: p.active ? 'Active program cannot be deleted' : 'Delete',
          disabled: p.active, onclick: () => remove(p),
        })),
    );
  }

  function render() {
    const atLimit = data.programs.length >= data.limits.MAX_PROGRAMS;
    const active = data.programs.find((p) => p.active);
    replace(el, 
      pageHead('Programs', 'A program is a complete daily playback schedule. At most one is active at a time.',
        button('Upload JSON', { iconName: 'upload', onclick: () => fileInput.click(), disabled: atLimit }),
        h('a', { class: 'btn primary', href: '#/programs/new', 'aria-disabled': atLimit ? 'true' : null }, icon('plus'), 'New program')),
      fileInput,
      !active && notice('warn', 'No active program', 'Scheduled playback is blocked until a valid program is activated.'),
      data.programs.length
        ? h('div', { class: 'grid two', style: { marginTop: '12px' } },
          [...data.programs].sort((a, b) => (b.active - a.active) || a.name.localeCompare(b.name)).map(card))
        : h('div', { class: 'card empty' }, mark(), h('b', null, 'No programs on the SD card'),
          h('span', null, 'Create one, or upload a program JSON file.'),
          h('a', { class: 'btn primary', href: '#/programs/new' }, icon('plus'), 'New program')),
      h('p', { class: 'muted small', style: { marginTop: '16px' } },
        `${data.programs.length} of ${data.limits.MAX_PROGRAMS} programs · up to ${data.limits.MAX_EVENTS} events each`),
    );
  }

  load();
  return {};
}
