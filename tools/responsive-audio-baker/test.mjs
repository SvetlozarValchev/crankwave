import assert from "node:assert/strict";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { spawnSync } from "node:child_process";
import test from "node:test";
import { fileURLToPath } from "node:url";

import {
  MAXIMUM_INPUT_JSON_BYTES,
  PROFILE_SCHEMA,
  acquireCacheLock,
  cleanupPublishedLifecycleRuns,
  createBakeCacheIdentity,
  createBakeInventory,
  createBakeReport,
  createExecutionRuntimeIdentity,
  createRevengineDescriptor,
  main,
  runGloballyBoundedCaptureJobs,
  validateEngineProfileCompatibility,
  validateProfile,
} from "./bake.mjs";
import {
  compareCodeUnits,
  isPortableRevenginePath,
  portableArtifactToken,
  rendererFileIdentity,
  reusablePriorPhaseAlignment,
  validateRevenginePackageTree,
} from "./internal/bake-contract.mjs";

const here = path.dirname(fileURLToPath(import.meta.url));
const repository = path.resolve(here, "../..");
const enginePath = path.join(
  repository,
  "data/engines/bmw-m52tub28-cleanroom/engine.json",
);
const profilePath = path.join(here, "profiles/interactive-preview-v1.json");

function json(filePath) {
  return JSON.parse(fs.readFileSync(filePath, "utf8"));
}

function assembleTestBundle(root, engine) {
  fs.mkdirSync(path.join(root, "payloads"), { recursive: true });
  fs.copyFileSync(
    path.join(repository, "assets/builtin/catalog.v1.json"),
    path.join(root, "catalog.v1.json"),
  );
  const declarations = [
    ...engine.presentation.assets.map((asset) => ({
      ...asset,
      source: path.resolve(path.dirname(enginePath), asset.uri),
    })),
  ];
  const selectedAccessory = engine.engine.accessory_configurations.find(
    ({ id }) => id === engine.engine.losses.accessory_configuration_id,
  );
  declarations.push({
    ...selectedAccessory,
    source: path.resolve(path.dirname(enginePath), selectedAccessory.uri),
  });
  for (const declaration of declarations) {
    fs.copyFileSync(
      declaration.source,
      path.join(root, "payloads", declaration.sha256),
    );
  }
  fs.cpSync(
    path.join(
      repository,
      "reference/fixtures/responsive-audio/shared-recorded-starter",
    ),
    path.join(root, "runtime-audio/shared-recorded-starter"),
    { recursive: true },
  );
}

test("the standalone baker exposes help without requiring inputs", () => {
  const result = spawnSync(process.execPath, [path.join(here, "bake.mjs"), "--help"], {
    encoding: "utf8",
  });
  assert.equal(result.status, 0, result.stderr);
  assert.match(result.stdout, /^usage:\n/u);
  assert.match(result.stdout, /--engine ENGINE\.json/u);
  assert.equal(result.stderr, "");
});

test("the tracked profile validates and rejects an altered lane grid", () => {
  const profile = validateProfile(json(profilePath));
  assert.equal(profile.schema, PROFILE_SCHEMA);
  assert.equal(profile.rpm.anchors.length, 11);
  profile.capture.load_lanes[1].throttle_01 = 0.3;
  assert.throws(() => validateProfile(profile), /coast\/mid\/power grid/u);
});

test("profile validation is key-order independent and matches lifecycle semantics", () => {
  const profile = json(profilePath);
  profile.capture.load_lanes = profile.capture.load_lanes.map(
    ({ id, throttle_01: throttle }) => ({ throttle_01: throttle, id }),
  );
  assert.doesNotThrow(() => validateProfile(profile));

  const badId = structuredClone(profile);
  badId.id = "profile with spaces";
  assert.throws(() => validateProfile(badId), /profile schema/u);

  const contradictory = structuredClone(profile);
  contradictory.lifecycle.enabled = false;
  contradictory.lifecycle.shared_recorded_starter = true;
  assert.throws(() => validateProfile(contradictory), /requires lifecycle/u);

  const elevatedOutsideDomain = structuredClone(profile);
  elevatedOutsideDomain.lifecycle.elevated_shutdown.rpm = 10_000;
  assert.throws(() => validateProfile(elevatedOutsideDomain), /responsive RPM domain/u);
});

