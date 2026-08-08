#!/usr/bin/env node
import assert from "node:assert/strict";
import crypto from "node:crypto";
import fs from "node:fs";
import path from "node:path";
import { spawnSync } from "node:child_process";
import { pathToFileURL } from "node:url";

function fail(message) {
  throw new Error(message);
}

function parseArguments(argv) {
  const result = new Map();
  for (let index = 0; index < argv.length; index += 2) {
    const name = argv[index];
    const value = argv[index + 1];
    if (!name?.startsWith("--") || value === undefined) fail("arguments must be --name value pairs");
    if (result.has(name)) fail(`duplicate argument: ${name}`);
    result.set(name, value);
  }
  for (const name of ["--archive", "--sidecar", "--prefix", "--release", "--revision"]) {
    if (!result.has(name)) fail(`missing argument: ${name}`);
  }
  return Object.fromEntries([...result].map(([name, value]) => [name.slice(2), value]));
}

function sha256(bytes) {
  return crypto.createHash("sha256").update(bytes).digest("hex");
}

function readJson(file) {
  return JSON.parse(fs.readFileSync(file, "utf8"));
}

function run(executable, commandArguments) {
  const result = spawnSync(executable, commandArguments, {
    encoding: "utf8",
    env: { LANG: "C", LC_ALL: "C", PATH: process.env.PATH, TZ: "UTC" },
  });
  if (result.status !== 0) {
    fail(`${executable} failed (${result.status}): ${result.stderr || result.stdout}`);
  }
  return result.stdout.trim();
}

function listRegularFiles(root) {
  const result = [];
  function visit(directory, relativeDirectory) {
    const entries = fs.readdirSync(directory, { withFileTypes: true })
      .sort((left, right) => left.name.localeCompare(right.name, "en"));
    for (const entry of entries) {
      const absolute = path.join(directory, entry.name);
      const relative = relativeDirectory ? `${relativeDirectory}/${entry.name}` : entry.name;
      const stat = fs.lstatSync(absolute);
      assert(!stat.isSymbolicLink(), `release member is a symbolic link: ${relative}`);
      if (stat.isDirectory()) {
        visit(absolute, relative);
      } else {
        assert(stat.isFile(), `release member is not regular: ${relative}`);
        result.push(relative);
      }
    }
  }
  visit(root, "");
  return result.sort();
}

const args = parseArguments(process.argv.slice(2));
assert.match(args.release, /^[0-9]+\.[0-9]+\.[0-9]+$/u);
assert.match(args.revision, /^[0-9a-f]{40}$/u);
assert.equal(path.basename(args.prefix), `engine-sim-offline-${args.release}`);

const archiveBytes = fs.readFileSync(args.archive);
const archiveSha256 = sha256(archiveBytes);
assert.equal(fs.readFileSync(args.sidecar, "utf8"), `${archiveSha256}\n`);
const archiveRoot = `engine-sim-offline-${args.release}`;
const archiveMembers = run("tar", ["-tf", args.archive]).split("\n");
for (const member of archiveMembers) {
  assert(member === archiveRoot || member.startsWith(`${archiveRoot}/`),
    `archive member escapes release root: ${member}`);
  assert(!member.startsWith("/") && !member.split("/").includes(".."),
    `archive member traverses: ${member}`);
}

const resourceRelative = `share/engine-sim-offline/${args.release}`;
const resourceRoot = path.join(args.prefix, resourceRelative);
const releasePath = path.join(resourceRoot, "release.json");
const releaseBindingPath = `${releasePath}.sha256`;
const releaseBytes = fs.readFileSync(releasePath);
const releaseSha256 = sha256(releaseBytes);
assert.equal(fs.readFileSync(releaseBindingPath, "utf8"), `${releaseSha256}\n`);

const release = JSON.parse(releaseBytes);
assert.equal(release.schema, "engine-sim-offline/installed-distribution.v2");
assert.equal(release.release_identity, args.release);
assert.equal(release.classification, "immutable_release");
assert.equal(release.complete, true);
assert.equal(release.source.state, "clean");
assert.equal(release.source.renderer_state, "clean");
assert.equal(release.source.installed_inputs_state, "clean");
assert.equal(release.source.git_commit, args.revision);
assert.match(release.source.closure_sha256, /^[0-9a-f]{64}$/u);
assert.match(release.source.installed_inputs_closure_sha256, /^[0-9a-f]{64}$/u);
assert.equal(release.toolchain.state, "available");
assert.equal(release.toolchain.target_triple, "x86_64-pc-linux-gnu");
assert.equal(release.layout.resource_root, resourceRelative);
assert.deepEqual(release.production_runtime, {
  kind: "native",
  node_required: false,
  simulation_wasm_required: false,
});

