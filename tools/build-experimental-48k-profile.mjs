#!/usr/bin/env node

// Build an isolated, directory-form 48 kHz delivery profile from an accepted
// responsive package. This is intentionally experimental: it does not edit
// the source package and does not claim native bake parity. The native package
// contract still needs a corresponding profile before this can be published as
// a complete carrier.

import { createHash } from "node:crypto";
import fs from "node:fs/promises";
import path from "node:path";
import process from "node:process";
import { spawn } from "node:child_process";

const SOURCE_RATE = 192_000;
const TARGET_RATE = 48_000;
const DECIMATION = SOURCE_RATE / TARGET_RATE;
const TARGET_BLOCK_FRAMES = 8_192;
const TARGET_FFT_SIZE = 16_384;
const PCM16_ENCODING = "pcm16le-scale-v1";

function usage() {
  throw new Error("usage: node tools/build-experimental-48k-profile.mjs <source-package-directory> <new-output-directory>");
}

const args = process.argv.slice(2);
if (args.length < 2 || args.length > 3) usage();
const sourceRoot = path.resolve(args[0]);
const outputRoot = path.resolve(args[1]);
const outputCarrier = args[2] === undefined ? null : path.resolve(args[2]);

async function readJson(root, relativePath) {
  return JSON.parse(await fs.readFile(path.join(root, relativePath), "utf8"));
}

async function writeJson(root, relativePath, value) {
  const file = path.join(root, relativePath);
  await fs.writeFile(file, `${JSON.stringify(value, null, 2)}\n`, { flag: "w" });
}

function digest(bytes) {
  return createHash("sha256").update(bytes).digest("hex");
}

async function bytes(root, relativePath) {
  return new Uint8Array(await fs.readFile(path.join(root, relativePath)));
}

async function writeBytes(root, relativePath, value) {
  const file = path.join(root, relativePath);
  await fs.mkdir(path.dirname(file), { recursive: true });
  await fs.writeFile(file, value, { flag: "w" });
}

function run(command, commandArgs) {
  return new Promise((resolve, reject) => {
    const child = spawn(command, commandArgs, { stdio: ["ignore", "ignore", "pipe"] });
    let stderr = "";
    child.stderr.on("data", (chunk) => { stderr += chunk.toString(); });
    child.once("error", reject);
    child.once("exit", (code, signal) => {
      if (code === 0) resolve();
      else reject(new Error(`${command} failed (${code ?? signal}): ${stderr}`));
    });
  });
}

async function resampleF32(relativePath) {
  const source = path.join(outputRoot, relativePath);
  const temporary = `${source}.48k.tmp`;
  await run("ffmpeg", [
    "-v", "error", "-y",
    "-f", "f32le", "-ar", String(SOURCE_RATE), "-ac", "1", "-i", source,
    "-f", "f32le", "-ar", String(TARGET_RATE), "-ac", "1", temporary,
  ]);
  await fs.rename(temporary, source);
}

async function f32Stats(relativePath) {
  const payload = await bytes(outputRoot, relativePath);
  if (payload.byteLength % 4 !== 0) throw new Error(`${relativePath} is not f32le`);
  const samples = new Float32Array(payload.buffer, payload.byteOffset, payload.byteLength / 4);
  let peak = 0;
  let energy = 0;
  for (const sample of samples) {
    if (!Number.isFinite(sample)) throw new Error(`${relativePath} has a non-finite sample`);
    peak = Math.max(peak, Math.abs(sample));
    energy += sample * sample;
  }
  return { payload, frameCount: samples.length, byteCount: payload.byteLength, peak, rms: Math.sqrt(energy / samples.length) };
}