test("an engine profile cannot capture above the declared redline", () => {
  const profile = validateProfile(json(profilePath));
  assert.doesNotThrow(() =>
    validateEngineProfileCompatibility(json(enginePath), profile)
  );

  const kohler = json(path.join(
    repository,
    "data/engines/kohler-ch750-cleanroom/engine.json",
  ));
  assert.throws(
    () => validateEngineProfileCompatibility(kohler, profile),
    /maximum RPM 6500 exceeds engine redline 3600/u,
  );

  const lowRpmProfile = structuredClone(json(profilePath));
  lowRpmProfile.rpm.anchors = [600, 900, 1600, 2200, 3000, 3500];
  lowRpmProfile.rpm.outer_maximum_rpm = 3675;
  assert.doesNotThrow(() =>
    validateEngineProfileCompatibility(
      kohler,
      validateProfile(lowRpmProfile),
    )
  );
  lowRpmProfile.lifecycle.elevated_shutdown.rpm = 3601;
  assert.throws(
    () => validateEngineProfileCompatibility(
      kohler,
      validateProfile(lowRpmProfile),
    ),
    /elevated-shutdown RPM exceeds/u,
  );
});

test("artifact tokens are portable and do not inherit slug collisions", () => {
  const dotted = portableArtifactToken("route", "front.left_dry");
  const dashed = portableArtifactToken("route", "front-left-dry");
  assert.notEqual(dotted, dashed);
  assert.equal(isPortableRevenginePath(`audio/${dotted}.f32le`), true);
  assert.equal(isPortableRevenginePath("Upper/audio.f32le"), false);
  assert.equal(isPortableRevenginePath("con.bin"), false);
  assert.equal(isPortableRevenginePath(`${"a".repeat(128)}.bin`), false);
});

test("code-unit ordering and execution runtime identity are explicit", () => {
  assert.deepEqual(
    ["route_a", "route.a", "route-a"].sort(compareCodeUnits),
    ["route-a", "route.a", "route_a"],
  );
  const runtime = createExecutionRuntimeIdentity({
    node: "20.11.0",
    v8: "11.3.244.8-node.17",
    icu: "73.2",
    platform: "linux",
    arch: "x64",
    endianness: "LE",
  });
  assert.deepEqual(runtime, {
    node: "20.11.0",
    v8: "11.3.244.8-node.17",
    icu: "73.2",
    platform: "linux",
    arch: "x64",
    endianness: "LE",
  });
  const digest = "a".repeat(64);
  const inputs = {
    engineSha256: digest,
    profileSha256: digest,
    bakerSourceSha256: digest,
    builtinAssetCatalogSha256: digest,
    resolvedAssets: [
      { kind: "audio", id: "route_a", sha256: digest },
      { kind: "audio", id: "route-a", sha256: digest },
    ],
    sharedStarterAggregateSha256: null,
    rendererIdentity: { loader_sha256: digest, wasm_sha256: digest },
    irDumperSha256: digest,
    executionRuntime: runtime,
  };
  const first = createBakeCacheIdentity(inputs);
  assert.equal(
    createBakeCacheIdentity({
      ...inputs,
      resolvedAssets: [...inputs.resolvedAssets].reverse(),
    }),
    first,
  );
  assert.notEqual(
    createBakeCacheIdentity({
      ...inputs,
      executionRuntime: { ...runtime, node: "20.11.1" },
    }),
    first,
  );
});

test("renderer and prior-phase identities include every reuse boundary", () => {
  const temporary = fs.mkdtempSync(path.join(os.tmpdir(), "eso-renderer-id-"));
  try {
    const loader = path.join(temporary, "engine-sim-offline.js");
    const wasm = path.join(temporary, "engine-sim-offline.wasm");
    fs.writeFileSync(loader, "export default 1;\n");
    fs.writeFileSync(wasm, Buffer.from([0, 97, 115, 109]));
    const first = rendererFileIdentity(loader, wasm);
    fs.writeFileSync(wasm, Buffer.from([0, 97, 115, 110]));
    const second = rendererFileIdentity(loader, wasm);
    assert.equal(first.loader_sha256, second.loader_sha256);
    assert.notEqual(first.wasm_sha256, second.wasm_sha256);

    const prior = {
      provenance: { capture_identity: { sha256: "current" } },
      phase_alignment: { reference_cell_id: "cell", cells: [] },
    };
    assert.equal(
      reusablePriorPhaseAlignment(prior, "current"),
      prior.phase_alignment,
    );
    assert.equal(reusablePriorPhaseAlignment(prior, "different"), null);
  } finally {
    fs.rmSync(temporary, { recursive: true, force: true });
  }
});

