// Audio pipeline for the converter: decode -> resample -> channel mix -> trim.
// Decoding and resampling use the browser (Web Audio API); the rest is plain
// Float32Array math so it can also run under Node for tests.

import { ACORN_WAV } from './wav-encoder.js';

export const CHANNEL_MODES = ['combine', 'left', 'right'];

const FORMAT_NAMES = { 1: 'PCM', 3: 'IEEE float', 6: 'A-law', 7: 'µ-law', 0xfffe: 'Extensible' };

/** Reads the fmt chunk of a RIFF/WAVE file; null if the bytes are not a WAV. */
export function readWavInfo(arrayBuffer) {
  const bytes = new Uint8Array(arrayBuffer);
  if (bytes.length < 12) return null;
  const view = new DataView(arrayBuffer);
  const tag = (off) => String.fromCharCode(bytes[off], bytes[off + 1], bytes[off + 2], bytes[off + 3]);
  if (tag(0) !== 'RIFF' || tag(8) !== 'WAVE') return null;

  let pos = 12;
  let info = null;
  while (pos + 8 <= bytes.length) {
    const id = tag(pos);
    const size = view.getUint32(pos + 4, true);
    if (id === 'fmt ' && size >= 16 && pos + 24 <= bytes.length) {
      let format = view.getUint16(pos + 8, true);
      if (format === 0xfffe && size >= 40 && pos + 34 <= bytes.length) format = view.getUint16(pos + 32, true);
      info = {
        format,
        formatName: FORMAT_NAMES[format] || `format ${format}`,
        channels: view.getUint16(pos + 10, true),
        sampleRate: view.getUint32(pos + 12, true),
        bitsPerSample: view.getUint16(pos + 22, true),
      };
    } else if (id === 'data') {
      break;
    }
    pos += 8 + size + (size & 1);
  }
  return info;
}

/** True when the header already matches what ACORN plays (format, channels, depth, rate). */
export function isAcornCompatible(info) {
  return !!info && info.format === ACORN_WAV.format && info.channels === ACORN_WAV.channels &&
    info.bitsPerSample === ACORN_WAV.bitsPerSample && ACORN_WAV.sampleRates.includes(info.sampleRate);
}

/** Default output rate: 48 kHz for the 8 kHz family (16, 32, 96 kHz...), otherwise 44.1 kHz. */
export function pickSampleRate(sourceRate) {
  return sourceRate > 0 && sourceRate % 8000 === 0 ? 48000 : 44100;
}

/**
 * Decodes any browser-supported audio file and resamples it to `sampleRate`.
 * decodeAudioData always delivers samples at its context's rate, so decoding in an
 * OfflineAudioContext at the target rate performs the resampling.
 */
export async function decodeAudio(arrayBuffer, sampleRate) {
  const Ctx = globalThis.OfflineAudioContext || globalThis.webkitOfflineAudioContext;
  if (!Ctx) throw new Error('This browser does not support the Web Audio API.');
  const ctx = new Ctx(1, 1, sampleRate);
  const copy = arrayBuffer.slice(0);
  return new Promise((resolve, reject) => {
    const fail = (e) => reject(new Error(e && e.message ? `Could not decode audio file (${e.message}).` : 'Could not decode audio file.'));
    const p = ctx.decodeAudioData(copy, resolve, fail);
    if (p && p.catch) p.catch(fail);
  });
}

export function channelsOf(audioBuffer) {
  return Array.from({ length: audioBuffer.numberOfChannels }, (_, i) => audioBuffer.getChannelData(i));
}

/**
 * Mixes channels to mono. combine = average of all channels (no clipping from summing);
 * left = channel 0; right = channel 1. Mono input is returned as is.
 */
export function mixToMono(channels, mode = 'combine') {
  if (channels.length === 1) return channels[0];
  if (mode === 'left') return channels[0];
  if (mode === 'right') return channels[1];
  const n = channels[0].length;
  const out = new Float32Array(n);
  const k = 1 / channels.length;
  for (const ch of channels) for (let i = 0; i < n; i++) out[i] += ch[i] * k;
  return out;
}

/** Frame range [start, end) for a trim in seconds, clamped to the buffer. */
export function trimRange(totalFrames, sampleRate, startSec, endSec) {
  const start = Math.max(0, Math.min(totalFrames, Math.round(startSec * sampleRate)));
  const end = Math.max(start, Math.min(totalFrames, Math.round(endSec * sampleRate)));
  return { start, end };
}

export function trimSamples(samples, sampleRate, startSec, endSec) {
  const { start, end } = trimRange(samples.length, sampleRate, startSec, endSec);
  return samples.subarray(start, end);
}

/** Peak and RMS over [start, end). */
export function measure(samples, start = 0, end = samples.length) {
  let peak = 0, sum = 0;
  for (let i = start; i < end; i++) {
    const v = samples[i];
    const a = v < 0 ? -v : v;
    if (a > peak) peak = a;
    sum += v * v;
  }
  return { peak, rms: end > start ? Math.sqrt(sum / (end - start)) : 0 };
}

/**
 * Flags stereo material that Combine handles badly over [start, end):
 * - one channel nearly silent: averaging halves the level (about -6 dB)
 * - channels out of phase: averaging cancels much of the signal
 */
export function stereoAdvice(channels, start, end) {
  if (channels.length < 2) return null;
  const left = measure(channels[0], start, end).rms;
  const right = measure(channels[1], start, end).rms;
  const loud = Math.max(left, right);
  if (loud < 1e-4) return null;
  if (Math.min(left, right) < loud * 0.05) return { kind: 'silent', silent: left < right ? 'left' : 'right' };
  const [l, r] = channels;
  let sum = 0;
  for (let i = start; i < end; i++) { const m = (l[i] + r[i]) * 0.5; sum += m * m; }
  const combined = Math.sqrt(sum / (end - start));
  if (combined < loud * 0.5) return { kind: 'phase' };
  return null;
}

/** Full conversion of a decoded buffer to the mono samples that get exported. */
export function processAudio(audioBuffer, { channelMode = 'combine', trimStart = 0, trimEnd = audioBuffer.duration } = {}) {
  const mono = mixToMono(channelsOf(audioBuffer), channelMode);
  return trimSamples(mono, audioBuffer.sampleRate, trimStart, trimEnd);
}

function cleanBase(name) {
  return String(name || '').replace(/^.*[\\/]/, '').toLowerCase().replace(/[^a-z0-9._-]+/g, '-').replace(/^[-.]+|[-.]+$/g, '');
}

/**
 * Name the device keeps on upload (firmware AudioLibrary::sanitizeName lowercases and
 * maps anything outside [a-z0-9._-] to "-"), always ending in ".wav".
 */
export function cleanFileName(name) {
  const base = cleanBase(String(name || '').replace(/\.wav$/i, ''));
  return `${base || 'audio'}.wav`;
}

/** Default export name: "Bird Call.mp3" -> "bird-call-acorn.wav". */
export function outputName(sourceName) {
  const base = cleanBase(String(sourceName || '').replace(/^.*[\\/]/, '').replace(/\.[^.]+$/, ''));
  return `${base || 'audio'}-acorn.wav`;
}
