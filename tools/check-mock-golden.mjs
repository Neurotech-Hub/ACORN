// Verifies that the mock scheduler (dev/mock) and the firmware scheduler
// (AcornCore, via arduino/host-tests/golden/*.json) agree exactly on
// program revision and preview output.
// Usage: node tools/check-mock-golden.mjs

import { readdirSync, readFileSync } from 'node:fs';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { preview, programRevision } from '../dev/mock/scheduler.mjs';

const root = join(dirname(fileURLToPath(import.meta.url)), '..');
const dir = join(root, 'arduino', 'host-tests', 'golden');
const files = readdirSync(dir).filter((f) => f.endsWith('.json')).sort();
if (!files.length) {
  console.error(`no golden files in ${dir}; run arduino/host-tests/run.sh --write-golden`);
  process.exit(1);
}

let failed = 0;
for (const f of files) {
  const g = JSON.parse(readFileSync(join(dir, f), 'utf8'));
  const problems = [];
  const rev = programRevision(g.program);
  if (rev !== g.revision) problems.push(`revision mock=${rev} firmware=${g.revision}`);
  const got = preview(g.program, g.utc_offset_minutes, Date.parse(g.from_utc), g.count)
    .map(({ event_id, scheduled_utc, scheduled_local }) => ({ event_id, scheduled_utc, scheduled_local }));
  if (got.length !== g.events.length) problems.push(`count mock=${got.length} firmware=${g.events.length}`);
  const n = Math.min(got.length, g.events.length);
  for (let i = 0; i < n && problems.length < 5; i++) {
    const a = got[i], b = g.events[i];
    if (a.event_id !== b.event_id || a.scheduled_utc !== b.scheduled_utc || a.scheduled_local !== b.scheduled_local) {
      problems.push(`#${i} mock=${JSON.stringify(a)} firmware=${JSON.stringify(b)}`);
    }
  }
  if (problems.length) {
    failed++;
    console.log(`[FAIL] ${f}`);
    for (const p of problems) console.log(`  ${p}`);
  } else {
    console.log(`[PASS] ${f} (${g.events.length} events, revision ${rev})`);
  }
}
console.log(`==== ${files.length} golden files, ${failed} failed ====`);
process.exit(failed ? 1 : 0);
