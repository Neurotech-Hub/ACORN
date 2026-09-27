// Shared device status, polled from /api/v1/status. Views subscribe instead of polling themselves.

import { api } from './api.js';

const POLL_MS = 5000;
const POLL_PLAYING_MS = 1000;

const listeners = new Set();
let timer = null;
let failures = 0;

export const store = {
  status: null,
  fetchedAt: 0,
  online: true,
  error: null,
};

function emit() {
  for (const fn of listeners) {
    try { fn(store); } catch (e) { console.error(e); }
  }
}

export function subscribe(fn) {
  listeners.add(fn);
  if (store.status || store.error) fn(store);
  return () => listeners.delete(fn);
}

export async function refresh() {
  clearTimeout(timer);
  try {
    store.status = await api.status();
    store.fetchedAt = performance.now();
    store.online = true;
    store.error = null;
    failures = 0;
  } catch (e) {
    failures++;
    store.error = e;
    if (failures >= 2 || !store.status) store.online = false;
  }
  emit();
  const playing = store.status && store.status.playback && store.status.playback.state === 'playing';
  timer = setTimeout(refresh, !store.online ? 3000 : playing ? POLL_PLAYING_MS : POLL_MS);
  return store.status;
}

/** Device UTC "now", advanced locally between polls. Uses the device clock, not the browser's. */
export function deviceNow() {
  if (!store.status) return Date.now();
  return Date.parse(store.status.time.utc) + (performance.now() - store.fetchedAt);
}

export function offsetMinutes() {
  return store.status ? store.status.time.utc_offset_minutes : 0;
}
