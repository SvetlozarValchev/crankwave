#!/usr/bin/env node

import { spawn, spawnSync } from "node:child_process";
import { randomUUID } from "node:crypto";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import process from "node:process";
import { fileURLToPath, pathToFileURL } from "node:url";

import {
  compareCodeUnits,
  fingerprintRegularTree,
  hashLabeledBytes,
  hashLabeledFiles,
  rendererBytesIdentity,
  sha256Hex,
  validateRevenginePackageTree,
} from "./internal/bake-contract.mjs";

export const PROFILE_SCHEMA =
  "engine-sim-offline/responsive-audio-bake-profile-v1";
export const MAXIMUM_INPUT_JSON_BYTES = 4 * 1024 * 1024;

const CACHE_LOCK_OWNER_NAME =
  /^owner-([0-9a-f]{32})-m([0-9a-f]{64}|none)-b([0-9a-f]{32}|none)-n([0-9]+|none)-p([1-9][0-9]{0,9})-s([0-9]+|none)\.lock$/u;

const here = path.dirname(fileURLToPath(import.meta.url));
const repository = path.resolve(here, "../..");
const heldStage = path.join(here, "internal/held-texture.mjs");
const directionalStage = path.join(
  here,
  "internal/directional-transients.mjs",
);
const lifecycleStage = path.join(here, "internal/lifecycle.mjs");
const bakeContractStage = path.join(here, "internal/bake-contract.mjs");
const bakerSourceFiles = Object.freeze([
  fileURLToPath(import.meta.url),
  heldStage,
  directionalStage,
  lifecycleStage,
  bakeContractStage,
  path.join(here, "dump-ir-spectrum.cpp"),
  path.join(repository, "web/runtime/c-api-abi.js"),
  path.join(repository, "web/runtime/c-api-client.js"),
  path.join(repository, "web/runtime/c-api-errors.js"),
  path.join(repository, "web/runtime/c-api-session.js"),
  path.join(repository, "web/runtime/wasm-heap.js"),
]);

function snapshotBakerSources() {
  return Object.freeze(bakerSourceFiles.map((filePath) => Object.freeze({
    label: path.relative(repository, filePath).split(path.sep).join("/"),
    bytes: fs.readFileSync(filePath),
  })));
}

function stageBakerSources(sourceRoot, snapshots, expectedSha256) {
  for (const snapshot of snapshots) {
    const components = snapshot.label.split("/");
    if (
      components.length === 0 ||
      components.some((component) =>
        component.length === 0 || component === "." || component === ".."
      )
    ) {
      fail(`baker source label is not portable: ${snapshot.label}`);
    }
    const destination = path.join(sourceRoot, ...components);
    fs.mkdirSync(path.dirname(destination), { recursive: true });
    fs.writeFileSync(destination, snapshot.bytes);
  }
  fs.writeFileSync(
    path.join(sourceRoot, "package.json"),
    `${JSON.stringify({ private: true, type: "module" }, null, 2)}\n`,
  );
  const stagedSha256 = hashLabeledFiles(snapshots.map(({ label }) => ({
    label,
    path: path.join(sourceRoot, ...label.split("/")),
  })));
  if (stagedSha256 !== expectedSha256) {
    fail("staged baker sources differ from their acquired bytes");
  }
}

function fail(message, options) {
  throw new Error(message, options);
}

function sha256(bytes) {
  return sha256Hex(bytes);
}

function readJson(filePath, label) {
  try {
    return JSON.parse(fs.readFileSync(filePath, "utf8"));
  } catch (error) {
    fail(`${label} is not readable JSON: ${filePath}`, { cause: error });
  }
}

function readBoundedRegularFile(filePath, label, maximumBytes) {
  let descriptor;
  try {
    const noFollow = fs.constants.O_NOFOLLOW ?? 0;
    descriptor = fs.openSync(filePath, fs.constants.O_RDONLY | noFollow);
  } catch (error) {
    fail(`${label} is not readable: ${filePath}`, { cause: error });
  }
  try {
    const status = fs.fstatSync(descriptor);
    if (!status.isFile()) {
      fail(`${label} must be a regular file: ${filePath}`);
    }
    if (
      !Number.isSafeInteger(status.size) ||
      status.size < 0 ||
      status.size > maximumBytes
    ) {
      fail(`${label} exceeds its ${maximumBytes}-byte limit: ${filePath}`);
    }
    const bytes = Buffer.allocUnsafe(status.size);
    let offset = 0;
    while (offset < bytes.byteLength) {
      const count = fs.readSync(
        descriptor,
        bytes,
        offset,
        bytes.byteLength - offset,
        null,
      );
      if (count === 0) break;
      offset += count;
    }
    const probe = Buffer.allocUnsafe(1);
    if (
      offset !== bytes.byteLength ||
      fs.readSync(descriptor, probe, 0, 1, null) !== 0
    ) {
      fail(`${label} changed while it was read: ${filePath}`);
    }
    return bytes;
  } finally {
    fs.closeSync(descriptor);
  }
}

function readJsonSnapshot(
  filePath,
  label,
  maximumBytes = MAXIMUM_INPUT_JSON_BYTES,
) {
  const bytes = readBoundedRegularFile(filePath, label, maximumBytes);
  let value;
  try {
    value = JSON.parse(bytes.toString("utf8"));
  } catch (error) {
    fail(`${label} is not readable JSON: ${filePath}`, { cause: error });
  }
  return Object.freeze({ bytes, value, sha256: sha256(bytes) });
}

function writeJson(filePath, value) {
  fs.mkdirSync(path.dirname(filePath), { recursive: true });
  fs.writeFileSync(filePath, `${JSON.stringify(value, null, 2)}\n`);
}

function object(value, label) {
  if (value === null || typeof value !== "object" || Array.isArray(value)) {
    fail(`${label} must be an object`);
  }
  return value;
}

function string(value, label) {
  if (typeof value !== "string" || value.length === 0 || value.trim() !== value) {
    fail(`${label} must be nonempty canonical text`);
  }
  return value;
}

function positive(value, label) {
  if (typeof value !== "number" || !Number.isFinite(value) || !(value > 0)) {
    fail(`${label} must be positive`);
  }
  return value;
}

function positiveInteger(value, label, maximum = Number.MAX_SAFE_INTEGER) {
  if (!Number.isSafeInteger(value) || value < 1 || value > maximum) {
    fail(`${label} must be an integer in [1, ${maximum}]`);
  }
  return value;
}

function defaultJobs() {
  const available = typeof os.availableParallelism === "function"
    ? os.availableParallelism()
    : os.cpus().length;
  return Math.max(1, Math.min(24, available));
}

export function createExecutionRuntimeIdentity({
  node = process.versions.node,
  v8 = process.versions.v8,
  icu = process.versions.icu ?? null,
  platform = process.platform,
  arch = process.arch,
  endianness = os.endianness(),
} = {}) {
  const identity = {
    node: string(node, "execution runtime Node version"),
    v8: string(v8, "execution runtime V8 version"),
    icu: icu === null ? null : string(icu, "execution runtime ICU version"),
    platform: string(platform, "execution runtime platform"),
    arch: string(arch, "execution runtime architecture"),
    endianness: string(endianness, "execution runtime endianness"),
  };
  if (!new Set(["LE", "BE"]).has(identity.endianness)) {
    fail("execution runtime endianness must be LE or BE");
  }
  return Object.freeze(identity);
}

function compareAssetRecords(left, right) {
  return compareCodeUnits(left.kind, right.kind) ||
    compareCodeUnits(left.id, right.id) ||
    compareCodeUnits(left.sha256, right.sha256);
}

function lowercaseSha256(value, label) {
  if (typeof value !== "string" || !/^[0-9a-f]{64}$/u.test(value)) {
    fail(`${label} must be a lowercase SHA-256 digest`);
  }
  return value;
}

function revengineEngineId(value, label) {
  const id = string(value, label);
  if (
    id.length > 128 ||
    !/^[a-z0-9](?:[a-z0-9._-]*[a-z0-9])?$/u.test(id)
  ) {
    fail(`${label} is not a portable REVENGINE engine identifier`);
  }
  return id;
}

function absolutePath(value, label) {
  const input = string(value, label);
  return path.resolve(process.cwd(), input);
}

function portableRelativePath(value, label) {
  const candidate = string(value, label);
  if (
    candidate.startsWith("/") ||
    candidate.includes("\\") ||
    candidate.split("/").some((segment) => segment === "" || segment === "..")
  ) {
    fail(`${label} must be a confined relative path`);
  }
  return candidate;
}

