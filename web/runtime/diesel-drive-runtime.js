import {loadSharedRecordedStarterRuntime} from './shared-recorded-starter-runtime.js';

export const DIESEL_DRIVE_PATH = 'diesel-drive/runtime.json';
const requireValue = (ok, message) => {
  if (!ok) throw new RangeError(`Invalid diesel driving package: ${message}`);
};
const number = (x, lo, hi) => requireValue(Number.isFinite(x) && x >= lo && x <= hi, 'numeric bound');
const keys = (x, expected) => requireValue(x && typeof x === 'object' && !Array.isArray(x) &&
  Object.keys(x).sort().join(',') === [...expected].sort().join(','), 'unknown or missing fields');
const hashPattern = /^[a-f0-9]{64}$/u;

export function validateDieselDrive(m, runtime) {
  keys(m, ['schema','engine','source_sha256','gain_db','starter','pickup','backdrive','vehicle','note']);
  requireValue(m.schema === 'crankwave/diesel-drive-v1' && m.engine === runtime.manifest.engine &&
    m.source_sha256 === runtime.manifest.source_sha256, 'source identity');
  requireValue(runtime.sampleRate === 192000 && runtime.batchFrames === 3840, 'audio clock');
  number(m.gain_db, -30, 0);
  requireValue(typeof m.note === 'string' && m.note.length < 4000, 'provenance note');
  const s = m.starter;
  keys(s, ['processed_path','processed_sha256','filter_graph','overrides']);
  requireValue(s.processed_path === 'starter-heavy.f32le' && hashPattern.test(s.processed_sha256), 'starter payload');
  requireValue(typeof s.filter_graph === 'string' && s.filter_graph.length < 1000, 'starter treatment');
  const ranges = {basePlaybackRate:[.4,1.5],catchPlaybackRate:[.4,1.5],speedUpStartRpm:[0,500],
    speedUpEndRpm:[100,1500],rpmSmoothingMilliseconds:[1,500],attackMilliseconds:[1,100],
    catchStarterGain:[0,1],handoffMilliseconds:[20,1000]};
  keys(s.overrides, Object.keys(ranges));
  for (const [key, range] of Object.entries(ranges)) number(s.overrides[key], ...range);
  requireValue(s.overrides.speedUpEndRpm > s.overrides.speedUpStartRpm, 'starter speed range');
  const p = m.pickup;
  keys(p, ['crank_seconds','crank_rpm','running_seconds','release_seconds','onset_seconds','curve']);
  number(p.crank_seconds, 0, 2); number(p.crank_rpm, 50, 400);
  number(p.running_seconds, p.crank_seconds + .2, 10); number(p.release_seconds, 0, .5);
  number(p.onset_seconds, .01, .5);
  requireValue(Math.abs(p.running_seconds * 50 - Math.round(p.running_seconds * 50)) < 1e-9, 'pickup frame boundary');
  requireValue(Array.isArray(p.curve) && p.curve.length >= 2 && p.curve.length <= 30, 'pickup curve');
  let previous = -1;
  for (const point of p.curve) {
    keys(point, ['seconds','rpm','fuel']);
    number(point.seconds, 0, 5); number(point.rpm, runtime.minimumRpm, runtime.maximumRpm);
    number(point.fuel, 0, 1);
    requireValue(point.seconds > previous, 'pickup order'); previous = point.seconds;
  }
  requireValue(p.curve[0].seconds === 0, 'pickup origin');
  const b = m.backdrive;
  const bounds = {engineInertiaKgM2:[.1,50],vehicleReflectedInertiaKgM2:[1,1000],
    frictionConstantNm:[0,1000],frictionNmPerRpm:[0,1],reflectedRoadDragNm:[0,500],
    takeupSeconds:[.01,2],referenceBackdriveTorqueNm:[1,2000],
    radiationHighpassHz:[20,1000],radiationLowpassHz:[100,10000]};
  keys(b, [...Object.keys(bounds), 'modes']);
  for (const [key, range] of Object.entries(bounds)) number(b[key], ...range);
  requireValue(b.radiationLowpassHz > b.radiationHighpassHz, 'radiation band');
  requireValue(Array.isArray(b.modes) && b.modes.length > 0 && b.modes.length <= 12, 'radiation modes');
  for (const mode of b.modes) {
    keys(mode, ['frequencyHz','q','gainPcmPerNm']);
    number(mode.frequencyHz, 20, 10000); number(mode.q, .3, 20); number(mode.gainPcmPerNm, 0, .001);
  }
  const v = m.vehicle;
  keys(v, ['shaft_rpm_per_mps','inertia_reference_gear','shift_seconds','minimum_gear_seconds','shift_rpm','idle_rpm']);
  requireValue(Array.isArray(v.shaft_rpm_per_mps) && v.shaft_rpm_per_mps.length >= 2 &&
    v.shaft_rpm_per_mps.length <= 8, 'gears');
  previous = Infinity;
  for (const ratio of v.shaft_rpm_per_mps) {
    number(ratio, 10, 1000); requireValue(ratio < previous, 'gear order'); previous = ratio;
  }
  requireValue(Number.isInteger(v.inertia_reference_gear), 'inertia gear');
  number(v.inertia_reference_gear, 1, v.shaft_rpm_per_mps.length);
  number(v.shift_seconds, .3, 3); number(v.minimum_gear_seconds, 1, 30);
  number(v.idle_rpm, runtime.minimumRpm, runtime.maximumRpm);
  number(v.shift_rpm, v.idle_rpm + 200, runtime.maximumRpm);
  requireValue(p.curve.at(-1).rpm === v.idle_rpm, 'pickup must settle at driving idle');
  return m;
}

export async function loadDieselDriveRuntime(url, runtime, geometry, {fetch, crypto}) {
  const read = async relative => {
    const response = await fetch(new URL(relative, url).href);
    requireValue(response.ok, `missing ${relative}`);
    return new Uint8Array(await response.arrayBuffer());
  };
  const manifestBytes = await read('runtime.json');
  requireValue(manifestBytes.length <= 16384, 'manifest size');
  const manifest = validateDieselDrive(JSON.parse(new TextDecoder().decode(manifestBytes)), runtime);
  // Authenticate the unchanged canonical starter before applying engine-local treatment.
  const shared = await loadSharedRecordedStarterRuntime(new URL('shared-starter/runtime.json', url).href, {fetch, crypto});
  const payload = await read(manifest.starter.processed_path);
  requireValue(payload.length === shared.samples.length * 4, 'treated starter length');
  const digest = Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256', payload)),
    x => x.toString(16).padStart(2, '0')).join('');
  requireValue(digest === manifest.starter.processed_sha256, 'treated starter digest');
  const samples = new Float32Array(shared.samples.length);
  const view = new DataView(payload.buffer, payload.byteOffset, payload.byteLength);
  for (let i = 0; i < samples.length; ++i) {
    samples[i] = view.getFloat32(i * 4, true);
    number(samples[i], -4, 4);
  }
  number(geometry.stroke_m, .01, 1);
  number(geometry.connecting_rod_length_m, geometry.stroke_m / 2 + .001, 3);
  return Object.freeze({manifest, geometry: Object.freeze({...geometry}),
    starter: Object.freeze({...shared, samples, settings: Object.freeze({...shared.settings, ...manifest.starter.overrides})})});
}
