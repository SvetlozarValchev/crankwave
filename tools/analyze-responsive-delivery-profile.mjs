#!/usr/bin/env node

// Report the parts of a responsive carrier that can be reduced for a game
// delivery profile.  This is deliberately an analysis tool: it never edits a
// baked package or rewrites an accepted manifest.

import fs from "node:fs/promises";
import path from "node:path";
import process from "node:process";

function usage() {
  throw new Error("usage: node tools/analyze-responsive-delivery-profile.mjs <package-directory> [--json]");
}

const args = process.argv.slice(2);
if (args.length < 1 || args.length > 2 || (args.length === 2 && args[1] !== "--json")) usage();
const packageDirectory = path.resolve(args[0]);
const jsonOutput = args[1] === "--json";

async function readJson(relativePath) {
  return JSON.parse(await fs.readFile(path.join(packageDirectory, relativePath), "utf8"));
}

async function walk(directory, relative = "") {
  const entries = await fs.readdir(directory, { withFileTypes: true });
  const result = [];
  for (const entry of entries) {
    const childRelative = path.join(relative, entry.name);
    const child = path.join(directory, entry.name);
    if (entry.isDirectory()) result.push(...await walk(child, childRelative));
    else if (entry.isFile()) {
      const stat = await fs.stat(child);
      result.push({ path: childRelative.split(path.sep).join("/"), bytes: stat.size });
    }
  }
  return result;
}

function sum(items) {
  return items.reduce((total, item) => total + item.bytes, 0);
}

function mb(bytes) {
  return bytes / 1_000_000;
}

function classify(file) {
  const normalized = file.path;
  if (normalized.startsWith("held/audio/")) return "held-phase";
  if (normalized.startsWith("directional/audio/")) return "directional-phase";
  if (normalized.startsWith("lifecycle/")) return "lifecycle-time";
  if (normalized.startsWith("shared-recorded-starter/audio/")) return "starter-time";
  if (normalized.startsWith("limiter/")) return "limiter-time";
  if (normalized.startsWith("held/") || normalized.startsWith("directional/")) return "responsive-metadata";
  return "other";
}

function isFloat32Payload(file) {
  return /(?:\.f32le|\.f32)$/u.test(file.path);
}

const files = await walk(packageDirectory);
const groups = new Map();
for (const file of files) {
  const group = classify(file);
  const value = groups.get(group) ?? { bytes: 0, files: 0, float32Bytes: 0 };
  value.bytes += file.bytes;
  value.files += 1;
  if (isFloat32Payload(file)) value.float32Bytes += file.bytes;
  groups.set(group, value);
}

const runtime = await readJson("runtime.json");
const held = await readJson(runtime.held_package_path);
const firstRoutePath = held.route_manifests?.[0]?.manifest_path;
if (typeof firstRoutePath !== "string") throw new Error("held package has no route manifest");
const route = await readJson(path.posix.join(path.posix.dirname(runtime.held_package_path), firstRoutePath));
const samplesPerCycle = Number(route.phase?.samples_per_cycle);
const residualCycleCount = Number(route.phase?.residual_cycle_count);
const minimumRpm = Number(held.domain?.minimum_rpm);
if (!Number.isSafeInteger(samplesPerCycle) || samplesPerCycle <= 0 ||
    !Number.isSafeInteger(residualCycleCount) || residualCycleCount <= 0 ||
    !Number.isFinite(minimumRpm) || minimumRpm <= 0) {
  throw new Error("held phase manifest has invalid resolution metadata");
}

// A four-stroke cycle spans two crank revolutions, so its duration is 120/RPM
// seconds. This is the effective sample rate of the phase grid at the floor.
const phaseNyquistAtFloorHz = samplesPerCycle * minimumRpm / 240;
const residualBytes = files
  .filter((file) => file.path.startsWith("held/audio/") && file.path.endsWith(".residuals.f32le"))
  .reduce((total, file) => total + file.bytes, 0);
