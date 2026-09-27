// Thin client for the ACORN device API (/api/v1). The UI never talks to hardware directly.

const BASE = '/api/v1';

export class ApiError extends Error {
  constructor(code, message, { field, details, status } = {}) {
    super(message);
    this.code = code;
    this.field = field;
    this.details = details || {};
    this.status = status;
  }
  /** Field-level errors: validation endpoints put the full list in details.errors. */
  get errors() {
    return this.details.errors || [{ code: this.code, message: this.message, field: this.field }];
  }
}

function toError(status, json) {
  const e = (json && json.error) || {};
  return new ApiError(e.code || `HTTP_${status}`, e.message || `Request failed (${status})`, {
    field: e.field, details: e.details, status,
  });
}

async function request(method, path, body) {
  const init = { method, headers: {} };
  if (body !== undefined) {
    init.headers['Content-Type'] = 'application/json';
    init.body = JSON.stringify(body);
  }
  let res;
  try {
    res = await fetch(BASE + path, init);
  } catch {
    throw new ApiError('NETWORK', 'Could not reach ACORN. Check that you are connected to its Wi-Fi.');
  }
  const type = res.headers.get('content-type') || '';
  if (!type.includes('application/json')) {
    if (!res.ok) throw new ApiError(`HTTP_${res.status}`, `Request failed (${res.status})`, { status: res.status });
    return res.text();
  }
  const json = await res.json().catch(() => null);
  if (!json || !json.ok) throw toError(res.status, json);
  return json.data;
}

/** Multipart upload with progress (fetch has no upload progress events). */
function upload(path, file, onProgress) {
  return new Promise((resolve, reject) => {
    const xhr = new XMLHttpRequest();
    xhr.open('POST', BASE + path);
    xhr.upload.onprogress = (e) => e.lengthComputable && onProgress && onProgress(e.loaded / e.total);
    xhr.onerror = () => reject(new ApiError('NETWORK', 'Upload interrupted. Check the Wi-Fi connection and try again.'));
    xhr.onload = () => {
      let json = null;
      try { json = JSON.parse(xhr.responseText); } catch { /* non-JSON */ }
      if (json && json.ok) resolve(json.data);
      else reject(toError(xhr.status, json));
    };
    const fd = new FormData();
    fd.append('file', file, file.name);
    xhr.send(fd);
  });
}

const enc = encodeURIComponent;

export const api = {
  status: () => request('GET', '/status'),
  extendSession: () => request('POST', '/session/extend'),

  device: () => request('GET', '/device'),
  updateDevice: (patch) => request('PUT', '/device', patch),

  time: () => request('GET', '/time'),
  setTime: (patch) => request('PUT', '/time', patch),

  audio: () => request('GET', '/audio'),
  audioFile: (name) => request('GET', `/audio/${enc(name)}`),
  uploadAudio: (file, onProgress) => upload('/audio', file, onProgress),
  deleteAudio: (name) => request('DELETE', `/audio/${enc(name)}`),
  playTest: (opts) => request('POST', '/audio/test', opts),
  stop: () => request('POST', '/audio/stop'),

  programs: () => request('GET', '/programs'),
  program: (id) => request('GET', `/programs/${enc(id)}`),
  createProgram: (p) => request('POST', '/programs', p),
  saveProgram: (id, p) => request('PUT', `/programs/${enc(id)}`, p),
  deleteProgram: (id) => request('DELETE', `/programs/${enc(id)}`),
  validateProgram: (id, draft) => request('POST', `/programs/${enc(id || '_draft')}/validate`, draft),
  previewProgram: (id, draft, count = 100) =>
    draft
      ? request('POST', `/programs/${enc(id || '_draft')}/preview?count=${count}`, draft)
      : request('GET', `/programs/${enc(id)}/preview?count=${count}`),
  activateProgram: (id) => request('POST', `/programs/${enc(id)}/activate`),

  environment: () => request('GET', '/environment'),
  updateEnvironment: (patch) => request('PUT', '/environment', patch),

  logs: () => request('GET', '/logs'),
  logTail: (kind, n = 20) => request('GET', `/logs/${kind}?tail=${n}`),
  logUrl: (kind) => `${BASE}/logs/${kind}`,

  firmware: () => request('GET', '/firmware'),
  uploadFirmware: (file, onProgress) => upload('/firmware', file, onProgress),
};