function loadBuiltinAssetBundle(bundleRoot) {
  const root = path.resolve(bundleRoot);
  const catalogPath = path.join(root, "catalog.v1.json");
  const catalogSnapshot = readJsonSnapshot(
    catalogPath,
    "built-in asset catalog",
  );
  const catalogBytes = catalogSnapshot.bytes;
  const catalog = catalogSnapshot.value;
  if (
    catalog.schema !== "engine-sim-offline/builtin-asset-catalog.v1" ||
    !Array.isArray(catalog.assets)
  ) {
    fail(`unsupported built-in asset catalog: ${catalogPath}`);
  }
  const entries = new Map();
  for (const [index, raw] of catalog.assets.entries()) {
    const entry = object(raw, `built-in asset catalog entry ${index}`);
    exactKeys(entry, ["kind", "id", "sha256"], `catalog entry ${index}`);
    const kind = string(entry.kind, `catalog entry ${index}.kind`);
    const id = string(entry.id, `catalog entry ${index}.id`);
    const digest = lowercaseSha256(
      entry.sha256,
      `catalog entry ${index}.sha256`,
    );
    const key = `${kind}\u0000${id}\u0000${digest}`;
    if (entries.has(key)) fail(`duplicate built-in asset catalog entry ${kind}/${id}`);
    entries.set(key, Object.freeze({ kind, id, sha256: digest }));
  }
  return Object.freeze({
    root,
    catalogPath,
    catalogSha256: sha256(catalogBytes),
    resolve(kind, id, digest) {
      const canonicalDigest = lowercaseSha256(digest, `${kind}/${id} sha256`);
      const key = `${kind}\u0000${id}\u0000${canonicalDigest}`;
      if (!entries.has(key)) {
        fail(`built-in asset catalog does not authorize ${kind}/${id}/${canonicalDigest}`);
      }
      const payload = path.join(root, "payloads", canonicalDigest);
      if (!fs.existsSync(payload) || !fs.lstatSync(payload).isFile()) {
        fail(`built-in asset payload is absent: ${payload}`);
      }
      const bytes = fs.readFileSync(payload);
      if (sha256(bytes) !== canonicalDigest) {
        fail(`built-in asset payload hash differs: ${payload}`);
      }
      return Object.freeze({ path: payload, bytes, sha256: canonicalDigest });
    },
    sharedStarter() {
      const starterRoot = path.join(
        root,
        "runtime-audio/shared-recorded-starter",
      );
      const runtimePath = path.join(starterRoot, "runtime.json");
      const runtime = readJson(runtimePath, "shared recorded starter");
      if (runtime.schema !== "engine-sim-offline/shared-recorded-starter") {
        fail(`unsupported shared recorded starter: ${runtimePath}`);
      }
      const audio = object(runtime.audio, "shared recorded starter audio");
      const relative = portableRelativePath(
        audio.relative_path,
        "shared recorded starter audio.relative_path",
      );
      const payloadPath = path.join(starterRoot, relative);
      const bytes = fs.readFileSync(payloadPath);
      if (
        bytes.byteLength !== positiveInteger(
          audio.byte_count,
          "shared recorded starter audio.byte_count",
        ) ||
        sha256(bytes) !== lowercaseSha256(
          audio.payload_sha256,
          "shared recorded starter audio.payload_sha256",
        )
      ) {
        fail("shared recorded starter payload identity differs");
      }
      const identity = fingerprintRegularTree(starterRoot);
      return Object.freeze({ root: starterRoot, identity });
    },
  });
}

function exactKeys(value, expected, label) {
  const actual = Object.keys(value).sort();
  const wanted = [...expected].sort();
  if (JSON.stringify(actual) !== JSON.stringify(wanted)) {
    fail(`${label} has unexpected or missing fields`);
  }
}

export function validateProfile(value) {
  const profile = object(value, "profile");
  exactKeys(
    profile,
    ["schema", "id", "rpm", "capture", "lifecycle"],
    "profile",
  );
  if (profile.schema !== PROFILE_SCHEMA) {
    fail(`unsupported profile schema ${profile.schema}`);
  }
  const profileId = string(profile.id, "profile.id");
  if (
    profileId.length > 128 ||
    !/^[A-Za-z0-9][A-Za-z0-9._-]{0,127}$/u.test(profileId)
  ) {
    fail("profile.id does not satisfy the profile schema");
  }

  const rpm = object(profile.rpm, "profile.rpm");
  exactKeys(
    rpm,
    [
      "anchors",
      "outer_minimum_rpm",
      "outer_maximum_rpm",
      "held_preparation_floor_seconds",
      "held_extend_preparation_below_rpm",
    ],
    "profile.rpm",
  );
  if (!Array.isArray(rpm.anchors) || rpm.anchors.length < 2) {
    fail("profile.rpm.anchors must contain at least two RPM values");
  }
  const anchors = rpm.anchors.map((value, index) =>
    positive(value, `profile.rpm.anchors[${index}]`)
  );
  if (anchors.some((value, index) => index > 0 && value <= anchors[index - 1])) {
    fail("profile.rpm.anchors must be strictly increasing");
  }
  const outerMinimumRpm = positive(
    rpm.outer_minimum_rpm,
    "profile.rpm.outer_minimum_rpm",
  );
  const outerMaximumRpm = positive(
    rpm.outer_maximum_rpm,
    "profile.rpm.outer_maximum_rpm",
  );
  if (
    outerMinimumRpm > anchors[0] ||
    outerMaximumRpm < anchors.at(-1) ||
    !(outerMaximumRpm > outerMinimumRpm)
  ) {
    fail("profile.rpm outer domain must contain every RPM anchor");
  }
  if (
    Math.max(50, anchors[0] * 0.8) > outerMinimumRpm ||
    anchors.at(-1) * 1.05 < outerMaximumRpm
  ) {
    fail("profile.rpm outer domain exceeds the directional capture envelope");
  }
  const heldFloor = positive(
    rpm.held_preparation_floor_seconds,
    "profile.rpm.held_preparation_floor_seconds",
  );
  if (heldFloor < 3 || !Number.isInteger(heldFloor * 50)) {
    fail("held preparation floor must be at least 3 seconds on a 20 ms boundary");
  }
  positive(
    rpm.held_extend_preparation_below_rpm,
    "profile.rpm.held_extend_preparation_below_rpm",
  );

  const capture = object(profile.capture, "profile.capture");
  exactKeys(capture, ["physics_rate_hz", "load_lanes"], "profile.capture");
  if (capture.physics_rate_hz !== 10_000) {
    fail("the promoted accepted baker currently supports only 10 kHz physics");
  }
  const expectedLanes = [
    { id: "coast", throttle_01: 0 },
    { id: "mid", throttle_01: 0.2 },
    { id: "power", throttle_01: 1 },
  ];
  if (
    !Array.isArray(capture.load_lanes) ||
    capture.load_lanes.length !== expectedLanes.length ||
    capture.load_lanes.some((rawLane, index) => {
      const lane = object(rawLane, `profile.capture.load_lanes[${index}]`);
      exactKeys(
        lane,
        ["id", "throttle_01"],
        `profile.capture.load_lanes[${index}]`,
      );
      return lane.id !== expectedLanes[index].id ||
        lane.throttle_01 !== expectedLanes[index].throttle_01;
    })
  ) {
    fail("profile.capture.load_lanes must equal the accepted coast/mid/power grid");
  }

  const lifecycle = object(profile.lifecycle, "profile.lifecycle");
  exactKeys(
    lifecycle,
    ["enabled", "shared_recorded_starter", "elevated_shutdown"],
    "profile.lifecycle",
  );
  if (
    typeof lifecycle.enabled !== "boolean" ||
    typeof lifecycle.shared_recorded_starter !== "boolean"
  ) {
    fail("profile.lifecycle booleans are invalid");
  }
  if (!lifecycle.enabled && lifecycle.shared_recorded_starter) {
    fail("shared recorded starter requires lifecycle.enabled");
  }
  const elevated = object(
    lifecycle.elevated_shutdown,
    "profile.lifecycle.elevated_shutdown",
  );
  exactKeys(elevated, ["rpm", "keyoff_seconds"], "elevated shutdown");
  const elevatedRpm = positive(elevated.rpm, "elevated shutdown.rpm");
  if (elevatedRpm < outerMinimumRpm || elevatedRpm > outerMaximumRpm) {
    fail("elevated shutdown.rpm must be inside the responsive RPM domain");
  }
  positive(elevated.keyoff_seconds, "elevated shutdown.keyoff_seconds");

  return structuredClone(profile);
}