const timeDomainGroups = ["lifecycle-time", "starter-time", "limiter-time"];
const timeDomainFloat32Bytes = timeDomainGroups.reduce(
  (total, group) => total + (groups.get(group)?.float32Bytes ?? 0),
  0,
);
const allFloat32Bytes = files.filter(isFloat32Payload).reduce((total, file) => total + file.bytes, 0);
const packageBytes = sum(files);
const projected48kFloat32 = packageBytes - timeDomainFloat32Bytes + Math.ceil(timeDomainFloat32Bytes / 4);
const projectedPcm16Audio = Math.ceil(allFloat32Bytes / 2);
const projected48kPcm16Audio = Math.ceil(
  (allFloat32Bytes - timeDomainFloat32Bytes) / 2 + timeDomainFloat32Bytes / 8,
);
const projectedResidual12 = Math.ceil(residualBytes * 12 / residualCycleCount);

const report = {
  package_directory: packageDirectory,
  package_bytes: packageBytes,
  package_megabytes: mb(packageBytes),
  source_audio: {
    sample_rate_hz: Number(runtime.audio?.sample_rate_hz),
    encoding: runtime.audio?.encoding,
  },
  groups: Object.fromEntries([...groups.entries()].map(([name, value]) => [name, {
    ...value,
    megabytes: mb(value.bytes),
    float32_megabytes: mb(value.float32Bytes),
  }])),
  phase_resolution: {
    samples_per_cycle: samplesPerCycle,
    residual_cycle_count: residualCycleCount,
    floor_rpm: minimumRpm,
    effective_floor_sample_rate_hz: samplesPerCycle * minimumRpm / 120,
    effective_floor_nyquist_hz: phaseNyquistAtFloorHz,
    note: "Phase-domain banks should retain this grid in the first 48 kHz profile; they are not ordinary 192 kHz time-domain recordings.",
  },
  projections: {
    delivery_48k_float32_bytes: projected48kFloat32,
    delivery_48k_float32_megabytes: mb(projected48kFloat32),
    pcm16_all_audio_bytes: projectedPcm16Audio,
    pcm16_all_audio_megabytes: mb(projectedPcm16Audio),
    delivery_48k_pcm16_audio_bytes: projected48kPcm16Audio,
    delivery_48k_pcm16_audio_megabytes: mb(projected48kPcm16Audio),
    twelve_cycle_residual_bytes: projectedResidual12,
    twelve_cycle_residual_megabytes: mb(projectedResidual12),
  },
  profile: {
    canonical_rate_hz: 192000,
    game_delivery_rate_hz: 48000,
    phase_samples_per_cycle: samplesPerCycle,
    status: "projection-only",
  },
};

if (jsonOutput) {
  process.stdout.write(`${JSON.stringify(report, null, 2)}\n`);
} else {
  console.log(`Package: ${packageDirectory}`);
  console.log(`Current: ${mb(packageBytes).toFixed(2)} MB at ${report.source_audio.sample_rate_hz} Hz`);
  for (const [name, value] of Object.entries(report.groups)) {
    console.log(`  ${name.padEnd(20)} ${value.megabytes.toFixed(2).padStart(8)} MB  (${value.files} files)`);
  }
  console.log(`Phase floor Nyquist: ${phaseNyquistAtFloorHz.toFixed(0)} Hz at ${minimumRpm.toFixed(1)} RPM (${samplesPerCycle} samples/cycle)`);
  console.log(`48 kHz Float32 projection: ${mb(projected48kFloat32).toFixed(2)} MB`);
  console.log(`48 kHz + PCM16 audio projection: ${mb(projected48kPcm16Audio).toFixed(2)} MB before metadata/IR redesign`);
  console.log(`12-cycle residual projection: ${mb(projectedResidual12).toFixed(2)} MB for residual banks only`);
}
