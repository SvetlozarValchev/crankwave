// Attach actual native limiter cut/recovery captures to a verified responsive bank.
// Native render owns the simulation, dry stems and telemetry; pack-crankwave owns
// the carrier. This does not synthesize a limiter by chopping a running sample.
import fs from 'node:fs/promises';
import path from 'node:path';
import { createHash } from 'node:crypto';
import { spawnSync } from 'node:child_process';
import { loadResponsiveAudioCrankwave } from '../web/runtime/crankwave-package.js';

const [carrierFile, engineFile, renderDirectory, outputDirectory, outputCarrier, binary = 'build/crankwave', ...options] = process.argv.slice(2);
if (!outputCarrier) throw new Error('Usage: node tools/bake-limiter-performance.mjs <carrier> <engine.json> <native-render-directory> <new-package-directory> <new-output.crankwave> [crankwave-binary] [--retain-diagnostics]');
const retainDiagnostics = options.length === 1 && options[0] === '--retain-diagnostics';
if (options.length > 1 || (options.length === 1 && !retainDiagnostics))
  throw new Error('Optional final argument must be --retain-diagnostics.');
const hash = bytes => createHash('sha256').update(bytes).digest('hex');
const json = async file => JSON.parse(await fs.readFile(file, 'utf8'));
const writeJson = (file, value) => fs.writeFile(file, JSON.stringify(value, null, 2) + '\n');
const original = new Uint8Array(await fs.readFile(carrierFile));
const loaded = await loadResponsiveAudioCrankwave(original);
const authored = await json(engineFile);
const render = await json(path.join(renderDirectory, 'manifest/render-manifest.v10.json'));
const telemetryBytes = await fs.readFile(path.join(renderDirectory, 'telemetry/engine-telemetry.v1.ndjson'));
const records = telemetryBytes.toString().trim().split('\n').map(line => JSON.parse(line));
const header = records[0];
const blocks = records.filter(r => r.record_type === 'block' && r.phase === 'audible');
const engineId = authored.engine.identity.id;
if (engineId !== loaded.package.descriptor.engineId || header.engine_id !== engineId || header.motion_mode !== 'free_engine')
  throw new Error('Limiter capture must be the same engine in free-engine mode.');
const limiter = authored.engine.ignition.limiter;
if (limiter.activation_speed.unit !== 'rpm' || limiter.cut_duration.unit !== 's') throw new Error('Expected RPM/seconds limiter units.');
// Startup can begin at zero RPM. Publish settled pre-rev idle, not the first
// cranking telemetry sample, because the harness uses this as its idle target.
const firstThrottle = blocks.findIndex(b => b.telemetry.engine.requested_throttle_01 > .05);
const idleCandidates = blocks.slice(0, firstThrottle < 0 ? blocks.length : firstThrottle)
  .filter(b => !b.telemetry.engine.starter_enabled && b.telemetry.engine.ignition_enabled &&
    b.telemetry.engine.fuel_enabled && b.telemetry.engine.engine_speed_rpm > loaded.runtime.minimumRpm)
  .slice(-100).map(b => b.telemetry.engine.engine_speed_rpm).sort((a,b) => a-b);