export function validateEngineProfileCompatibility(engine, profile) {
  const engineDocument = object(engine?.engine, "engine.engine");
  const limits = object(engineDocument.limits, "engine.engine.limits");
  const redline = object(limits.redline, "engine.engine.limits.redline");
  const redlineRpm = positive(
    redline.value,
    "engine.engine.limits.redline.value",
  );
  if (redline.unit !== "rpm") {
    fail("engine.engine.limits.redline.unit must be rpm");
  }
  const maximumAnchorRpm = profile.rpm.anchors.at(-1);
  if (maximumAnchorRpm > redlineRpm) {
    fail(
      `profile maximum RPM ${maximumAnchorRpm} exceeds engine redline ${redlineRpm}`,
    );
  }
  if (
    profile.lifecycle.enabled &&
    profile.lifecycle.elevated_shutdown.rpm > redlineRpm
  ) {
    fail(
      "profile elevated-shutdown RPM exceeds the engine redline",
    );
  }
  return profile;
}

function selectedAccessory(engine, resolveAsset) {
  const accessoryId = engine.engine?.losses?.accessory_configuration_id;
  const declarations = engine.engine?.accessory_configurations;
  if (typeof accessoryId !== "string" || !Array.isArray(declarations)) {
    fail("engine must select one authored accessory configuration");
  }
  const declaration = declarations.find(({ id }) => id === accessoryId);
  if (declaration === undefined) {
    fail(`selected accessory configuration ${accessoryId} is absent`);
  }
  const digest = lowercaseSha256(declaration.sha256, "accessory sha256");
  const assetPath = resolveAsset(
    "accessory-configuration",
    accessoryId,
    digest,
  );
  return Object.freeze({
    reference_id: accessoryId,
    path: assetPath,
    sha256: digest,
    verified: true,
  });
}

function presentationAssets(engine, resolveAsset) {
  const assets = engine.presentation?.assets;
  if (!Array.isArray(assets)) fail("engine presentation.assets must be an array");
  const impulseResponses = assets.filter(
    ({ kind }) => kind === "impulse_response",
  );
  if (impulseResponses.length !== 1) {
    fail("the accepted baker currently requires exactly one impulse-response asset");
  }
  return impulseResponses.map((asset) => {
    const assetId = string(asset.id, "presentation asset id");
    const digest = lowercaseSha256(asset.sha256, `${assetId}.sha256`);
    const assetPath = resolveAsset("audio", assetId, digest);
    return Object.freeze({
      id: assetId,
      kind: "impulse_response",
      path: assetPath,
      sha256: digest,
      verified: true,
    });
  });
}

function scenarioTemplate(engine, profile) {
  const engineId = engine.engine.identity.id;
  const crankshaft = engine.engine.crankshafts.find(
    ({ id }) => id === engine.engine.output_crankshaft,
  );
  if (crankshaft?.tdc_reference_angle === undefined) {
    fail("output crankshaft must declare tdc_reference_angle");
  }
  const authoredTdc = object(
    crankshaft.tdc_reference_angle,
    "output crankshaft tdc_reference_angle",
  );
  const tdcRadians = authoredTdc.unit === "rad"
    ? authoredTdc.value
    : authoredTdc.unit === "deg"
      ? authoredTdc.value * (3.14159265359 / 180)
      : fail("output crankshaft tdc_reference_angle must use rad or deg");
  if (typeof tdcRadians !== "number" || !Number.isFinite(tdcRadians)) {
    fail("output crankshaft tdc_reference_angle must be finite");
  }
  return {
    schema: "engine-sim-offline/scenario",
    id: `${engineId}-${profile.id}-template`,
    engine: engineId,
    fuel: engine.engine.default_fuel,
    ambient: {
      pressure: { value: 101_325, unit: "Pa" },
      temperature: { value: 298.15, unit: "K" },
      relative_humidity_01: 0,
    },
    initial_thermal_state: {
      gas_temperature: { value: 298.15, unit: "K" },
      wall_temperature: { value: 363.15, unit: "K" },
      coolant_temperature: { value: 363.15, unit: "K" },
      oil_temperature: { value: 363.15, unit: "K" },
    },
    crankcase: {
      pressure: { value: 101_325, unit: "Pa" },
      temperature: { value: 298.15, unit: "K" },
    },
    initial_state: {
      engine_speed: { value: profile.rpm.anchors[0], unit: "rpm" },
      // The current executor admits an exact bitwise match against the engine's
      // legacy-low-order angle conversion, whose public constant is deliberate.
      crank_angle: { value: tdcRadians, unit: "rad" },
      ignition_enabled: true,
      fuel_enabled: true,
      starter_enabled: false,
      dyno_enabled: false,
      limiter_enabled: true,
    },
    preparation: {
      type: "fixed_horizon",
      preparation_duration: { value: 1, unit: "s" },
      trailing_complete_cycle_count: 4,
    },
    mode: {
      type: "free_engine",
      throttle_01: {
        interpolation: "right_continuous_hold",
        points: [{ time: { value: 0, unit: "s" }, value: 0.1 }],
      },
    },
    events: [],
    rates: {
      physics: { numerator: "10000", denominator: "1", unit: "Hz" },
      capture: { numerator: "10000", denominator: "1", unit: "Hz" },
      source_processing: { numerator: "192000", denominator: "1", unit: "Hz" },
      acoustics: { numerator: "192000", denominator: "1", unit: "Hz" },
      delivery: { numerator: "192000", denominator: "1", unit: "Hz" },
    },
    quality: {
      id: "listening",
      process_block_capacity_frames: 3840,
      event_queue_capacity: 3800,
      telemetry_capacity_frames: 1,
    },
    total_duration: { value: 7, unit: "s" },
    audible_start: { value: 1, unit: "s" },
    audible_duration: { value: 6, unit: "s" },
    public_seed: "12648430",
    output: {
      buses: ["master-engine-raw", "master-engine-audition"],
      telemetry_channels: [],
    },
  };
}

export function createBakeInventory({
  engine,
  enginePath,
  profile,
  scenarioPath,
  resolveAsset,
}) {
  if (typeof resolveAsset !== "function") {
    fail("createBakeInventory requires a built-in asset resolver");
  }
  const engineDocument = object(engine.engine, "engine.engine");
  const engineId = string(engineDocument.identity?.id, "engine identity id");
  const defaultFuel = string(engineDocument.default_fuel, "engine default fuel");
  if (!engineDocument.fuels?.some(({ id }) => id === defaultFuel)) {
    fail(`engine default fuel ${defaultFuel} is absent`);
  }
  const sourceRoutes = engineDocument.source_routes;
  if (!Array.isArray(sourceRoutes) || sourceRoutes.length === 0) {
    fail("engine source_routes must not be empty");
  }
  const dryBusIds = sourceRoutes.map(({ id }, index) =>
    `${string(id, `source route ${index} id`)}.dry`
  );
  if (new Set(dryBusIds).size !== dryBusIds.length) {
    fail("engine source route IDs must be unique");
  }
  return {
    schema_id: "engine-sim-offline.multi-engine-bake-inventory.v1",
    source_commit: "standalone-profile-input",
    engines: [{
      engine_id: engineId,
      display_name: engineDocument.identity.display_name ?? engineId,
      engine_path: path.resolve(enginePath),
      fuel_id: defaultFuel,
      assets: {
        audio: presentationAssets(engine, resolveAsset),
        accessory_configuration: selectedAccessory(engine, resolveAsset),
      },
      presentation: {
        source_route_ids: sourceRoutes.map(({ id }) => id),
        dry_source_route_bus_ids: dryBusIds,
        published_master_bus_ids: engine.presentation.buses
          .filter(({ publish }) => publish === true)
          .map(({ id }) => id),
      },
      rpm_domain: {
        minimum_rpm: profile.rpm.anchors[0],
        maximum_rpm: profile.rpm.anchors.at(-1),
        recommended_rpm_grid: profile.rpm.anchors,
        responsive_audio_outer_domain: {
          minimum_rpm: profile.rpm.outer_minimum_rpm,
          maximum_rpm: profile.rpm.outer_maximum_rpm,
        },
        held_capture_preparation_floor_seconds:
          profile.rpm.held_preparation_floor_seconds,
        held_capture_extend_preparation_below_rpm:
          profile.rpm.held_extend_preparation_below_rpm,
      },
      canonical_scenarios: {
        free_interactive: {
          status: "standalone-generated",
          id: `${engineId}-${profile.id}-template`,
          path: path.resolve(scenarioPath),
          mode: "free_engine",
          physics_rate_hz: profile.capture.physics_rate_hz,
        },
      },
      lifecycle: {
        elevated_shutdown_rpm: profile.lifecycle.elevated_shutdown.rpm,
        elevated_keyoff_seconds:
          profile.lifecycle.elevated_shutdown.keyoff_seconds,
      },
    }],
  };
}