test("generic inventory derives routes and authored assets without engine branches", () => {
  const engine = json(enginePath);
  const profile = validateProfile(json(profilePath));
  const inventory = createBakeInventory({
    engine,
    enginePath,
    profile,
    scenarioPath: path.join(os.tmpdir(), "generated-scenario.json"),
    resolveAsset: (kind, id, digest) =>
      path.join(os.tmpdir(), "staged-assets", kind, id, digest),
  });
  assert.equal(inventory.engines.length, 1);
  assert.equal(inventory.engines[0].engine_id, "bmw-m52tub28-cleanroom");
  assert.deepEqual(
    inventory.engines[0].presentation.dry_source_route_bus_ids,
    ["exhaust.front.dry", "exhaust.rear.dry"],
  );
  assert.equal(inventory.engines[0].assets.audio.length, 1);
  assert.equal(
    inventory.engines[0].canonical_scenarios.free_interactive.path,
    path.join(os.tmpdir(), "generated-scenario.json"),
  );
});

test("REVENGINE descriptor binds the exact responsive runtime bytes", () => {
  const bytes = Buffer.from("{\"engine\":\"example\"}\n");
  const descriptor = createRevengineDescriptor("example-engine", bytes);
  assert.equal(descriptor.version, 1);
  assert.equal(descriptor.runtime.manifest_path, "runtime.json");
  assert.equal(
    descriptor.runtime.manifest_sha256,
    "e9953d2546c3cdaa7ae1699264bf035214b0f101b128cf95b8a049f5d997e65c",
  );
});

test("package-tree preflight enforces the carrier path and manifest binding", () => {
  const temporary = fs.mkdtempSync(path.join(os.tmpdir(), "eso-tree-check-"));
  try {
    const runtimeBytes = Buffer.from("not-json-but-bound\n");
    fs.writeFileSync(path.join(temporary, "runtime.json"), runtimeBytes);
    fs.writeFileSync(
      path.join(temporary, "revengine.json"),
      `${JSON.stringify(createRevengineDescriptor("example-engine", runtimeBytes))}\n`,
    );
    const report = validateRevenginePackageTree(temporary);
    assert.equal(report.entry_count, 2);

    const validDescriptor = fs.readFileSync(
      path.join(temporary, "revengine.json"),
      "utf8",
    );
    fs.writeFileSync(
      path.join(temporary, "revengine.json"),
      validDescriptor.replace(
        '"schema":"engine-sim-offline/revengine-package"',
        '"schema":"engine-sim-offline/revengine-package",' +
          '"sch\\u0065ma":"engine-sim-offline/revengine-package"',
      ),
    );
    assert.throws(
      () => validateRevenginePackageTree(temporary),
      /repeats object member schema/u,
    );
    fs.writeFileSync(path.join(temporary, "revengine.json"), validDescriptor);

    fs.writeFileSync(path.join(temporary, "Upper.bin"), "x");
    assert.throws(
      () => validateRevenginePackageTree(temporary),
      /nonportable relative path/u,
    );
    fs.rmSync(path.join(temporary, "Upper.bin"));
    fs.symlinkSync("runtime.json", path.join(temporary, "linked.bin"));
    assert.throws(
      () => validateRevenginePackageTree(temporary),
      /symlink/u,
    );
  } finally {
    fs.rmSync(temporary, { recursive: true, force: true });
  }
});

test("package-tree scanning rejects the first entry beyond the carrier bound", () => {
  const temporary = fs.mkdtempSync(path.join(os.tmpdir(), "eso-tree-bound-"));
  try {
    const first = path.join(temporary, "entry-00000.bin");
    fs.writeFileSync(first, "");
    for (let index = 1; index <= 8_192; ++index) {
      fs.linkSync(
        first,
        path.join(temporary, `entry-${String(index).padStart(5, "0")}.bin`),
      );
    }
    assert.throws(
      () => validateRevenginePackageTree(temporary),
      /entry count is outside the REVENGINE v1 bounds/u,
    );
  } finally {
    fs.rmSync(temporary, { recursive: true, force: true });
  }
});