const expectedCommands = [
  "render",
  "bake-revengine",
  "pack-revengine",
  "inspect-revengine",
  "verify-revengine",
  "inspect-ir-catalog",
];
assert.equal(release.native_cli.path, "bin/engine-sim-offline");
assert.match(release.native_cli.sha256, /^[0-9a-f]{64}$/u);
assert.deepEqual(release.native_cli.commands, expectedCommands);
assert.equal(release.browser_playback.kind, "simulator-free-esm");
assert.equal(release.browser_playback.resource_directory, `${resourceRelative}/web/runtime`);
assert.equal(release.browser_playback.entrypoint,
  `${resourceRelative}/web/runtime/revengine-audio-engine.js`);
assert.deepEqual(release.telemetry_contract, {
  role: "diagnostics.engine-telemetry.v1",
  kind: "telemetry",
  path: "telemetry/engine-telemetry.v1.ndjson",
  schema: "engine-sim-offline.engine-telemetry.ndjson.v1",
  originating_commit: "c8d672b59e3046654ad5f818f31725798aef7ffa",
});

const expectedWorkflowPath = `${resourceRelative}/contracts/revengine-bake-workflow.v2.json`;
assert.equal(release.revengine_bake_workflow.path, expectedWorkflowPath);
assert.match(release.revengine_bake_workflow.sha256, /^[0-9a-f]{64}$/u);

const declaredPaths = new Set();
let previousPath = "";
for (const file of release.files) {
  assert.match(file.path, /^[A-Za-z0-9._+/@-]+$/u);
  assert(!file.path.startsWith("/") &&
    !file.path.split("/").some(component => component === "" || component === "." || component === ".."),
  `release path traverses: ${file.path}`);
  assert(file.path > previousPath, `release files are not strictly sorted at ${file.path}`);
  previousPath = file.path;
  assert(!declaredPaths.has(file.path), `duplicate release path ${file.path}`);
  declaredPaths.add(file.path);
  const absolute = path.join(args.prefix, file.path);
  const stat = fs.lstatSync(absolute);
  assert(stat.isFile() && !stat.isSymbolicLink(), `release member is not regular: ${file.path}`);
  const bytes = fs.readFileSync(absolute);
  assert.equal(bytes.byteLength, Number(file.bytes), `byte count mismatch: ${file.path}`);
  assert.equal(sha256(bytes), file.sha256, `SHA-256 mismatch: ${file.path}`);
}

const actualFiles = listRegularFiles(args.prefix);
const expectedFiles = [...declaredPaths, `${resourceRelative}/release.json`,
  `${resourceRelative}/release.json.sha256`].sort();
assert.deepEqual(actualFiles, expectedFiles, "installed prefix contains unbound or missing files");
const expectedArchiveMembers = new Set([`${archiveRoot}/`]);
for (const file of expectedFiles) {
  expectedArchiveMembers.add(`${archiveRoot}/${file}`);
  let parent = path.posix.dirname(file);
  while (parent !== ".") {
    expectedArchiveMembers.add(`${archiveRoot}/${parent}/`);
    parent = path.posix.dirname(parent);
  }
}
assert.deepEqual(archiveMembers, [...expectedArchiveMembers].sort(),
  "archive contains unbound, missing, duplicated, or noncanonical members");
const cliRecord = release.files.find(file => file.path === "bin/engine-sim-offline");
assert(cliRecord, "native CLI is absent from release files");
assert.equal(cliRecord.sha256, release.native_cli.sha256);

const browserModuleNames = [
  "directional-phase-cell.js",
  "dry-directional-phase-runtime.js",
  "held-phase-texture-runtime.js",
  "held-texture-presentation-runtime.js",
  "release.js",
  "renderer-runtime-compatibility.js",
  "responsive-audio-lifecycle-runtime.js",
  "revengine-audio-engine.js",
  "revengine-package.js",
  "shared-recorded-starter-runtime.js",
  "state-phase-texture-runtime.js",
  "steady-transient-envelope.js",
];
const browserDirectory = `${resourceRelative}/web/runtime/`;
const actualBrowserModules = release.files
  .map(file => file.path)
  .filter(file => file.startsWith(browserDirectory) && file.endsWith(".js"))
  .map(file => file.slice(browserDirectory.length))
  .sort();
assert.deepEqual(actualBrowserModules, browserModuleNames);

for (const required of [
  `${resourceRelative}/licenses/ENGINE-SIM-OFFLINE.txt`,
  `${resourceRelative}/licenses/THIRD-PARTY-NOTICES.md`,
  `${resourceRelative}/assets/catalog.v1.json`,
  `${resourceRelative}/assets/ir-authoring-catalog.v1.json`,
  `${resourceRelative}/package.json`,
  `${resourceRelative}/profiles/interactive-preview-v1.json`,
  `${resourceRelative}/schemas/installed-distribution.v2.schema.json`,
  `${resourceRelative}/schemas/revengine-bake-workflow.v2.schema.json`,
  expectedWorkflowPath,
  release.browser_playback.entrypoint,
]) {
  assert(declaredPaths.has(required), `required release member is unbound: ${required}`);
}