function usage() {
  return [
    "usage:",
    "  node tools/responsive-audio-baker/bake.mjs --engine ENGINE.json \\",
    "    [--profile PROFILE.json] --output NEW_DIRECTORY --cache DIRECTORY \\",
    "    [--builtin-assets BUNDLE] [--module engine-sim-offline.js] \\",
    "    [--jobs 1..32] [--plan]",
    "",
    "Without --profile, a responsive-audio-bake-profile.json beside ENGINE is used.",
    "The output directory must not exist. --plan validates and prints the exact",
    "derived capture plan without requiring a renderer build.",
  ].join("\n");
}

export function parseArguments(argv) {
  if (argv.length === 1 && ["--help", "-h"].includes(argv[0])) {
    return Object.freeze({ help: true });
  }
  const values = new Map();
  let plan = false;
  for (let index = 0; index < argv.length; ++index) {
    const name = argv[index];
    if (name === "--plan") {
      plan = true;
      continue;
    }
    if (!["--engine", "--profile", "--output", "--cache", "--builtin-assets", "--module", "--jobs", "--ir-dumper", "--cxx"].includes(name)) {
      fail(`unknown argument ${name}\n${usage()}`);
    }
    const value = argv[++index];
    if (value === undefined || value.startsWith("--")) {
      fail(`${name} requires a value\n${usage()}`);
    }
    if (values.has(name)) fail(`${name} may appear only once`);
    values.set(name, value);
  }
  for (const required of ["--engine", "--output", "--cache"]) {
    if (!values.has(required)) fail(`${required} is required\n${usage()}`);
  }
  const enginePath = absolutePath(values.get("--engine"), "--engine");
  return Object.freeze({
    help: false,
    enginePath,
    profilePath: values.has("--profile")
      ? absolutePath(values.get("--profile"), "--profile")
      : path.join(path.dirname(enginePath), "responsive-audio-bake-profile.json"),
    outputPath: absolutePath(values.get("--output"), "--output"),
    cachePath: absolutePath(values.get("--cache"), "--cache"),
    builtinAssetsPath: values.has("--builtin-assets")
      ? absolutePath(values.get("--builtin-assets"), "--builtin-assets")
      : null,
    modulePath: values.has("--module")
      ? absolutePath(values.get("--module"), "--module")
      : null,
    jobs: positiveInteger(
      Number(values.get("--jobs") ?? defaultJobs()),
      "--jobs",
      32,
    ),
    irDumperPath: values.has("--ir-dumper")
      ? absolutePath(values.get("--ir-dumper"), "--ir-dumper")
      : null,
    cxx: values.get("--cxx") ?? process.env.CXX ?? "c++",
    plan,
  });
}

function discoverRendererModule() {
  const environmentCandidate = process.env.ENGINE_SIM_OFFLINE_WASM_MODULE;
  const candidates = [
    ...(environmentCandidate ? [path.resolve(environmentCandidate)] : []),
    path.join(repository, "build-wasm/engine-sim-offline.js"),
    path.join(repository, "build/engine-sim-offline.js"),
  ];
  return candidates.find((candidate) =>
    fs.existsSync(candidate) &&
    fs.existsSync(path.join(path.dirname(candidate), "engine-sim-offline.wasm"))
  ) ?? null;
}

function discoverBuiltinAssets(modulePath) {
  const environmentCandidate = process.env.ENGINE_SIM_OFFLINE_BUILTIN_ASSETS;
  const candidates = [
    ...(environmentCandidate ? [path.resolve(environmentCandidate)] : []),
    ...(modulePath ? [path.join(path.dirname(modulePath), "engine-sim-offline-assets")] : []),
    path.join(repository, "build/generated/engine-sim-offline-assets"),
    path.join(repository, "build/engine-sim-offline-assets"),
  ];
  return candidates.find((candidate) =>
    fs.existsSync(path.join(candidate, "catalog.v1.json")) &&
    fs.existsSync(path.join(candidate, "payloads"))
  ) ?? null;
}

function run(executable, args, environment = process.env) {
  return new Promise((resolve, reject) => {
    const child = spawn(executable, args, {
      cwd: repository,
      env: environment,
      stdio: ["ignore", "inherit", "inherit"],
    });
    child.once("error", reject);
    child.once("exit", (code, signal) => {
      if (code === 0) resolve();
      else reject(new Error(
        `${path.basename(executable)} exited with ${code ?? signal}`,
      ));
    });
  });
}

function runJson(executable, args, environment = process.env) {
  return new Promise((resolve, reject) => {
    const child = spawn(executable, args, {
      cwd: repository,
      env: environment,
      stdio: ["ignore", "pipe", "inherit"],
    });
    const chunks = [];
    let byteCount = 0;
    let settled = false;
    child.stdout.on("data", (chunk) => {
      byteCount += chunk.byteLength;
      if (byteCount <= 1024 * 1024) chunks.push(chunk);
    });
    child.once("error", (error) => {
      if (!settled) {
        settled = true;
        reject(error);
      }
    });
    child.once("exit", (code, signal) => {
      if (settled) return;
      settled = true;
      if (code !== 0) {
        reject(new Error(
          `${path.basename(executable)} exited with ${code ?? signal}`,
        ));
        return;
      }
      if (byteCount > 1024 * 1024) {
        reject(new Error(`${path.basename(executable)} JSON output is too large`));
        return;
      }
      try {
        resolve(JSON.parse(Buffer.concat(chunks).toString("utf8")));
      } catch (error) {
        reject(new Error(
          `${path.basename(executable)} did not emit one JSON document`,
          { cause: error },
        ));
      }
    });
  });
}

export async function runGloballyBoundedCaptureJobs(
  jobs,
  maximumConcurrency,
  onCompleted = () => {},
) {
  if (!Array.isArray(jobs)) fail("capture jobs must be an array");
  positiveInteger(maximumConcurrency, "capture job concurrency", 32);
  if (typeof onCompleted !== "function") {
    fail("capture completion observer must be a function");
  }
  const labels = new Set();
  const queue = jobs.map((job, index) => {
    object(job, `capture job ${index}`);
    const label = string(job.label, `capture job ${index}.label`);
    if (labels.has(label)) fail(`duplicate capture job ${label}`);
    labels.add(label);
    return Object.freeze({
      label,
      estimated_duration_seconds: positive(
        job.estimated_duration_seconds,
        `capture job ${label}.estimated_duration_seconds`,
      ),
      run: typeof job.run === "function"
        ? job.run
        : fail(`capture job ${label}.run must be a function`),
    });
  }).sort((left, right) =>
    right.estimated_duration_seconds - left.estimated_duration_seconds ||
    compareCodeUnits(left.label, right.label)
  );
  let next = 0;
  let active = 0;
  let completed = 0;
  let observedMaximumConcurrency = 0;
  let firstError = null;
  const worker = async () => {
    while (firstError === null) {
      const index = next++;
      if (index >= queue.length) return;
      const job = queue[index];
      ++active;
      observedMaximumConcurrency = Math.max(observedMaximumConcurrency, active);
      try {
        await job.run();
      } catch (error) {
        firstError ??= error;
      } finally {
        --active;
      }
      if (firstError !== null) return;
      ++completed;
      onCompleted(Object.freeze({
        completed,
        total: queue.length,
        label: job.label,
      }));
    }
  };
  await Promise.all(
    Array.from(
      { length: Math.min(maximumConcurrency, queue.length) },
      worker,
    ),
  );
  if (firstError !== null) throw firstError;
  return Object.freeze({
    scheduled_capture_count: queue.length,
    maximum_concurrency: maximumConcurrency,
    observed_maximum_concurrency: observedMaximumConcurrency,
  });
}

