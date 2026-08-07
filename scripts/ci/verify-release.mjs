#!/usr/bin/env node
import assert from "node:assert/strict";
import crypto from "node:crypto";
import fs from "node:fs";
import path from "node:path";
import { spawnSync } from "node:child_process";

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

function run(executable, args) {
  const result = spawnSync(executable, args, { encoding: "utf8", env: { PATH: process.env.PATH } });
  if (result.status !== 0) {
    fail(`${executable} failed (${result.status}): ${result.stderr || result.stdout}`);
  }
  return result.stdout.trim();
}

const args = parseArguments(process.argv.slice(2));
const archiveBytes = fs.readFileSync(args.archive);
const archiveSha256 = sha256(archiveBytes);
assert.match(args.revision, /^[0-9a-f]{40}$/u);
assert.equal(fs.readFileSync(args.sidecar, "utf8"), `${archiveSha256}\n`);

const resourceRoot = path.join(args.prefix, "share", "engine-sim-offline", args.release);
const releasePath = path.join(resourceRoot, "release.json");
const releaseBindingPath = `${releasePath}.sha256`;
const releaseBytes = fs.readFileSync(releasePath);
const releaseSha256 = sha256(releaseBytes);
assert.equal(fs.readFileSync(releaseBindingPath, "utf8"), `${releaseSha256}\n`);

const release = JSON.parse(releaseBytes);
assert.equal(release.schema, "engine-sim-offline/installed-distribution.v1");
assert.equal(release.release_identity, args.release);
assert.equal(release.classification, "immutable_release");
assert.equal(release.complete, true);
assert.equal(release.source.state, "clean");
assert.equal(release.source.renderer_state, "clean");
assert.equal(release.source.installed_inputs_state, "clean");
assert.equal(release.source.git_commit, args.revision);
assert.equal(release.renderers.wasm.source_closure_matches_native, true);

const declaredPaths = new Set();
for (const file of release.files) {
  assert.match(file.path, /^[A-Za-z0-9._+/@-]+$/u);
  assert(!file.path.split("/").includes(".."), `release path traverses: ${file.path}`);
  assert(!declaredPaths.has(file.path), `duplicate release path ${file.path}`);
  declaredPaths.add(file.path);
  const absolute = path.join(args.prefix, file.path);
  const stat = fs.lstatSync(absolute);
  assert(stat.isFile(), `release member is not regular: ${file.path}`);
  const bytes = fs.readFileSync(absolute);
  assert.equal(bytes.byteLength, Number(file.bytes), `byte count mismatch: ${file.path}`);
  assert.equal(sha256(bytes), file.sha256, `SHA-256 mismatch: ${file.path}`);
}

for (const required of [
  `share/engine-sim-offline/${args.release}/licenses/ENGINE-SIM-OFFLINE.txt`,
  `share/engine-sim-offline/${args.release}/licenses/THIRD-PARTY-NOTICES.md`,
  `share/engine-sim-offline/${args.release}/assets/ir-authoring-catalog.v1.json`,
]) {
  assert(declaredPaths.has(required), `required release member is unbound: ${required}`);
}

const packageMetadata = readJson(path.join(resourceRoot, "package.json"));
assert.equal(packageMetadata.private, true);
assert.equal(packageMetadata.license, "UNLICENSED");
assert.equal(packageMetadata.version, args.release);

const cli = path.join(args.prefix, "bin", "engine-sim-offline");
const launcher = path.join(args.prefix, "bin", "engine-sim-offline-responsive-bake");
assert.equal(run(cli, ["--version"]), `engine-sim-offline ${args.release}`);
assert.equal(run(launcher, ["--version"]), `engine-sim-offline-responsive-bake ${args.release}`);

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
  schema: "engine-sim-offline/release-verification.v1",
  release_identity: args.release,
  git_commit: args.revision,
  archive_sha256: archiveSha256,
  release_json_sha256: releaseSha256,
  catalog_sha256: catalogResult.result.catalog_sha256,
  catalog_entry_count: catalogResult.result.entry_count,
  verified_file_count: release.files.length,
})}\n`);