function compactPcm16(payload) {
  if (payload.byteLength % 4 !== 0) throw new Error("source payload is not f32le");
  const source = new Float32Array(payload.buffer, payload.byteOffset, payload.byteLength / 4);
  let scale = 0;
  for (const sample of source) {
    if (!Number.isFinite(sample)) throw new Error("source payload has a non-finite sample");
    scale = Math.max(scale, Math.abs(sample));
  }
  scale = Math.max(scale, 1e-9);
  const output = new Uint8Array(source.length * 2);
  const view = new DataView(output.buffer);
  const samples = new Float32Array(source.length);
  let peak = 0;
  let energy = 0;
  for (let index = 0; index < source.length; ++index) {
    const quantized = Math.max(-32768, Math.min(32767,
      Math.round(source[index] / scale * 32768)));
    view.setInt16(index * 2, quantized, true);
    const decoded = quantized / 32768 * scale;
    samples[index] = decoded;
    peak = Math.max(peak, Math.abs(decoded));
    energy += decoded * decoded;
  }
  return {
    payload: output,
    samples,
    frameCount: samples.length,
    byteCount: output.byteLength,
    peak,
    rms: Math.sqrt(energy / Math.max(1, samples.length)),
    scale,
  };
}

async function compactF32(relativePath) {
  return compactPcm16(await bytes(outputRoot, relativePath));
}

function scaleFrameFields(value) {
  if (Array.isArray(value)) return value.map(scaleFrameFields);
  if (value === null || typeof value !== "object") return value;
  const result = {};
  for (const [key, entry] of Object.entries(value)) {
    if (typeof entry === "number" && Number.isFinite(entry) &&
        (/^frame$/u.test(key) || /(?:_frame|_frames|frame_count)$/u.test(key))) {
      result[key] = Math.round(entry / DECIMATION);
    } else {
      result[key] = scaleFrameFields(entry);
    }
  }
  return result;
}

function updateScenarioRates(value) {
  if (Array.isArray(value)) return value.map(updateScenarioRates);
  if (value === null || typeof value !== "object") return value;
  const result = {};
  for (const [key, entry] of Object.entries(value)) {
    if (key === "delivery_rate_hz") result[key] = TARGET_RATE;
    else if (key === "rates" && entry && typeof entry === "object") {
      result[key] = updateScenarioRates(entry);
      if (result[key].delivery?.numerator !== undefined) {
        result[key].delivery.numerator = String(TARGET_RATE);
      }
    } else if (key === "delivery" && entry && typeof entry === "object") {
      result[key] = updateScenarioRates(entry);
      if (result[key].numerator !== undefined) result[key].numerator = String(TARGET_RATE);
    } else {
      result[key] = updateScenarioRates(entry);
    }
  }
  return result;
}

function transformDocument(value) {
  return updateScenarioRates(scaleFrameFields(value));
}

async function packCarrier(root, output) {
  const files = [];
  async function collect(directory, relative = "") {
    for (const entry of await fs.readdir(directory, { withFileTypes: true })) {
      const childRelative = path.posix.join(relative, entry.name);
      const child = path.join(directory, entry.name);
      if (entry.isDirectory()) await collect(child, childRelative);
      else if (entry.isFile()) files.push({ path: childRelative, bytes: await fs.readFile(child) });
    }
  }
  await collect(root);
  files.sort((left, right) => left.path.localeCompare(right.path));
  const indexBytes = files.reduce((sum, entry) => sum + 56 + Buffer.byteLength(entry.path), 0);
  const payloadBytes = files.reduce((sum, entry) => sum + entry.bytes.length, 0);
  const payloadOffset = 128 + indexBytes;
  const container = Buffer.alloc(payloadOffset + payloadBytes);
  container.write("CRKWAVE1", 0, "ascii");
  container.writeUInt16LE(1, 8);
  container.writeUInt16LE(128, 10);
  container.writeUInt32LE(0, 12);
  container.writeUInt32LE(files.length, 16);
  container.writeUInt32LE(56, 20);
  container.writeBigUInt64LE(128n, 24);
  container.writeBigUInt64LE(BigInt(indexBytes), 32);
  container.writeBigUInt64LE(BigInt(payloadOffset), 40);
  container.writeBigUInt64LE(BigInt(payloadBytes), 48);
  container.writeBigUInt64LE(BigInt(container.length), 56);
  let indexCursor = 128;
  let payloadCursor = payloadOffset;
  for (const entry of files) {
    const pathBytes = Buffer.from(entry.path, "utf8");
    const fileHash = createHash("sha256").update(entry.bytes).digest();
    container.writeUInt16LE(pathBytes.length, indexCursor);
    container.writeUInt16LE(0, indexCursor + 2);
    container.writeUInt32LE(0, indexCursor + 4);
    container.writeBigUInt64LE(BigInt(payloadCursor), indexCursor + 8);
    container.writeBigUInt64LE(BigInt(entry.bytes.length), indexCursor + 16);
    fileHash.copy(container, indexCursor + 24);
    pathBytes.copy(container, indexCursor + 56);
    entry.bytes.copy(container, payloadCursor);
    indexCursor += 56 + pathBytes.length;
    payloadCursor += entry.bytes.length;
  }
  createHash("sha256").update(container.subarray(128, payloadOffset)).digest().copy(container, 64);
  createHash("sha256").update(container.subarray(payloadOffset)).digest().copy(container, 96);
  await fs.writeFile(output, container, { flag: "wx" });
  return container.length;
}