function capturePlanStates(plan, schema, engineId, expectedCount, label) {
  object(plan, `${label} capture plan`);
  if (plan.schema !== schema || plan.engine !== engineId) {
    fail(`${label} capture plan identity differs`);
  }
  const cachedCount = Number(plan.cached_state_count);
  const newCount = Number(plan.new_state_count);
  if (
    !Number.isSafeInteger(cachedCount) ||
    !Number.isSafeInteger(newCount) ||
    cachedCount < 0 ||
    newCount < 0 ||
    cachedCount + newCount !== expectedCount ||
    !Array.isArray(plan.new_states) ||
    plan.new_states.length !== newCount
  ) {
    fail(`${label} capture plan counts are invalid`);
  }
  return plan.new_states;
}

async function captureResponsiveSources({
  engineId,
  profile,
  environment,
  maximumConcurrency,
  heldStagePath,
  directionalStagePath,
}) {
  const [heldPlan, directionalPlan] = await Promise.all([
    runJson(process.execPath, [
      heldStagePath,
      "--engine",
      engineId,
      "--print-capture-plan",
    ], environment),
    runJson(process.execPath, [
      directionalStagePath,
      "--engine",
      engineId,
      "--print-capture-plan",
    ], environment),
  ]);
  const lanes = new Set(profile.capture.load_lanes.map(({ id }) => id));
  const anchors = new Set(profile.rpm.anchors);
  const jobs = capturePlanStates(
    heldPlan,
    "engine-sim-offline/held-phase-texture-capture-plan",
    engineId,
    profile.rpm.anchors.length * profile.capture.load_lanes.length,
    "held",
  ).map((state, index) => {
    const rpm = state?.rpm;
    const lane = state?.lane;
    if (!anchors.has(rpm) || !lanes.has(lane)) {
      fail(`held capture plan state ${index} is outside the profile`);
    }
    const duration = positive(
      state.scenario_duration_seconds,
      `held capture plan state ${index}.scenario_duration_seconds`,
    );
    return Object.freeze({
      label: `held/${rpm}rpm/${lane}`,
      estimated_duration_seconds: duration,
      run: () => run(process.execPath, [
        heldStagePath,
        "--engine",
        engineId,
        "--capture-state",
        String(rpm),
        lane,
      ], environment),
    });
  });
  const directions = new Set(["rising", "falling"]);
  jobs.push(...capturePlanStates(
    directionalPlan,
    "engine-sim-offline/directional-transient-capture-plan",
    engineId,
    profile.capture.load_lanes.length * directions.size,
    "directional",
  ).map((state, index) => {
    const direction = state?.direction;
    const lane = state?.lane;
    if (!directions.has(direction) || !lanes.has(lane)) {
      fail(`directional capture plan state ${index} is outside the profile`);
    }
    const duration = positive(
      state.scenario_duration_seconds,
      `directional capture plan state ${index}.scenario_duration_seconds`,
    );
    return Object.freeze({
      label: `directional/${direction}/${lane}`,
      estimated_duration_seconds: duration,
      run: () => run(process.execPath, [
        directionalStagePath,
        "--capture-direction",
        engineId,
        direction,
        lane,
      ], environment),
    });
  }));
  return runGloballyBoundedCaptureJobs(
    jobs,
    maximumConcurrency,
    ({ completed, total, label }) => {
      process.stderr.write(
        `responsive capture ${completed}/${total}: ${label}\n`,
      );
    },
  );
}

const irDumperCompilerFlags = Object.freeze([
  "-std=c++20",
  "-O2",
  "-fno-fast-math",
  "-ffp-contract=off",
]);