test("engine, profile, and catalog JSON reads are bounded before parsing", async () => {
  const temporary = fs.mkdtempSync(path.join(os.tmpdir(), "eso-json-bound-"));
  const oversizedEngine = path.join(temporary, "engine.json");
  const oversizedProfile = path.join(temporary, "profile.json");
  const bundle = path.join(temporary, "builtin-assets");
  const cache = path.join(temporary, "cache");
  try {
    for (const filePath of [oversizedEngine, oversizedProfile]) {
      fs.writeFileSync(filePath, "");
      fs.truncateSync(filePath, MAXIMUM_INPUT_JSON_BYTES + 1);
    }
    await assert.rejects(
      main([
        "--engine", oversizedEngine,
        "--profile", profilePath,
        "--output", path.join(temporary, "engine-output"),
        "--cache", cache,
        "--builtin-assets", bundle,
        "--plan",
      ]),
      /engine exceeds its 4194304-byte limit/u,
    );
    await assert.rejects(
      main([
        "--engine", enginePath,
        "--profile", oversizedProfile,
        "--output", path.join(temporary, "profile-output"),
        "--cache", cache,
        "--builtin-assets", bundle,
        "--plan",
      ]),
      /profile exceeds its 4194304-byte limit/u,
    );

    assembleTestBundle(bundle, json(enginePath));
    const catalogPath = path.join(bundle, "catalog.v1.json");
    fs.truncateSync(catalogPath, MAXIMUM_INPUT_JSON_BYTES + 1);
    await assert.rejects(
      main([
        "--engine", enginePath,
        "--profile", profilePath,
        "--output", path.join(temporary, "catalog-output"),
        "--cache", cache,
        "--builtin-assets", bundle,
        "--plan",
      ]),
      /built-in asset catalog exceeds its 4194304-byte limit/u,
    );
    assert.equal(fs.existsSync(cache), false);
  } finally {
    fs.rmSync(temporary, { recursive: true, force: true });
  }
});

test("cache locks reject live owners and reap only same-scope dead owners", {
  skip: process.platform !== "linux",
}, () => {
  const temporary = fs.mkdtempSync(path.join(os.tmpdir(), "eso-cache-lock-"));
  try {
    const release = acquireCacheLock(temporary);
    const registry = path.join(temporary, ".active");
    const [ownerName] = fs.readdirSync(registry);
    assert.match(
      ownerName,
      /^owner-[0-9a-f]{32}-m[0-9a-f]{64}-b[0-9a-f]{32}-n[0-9]+-p[0-9]+-s[0-9]+\.lock$/u,
    );
    assert.throws(
      () => acquireCacheLock(temporary),
      /already active or has an unrecoverable owner/u,
    );
    assert.deepEqual(fs.readdirSync(registry), [ownerName]);
    release();

    const staleOwnerName = ownerName.replace(
      /-p[0-9]+-s[0-9]+\.lock$/u,
      "-p2147483647-s1.lock",
    );
    fs.writeFileSync(path.join(registry, staleOwnerName), "");
    const recoveredRelease = acquireCacheLock(temporary);
    assert.equal(fs.readdirSync(registry).length, 1);
    recoveredRelease();
    assert.deepEqual(fs.readdirSync(registry), []);

    const malformed = path.join(
      registry,
      "owner-malformed.lock",
    );
    fs.writeFileSync(malformed, "");
    assert.throws(
      () => acquireCacheLock(temporary),
      /already active or has an unrecoverable owner/u,
    );
    assert.equal(fs.existsSync(malformed), true);
  } finally {
    fs.rmSync(temporary, { recursive: true, force: true });
  }
});

test("successful-publication cleanup removes only lifecycle run scratch", () => {
  const temporary = fs.mkdtempSync(path.join(os.tmpdir(), "eso-run-cleanup-"));
  try {
    const runs = path.join(temporary, "lifecycle/runs/run-1");
    const candidate = path.join(temporary, "lifecycle/candidate/runtime.json");
    fs.mkdirSync(runs, { recursive: true });
    fs.mkdirSync(path.dirname(candidate), { recursive: true });
    fs.writeFileSync(path.join(runs, "scratch.bin"), "scratch");
    fs.writeFileSync(candidate, "candidate");
    assert.equal(cleanupPublishedLifecycleRuns(temporary), true);
    assert.equal(fs.existsSync(path.join(temporary, "lifecycle/runs")), false);
    assert.equal(fs.readFileSync(candidate, "utf8"), "candidate");
    assert.equal(cleanupPublishedLifecycleRuns(temporary), false);
  } finally {
    fs.rmSync(temporary, { recursive: true, force: true });
  }
});