function fft(real, imaginary, inverse = false) {
  const size = real.length;
  let bits = 0;
  for (let value = size; value > 1; value >>= 1) ++bits;
  for (let index = 0; index < size; ++index) {
    let remaining = index;
    let reversed = 0;
    for (let bit = 0; bit < bits; ++bit) {
      reversed = (reversed << 1) | (remaining & 1);
      remaining >>>= 1;
    }
    if (index < reversed) {
      [real[index], real[reversed]] = [real[reversed], real[index]];
      [imaginary[index], imaginary[reversed]] = [imaginary[reversed], imaginary[index]];
    }
  }
  for (let width = 2; width <= size; width <<= 1) {
    const half = width >> 1;
    const step = size / width;
    for (let base = 0; base < size; base += width) {
      for (let offset = 0; offset < half; ++offset) {
        const angle = (inverse ? 2 : -2) * Math.PI * offset * step / size;
        const c = Math.cos(angle);
        const s = Math.sin(angle);
        const odd = base + offset + half;
        const even = base + offset;
        const oddReal = real[odd] * c - imaginary[odd] * s;
        const oddImag = real[odd] * s + imaginary[odd] * c;
        const evenReal = real[even];
        const evenImag = imaginary[even];
        real[even] = evenReal + oddReal;
        imaginary[even] = evenImag + oddImag;
        real[odd] = evenReal - oddReal;
        imaginary[odd] = evenImag - oddImag;
      }
    }
  }
  if (inverse) {
    for (let index = 0; index < size; ++index) {
      real[index] /= size;
      imaginary[index] /= size;
    }
  }
}

function downsampleKernel(source, outputLength) {
  const result = new Float64Array(outputLength);
  const half = 32;
  const cutoff = 0.125;
  for (let output = 0; output < outputLength; ++output) {
    const center = output * DECIMATION;
    const first = Math.max(0, Math.floor(center) - half);
    const last = Math.min(source.length - 1, Math.floor(center) + half);
    let sum = 0;
    let weight = 0;
    for (let input = first; input <= last; ++input) {
      const distance = input - center;
      const x = 2 * cutoff * distance;
      const sinc = Math.abs(x) < 1e-12 ? 1 : Math.sin(Math.PI * x) / (Math.PI * x);
      const phase = (input - (Math.floor(center) - half)) / (2 * half);
      const window = 0.42 - 0.5 * Math.cos(2 * Math.PI * phase) + 0.08 * Math.cos(4 * Math.PI * phase);
      const coefficient = 2 * cutoff * sinc * window;
      sum += source[input] * coefficient;
      weight += coefficient;
    }
    result[output] = weight === 0 ? 0 : sum / weight;
  }
  return result;
}

