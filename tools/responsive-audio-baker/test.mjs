import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { spawn, spawnSync } from "node:child_process";
import test from "node:test";
import { fileURLToPath } from "node:url";

import {
  AUTOMATIC_PROFILE_ID,
  AUTOMATIC_PROFILE_POLICY_ID,
  MAXIMUM_INPUT_JSON_BYTES,
  PROFILE_SCHEMA,
  RESPONSIVE_BAKE_FAILURE_SCHEMA,
  ResponsiveBakeFailure,
  acquireCacheLock,
  cleanupPublishedLifecycleRuns,
  createBakeCacheIdentity,
  createBakeInventory,
  createBakeProcessSupervisor,
  createBakeReport,
  createExecutionRuntimeIdentity,
  createRevengineDescriptor,
  deriveResponsiveBakeProfile,
  createSanitizedChildEnvironment,
  main,
  releaseIdentityFromArguments,
  responsiveBakeFailureRecord,
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

function trackedEnginePaths(root) {
  const paths = [];
  for (const entry of fs.readdirSync(root, { withFileTypes: true })) {
    const entryPath = path.join(root, entry.name);
    if (entry.isDirectory()) {
      paths.push(...trackedEnginePaths(entryPath));
    } else if (/^engine(?:-[a-z0-9-]+)?\.json$/u.test(entry.name)) {
      const candidate = json(entryPath);
      if (candidate.schema === "engine-sim-offline/engine") {
        paths.push(entryPath);
      }
    }
  }
  return paths.sort();
}

function json(filePath) {
  return JSON.parse(fs.readFileSync(filePath, "utf8"));
}

function automaticProfileSha256(profile) {
  return createHash("sha256")
    .update(`${JSON.stringify(profile, null, 2)}\n`)
    .digest("hex");
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

test("command failures expose a stable machine-readable envelope", () => {
  const result = spawnSync(
    process.execPath,
    [path.join(here, "bake.mjs"), "--not-a-real-option"],
    { encoding: "utf8" },
  );
  assert.equal(result.status, 64);
  assert.equal(result.stdout, "");
  const failure = JSON.parse(result.stderr);
  assert.deepEqual(
    Object.keys(failure),
    [
      "schema",
      "release_identity",
      "code",
      "exit_code",
      "retryable",
      "signal",
      "message",
    ],
  );
  assert.equal(failure.schema, RESPONSIVE_BAKE_FAILURE_SCHEMA);
  assert.equal(failure.release_identity, null);
  assert.equal(failure.code, "invalid_invocation");
  assert.equal(failure.exit_code, 64);
  assert.equal(failure.retryable, false);
  assert.equal(failure.signal, null);
});

test("renderer children receive only an allowlisted environment", async () => {
  const inherited = {
    PATH: process.env.PATH ?? "/usr/bin:/bin",
    HOME: "/private/home",
    AWS_ACCESS_KEY_ID: "credential",
    AWS_SECRET_ACCESS_KEY: "secret",
    AWS_SESSION_TOKEN: "token",
    GOOGLE_APPLICATION_CREDENTIALS: "/private/google.json",
    HTTP_PROXY: "http://proxy.invalid",
    https_proxy: "http://proxy.invalid",
    NO_PROXY: "metadata.internal",
    NODE_OPTIONS: "--require=/private/inject.cjs",
  };
  const environment = createSanitizedChildEnvironment({
    ESO_RESPONSIVE_BAKE_OUTPUT: "/tmp/controlled-output",
  }, inherited);
  assert.deepEqual(
    Object.keys(environment).sort(),
    [
      "ESO_RESPONSIVE_BAKE_OUTPUT",
      "LANG",
      "LC_ALL",
      "PATH",
      "TZ",
    ],
  );
  const supervisor = createBakeProcessSupervisor();
  try {
    const result = await supervisor.runChild(
      process.execPath,
      ["-e", "process.stdout.write(JSON.stringify(process.env))"],
      { environment, stdout: "capture", stderr: "capture" },
    );
    const observed = JSON.parse(result.stdout);
    assert.equal(observed.ESO_RESPONSIVE_BAKE_OUTPUT, "/tmp/controlled-output");
    assert.equal(observed.LANG, "C");
    assert.equal(observed.LC_ALL, "C");
    assert.equal(observed.TZ, "UTC");
    for (const forbidden of [
      "HOME",
      "AWS_ACCESS_KEY_ID",
      "AWS_SECRET_ACCESS_KEY",
      "AWS_SESSION_TOKEN",
      "GOOGLE_APPLICATION_CREDENTIALS",
      "HTTP_PROXY",
      "https_proxy",
      "NO_PROXY",
      "NODE_OPTIONS",
    ]) {
      assert.equal(Object.hasOwn(observed, forbidden), false, forbidden);
    }
  } finally {
    supervisor.dispose();
  }
});

test("termination reaches the complete renderer child process group", {
  skip: process.platform !== "linux",
}, async () => {
  const temporary = fs.mkdtempSync(path.join(os.tmpdir(), "eso-child-tree-"));
  const pidFile = path.join(temporary, "pids.json");
  const controller = new AbortController();
  const supervisor = createBakeProcessSupervisor({
    signal: controller.signal,
    terminationGraceMs: 50,
  });
  let pids = null;
  const processIsLive = (pid) => {
    try {
      const status = fs.readFileSync(`/proc/${pid}/stat`, "utf8");
      const commandEnd = status.lastIndexOf(")");
      return status.slice(commandEnd + 1).trim().split(/\s+/u)[0] !== "Z";
    } catch {
      return false;
    }
  };
  try {
    const script = [
      "const {spawn}=require('node:child_process');",
      "const fs=require('node:fs');",
      "const nested=spawn(process.execPath,['-e','setInterval(()=>{},1000)'],{stdio:'ignore'});",
      "fs.writeFileSync(process.argv[1],JSON.stringify({parent:process.pid,nested:nested.pid}));",
      "setInterval(()=>{},1000);",
    ].join("");
    const pending = supervisor.runChild(
      process.execPath,
      ["-e", script, pidFile],
      {
        environment: createSanitizedChildEnvironment(),
        stdout: "ignore",
        stderr: "capture",
      },
    );
    for (let attempt = 0; attempt < 200 && !fs.existsSync(pidFile); ++attempt) {
      await new Promise((resolve) => setTimeout(resolve, 5));
    }
    assert.equal(fs.existsSync(pidFile), true, "child process did not become ready");
    pids = JSON.parse(fs.readFileSync(pidFile, "utf8"));
    controller.abort(new ResponsiveBakeFailure(
      "terminated",
      "responsive bake terminated by SIGTERM",
      { retryable: true, signal: "SIGTERM" },
    ));
    await assert.rejects(pending, (error) => {
      assert.equal(error.code, "terminated");
      return true;
    });
    await supervisor.drain();
    for (let attempt = 0; attempt < 200; ++attempt) {
      if (!processIsLive(pids.parent) && !processIsLive(pids.nested)) break;
      await new Promise((resolve) => setTimeout(resolve, 5));
    }
    assert.equal(processIsLive(pids.parent), false);
    assert.equal(processIsLive(pids.nested), false);
    assert.equal(supervisor.activeChildCount, 0);
  } finally {
    if (pids !== null) {
      for (const pid of [pids.parent, pids.nested]) {
        try {
          process.kill(pid, "SIGKILL");
        } catch (error) {
          if (error?.code !== "ESRCH") throw error;
        }
      }
    }
    supervisor.dispose();
    fs.rmSync(temporary, { recursive: true, force: true });
  }
});

test("a failed renderer leader cannot orphan its process group", {
  skip: process.platform !== "linux",
}, async () => {
  const temporary = fs.mkdtempSync(path.join(os.tmpdir(), "eso-failed-tree-"));
  const pidFile = path.join(temporary, "pids.json");
  const supervisor = createBakeProcessSupervisor({ terminationGraceMs: 25 });
  let pids = null;
  const processIsLive = (pid) => {
    try {
      const status = fs.readFileSync(`/proc/${pid}/stat`, "utf8");
      const commandEnd = status.lastIndexOf(")");
      return status.slice(commandEnd + 1).trim().split(/\s+/u)[0] !== "Z";
    } catch {
      return false;
    }
  };
  try {
    const script = [
      "const {spawn}=require('node:child_process');",
      "const fs=require('node:fs');",
      "const nested=spawn(process.execPath,['-e','setInterval(()=>{},1000)'],{stdio:'ignore'});",
      "fs.writeFileSync(process.argv[1],JSON.stringify({parent:process.pid,nested:nested.pid}));",
      "setTimeout(()=>process.exit(7),10);",
    ].join("");
    await assert.rejects(
      supervisor.runChild(process.execPath, ["-e", script, pidFile], {
        environment: createSanitizedChildEnvironment(),
        stdout: "ignore",
        stderr: "capture",
      }),
      (error) => error?.code === "child_process_failed",
    );
    pids = JSON.parse(fs.readFileSync(pidFile, "utf8"));
    await supervisor.drain();
    assert.equal(processIsLive(pids.parent), false);
    assert.equal(processIsLive(pids.nested), false);
    assert.equal(supervisor.activeChildCount, 0);
  } finally {
    if (pids !== null) {
      for (const pid of [pids.parent, pids.nested]) {
        try {
          process.kill(pid, "SIGKILL");
        } catch (error) {
          if (error?.code !== "ESRCH") throw error;
        }
      }
    }
    supervisor.dispose();
    fs.rmSync(temporary, { recursive: true, force: true });
  }
});

test("an absolute deadline cancels work with the stable retryable code", async () => {
  const supervisor = createBakeProcessSupervisor({
    deadlineUnixMs: Date.now() + 25,
    terminationGraceMs: 25,
  });
  try {
    await assert.rejects(
      supervisor.runChild(
        process.execPath,
        ["-e", "setInterval(()=>{},1000)"],
        {
          environment: createSanitizedChildEnvironment(),
          stdout: "ignore",
          stderr: "ignore",
        },
      ),
      (error) => {
        assert.equal(error.code, "deadline_exceeded");
        assert.equal(error.retryable, true);
        assert.deepEqual(responsiveBakeFailureRecord(error), {
          schema: RESPONSIVE_BAKE_FAILURE_SCHEMA,
          release_identity: null,
          code: "deadline_exceeded",
          exit_code: 75,
          retryable: true,
          signal: null,
          message: "responsive bake deadline exceeded",
        });
        return true;
      },
    );
    assert.equal(supervisor.activeChildCount, 0);
  } finally {
    supervisor.dispose();
  }
});

test("deadline checks remain authoritative across synchronous work", async () => {
  let currentTime = 10;
  const supervisor = createBakeProcessSupervisor({
    deadlineUnixMs: 20,
    now: () => currentTime,
  });
  try {
    supervisor.throwIfAborted();
    currentTime = 21;
    assert.throws(
      () => supervisor.throwIfAborted(),
      (error) => error?.code === "deadline_exceeded",
    );
    await supervisor.drain();
  } finally {
    supervisor.dispose();
  }
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

test("the automatic profile preserves the accepted 6500 RPM reference", () => {
  const engine = json(enginePath);
  const explicit = validateProfile(json(profilePath));
  const automatic = deriveResponsiveBakeProfile(engine);
  assert.equal(AUTOMATIC_PROFILE_POLICY_ID, "engine-redline-affine-v1");
  assert.equal(automatic.id, AUTOMATIC_PROFILE_ID);
  assert.deepEqual(automatic.rpm, explicit.rpm);
  assert.deepEqual(automatic.capture, explicit.capture);
  assert.deepEqual(automatic.lifecycle, explicit.lifecycle);
  assert.equal(
    automaticProfileSha256(automatic),
    "3ccec50bec6a04558f94de10edc17e4ddb7c2f0bddeae37e4118405b74932201",
  );
  assert.doesNotThrow(() =>
    validateEngineProfileCompatibility(engine, automatic)
  );
});

test("the automatic profile deterministically covers low and high redlines", () => {
  const radial = deriveResponsiveBakeProfile(json(path.join(
    repository,
    "data/engines/radial-5-cleanroom/engine.json",
  )));
  assert.deepEqual(radial.rpm.anchors, [
    600,
    640.677966,
    722.033898,
    844.067797,
    1006.779661,
    1250.847458,
    1576.271186,
    1983.050847,
    2389.830508,
    2796.610169,
    3000,
  ]);
  assert.equal(radial.lifecycle.elevated_shutdown.rpm, 1576.271186);
  assert.equal(
    automaticProfileSha256(radial),
    "5ba6d677d122c8d81eceb61e24801085c0246cba40644386f19b923f12ffcbaa",
  );

  const honda = deriveResponsiveBakeProfile(json(path.join(
    repository,
    "data/engines/honda-b18c5-cleanroom/engine.json",
  )));
  assert.deepEqual(honda.rpm.anchors, [
    600,
    732.20339,
    996.610169,
    1393.220339,
    1922.033898,
    2715.254237,
    3772.881356,
    5094.915254,
    6416.949153,
    7738.983051,
    8400,
  ]);
  assert.equal(honda.lifecycle.elevated_shutdown.rpm, 3772.881356);
  assert.equal(
    automaticProfileSha256(honda),
    "e5d922f8504b5000dd06d2c6f0a15309ec48961411a738412bf030b964da3292",
  );
  assert.notEqual(JSON.stringify(radial), JSON.stringify(honda));
});

test("the automatic policy validates every tracked engine deterministically", () => {
  const paths = trackedEnginePaths(path.join(repository, "data/engines"));
  assert.equal(paths.length, 15);
  const profilesByRedline = new Map();
  for (const trackedPath of paths) {
    const engine = json(trackedPath);
    const first = deriveResponsiveBakeProfile(engine);
    const second = deriveResponsiveBakeProfile(structuredClone(engine));
    const redline = engine.engine.limits.redline.value;
    assert.equal(JSON.stringify(first), JSON.stringify(second), trackedPath);
    assert.equal(first.rpm.anchors.length, 11, trackedPath);
    assert.equal(first.rpm.anchors.at(-1), redline, trackedPath);
    assert.equal(first.rpm.outer_maximum_rpm > redline, true, trackedPath);
    assert.equal(
      first.rpm.anchors.every(
        (rpm, index) => index === 0 || rpm > first.rpm.anchors[index - 1],
      ),
      true,
      trackedPath,
    );
    assert.doesNotThrow(
      () => validateEngineProfileCompatibility(engine, first),
      trackedPath,
    );
    const serialized = JSON.stringify(first);
    const prior = profilesByRedline.get(redline);
    if (prior === undefined) profilesByRedline.set(redline, serialized);
    else assert.equal(serialized, prior, trackedPath);
  }
  assert.deepEqual([...profilesByRedline.keys()].sort((a, b) => a - b), [
    3000,
    3600,
    5000,
    5900,
    6000,
    6500,
    7000,
    8400,
  ]);
});

test("automatic selection fails closed outside its declared RPM floor", () => {
  const engine = structuredClone(json(enginePath));
  engine.engine.limits.redline.value = 249;
  assert.throws(
    () => deriveResponsiveBakeProfile(engine),
    /requires an engine redline of at least 250 RPM; use --profile/u,
  );
  engine.engine.limits.redline = { value: 6500, unit: "rad\/s" };
  assert.throws(
    () => deriveResponsiveBakeProfile(engine),
    /redline\.unit must be rpm/u,
  );
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
    releaseIdentity: "1.2.3-rc.1",
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
  assert.equal(report.release_identity, "1.2.3-rc.1");
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
    const plan = await main([
      "--engine", externalEngine,
      "--output", output,
      "--cache", cache,
      "--builtin-assets", bundle,
      "--release-identity", "1.2.3-rc.1",
      "--jobs", "4",
      "--plan",
    ]);
    assert.equal(plan.profile, AUTOMATIC_PROFILE_ID);
    assert.equal(plan.release_identity, "1.2.3-rc.1");
    assert.deepEqual(plan.rpm_anchors, json(profilePath).rpm.anchors);
    assert.equal(fs.existsSync(
      path.join(temporary, "responsive-audio-bake-profile.json"),
    ), false);
    assert.equal(plan.held_cell_count, 33);
    assert.equal(plan.directional_capture_count, 6);
    assert.equal(plan.maximum_jobs, 4);
    assert.equal(plan.cache_identity_sha256, null);
    assert.equal(plan.cache_identity_complete, false);
    assert.equal(plan.cache_namespace, null);
    assert.equal(plan.builtin_assets, bundle);
    assert.equal(fs.existsSync(cache), false);
    assert.equal(fs.existsSync(output), false);

    const explicitPlan = await main([
      "--engine", externalEngine,
      "--profile", profilePath,
      "--output", path.join(temporary, "explicit-output"),
      "--cache", cache,
      "--builtin-assets", bundle,
      "--plan",
    ]);
    assert.equal(explicitPlan.profile, "interactive-preview-v1");
    assert.deepEqual(explicitPlan.rpm_anchors, plan.rpm_anchors);
    assert.notEqual(explicitPlan.profile_sha256, plan.profile_sha256);
    assert.equal(fs.existsSync(cache), false);
  } finally {
    fs.rmSync(temporary, { recursive: true, force: true });
  }
});

test("release identity extraction is bounded and validates semantic text", () => {
  assert.equal(
    releaseIdentityFromArguments(["--release-identity", "2.3.4+build.5"]),
    "2.3.4+build.5",
  );
  assert.equal(releaseIdentityFromArguments([]), null);
  assert.equal(
    releaseIdentityFromArguments([
      "--release-identity", "1.0.0",
      "--release-identity", "2.0.0",
    ]),
    null,
  );
  assert.equal(
    releaseIdentityFromArguments(["--release-identity", "development"]),
    null,
  );
});

test("an expired command deadline fails before creating cache or output", async () => {
  const temporary = fs.mkdtempSync(path.join(os.tmpdir(), "eso-past-deadline-"));
  const output = path.join(temporary, "output");
  const cache = path.join(temporary, "cache");
  try {
    await assert.rejects(
      main([
        "--engine", path.join(temporary, "missing-engine.json"),
        "--output", output,
        "--cache", cache,
        "--deadline-unix-ms", "1",
      ]),
      (error) => {
        assert.equal(error.code, "deadline_exceeded");
        return true;
      },
    );
    assert.equal(fs.existsSync(output), false);
    assert.equal(fs.existsSync(cache), false);
  } finally {
    fs.rmSync(temporary, { recursive: true, force: true });
  }
});

test("failed renderer work removes every incomplete output staging tree", {
  skip: process.platform === "win32",
}, async () => {
  const temporary = fs.mkdtempSync(path.join(os.tmpdir(), "eso-bake-cleanup-"));
  const output = path.join(temporary, "responsive-output");
  const cache = path.join(temporary, "cache");
  const bundle = path.join(temporary, "builtin-assets");
  const rendererRoot = path.join(temporary, "renderer");
  const modulePath = path.join(rendererRoot, "engine-sim-offline.js");
  const wasmPath = path.join(rendererRoot, "engine-sim-offline.wasm");
  const irDumper = path.join(temporary, "dump-ir-spectrum");
  try {
    assembleTestBundle(bundle, json(enginePath));
    fs.mkdirSync(rendererRoot, { recursive: true });
    fs.writeFileSync(
      modulePath,
      "export default async function(){throw new Error('fixture renderer failure');}\n",
    );
    fs.writeFileSync(wasmPath, Buffer.from([0, 97, 115, 109]));
    fs.writeFileSync(
      irDumper,
      "#!/bin/sh\n" +
        "if [ \"$#\" -eq 0 ]; then\n" +
        "  printf 'usage: dump-ir-spectrum fixture\\n' >&2\n" +
        "  exit 2\n" +
        "fi\n" +
        "exit 1\n",
      { mode: 0o755 },
    );
    fs.chmodSync(irDumper, 0o755);

    await assert.rejects(
      main([
        "--engine", enginePath,
        "--profile", path.join(here, "testdata/smoke-profile.json"),
        "--output", output,
        "--cache", cache,
        "--builtin-assets", bundle,
        "--module", modulePath,
        "--ir-dumper", irDumper,
        "--jobs", "2",
      ]),
      (error) => {
        assert.equal(error.code, "child_process_failed");
        return true;
      },
    );
    assert.equal(fs.existsSync(output), false);
    assert.deepEqual(
      fs.readdirSync(temporary).filter((name) =>
        name.startsWith(".responsive-output.staging-")
      ),
      [],
    );
    const ownerRegistries = [];
    if (fs.existsSync(cache)) {
      const pending = [cache];
      while (pending.length > 0) {
        const directory = pending.pop();
        for (const entry of fs.readdirSync(directory, { withFileTypes: true })) {
          const entryPath = path.join(directory, entry.name);
          if (entry.isDirectory()) pending.push(entryPath);
          if (entry.name === ".active") ownerRegistries.push(entryPath);
        }
      }
    }
    for (const registry of ownerRegistries) {
      assert.deepEqual(fs.readdirSync(registry), []);
    }
  } finally {
    fs.rmSync(temporary, { recursive: true, force: true });
  }
});

test("SIGTERM is reported and cleans an in-flight command output", {
  skip: process.platform === "win32",
}, async () => {
  const temporary = fs.mkdtempSync(path.join(os.tmpdir(), "eso-bake-sigterm-"));
  const output = path.join(temporary, "responsive-output");
  const cache = path.join(temporary, "cache");
  const bundle = path.join(temporary, "builtin-assets");
  const rendererRoot = path.join(temporary, "renderer");
  const modulePath = path.join(rendererRoot, "engine-sim-offline.js");
  const irDumper = path.join(temporary, "dump-ir-spectrum");
  let child = null;
  try {
    assembleTestBundle(bundle, json(enginePath));
    fs.mkdirSync(rendererRoot, { recursive: true });
    fs.writeFileSync(
      modulePath,
      "export default async function(){" +
        "await new Promise(()=>setInterval(()=>{},1000));}\n",
    );
    fs.writeFileSync(
      path.join(rendererRoot, "engine-sim-offline.wasm"),
      Buffer.from([0, 97, 115, 109]),
    );
    fs.writeFileSync(
      irDumper,
      "#!/bin/sh\n" +
        "if [ \"$#\" -eq 0 ]; then\n" +
        "  printf 'usage: dump-ir-spectrum fixture\\n' >&2\n" +
        "  exit 2\n" +
        "fi\n" +
        "exit 1\n",
      { mode: 0o755 },
    );
    fs.chmodSync(irDumper, 0o755);

    child = spawn(process.execPath, [
      path.join(here, "bake.mjs"),
      "--engine", enginePath,
      "--profile", path.join(here, "testdata/smoke-profile.json"),
      "--output", output,
      "--cache", cache,
      "--builtin-assets", bundle,
      "--module", modulePath,
      "--ir-dumper", irDumper,
      "--jobs", "2",
    ], { stdio: ["ignore", "pipe", "pipe"] });
    const stdoutChunks = [];
    const stderrChunks = [];
    child.stdout.on("data", (chunk) => stdoutChunks.push(chunk));
    child.stderr.on("data", (chunk) => stderrChunks.push(chunk));
    for (let attempt = 0; attempt < 400; ++attempt) {
      const staged = fs.readdirSync(temporary).some((name) =>
        name.startsWith(".responsive-output.staging-")
      );
      if (staged) break;
      await new Promise((resolve) => setTimeout(resolve, 5));
    }
    assert.equal(
      fs.readdirSync(temporary).some((name) =>
        name.startsWith(".responsive-output.staging-")
      ),
      true,
      "responsive bake did not reach its staging transaction",
    );
    assert.equal(child.kill("SIGTERM"), true);
    const result = await new Promise((resolve, reject) => {
      child.once("error", reject);
      child.once("close", (code, signal) => resolve({ code, signal }));
    });
    assert.deepEqual(result, { code: 75, signal: null });
    assert.equal(Buffer.concat(stdoutChunks).toString("utf8"), "");
    const stderrLines = Buffer.concat(stderrChunks).toString("utf8")
      .trim().split("\n");
    const failure = JSON.parse(stderrLines.at(-1));
    assert.equal(failure.schema, RESPONSIVE_BAKE_FAILURE_SCHEMA);
    assert.equal(failure.code, "terminated");
    assert.equal(failure.exit_code, 75);
    assert.equal(failure.retryable, true);
    assert.equal(failure.signal, "SIGTERM");
    assert.equal(fs.existsSync(output), false);
    assert.deepEqual(
      fs.readdirSync(temporary).filter((name) =>
        name.startsWith(".responsive-output.staging-")
      ),
      [],
    );
  } finally {
    if (child?.exitCode === null && child?.signalCode === null) {
      child.kill("SIGKILL");
    }
    fs.rmSync(temporary, { recursive: true, force: true });
  }
});