if (idleCandidates.length < 25) throw new Error('Limiter capture needs at least 0.5 seconds of running idle before throttle.');
const idleRpm = (idleCandidates[Math.floor((idleCandidates.length-1)/2)] + idleCandidates[Math.floor(idleCandidates.length/2)])/2;
const rate = Number(header.delivery_rate_hz.numerator) / Number(header.delivery_rate_hz.denominator);
if (rate !== 192000) throw new Error('Limiter dry stems must use canonical 192 kHz presentation.');
const activations = blocks.filter(b => Number(b.event_counters.limiter_activation_count) === 1);
const releases = blocks.filter(b => Number(b.event_counters.limiter_release_count) === 1);
if (activations.length < 10 || releases.length < 10) throw new Error('Capture needs at least ten actual limiter cut/recovery events.');
const frame = b => Number(b.audition_frame_range.begin);
const fadeFrames = Math.round(rate * .005);
const clips = [];
let outputFrames = 0;
// Skip the first settling cuts; retain eight independent native performances.
for (let i = 3; i < activations.length - 1 && clips.length < 8; i += 3) {
  const start = frame(activations[i]), end = frame(activations[i + 1]);
  const release = releases.find(b => frame(b) > start && frame(b) < end);
  if (!release) continue;
  const released = frame(release);
  const holdEnd = released - header.delivery_frames_per_block;
  const holdStart = holdEnd - Math.round(rate * 120 / release.telemetry.engine.engine_speed_rpm);
  if (holdStart <= start + fadeFrames || end - released < 2 * fadeFrames) continue;
  clips.push({ start_frame: outputFrames, release_frame: outputFrames + released - start,
    end_frame: outputFrames + end - start, hold_start_frame: outputFrames + holdStart - start,
    hold_end_frame: outputFrames + holdEnd - start, source_start_frame: start, source_end_frame: end });
  outputFrames += end - start;
}
if (clips.length < 4) throw new Error('Insufficient bounded limiter performances after event selection.');
await fs.mkdir(outputDirectory); // Never overwrite another package tree.
for (const entry of loaded.package.entries) {
  // Native telemetry is evidence for the bake and its hash remains in the
  // limiter provenance, but the browser runtime never reads the NDJSON. Keep
  // it beside the build artifacts by default instead of paying its transfer,
  // verification, and carrier-memory cost on every live load.
  if (!retainDiagnostics && (entry.path === 'limiter/telemetry.ndjson' || entry.path === 'bake-report.json')) continue;
  const file = path.join(outputDirectory, entry.path);
  await fs.mkdir(path.dirname(file), { recursive: true });
  await fs.writeFile(file, original.subarray(Number(entry.offset), Number(entry.offset) + Number(entry.byteCount)));
}
await fs.mkdir(path.join(outputDirectory, 'limiter'));
const routes = [];
for (const route of loaded.runtime.routePresentations) {
  // Resolve by the declared dry bus, never by directory order.
  const dryId = loaded.runtime.heldPackage.busIds[routes.length];
  const selected = render.content.artifacts.find(a => a.role === dryId);
  if (!selected) throw new Error(`Missing native dry stem ${dryId}`);
  const wav = await fs.readFile(path.join(renderDirectory, selected.relative_path));
  if (hash(wav) !== selected.payload_sha256) throw new Error('Native dry stem hash mismatch.');
  let pcm, format;
  for (let offset = 12; offset + 8 <= wav.length;) {
    const kind = wav.toString('ascii', offset, offset + 4), bytes = wav.readUInt32LE(offset + 4);
    if (kind === 'fmt ') format = wav.subarray(offset + 8, offset + 8 + bytes);
    if (kind === 'data') pcm = wav.subarray(offset + 8, offset + 8 + bytes);
    offset += 8 + bytes + (bytes & 1);
  }
  if (!pcm || !format || format.readUInt16LE(0) !== 3 || format.readUInt16LE(2) !== 1 || format.readUInt32LE(4) !== rate || format.readUInt16LE(14) !== 32)
    throw new Error('Expected float32 mono native dry WAV.');
  const samples = Buffer.concat(clips.map(c => pcm.subarray(c.source_start_frame * 4, c.source_end_frame * 4)));
  if (samples.length !== outputFrames * 4) throw new Error('Limiter slice exceeds native capture.');
  const file = `route-${routes.length}.f32`;
  await fs.writeFile(path.join(outputDirectory, 'limiter', file), samples);
  const referenceRms = clips.map(c => {
    let energy = 0;
    for (let f = c.release_frame; f < c.end_frame; ++f) energy += samples.readFloatLE(f * 4) ** 2;
    return Math.sqrt(energy / (c.end_frame - c.release_frame));
  });
  routes.push({ bus_id: dryId, file, sha256: hash(samples), reference_rms: referenceRms });
}
const rpmValues = blocks.filter(b => b.telemetry.engine.limiter_cut_active).map(b => b.telemetry.engine.engine_speed_rpm);
const manifest = { schema: 'crankwave/limiter-performance-v1', engine: engineId, sample_rate_hz: rate,
  idle_rpm: idleRpm,
  activation_rpm: limiter.activation_speed.value, cut_seconds: limiter.cut_duration.value,
  maximum_rpm: limiter.activation_speed.value + 200, fade_frames: fadeFrames,
  frame_count: outputFrames, clips, routes,
  provenance: { base_carrier_sha256: hash(original), engine_json_sha256: hash(await fs.readFile(engineFile)),
    telemetry_sha256: hash(telemetryBytes), simulation_request_sha256: header.simulation_request_identity_v7_sha256,
    physics_rate_hz: Number(header.physics_rate_hz.numerator), event_resolution_frames: header.delivery_frames_per_block,
    activation_count: activations.length, release_count: releases.length,
    idle_rpm_method: 'median-of-last-up-to-100-running-idle-blocks-before-first-throttle',
    cut_rpm_minimum: Math.min(...rpmValues), cut_rpm_maximum: Math.max(...rpmValues),
    representation: 'native-free-engine-dry-limiter-cut-and-recovery-performances;unpitched;20ms-event-bounds' } };
await writeJson(path.join(outputDirectory, 'limiter/runtime.json'), manifest);
if (retainDiagnostics) await fs.writeFile(path.join(outputDirectory, 'limiter/telemetry.ndjson'), telemetryBytes);
const runtimeFile = path.join(outputDirectory, 'runtime.json');
const runtime = await json(runtimeFile);
runtime.limiter_package_path = 'limiter/runtime.json';
await writeJson(runtimeFile, runtime);
const descriptorFile = path.join(outputDirectory, 'crankwave.json');
const descriptor = await json(descriptorFile);
descriptor.runtime.manifest_sha256 = hash(await fs.readFile(runtimeFile));
await writeJson(descriptorFile, descriptor);
const result = spawnSync(path.resolve(binary), ['pack-crankwave', '--package-directory', path.resolve(outputDirectory), '--output', path.resolve(outputCarrier), '--result-format', 'json'], { encoding: 'utf8' });
if (result.status !== 0) throw new Error(result.stdout + result.stderr);
console.log(result.stdout.trim());
console.log(JSON.stringify({ clips: clips.length, frame_count: outputFrames, manifest }, null, 2));