async function rebuildSpectrum(heldManifest) {
  const transfer = heldManifest.presentation.routes[0].transfer;
  const oldBytes = await bytes(outputRoot, path.posix.join("held", transfer.spectrum_path));
  const oldBins = transfer.fft_size;
  const real = new Float64Array(oldBins);
  const imaginary = new Float64Array(oldBins);
  const view = new DataView(oldBytes.buffer, oldBytes.byteOffset, oldBytes.byteLength);
  for (let index = 0; index < oldBins; ++index) {
    real[index] = view.getFloat64(index * 16, true);
    imaginary[index] = view.getFloat64(index * 16 + 8, true);
  }
  fft(real, imaginary, true);
  const kernel = downsampleKernel(real.subarray(0, transfer.coefficient_count), Math.ceil(transfer.coefficient_count / DECIMATION));
  const outputReal = new Float64Array(TARGET_FFT_SIZE);
  const outputImaginary = new Float64Array(TARGET_FFT_SIZE);
  outputReal.set(kernel);
  fft(outputReal, outputImaginary, false);
  const outputBytes = new Uint8Array(TARGET_FFT_SIZE * 16);
  const outputView = new DataView(outputBytes.buffer);
  for (let index = 0; index < TARGET_FFT_SIZE; ++index) {
    outputView.setFloat64(index * 16, outputReal[index], true);
    outputView.setFloat64(index * 16 + 8, outputImaginary[index], true);
  }
  const outputName = "ir-game48-kernel-complex-f64le.bin";
  await writeBytes(outputRoot, path.posix.join("held", outputName), outputBytes);
  transfer.profile = "game-48k-v1";
  transfer.fft_size = TARGET_FFT_SIZE;
  transfer.coefficient_count = kernel.length;
  transfer.spectrum_path = outputName;
  transfer.spectrum_byte_count = outputBytes.byteLength;
  transfer.spectrum_sha256 = digest(outputBytes);
}

await fs.cp(sourceRoot, outputRoot, { recursive: true, errorOnExist: true });

const rootManifest = await readJson(outputRoot, "runtime.json");
const heldManifest = await readJson(outputRoot, "held/package.json");
const lifecycleManifest = await readJson(outputRoot, "lifecycle/runtime.json");
const starterManifest = await readJson(outputRoot, "shared-recorded-starter/runtime.json");
const limiterManifest = await readJson(outputRoot, "limiter/runtime.json");

// Resample lifecycle and limiter tapes. Phase-domain held/directional assets
// remain at their authored phase resolution.
for (const artifact of [
  lifecycleManifest.starter.artifact,
  lifecycleManifest.startup.artifact,
  lifecycleManifest.shutdown.artifact,
  ...(lifecycleManifest.shutdown_elevated ? [lifecycleManifest.shutdown_elevated.artifact] : []),
]) {
  await resampleF32(path.posix.join("lifecycle", artifact.path));
}

