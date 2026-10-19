// Writes the WAV format ACORN accepts (firmware WavReader::isSupported):
// linear PCM (format 1), 16-bit, mono, 44.1 or 48 kHz, canonical 44-byte header.

export const ACORN_WAV = Object.freeze({ format: 1, channels: 1, bitsPerSample: 16, sampleRates: [44100, 48000] });

const HEADER_BYTES = 44;

export function wavByteLength(frames) {
  return HEADER_BYTES + frames * 2;
}

function writeString(view, offset, str) {
  for (let i = 0; i < str.length; i++) view.setUint8(offset + i, str.charCodeAt(i));
}

/** Float32 samples (nominal -1..1, clamped) -> 16-bit PCM mono WAV bytes. */
export function encodeWavBytes(samples, sampleRate) {
  if (!ACORN_WAV.sampleRates.includes(sampleRate)) throw new Error(`ACORN needs 44100 or 48000 Hz, not ${sampleRate} Hz.`);
  const numChannels = ACORN_WAV.channels;
  const bytesPerSample = ACORN_WAV.bitsPerSample / 8;
  const blockAlign = numChannels * bytesPerSample;
  const byteRate = sampleRate * blockAlign;
  const dataSize = samples.length * bytesPerSample;

  const buffer = new ArrayBuffer(HEADER_BYTES + dataSize);
  const view = new DataView(buffer);

  writeString(view, 0, 'RIFF');
  view.setUint32(4, 36 + dataSize, true);
  writeString(view, 8, 'WAVE');

  writeString(view, 12, 'fmt ');
  view.setUint32(16, 16, true);
  view.setUint16(20, ACORN_WAV.format, true);
  view.setUint16(22, numChannels, true);
  view.setUint32(24, sampleRate, true);
  view.setUint32(28, byteRate, true);
  view.setUint16(32, blockAlign, true);
  view.setUint16(34, ACORN_WAV.bitsPerSample, true);

  writeString(view, 36, 'data');
  view.setUint32(40, dataSize, true);

  // Asymmetric scaling matches Chromium's 16-bit decoder (x / 32767 above zero, x / 32768
  // below), so an untouched 16-bit source round-trips bit for bit.
  let offset = HEADER_BYTES;
  for (let i = 0; i < samples.length; i++) {
    const s = samples[i] > 1 ? 1 : samples[i] < -1 ? -1 : samples[i] || 0;
    view.setInt16(offset, Math.round(s < 0 ? s * 0x8000 : s * 0x7fff), true);
    offset += 2;
  }
  return new Uint8Array(buffer);
}

export function encodeWav(samples, sampleRate) {
  return new Blob([encodeWavBytes(samples, sampleRate)], { type: 'audio/wav' });
}