for (const forbidden of [
  "bin/engine-sim-offline-responsive-bake",
  "libexec/engine-sim-offline/dump-ir-spectrum",
]) {
  assert(!declaredPaths.has(forbidden), `obsolete production executable was shipped: ${forbidden}`);
}
for (const file of declaredPaths) {
  assert(!file.endsWith(".wasm"), `simulation WASM was shipped: ${file}`);
  assert(!file.includes("node_modules/"), `Node dependency was shipped: ${file}`);
  assert(!file.includes("tools/responsive-audio-baker/"), `Node baker was shipped: ${file}`);
  assert(!file.endsWith("/c-api-abi.js") && !file.endsWith("/c-api-client.js") &&
    !file.endsWith("/c-api-errors.js") && !file.endsWith("/c-api-session.js") &&
    !file.endsWith("/wasm-heap.js"), `simulation JS adapter was shipped: ${file}`);
}

const packageMetadata = readJson(path.join(resourceRoot, "package.json"));
assert.deepEqual(packageMetadata, {
  name: "engine-sim-offline-installed-resources",
  private: true,
  license: "UNLICENSED",
  version: args.release,
  type: "module",
});
const releaseModule = await import(pathToFileURL(
  path.join(resourceRoot, "web", "runtime", "release.js")).href);
assert.equal(releaseModule.ENGINE_SIM_OFFLINE_RELEASE_IDENTITY, args.release);
assert.equal(releaseModule.default.releaseIdentity, args.release);

const workflowPath = path.join(args.prefix, expectedWorkflowPath);
assert.equal(sha256(fs.readFileSync(workflowPath)), release.revengine_bake_workflow.sha256);
const workflow = readJson(workflowPath);
assert.equal(workflow.schema, "engine-sim-offline/revengine-bake-workflow.v2");
assert.equal(workflow.release_identity, args.release);
assert.equal(workflow.path_base, "distribution_root");
assert.deepEqual(workflow.production_runtime, release.production_runtime);
assert.equal(workflow.responsive_profile_selection.default_policy, "engine-redline-affine-v1");
assert.equal(workflow.responsive_profile_selection.profile_id,
  "interactive-preview-redline-v1");
assert.equal(workflow.responsive_profile_selection.explicit_override, null);
assert.deepEqual(workflow.telemetry_contract, {
  producer_command: "render",
  ...release.telemetry_contract,
  included_in_revengine_workflow: false,
});
assert.equal(workflow.steps.length, 1);
assert.equal(workflow.steps[0].ordinal, 1);
assert.equal(workflow.steps[0].id, "bake_revengine");
assert.equal(workflow.steps[0].executable, "bin/engine-sim-offline");
assert.deepEqual(workflow.steps[0].arguments, [
  "bake-revengine",
  "--engine",
  "{engine_json}",
  "--output",
  "{new_revengine_file}",
  "--deadline-unix-ms",
  "{deadline_unix_ms}",
  "--result-format",
  "json",
]);
assert.equal(workflow.steps[0].required_output, "{new_revengine_file}");
assert.deepEqual(workflow.steps[0].required_stdout_record, {
  schema: "engine-sim-offline.cli-result.v1",
  release_identity: args.release,
  command: "bake-revengine",
  ok: true,
  code: "success",
  exit_code: 0,
});
assert.deepEqual(workflow.steps[0].required_result_fields, {
  output_file: "{new_revengine_file}",
  verified: true,
});
assert.deepEqual(workflow.success, {
  definition: "the single native step exits zero after full carrier verification",
  publishable_after_step: 1,
});

const cli = path.join(args.prefix, "bin", "engine-sim-offline");
assert.equal(run(cli, ["--version"]), `engine-sim-offline ${args.release}`);
const catalogResult = JSON.parse(run(cli, ["inspect-ir-catalog", "--result-format", "json"]));
assert.equal(catalogResult.ok, true);
assert.equal(catalogResult.release_identity, args.release);
assert.equal(catalogResult.result.entry_count, 73);
assert.equal(catalogResult.result.catalog.release_identity, args.release);
assert.equal(catalogResult.result.catalog.rights_summary.status, "MIT");
assert.equal(catalogResult.result.catalog.rights_summary.release_gate, "none");
assert.equal(catalogResult.result.catalog.compatibility_summary.selectable, 73);
assert.equal(catalogResult.result.catalog.compatibility_summary["requires-long-kernel"], 0);
assert.equal(catalogResult.result.catalog.compatibility_summary["requires-conversion"], 0);
for (const entry of catalogResult.result.catalog.entries) {
  assert.equal(entry.rights.status, "MIT");
  assert.equal(entry.rights.redistribution_status, "permitted-with-license-notice");
}

process.stdout.write(`${JSON.stringify({
  schema: "engine-sim-offline/release-verification.v2",
  release_identity: args.release,
  git_commit: args.revision,
  archive_sha256: archiveSha256,
  release_json_sha256: releaseSha256,
  catalog_sha256: catalogResult.result.catalog_sha256,
  catalog_entry_count: catalogResult.result.entry_count,
  native_command_count: release.native_cli.commands.length,
  browser_module_count: actualBrowserModules.length,
  verified_file_count: release.files.length,
})}\n`);