const lifecycleDocument = transformDocument(lifecycleManifest);
lifecycleDocument.audio.sample_rate_hz = TARGET_RATE;
lifecycleDocument.audio.encoding = PCM16_ENCODING;
lifecycleDocument.provenance.delivery_rate_hz = TARGET_RATE;
for (const scenario of lifecycleDocument.provenance.scenarios ?? []) {
  const scenarioPath = path.posix.join("lifecycle", scenario.path);
  const source = await readJson(outputRoot, scenarioPath);
  const transformed = transformDocument(source);
  await writeJson(outputRoot, scenarioPath, transformed);
  scenario.sha256 = digest(await bytes(outputRoot, scenarioPath));
  if (scenario.evidence_path) {
    const evidencePath = path.posix.join("lifecycle", scenario.evidence_path);
    const evidence = transformDocument(await readJson(outputRoot, evidencePath));
    await writeJson(outputRoot, evidencePath, evidence);
    scenario.evidence_sha256 = digest(await bytes(outputRoot, evidencePath));
  }
}
for (const artifact of [
  lifecycleDocument.starter.artifact,
  lifecycleDocument.startup.artifact,
  lifecycleDocument.shutdown.artifact,
  ...(lifecycleDocument.shutdown_elevated ? [lifecycleDocument.shutdown_elevated.artifact] : []),
]) {
  const sourcePath = path.posix.join("lifecycle", artifact.path);
  const stats = await compactF32(sourcePath);
  const compactPath = artifact.path.replace(/\.f32le$/u, ".pcm16le");
  await writeBytes(outputRoot, path.posix.join("lifecycle", compactPath), stats.payload);
  await fs.unlink(path.join(outputRoot, sourcePath));
  artifact.path = compactPath;
  artifact.quantization = { method: PCM16_ENCODING, scale: stats.scale };
  artifact.frame_count = stats.frameCount;
  artifact.byte_count = stats.byteCount;
  artifact.sha256 = digest(stats.payload);
  artifact.peak = stats.peak;
  artifact.rms = stats.rms;
}
const admissionEvidencePath = path.posix.join("lifecycle", lifecycleDocument.startup_admission.evidence.path);
if (lifecycleDocument.startup_admission.evidence.path) {
  lifecycleDocument.startup_admission.evidence.sha256 = digest(await bytes(outputRoot, admissionEvidencePath));
}
await writeJson(outputRoot, "lifecycle/runtime.json", lifecycleDocument);

const starterDocument = structuredClone(starterManifest);
const starterSourcePath = path.posix.join("shared-recorded-starter", starterDocument.audio.relative_path);
const starterTargetRelative = "audio/recorded-starter.cropped.48000hz.mono.pcm16le";
const starterTargetPath = path.posix.join("shared-recorded-starter", starterTargetRelative);
await resampleF32(starterSourcePath);
const starterStats = await compactF32(starterSourcePath);
await writeBytes(outputRoot, starterTargetPath, starterStats.payload);
await fs.unlink(path.join(outputRoot, starterSourcePath));
starterDocument.id = "shared-recorded-starter-cc0-v1-48k";
starterDocument.audio.relative_path = starterTargetRelative;
starterDocument.audio.sample_rate_hz = TARGET_RATE;
starterDocument.audio.encoding = PCM16_ENCODING;
starterDocument.audio.quantization = { method: PCM16_ENCODING, scale: starterStats.scale };
starterDocument.audio.frame_count = starterStats.frameCount;
starterDocument.audio.byte_count = starterStats.byteCount;
starterDocument.audio.payload_sha256 = digest(starterStats.payload);
starterDocument.audio.duration_seconds = starterStats.frameCount / TARGET_RATE;
starterDocument.audio.peak = starterStats.peak;
starterDocument.audio.rms = starterStats.rms;
for (const key of ["repeat_in_frame", "repeat_out_frame", "cut_frame", "seam_crossfade_frames"]) {
  starterDocument.markers[key] = Math.round(starterDocument.markers[key] / DECIMATION);
}
starterDocument.provenance.canonicalization.output_sample_rate_hz = TARGET_RATE;
starterDocument.provenance.canonicalization.output_frames = starterStats.frameCount;
starterDocument.provenance.canonicalization.marker_mapping = "relative_source_frame";
starterDocument.provenance.canonicalization.filter_graph =
  starterDocument.provenance.canonicalization.filter_graph
    .replace("aresample=192000:", "aresample=48000:")
    .replace("atrim=end_sample=1470912", `atrim=end_sample=${starterStats.frameCount}`);
await writeJson(outputRoot, "shared-recorded-starter/runtime.json", starterDocument);