function irDumperSourceClosure() {
  const roots = [
    path.join(here, "dump-ir-spectrum.cpp"),
    path.join(repository, "src/dsp/fixed_fft.cpp"),
    path.join(repository, "src/dsp/static_ir_conversion.cpp"),
    path.join(repository, "src/presentation/pcm16_ir_decoder.cpp"),
  ];
  const result = new Set();
  const pending = roots.slice();
  while (pending.length > 0) {
    const filePath = path.resolve(pending.pop());
    if (result.has(filePath)) continue;
    result.add(filePath);
    const source = fs.readFileSync(filePath, "utf8");
    for (const match of source.matchAll(/^\s*#\s*include\s+"([^"]+)"/gmu)) {
      const include = match[1];
      const candidates = [
        path.resolve(path.dirname(filePath), include),
        path.resolve(repository, "include", include),
        path.resolve(repository, "src", include),
      ];
      const resolved = candidates.find((candidate) => fs.existsSync(candidate));
      if (resolved === undefined) {
        fail(`IR spectrum helper include is unresolved: ${include}`);
      }
      pending.push(resolved);
    }
  }
  return [...result].sort(compareCodeUnits);
}

function compilerIdentity(compiler) {
  const result = spawnSync(compiler, ["--version"], {
    encoding: "utf8",
    maxBuffer: 1024 * 1024,
  });
  if (result.error !== undefined || result.status !== 0) {
    fail(`cannot identify IR spectrum helper compiler ${compiler}`, {
      cause: result.error,
    });
  }
  return sha256(Buffer.from(JSON.stringify({
    command: compiler,
    stdout: result.stdout,
    stderr: result.stderr,
  })));
}

function assertUsableIrDumper(executable) {
  let status;
  try {
    status = fs.lstatSync(executable);
    fs.accessSync(executable, fs.constants.X_OK);
  } catch (error) {
    fail(`IR spectrum helper is not an executable regular file: ${executable}`, {
      cause: error,
    });
  }
  if (status.isSymbolicLink() || !status.isFile()) {
    fail(`IR spectrum helper is not an executable regular file: ${executable}`);
  }
  const probe = spawnSync(executable, [], {
    encoding: "utf8",
    maxBuffer: 64 * 1024,
  });
  if (
    probe.error !== undefined ||
    probe.status !== 2 ||
    !probe.stderr.startsWith("usage: dump-ir-spectrum ")
  ) {
    fail(`IR spectrum helper self-test failed: ${executable}`, {
      cause: probe.error,
    });
  }
}

async function ensureIrDumper(cachePath, compiler, executionRuntime) {
  const sourceFiles = irDumperSourceClosure();
  const sourceEntries = sourceFiles.map((filePath) => ({
    label: path.relative(repository, filePath).split(path.sep).join("/"),
    path: filePath,
  }));
  const sourceIdentity = hashLabeledFiles(sourceEntries);
  const compilerSha256 = compilerIdentity(compiler);
  const identity = sha256(Buffer.from(JSON.stringify({
    schema: "engine-sim-offline/ir-spectrum-helper-build-v1",
    source_sha256: sourceIdentity,
    compiler_sha256: compilerSha256,
    flags: irDumperCompilerFlags,
    execution_runtime: executionRuntime,
  })));
  const toolRoot = path.join(cachePath, "tools", identity);
  const executable = path.join(toolRoot, "dump-ir-spectrum");
  if (fs.existsSync(executable)) {
    assertUsableIrDumper(executable);
    const bytes = fs.readFileSync(executable);
    return Object.freeze({
      path: executable,
      bytes,
      sha256: sha256(bytes),
      build_identity_sha256: identity,
    });
  }
  fs.mkdirSync(toolRoot, { recursive: true });
  const temporary = fs.mkdtempSync(path.join(toolRoot, ".build-"));
  const candidate = path.join(temporary, "dump-ir-spectrum");
  try {
    await run(compiler, [
      ...irDumperCompilerFlags,
      `-I${path.join(repository, "include")}`,
      `-I${path.join(repository, "src")}`,
      ...sourceFiles.filter((filePath) => filePath.endsWith(".cpp")),
      "-o",
      candidate,
    ]);
    if (hashLabeledFiles(sourceEntries) !== sourceIdentity) {
      fail("IR spectrum helper sources changed while the helper was compiled");
    }
    assertUsableIrDumper(candidate);
    try {
      fs.linkSync(candidate, executable);
    } catch (error) {
      if (error?.code !== "EEXIST") throw error;
    }
    assertUsableIrDumper(executable);
    const bytes = fs.readFileSync(executable);
    return Object.freeze({
      path: executable,
      bytes,
      sha256: sha256(bytes),
      build_identity_sha256: identity,
    });
  } finally {
    fs.rmSync(temporary, { recursive: true, force: true });
  }
}

function assertNewOutput(outputPath) {
  if (fs.existsSync(outputPath)) {
    fail(`output directory already exists: ${outputPath}`);
  }
  const parent = path.dirname(outputPath);
  fs.mkdirSync(parent, { recursive: true });
  if (!fs.statSync(parent).isDirectory()) fail(`output parent is not a directory`);
}

function readLinuxProcessStartTicks(pid) {
  try {
    const bytes = fs.readFileSync(`/proc/${pid}/stat`);
    if (bytes.byteLength > 4 * 1024) return { kind: "unreadable" };
    const text = bytes.toString("utf8").trim();
    const commandEnd = text.lastIndexOf(")");
    if (commandEnd < 0) return { kind: "unreadable" };
    const fields = text.slice(commandEnd + 1).trim().split(/\s+/u);
    if (
      fields.length < 20 ||
      !/^[A-Z]$/u.test(fields[0]) ||
      !/^[0-9]+$/u.test(fields[19])
    ) {
      return { kind: "unreadable" };
    }
    if (fields[0] === "Z") return { kind: "absent" };
    return { kind: "present", start_ticks: fields[19] };
  } catch (error) {
    return error?.code === "ENOENT"
      ? { kind: "absent" }
      : { kind: "unreadable" };
  }
}

function currentCacheLockScope() {
  if (process.platform !== "linux") return null;
  try {
    const machineId = readBoundedRegularFile(
      "/etc/machine-id",
      "Linux machine identity",
      128,
    ).toString("utf8").trim();
    const bootIdBytes = fs.readFileSync("/proc/sys/kernel/random/boot_id");
    if (bootIdBytes.byteLength > 128) return null;
    const bootId = bootIdBytes.toString("utf8").trim().replaceAll("-", "");
    const namespaceText = fs.readlinkSync("/proc/self/ns/pid");
    const namespaceMatch = /^pid:\[([0-9]+)\]$/u.exec(namespaceText);
    const hostname = os.hostname();
    const processState = readLinuxProcessStartTicks(process.pid);
    if (
      !/^[0-9a-f]{32}$/u.test(machineId) ||
      !/^[0-9a-f]{32}$/u.test(bootId) ||
      namespaceMatch === null ||
      hostname.length === 0 ||
      hostname.length > 255 ||
      processState.kind !== "present"
    ) {
      return null;
    }
    return Object.freeze({
      machine_scope_sha256: sha256(Buffer.from(JSON.stringify({
        machine_id: machineId,
        hostname,
      }))),
      boot_id: bootId,
      pid_namespace: namespaceMatch[1],
      start_ticks: processState.start_ticks,
    });
  } catch {
    return null;
  }
}

function parseCacheLockOwnerName(name) {
  const match = CACHE_LOCK_OWNER_NAME.exec(name);
  if (match === null) return null;
  const pid = Number(match[5]);
  if (!Number.isSafeInteger(pid) || pid > 0x7fff_ffff) return null;
  return Object.freeze({
    machine_scope_sha256: match[2],
    boot_id: match[3],
    pid_namespace: match[4],
    pid,
    start_ticks: match[6],
  });
}

function cacheLockOwnerIsProvablyStale(owner, currentScope) {
  if (
    currentScope === null ||
    owner.machine_scope_sha256 === "none" ||
    owner.machine_scope_sha256 !== currentScope.machine_scope_sha256
  ) {
    return false;
  }
  if (owner.boot_id !== currentScope.boot_id) return true;
  if (
    owner.pid_namespace !== currentScope.pid_namespace ||
    owner.start_ticks === "none"
  ) {
    return false;
  }
  const processState = readLinuxProcessStartTicks(owner.pid);
  return processState.kind === "absent" ||
    (processState.kind === "present" &&
      processState.start_ticks !== owner.start_ticks);
}

export function acquireCacheLock(cacheNamespace) {
  fs.mkdirSync(cacheNamespace, { recursive: true });
  const lockPath = path.join(cacheNamespace, ".active");
  try {
    fs.mkdirSync(lockPath);
  } catch (error) {
    if (error?.code !== "EEXIST") throw error;
  }
  const lockStatus = fs.lstatSync(lockPath);
  if (lockStatus.isSymbolicLink() || !lockStatus.isDirectory()) {
    fail(`responsive bake cache lock registry is unsafe: ${lockPath}`);
  }

  const currentScope = currentCacheLockScope();
  const ownerName = [
    `owner-${randomUUID().replaceAll("-", "")}`,
    `m${currentScope?.machine_scope_sha256 ?? "none"}`,
    `b${currentScope?.boot_id ?? "none"}`,
    `n${currentScope?.pid_namespace ?? "none"}`,
    `p${process.pid}`,
    `s${currentScope?.start_ticks ?? "none"}.lock`,
  ].join("-");
  const ownerPath = path.join(lockPath, ownerName);
  fs.writeFileSync(ownerPath, "", { flag: "wx", mode: 0o600 });

  try {
    const blockers = [];
    for (const name of fs.readdirSync(lockPath).sort(compareCodeUnits)) {
      if (name === ownerName) continue;
      const otherPath = path.join(lockPath, name);
      const owner = parseCacheLockOwnerName(name);
      let status;
      try {
        status = fs.lstatSync(otherPath);
      } catch (error) {
        if (error?.code !== "ENOENT") blockers.push(name);
        continue;
      }
      if (
        owner === null ||
        status.isSymbolicLink() ||
        !status.isFile() ||
        status.size !== 0
      ) {
        if (fs.existsSync(otherPath)) blockers.push(name);
        continue;
      }
      if (cacheLockOwnerIsProvablyStale(owner, currentScope)) {
        try {
          fs.unlinkSync(otherPath);
        } catch (error) {
          if (error?.code !== "ENOENT") blockers.push(name);
        }
        continue;
      }
      blockers.push(name);
    }
    if (blockers.length > 0) {
      fail(
        `the same responsive bake cache is already active or has an ` +
        `unrecoverable owner: ${lockPath}`,
      );
    }
  } catch (error) {
    try {
      fs.unlinkSync(ownerPath);
    } catch (cleanupError) {
      if (cleanupError?.code !== "ENOENT") throw cleanupError;
    }
    throw error;
  }

  let released = false;
  return () => {
    if (released) return;
    try {
      fs.unlinkSync(ownerPath);
    } catch (error) {
      if (error?.code !== "ENOENT") throw error;
    }
    released = true;
  };
}

export function cleanupPublishedLifecycleRuns(cacheNamespace) {
  const runsPath = path.join(cacheNamespace, "lifecycle", "runs");
  let status;
  try {
    status = fs.lstatSync(runsPath);
  } catch (error) {
    if (error?.code === "ENOENT") return false;
    throw error;
  }
  if (status.isSymbolicLink() || !status.isDirectory()) {
    fail(`lifecycle runs path is unsafe: ${runsPath}`);
  }
  fs.rmSync(runsPath, { recursive: true });
  return true;
}

export function createRevengineDescriptor(engineId, runtimeBytes) {
  const canonicalEngineId = revengineEngineId(engineId, "engine identity id");
  return {
    schema: "engine-sim-offline/revengine-package",
    version: 1,
    engine_id: canonicalEngineId,
    runtime: {
      kind: "responsive-audio",
      manifest_path: "runtime.json",
      manifest_sha256: sha256(runtimeBytes),
    },
  };
}

function snapshotRegularFile(filePath, label) {
  let status;
  try {
    status = fs.lstatSync(filePath);
  } catch (error) {
    fail(`${label} is absent: ${filePath}`, { cause: error });
  }
  if (status.isSymbolicLink() || !status.isFile()) {
    fail(`${label} must be a non-symlink regular file: ${filePath}`);
  }
  const bytes = fs.readFileSync(filePath);
  return Object.freeze({ path: filePath, bytes, sha256: sha256(bytes) });
}

function snapshotRenderer(modulePath) {
  const wasmPath = path.join(
    path.dirname(modulePath),
    "engine-sim-offline.wasm",
  );
  const loader = snapshotRegularFile(modulePath, "renderer loader");
  const wasm = snapshotRegularFile(wasmPath, "renderer WASM");
  return Object.freeze({
    loader,
    wasm,
    identity: rendererBytesIdentity(loader.bytes, wasm.bytes),
  });
}

function snapshotExplicitIrDumper(irDumperPath) {
  assertUsableIrDumper(irDumperPath);
  const snapshot = snapshotRegularFile(irDumperPath, "IR spectrum helper");
  return Object.freeze({
    ...snapshot,
    build_identity_sha256: null,
  });
}

export function createBakeCacheIdentity({
  engineSha256,
  profileSha256,
  bakerSourceSha256,
  builtinAssetCatalogSha256,
  resolvedAssets,
  sharedStarterAggregateSha256,
  rendererIdentity,
  irDumperSha256,
  executionRuntime,
}) {
  return sha256(Buffer.from(JSON.stringify({
    schema: "engine-sim-offline/responsive-audio-bake-cache-identity-v3",
    engine_sha256: engineSha256,
    profile_sha256: profileSha256,
    baker_source_sha256: bakerSourceSha256,
    builtin_asset_catalog_sha256: builtinAssetCatalogSha256,
    assets: resolvedAssets.map(({ kind, id, sha256: digest }) => ({
      kind,
      id,
      sha256: digest,
    })).sort(compareAssetRecords),
    shared_recorded_starter_aggregate_sha256:
      sharedStarterAggregateSha256,
    renderer: rendererIdentity,
    ir_spectrum_helper_sha256: irDumperSha256,
    execution_runtime: executionRuntime,
  })));
}

export function createBakeReport({
  engineId,
  engineSha256,
  profileId,
  profileSha256,
  bakerSourceSha256,
  builtinAssetCatalogSha256,
  cacheIdentitySha256,
  rendererIdentity,
  irDumperIdentity,
  executionRuntime,
  resolvedAssets,
  starterIdentity,
  runtimeManifestSha256,
  revengineDescriptorSha256,
}) {
  const assets = resolvedAssets.map(({ kind, id, sha256: digest }) => ({
    kind,
    id,
    sha256: digest,
  })).sort(compareAssetRecords);
  return {
    schema: "engine-sim-offline/responsive-audio-bake-report-v1",
    engine: { id: engineId, sha256: engineSha256 },
    profile: { id: profileId, sha256: profileSha256 },
    implementation: {
      baker_source_sha256: bakerSourceSha256,
      builtin_asset_catalog_sha256: builtinAssetCatalogSha256,
      cache_identity_sha256: cacheIdentitySha256,
      renderer: {
        loader_sha256: rendererIdentity.loader_sha256,
        wasm_sha256: rendererIdentity.wasm_sha256,
      },
      ir_spectrum_helper: {
        executable_sha256: irDumperIdentity.sha256,
        build_identity_sha256:
          irDumperIdentity.build_identity_sha256 ?? null,
      },
      execution_runtime: executionRuntime,
    },
    resolved_assets: assets,
    shared_recorded_starter: starterIdentity === null
      ? null
      : {
          aggregate_sha256: starterIdentity.aggregate_sha256,
          entry_count: starterIdentity.entries.length,
        },
    capture_scheduler: {
      id: "global-longest-scenario-first-v1",
      scope: "held-and-directional-renderer-captures",
    },
    runtime_manifest_sha256: runtimeManifestSha256,
    revengine_descriptor_sha256: revengineDescriptorSha256,
    completed: true,
  };
}

export async function main(argv = process.argv.slice(2)) {
  const options = parseArguments(argv);
  if (options.help) {
    process.stdout.write(`${usage()}\n`);
    return Object.freeze({ help: true });
  }
  const engineSnapshot = readJsonSnapshot(options.enginePath, "engine");
  const engine = engineSnapshot.value;
  if (engine.schema !== "engine-sim-offline/engine") {
    fail(`unsupported engine schema ${engine.schema}`);
  }
  const profileSnapshot = readJsonSnapshot(options.profilePath, "profile");
  const profile = validateEngineProfileCompatibility(
    engine,
    validateProfile(profileSnapshot.value),
  );
  const modulePath = options.modulePath ?? discoverRendererModule();
  const builtinAssetsPath = options.builtinAssetsPath ??
    discoverBuiltinAssets(modulePath);
  if (builtinAssetsPath === null) {
    fail(
      "--builtin-assets is required because no content-addressed built-in " +
      "asset bundle was found",
    );
  }
  const builtinAssets = loadBuiltinAssetBundle(builtinAssetsPath);
  const sharedStarterSource = profile.lifecycle.shared_recorded_starter
    ? builtinAssets.sharedStarter()
    : null;
  const engineId = revengineEngineId(
    engine.engine?.identity?.id,
    "engine identity id",
  );
  const engineSha256 = engineSnapshot.sha256;
  const profileSha256 = profileSnapshot.sha256;
  const executionRuntime = createExecutionRuntimeIdentity();
  const bakerSourceSnapshots = snapshotBakerSources();
  const bakerSourceSha256 = hashLabeledBytes(bakerSourceSnapshots);

  const resolvedAssetInputs = new Map();
  const collectAsset = (kind, id, digest) => {
    const key = `${kind}\u0000${id}\u0000${digest}`;
    if (!resolvedAssetInputs.has(key)) {
      resolvedAssetInputs.set(key, Object.freeze({
        kind,
        id,
        ...builtinAssets.resolve(kind, id, digest),
      }));
    }
    return resolvedAssetInputs.get(key).path;
  };
  presentationAssets(engine, collectAsset);
  selectedAccessory(engine, collectAsset);
  const resolvedAssets = [...resolvedAssetInputs.values()].sort(
    compareAssetRecords,
  );

  let rendererSnapshot = null;
  let irDumperSnapshot = null;
  if (!options.plan) {
    assertNewOutput(options.outputPath);
    if (modulePath === null) {
      fail(
        "--module is required because no renderer module was found in an installed " +
        "or conventional build tree",
      );
    }
    rendererSnapshot = snapshotRenderer(modulePath);
    irDumperSnapshot = options.irDumperPath === null
      ? await ensureIrDumper(options.cachePath, options.cxx, executionRuntime)
      : snapshotExplicitIrDumper(options.irDumperPath);
  }
  const cacheIdentity = createBakeCacheIdentity({
    engineSha256,
    profileSha256,
    bakerSourceSha256,
    builtinAssetCatalogSha256: builtinAssets.catalogSha256,
    resolvedAssets,
    sharedStarterAggregateSha256:
      sharedStarterSource?.identity.aggregate_sha256 ?? null,
    rendererIdentity: rendererSnapshot?.identity ?? null,
    irDumperSha256: irDumperSnapshot?.sha256 ?? null,
    executionRuntime,
  });
  const cacheNamespace = path.join(options.cachePath, "bakes", cacheIdentity);
  const workspace = path.join(cacheNamespace, "workspace");
  const scenarioPath = path.join(workspace, "scenario-template.json");
  const inventoryPath = path.join(workspace, "inventory.json");
  const stagedEnginePath = path.join(workspace, "input", "engine.json");
  const stagedProfilePath = path.join(workspace, "input", "profile.json");
  const stagedRendererModulePath = path.join(
    workspace,
    "renderer",
    "engine-sim-offline.js",
  );
  const stagedRendererWasmPath = path.join(
    workspace,
    "renderer",
    "engine-sim-offline.wasm",
  );
  const stagedIrDumperPath = path.join(
    workspace,
    "tools",
    "dump-ir-spectrum",
  );
  const stagedSourceRoot = path.join(workspace, "source");
  const stagedHeldStage = path.join(
    stagedSourceRoot,
    "tools/responsive-audio-baker/internal/held-texture.mjs",
  );
  const stagedDirectionalStage = path.join(
    stagedSourceRoot,
    "tools/responsive-audio-baker/internal/directional-transients.mjs",
  );
  const stagedLifecycleStage = path.join(
    stagedSourceRoot,
    "tools/responsive-audio-baker/internal/lifecycle.mjs",
  );
  const resolveAsset = (kind, id, digest) => {
    const key = `${kind}\u0000${id}\u0000${digest}`;
    const source = resolvedAssetInputs.get(key);
    if (source === undefined) {
      fail(`inventory requested an unresolved asset ${kind}/${id}/${digest}`);
    }
    const stagedPath = path.join(workspace, "assets", digest);
    return stagedPath;
  };
  const scenario = scenarioTemplate(engine, profile);
  const inventory = createBakeInventory({
    engine,
    enginePath: stagedEnginePath,
    profile,
    scenarioPath,
    resolveAsset,
  });

  const plan = {
    schema: "engine-sim-offline/responsive-audio-bake-plan-v1",
    engine: engineId,
    engine_sha256: engineSha256,
    profile: profile.id,
    profile_sha256: profileSha256,
    baker_source_sha256: bakerSourceSha256,
    builtin_asset_catalog_sha256: builtinAssets.catalogSha256,
    cache_identity_sha256: options.plan ? null : cacheIdentity,
    cache_identity_complete: !options.plan,
    physics_rate_hz: profile.capture.physics_rate_hz,
    sample_rate_hz: 192_000,
    rpm_anchors: profile.rpm.anchors,
    load_lanes: profile.capture.load_lanes,
    held_cell_count:
      profile.rpm.anchors.length * profile.capture.load_lanes.length,
    directional_capture_count: profile.capture.load_lanes.length * 2,
    lifecycle_enabled: profile.lifecycle.enabled,
    shared_recorded_starter: profile.lifecycle.shared_recorded_starter,
    maximum_jobs: options.jobs,
    capture_scheduler: {
      id: "global-longest-scenario-first-v1",
      scope: "held-and-directional-renderer-captures",
      maximum_concurrency: options.jobs,
    },
    cache_root: options.cachePath,
    cache_namespace: options.plan ? null : cacheNamespace,
    builtin_assets: builtinAssets.root,
    renderer_identity: rendererSnapshot?.identity ?? null,
    ir_spectrum_helper_sha256: irDumperSnapshot?.sha256 ?? null,
    execution_runtime: executionRuntime,
    output: options.outputPath,
  };
  if (options.plan) {
    process.stdout.write(`${JSON.stringify(plan, null, 2)}\n`);
    return plan;
  }

  const stageOutput = path.join(
    path.dirname(options.outputPath),
    `.${path.basename(options.outputPath)}.staging-${process.pid}`,
  );
  if (fs.existsSync(stageOutput)) {
    fail(`staging directory already exists: ${stageOutput}`);
  }
  const releaseCacheLock = acquireCacheLock(cacheNamespace);
  try {
    fs.mkdirSync(path.dirname(stagedEnginePath), { recursive: true });
    fs.writeFileSync(stagedEnginePath, engineSnapshot.bytes);
    fs.writeFileSync(stagedProfilePath, profileSnapshot.bytes);
    stageBakerSources(
      stagedSourceRoot,
      bakerSourceSnapshots,
      bakerSourceSha256,
    );
    for (const { bytes, sha256: digest } of resolvedAssets) {
      const stagedPath = path.join(workspace, "assets", digest);
      fs.mkdirSync(path.dirname(stagedPath), { recursive: true });
      if (!fs.existsSync(stagedPath)) fs.writeFileSync(stagedPath, bytes);
      if (sha256(fs.readFileSync(stagedPath)) !== digest) {
        fail(`staged built-in asset changed: ${stagedPath}`);
      }
    }
    fs.mkdirSync(path.dirname(stagedRendererModulePath), { recursive: true });
    fs.writeFileSync(stagedRendererModulePath, rendererSnapshot.loader.bytes);
    fs.writeFileSync(stagedRendererWasmPath, rendererSnapshot.wasm.bytes);
    if (
      JSON.stringify(rendererBytesIdentity(
        fs.readFileSync(stagedRendererModulePath),
        fs.readFileSync(stagedRendererWasmPath),
      )) !== JSON.stringify(rendererSnapshot.identity)
    ) {
      fail("staged renderer differs from its acquired bytes");
    }
    fs.mkdirSync(path.dirname(stagedIrDumperPath), { recursive: true });
    fs.writeFileSync(stagedIrDumperPath, irDumperSnapshot.bytes, { mode: 0o755 });
    fs.chmodSync(stagedIrDumperPath, 0o755);
    if (sha256(fs.readFileSync(stagedIrDumperPath)) !== irDumperSnapshot.sha256) {
      fail("staged IR spectrum helper differs from its acquired bytes");
    }
    assertUsableIrDumper(stagedIrDumperPath);
    writeJson(scenarioPath, scenario);
    writeJson(inventoryPath, inventory);
    fs.mkdirSync(stageOutput, { recursive: true });
    const environment = {
      ...process.env,
      ESO_RESPONSIVE_BAKE_INVENTORY: inventoryPath,
      ESO_RESPONSIVE_BAKE_CACHE: cacheNamespace,
      ESO_RESPONSIVE_BAKE_OUTPUT: stageOutput,
      ESO_RESPONSIVE_BAKE_MODULE: stagedRendererModulePath,
      ESO_RESPONSIVE_BAKE_WASM: stagedRendererWasmPath,
      ESO_RESPONSIVE_BAKE_IR_DUMPER: stagedIrDumperPath,
      ESO_HELD_CAPTURE_CONCURRENCY: String(options.jobs),
      ESO_RESPONSIVE_BAKE_SHARED_STARTER:
        profile.lifecycle.shared_recorded_starter ? "1" : "0",
    };

    await captureResponsiveSources({
      engineId,
      profile,
      environment,
      maximumConcurrency: options.jobs,
      heldStagePath: stagedHeldStage,
      directionalStagePath: stagedDirectionalStage,
    });
    await run(process.execPath, [
      stagedHeldStage,
      "--engine",
      engineId,
      "--held-only",
    ], environment);
    await run(process.execPath, [
      stagedDirectionalStage,
      "--engine",
      engineId,
      "--concurrency",
      String(options.jobs),
    ], environment);
    await run(
      process.execPath,
      [stagedHeldStage, "--engine", engineId],
      environment,
    );

    if (profile.lifecycle.enabled) {
      fs.rmSync(path.join(cacheNamespace, "lifecycle", "candidate"), {
        recursive: true,
        force: true,
      });
      if (profile.lifecycle.shared_recorded_starter) {
        fs.cpSync(
          sharedStarterSource.root,
          path.join(stageOutput, "shared-recorded-starter"),
          { recursive: true, errorOnExist: true },
        );
        const stagedStarterIdentity = fingerprintRegularTree(
          path.join(stageOutput, "shared-recorded-starter"),
        );
        if (
          stagedStarterIdentity.aggregate_sha256 !==
            sharedStarterSource.identity.aggregate_sha256
        ) {
          fail("shared recorded starter changed while it was staged");
        }
      }
      await run(process.execPath, [stagedLifecycleStage, engineId], environment);
      await run(process.execPath, [
        stagedLifecycleStage,
        engineId,
        "--capture-elevated-shutdown",
      ], environment);
    }

    const runtimePath = path.join(stageOutput, "runtime.json");
    const runtime = readJson(runtimePath, "responsive runtime");
    if (
      runtime.engine !== engineId ||
      runtime.held_package_path !== "held/package.json" ||
      runtime.directional_package_path !== "directional/runtime.json" ||
      (profile.lifecycle.enabled &&
        runtime.lifecycle_package_path !== "lifecycle/runtime.json") ||
      (!profile.lifecycle.enabled &&
        runtime.lifecycle_package_path !== undefined) ||
      (profile.lifecycle.shared_recorded_starter &&
        runtime.shared_recorded_starter_package_path !==
          "shared-recorded-starter/runtime.json") ||
      (!profile.lifecycle.shared_recorded_starter &&
        runtime.shared_recorded_starter_package_path !== undefined)
    ) {
      fail("responsive package root does not match the requested plan");
    }
    const runtimeBytes = fs.readFileSync(runtimePath);
    const runtimeManifestSha256 = sha256(runtimeBytes);
    const descriptorPath = path.join(stageOutput, "revengine.json");
    writeJson(descriptorPath, createRevengineDescriptor(engineId, runtimeBytes));
    const revengineDescriptorSha256 = sha256(fs.readFileSync(descriptorPath));
    writeJson(
      path.join(stageOutput, "bake-report.json"),
      createBakeReport({
        engineId,
        engineSha256,
        profileId: profile.id,
        profileSha256,
        bakerSourceSha256,
        builtinAssetCatalogSha256: builtinAssets.catalogSha256,
        cacheIdentitySha256: cacheIdentity,
        rendererIdentity: rendererSnapshot.identity,
        irDumperIdentity: irDumperSnapshot,
        executionRuntime,
        resolvedAssets,
        starterIdentity: sharedStarterSource?.identity ?? null,
        runtimeManifestSha256,
        revengineDescriptorSha256,
      }),
    );
    validateRevenginePackageTree(stageOutput);
    fs.renameSync(stageOutput, options.outputPath);
    try {
      cleanupPublishedLifecycleRuns(cacheNamespace);
    } catch (error) {
      process.stderr.write(
        `warning: published bake retained lifecycle run scratch: ` +
        `${error.stack ?? error}\n`,
      );
    }
  } catch (error) {
    process.stderr.write(`responsive bake staging retained at ${stageOutput}\n`);
    throw error;
  } finally {
    releaseCacheLock();
  }
  const result = {
    ...plan,
    runtime_manifest: path.join(options.outputPath, "runtime.json"),
    completed: true,
  };
  process.stdout.write(`${JSON.stringify(result, null, 2)}\n`);
  return result;
}

if (process.argv[1] !== undefined &&
    import.meta.url === pathToFileURL(path.resolve(process.argv[1])).href) {
  main().catch((error) => {
    process.stderr.write(`responsive audio bake failed: ${error.stack ?? error}\n`);
    process.exitCode = 1;
  });
}
