// Minimal multipart, WAV header and ESP32 image parsing for the mock device.

export function parseMultipart(buf, contentType) {
  const m = /boundary=(?:"([^"]+)"|([^;]+))/i.exec(contentType || '');
  if (!m) return null;
  const boundary = Buffer.from('--' + (m[1] || m[2]));
  const parts = [];
  let pos = buf.indexOf(boundary);
  while (pos !== -1) {
    const start = pos + boundary.length;
    if (buf.slice(start, start + 2).toString() === '--') break;
    const headerEnd = buf.indexOf('\r\n\r\n', start);
    if (headerEnd === -1) break;
    const headers = buf.slice(start + 2, headerEnd).toString();
    const next = buf.indexOf(boundary, headerEnd);
    if (next === -1) break;
    const data = buf.slice(headerEnd + 4, next - 2);
    const name = /name="([^"]*)"/i.exec(headers)?.[1];
    const filename = /filename="([^"]*)"/i.exec(headers)?.[1];
    parts.push({ name, filename, data });
    pos = next;
  }
  return parts;
}

export const WAV_RULES = { bits: 16, channels: 1, rates: [44100, 48000] };

export function inspectWav(buf) {
  const corrupt = (message) => ({ valid: false, error: { code: 'WAV_CORRUPT', message } });
  if (buf.length < 12 || buf.toString('ascii', 0, 4) !== 'RIFF' || buf.toString('ascii', 8, 12) !== 'WAVE') {
    return corrupt('Not a RIFF/WAVE file');
  }
  let off = 12, fmt = null, dataBytes = null;
  while (off + 8 <= buf.length) {
    const id = buf.toString('ascii', off, off + 4);
    const size = buf.readUInt32LE(off + 4);
    if (id === 'fmt ' && off + 24 <= buf.length) {
      fmt = {
        format: buf.readUInt16LE(off + 8),
        channels: buf.readUInt16LE(off + 10),
        sample_rate: buf.readUInt32LE(off + 12),
        bits_per_sample: buf.readUInt16LE(off + 22),
      };
    } else if (id === 'data') {
      dataBytes = Math.min(size, buf.length - off - 8);
      break;
    }
    off += 8 + size + (size & 1);
  }
  if (!fmt) return corrupt('Missing fmt chunk');
  if (dataBytes == null) return corrupt('Missing data chunk');
  const meta = {
    sample_rate: fmt.sample_rate,
    bits_per_sample: fmt.bits_per_sample,
    channels: fmt.channels,
    duration_ms: Math.round((dataBytes / (fmt.sample_rate * fmt.channels * (fmt.bits_per_sample / 8) || 1)) * 1000),
  };
  const problems = [];
  if (fmt.format !== 1) problems.push('not linear PCM');
  if (fmt.bits_per_sample !== WAV_RULES.bits) problems.push(`${fmt.bits_per_sample}-bit`);
  if (fmt.channels !== WAV_RULES.channels) problems.push(fmt.channels === 2 ? 'stereo' : `${fmt.channels} channels`);
  if (!WAV_RULES.rates.includes(fmt.sample_rate)) problems.push(`${fmt.sample_rate} Hz`);
  if (problems.length) {
    return { ...meta, valid: false, error: { code: 'WAV_UNSUPPORTED', message: `Unsupported: ${problems.join(', ')}` } };
  }
  return { ...meta, valid: true };
}

// ESP-IDF app image: 0xE9 magic; esp_app_desc_t (magic 0xABCD5432) follows the first segment header.
export function inspectFirmware(buf) {
  if (buf.length < 256 || buf[0] !== 0xe9) return { valid: false, message: 'Not an ESP32 application image (bad magic byte).' };
  const descOff = 24 + 8;
  let version = null;
  if (buf.readUInt32LE(descOff) === 0xabcd5432) {
    version = buf.toString('ascii', descOff + 16, descOff + 48).replace(/\0.*$/s, '') || null;
  }
  return { valid: true, version };
}