const limiterDocument = transformDocument(limiterManifest);
limiterDocument.sample_rate_hz = TARGET_RATE;
limiterDocument.encoding = PCM16_ENCODING;
for (const route of limiterDocument.routes) {
  const sourcePath = path.posix.join("limiter", route.file);
  await resampleF32(sourcePath);
  const stats = await compactF32(sourcePath);
  const compactFile = route.file.replace(/\.f32$/u, ".pcm16le");
  await writeBytes(outputRoot, path.posix.join("limiter", compactFile), stats.payload);
  await fs.unlink(path.join(outputRoot, sourcePath));
  route.file = compactFile;
  route.quantization = { method: PCM16_ENCODING, scale: stats.scale };
  route.sha256 = digest(stats.payload);
  route.reference_rms = limiterDocument.clips.map((clip) => {
    let energy = 0;
    const begin = clip.release_frame;
    const end = clip.end_frame;
    for (let frame = begin; frame < end; ++frame) energy += stats.samples[frame] ** 2;
    return Math.sqrt(energy / Math.max(1, end - begin));
  });
}
limiterDocument.fade_frames = Math.max(1, limiterDocument.fade_frames);
await writeJson(outputRoot, "limiter/runtime.json", limiterDocument);

for (const relativePath of [
  "held/exhaust-000-dry.json",
  "directional/runtime.json",
  "directional/exhaust-000-dry-rising.json",
  "directional/exhaust-000-dry-falling.json",
]) {
  const document = await readJson(outputRoot, relativePath);
  if (document.audio) document.audio.sample_rate_hz = TARGET_RATE;
  await writeJson(outputRoot, relativePath, document);
}
await rebuildSpectrum(heldManifest);
await writeJson(outputRoot, "held/package.json", heldManifest);
const heldManifestSha256 = digest(await bytes(outputRoot, "held/package.json"));
lifecycleDocument.startup_admission.evidence.corrected_held_manifest_sha256 = heldManifestSha256;
await writeJson(outputRoot, "lifecycle/runtime.json", lifecycleDocument);

rootManifest.audio.sample_rate_hz = TARGET_RATE;
rootManifest.audio.block_frames = TARGET_BLOCK_FRAMES;
rootManifest.audio.delivery_profile = "game-48k-v1";
rootManifest.fidelity = {
  ...rootManifest.fidelity,
  presentation_profile: "game-48k-v1",
  phase_samples_per_cycle: heldManifest.phase?.samples_per_cycle ?? 4_096,
};
rootManifest.lifecycle_package_path = "lifecycle/runtime.json";
rootManifest.shared_recorded_starter_package_path = "shared-recorded-starter/runtime.json";
rootManifest.limiter_package_path = "limiter/runtime.json";
await writeJson(outputRoot, "runtime.json", rootManifest);

const descriptor = await readJson(outputRoot, "crankwave.json");
descriptor.runtime.manifest_sha256 = digest(await bytes(outputRoot, "runtime.json"));
await writeJson(outputRoot, "crankwave.json", descriptor);

const files = [];
async function collect(directory, relative = "") {
  for (const entry of await fs.readdir(directory, { withFileTypes: true })) {
    const childRelative = path.posix.join(relative, entry.name);
    const child = path.join(directory, entry.name);
    if (entry.isDirectory()) await collect(child, childRelative);
    else if (entry.isFile()) files.push((await fs.stat(child)).size);
  }
}
await collect(outputRoot);
const result = {
  status: "experimental-directory-profile",
  source: sourceRoot,
  output: outputRoot,
  delivery_profile: "game-48k-v1",
  sample_rate_hz: TARGET_RATE,
  block_frames: TARGET_BLOCK_FRAMES,
  fft_size: TARGET_FFT_SIZE,
  package_bytes: files.reduce((sum, value) => sum + value, 0),
  package_megabytes: files.reduce((sum, value) => sum + value, 0) / 1_000_000,
};
if (outputCarrier !== null) {
  result.carrier = outputCarrier;
  result.carrier_bytes = await packCarrier(outputRoot, outputCarrier);
  result.carrier_megabytes = result.carrier_bytes / 1_000_000;
}
console.log(JSON.stringify(result, null, 2));