test("packaged bake reports contain portable runtime identity but no host paths", () => {
  const digest = "a".repeat(64);
  const executionRuntime = createExecutionRuntimeIdentity();
  const report = createBakeReport({
    engineId: "example-engine",
    engineSha256: digest,
    profileId: "example-profile",
    profileSha256: digest,
    bakerSourceSha256: digest,
    builtinAssetCatalogSha256: digest,
    cacheIdentitySha256: digest,
    rendererIdentity: { loader_sha256: digest, wasm_sha256: digest },
    irDumperIdentity: { sha256: digest, build_identity_sha256: digest },
    executionRuntime,
    resolvedAssets: [
      {
        kind: "audio",
        id: "example_ir",
        sha256: digest,
        path: "/tmp/private/asset.wav",
      },
      { kind: "audio", id: "example-ir", sha256: digest },
    ],
    starterIdentity: { aggregate_sha256: digest, entries: [] },
    runtimeManifestSha256: digest,
    revengineDescriptorSha256: digest,
  });
  const text = JSON.stringify(report);
  assert.doesNotMatch(text, /\/tmp\/private/u);
  assert.doesNotMatch(text, /maximum_jobs|cache_root|output/u);
  assert.equal(report.implementation.renderer.wasm_sha256, digest);
  assert.deepEqual(report.implementation.execution_runtime, executionRuntime);
  assert.deepEqual(
    report.resolved_assets.map(({ id }) => id),
    ["example-ir", "example_ir"],
  );
});

test("the shared capture scheduler honors one global worker bound", async () => {
  let active = 0;
  let observed = 0;
  const started = [];
  const completed = [];
  const jobs = [
    ["short", 1],
    ["long", 4],
    ["medium", 2],
    ["long-b", 4],
    ["medium-b", 2],
  ].map(([label, estimate]) => ({
    label,
    estimated_duration_seconds: estimate,
    async run() {
      started.push(label);
      ++active;
      observed = Math.max(observed, active);
      await new Promise((resolve) => setTimeout(resolve, 5));
      --active;
    },
  }));
  const report = await runGloballyBoundedCaptureJobs(
    jobs,
    2,
    ({ label }) => completed.push(label),
  );
  assert.deepEqual(started.slice(0, 2), ["long", "long-b"]);
  assert.equal(observed, 2);
  assert.equal(report.maximum_concurrency, 2);
  assert.equal(report.observed_maximum_concurrency, 2);
  assert.equal(report.scheduled_capture_count, jobs.length);
  assert.equal(completed.length, jobs.length);
});

test("plan mode is renderer-free and has no filesystem side effects", async () => {
  const temporary = fs.mkdtempSync(path.join(os.tmpdir(), "eso-bake-plan-"));
  const cache = path.join(temporary, "cache");
  const output = path.join(temporary, "output");
  const bundle = path.join(temporary, "builtin-assets");
  const externalEngine = path.join(temporary, "uploaded-engine.json");
  try {
    const engine = json(enginePath);
    assembleTestBundle(bundle, engine);
    fs.copyFileSync(enginePath, externalEngine);
    fs.copyFileSync(
      profilePath,
      path.join(temporary, "responsive-audio-bake-profile.json"),
    );
    const plan = await main([
      "--engine", externalEngine,
      "--output", output,
      "--cache", cache,
      "--builtin-assets", bundle,
      "--jobs", "4",
      "--plan",
    ]);
    assert.equal(plan.held_cell_count, 33);
    assert.equal(plan.directional_capture_count, 6);
    assert.equal(plan.maximum_jobs, 4);
    assert.equal(plan.cache_identity_sha256, null);
    assert.equal(plan.cache_identity_complete, false);
    assert.equal(plan.cache_namespace, null);
    assert.equal(plan.builtin_assets, bundle);
    assert.equal(fs.existsSync(cache), false);
    assert.equal(fs.existsSync(output), false);
  } finally {
    fs.rmSync(temporary, { recursive: true, force: true });
  }
});
