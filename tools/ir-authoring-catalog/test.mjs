#!/usr/bin/env node

import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import { spawnSync } from "node:child_process";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const HERE = path.dirname(fileURLToPath(import.meta.url));
const REPOSITORY = path.resolve(HERE, "../..");
const CATALOG_PATH = path.join(
  REPOSITORY,
  "assets/builtin/ir-authoring-catalog.v1.json",
);
const RUNTIME_CATALOG_PATH = path.join(
  REPOSITORY,
  "assets/builtin/catalog.v1.json",
);
const SCHEMA_PATH = path.join(
  REPOSITORY,
  "schemas/ir-authoring-catalog.schema.json",
);
const EXPECTED_SOURCE_BINDING =
  "018b51f0650683e8677a22cbb66c72cada3dd8e9ded3a4e0b15cb049d36b3fb8";

function digest(algorithm, ...buffers) {
  const hash = createHash(algorithm);
  for (const buffer of buffers) hash.update(buffer);
  return hash.digest("hex");
}

function gitBlobSha1(bytes) {
  return digest(
    "sha1",
    Buffer.from(`blob ${bytes.byteLength}\0`, "ascii"),
    bytes,
  );
}

function exactKeys(value, expected, label) {
  assert(value !== null && typeof value === "object" && !Array.isArray(value), label);
  assert.deepEqual(Object.keys(value).sort(), [...expected].sort(), `${label} keys`);
}

function codeUnitCompare(left, right) {
  return left < right ? -1 : left > right ? 1 : 0;
}

const check = spawnSync(
  process.execPath,
  [path.join(HERE, "generate.mjs"), "--check"],
  { cwd: REPOSITORY, encoding: "utf8" },
);
assert.equal(check.status, 0, check.stderr);
assert.match(check.stdout, /verified 73 exact IR payloads/u);

const schema = JSON.parse(fs.readFileSync(SCHEMA_PATH, "utf8"));
assert.equal(schema.$schema, "https://json-schema.org/draft/2020-12/schema");
assert.equal(
  schema.$id,
  "https://engine-sim-offline.dev/schemas/ir-authoring-catalog.schema.json",
);
assert.equal(schema.properties.release_identity.const, "1.1.0");
assert.equal(schema.properties.entry_count.const, 73);

const catalog = JSON.parse(fs.readFileSync(CATALOG_PATH, "utf8"));
exactKeys(catalog, [
  "schema",
  "catalog_id",
  "catalog_revision",
  "release_identity",
  "entry_count",
  "selection_contract",
  "source_collection",
  "measurement_method",
  "semantic_method",
  "compatibility_summary",
  "rights_summary",
  "entries",
], "catalog");
assert.equal(catalog.schema, "engine-sim-offline/ir-authoring-catalog.v1");
assert.equal(catalog.release_identity, "1.1.0");
assert.equal(catalog.entry_count, 73);
assert.equal(catalog.entries.length, 73);
assert.deepEqual(
  catalog.selection_contract.required_selector,
  ["release_identity", "id", "sha256"],
);
assert.equal(
  catalog.selection_contract.engine_json_declaration_rule,
  "declare-only-assets-used-by-presentation-routes",
);

const ids = catalog.entries.map(({ id }) => id);
assert.equal(new Set(ids).size, 73);
assert.deepEqual(ids, [...ids].sort(codeUnitCompare), "entries must use code-unit order");
assert.equal(new Set(catalog.entries.map(({ sha256 }) => sha256)).size, 73);
assert.equal(
  catalog.entries.find(({ id }) => id === "smooth-39").sha256,
  "75de9db47063395665d36b6d4232f477aae385feaa9ba158353fbdaf122db5cc",
);
assert.equal(
  catalog.entries.find(({ id }) => id === "smooth-46").sha256,
  "97408471f8b699a93b888db448367cf7abbe20e6c4891cc8af0d1946889f8b69",
);

const sourceBinding = catalog.entries.map((entry) => ({
  id: entry.id,
  sha256: entry.sha256,
  byte_count: entry.payload.byte_count,
  source: entry.source.repository_relative_path,
}));
assert.equal(
  digest("sha256", Buffer.from(JSON.stringify(sourceBinding))),
  EXPECTED_SOURCE_BINDING,
  "the exact 73-file source collection changed without an explicit binding update",
);

const runtimeCatalog = JSON.parse(fs.readFileSync(RUNTIME_CATALOG_PATH, "utf8"));
const runtimeAudio = new Set(
  runtimeCatalog.assets
    .filter(({ kind }) => kind === "audio")
    .map(({ id, sha256 }) => `${id}\0${sha256}`),
);

const compatibilityCounts = {
  selectable: 0,
  "requires-long-kernel": 0,
  "requires-conversion": 0,
};
const runtimeBranches = {
  "legacy-fixed-v1-bit-identical": 0,
  "uniform-partitioned-v2": 0,
};
let pcm24Count = 0;
for (const entry of catalog.entries) {
  exactKeys(entry, [
    "id",
    "display_name",
    "sha256",
    "payload",
    "source",
    "media",
    "objective",
    "compatibility",
    "rights",
    "authoring",
  ], `entry ${entry.id}`);
  assert.match(entry.id, /^[a-z0-9][a-z0-9-]{0,127}$/u);
  assert.match(entry.sha256, /^[0-9a-f]{64}$/u);
  assert.equal(entry.source.source_file_sha256, entry.sha256);
  assert.equal(
    entry.payload.repository_relative_path,
    `assets/builtin/ir-library/payloads/${entry.sha256}.wav`,
  );
  assert.equal(
    entry.payload.installed_bundle_relative_path,
    `payloads/${entry.sha256}`,
  );
  const payloadPath = path.join(REPOSITORY, entry.payload.repository_relative_path);
  const bytes = fs.readFileSync(payloadPath);
  assert.equal(bytes.byteLength, entry.payload.byte_count);
  assert.equal(digest("sha256", bytes), entry.sha256);
  assert.equal(gitBlobSha1(bytes), entry.source.source_git_blob_sha1);
  assert.equal(bytes.toString("ascii", 0, 4), "RIFF");
  assert.equal(bytes.toString("ascii", 8, 12), "WAVE");
  assert(
    runtimeAudio.has(`${entry.id}\0${entry.sha256}`),
    `${entry.id} is not bound into the technical runtime catalog`,
  );

  assert.equal(entry.media.sample_rate_hz, 44_100);
  assert.equal(entry.media.channel_count, 1);
  assert([16, 24].includes(entry.media.bits_per_sample));
  if (entry.media.bits_per_sample === 24) pcm24Count += 1;
  assert.equal(
    entry.objective.duration_seconds,
    Number((entry.media.frame_count / 44_100).toFixed(9)),
  );
  assert(entry.objective.meaningful_support_frame_count <= entry.media.frame_count);
  assert(entry.objective.peak_frame < entry.media.frame_count);
  assert(entry.objective.peak_amplitude_01 > 0 && entry.objective.peak_amplitude_01 <= 1);
  assert(entry.objective.rms_amplitude_01 > 0 && entry.objective.rms_amplitude_01 <= 1);
  const bands = Object.values(entry.objective.spectrum.band_energy_fraction);
  assert(Math.abs(bands.reduce((sum, value) => sum + value, 0) - 1) < 1e-7);
  assert(entry.objective.spectrum.spectral_centroid_hz >= 20);
  assert(entry.objective.spectrum.spectral_centroid_hz <= 22_050);
  assert(entry.objective.spectrum.resonant_peaks.length >= 1);
  assert(entry.objective.spectrum.resonant_peaks.length <= 5);
  for (let index = 1; index < entry.objective.spectrum.resonant_peaks.length; index += 1) {
    assert(
      entry.objective.spectrum.resonant_peaks[index - 1].relative_level_db >=
        entry.objective.spectrum.resonant_peaks[index].relative_level_db,
    );
  }

  const decay = entry.objective.schroeder_tail_decay_from_peak;
  const finiteDecay = [
    decay.minus_20_db_seconds,
    decay.minus_40_db_seconds,
    decay.minus_60_db_seconds,
  ].filter((value) => value !== null);
  assert.deepEqual(finiteDecay, [...finiteDecay].sort((left, right) => left - right));

  assert.match(entry.authoring.caption.text, /measured support/u);
  assert([
    "objective-measurement-summary-v1",
    "human-level-matched-audition-v1",
  ].includes(entry.authoring.caption.basis));
  if (entry.authoring.caption.review_status === "pending-human-audition") {
    assert.equal(entry.authoring.caption.basis, "objective-measurement-summary-v1");
    assert.equal(
      entry.authoring.perceptual_tags.status,
      "measurement-proxies-only-pending-human-audition",
    );
    assert.deepEqual(entry.authoring.perceptual_tags.curated, []);
  }
  const gain = entry.authoring.recommended_controls.impulse_response_gain_linear;
  assert(gain.minimum <= gain.initial && gain.initial <= gain.maximum);
  const wet = entry.authoring.recommended_controls.wet_mix_01;
  assert(wet.minimum <= wet.initial && wet.initial <= wet.maximum && wet.maximum <= 1);
  assert.equal(entry.rights.status, "NOASSERTION");
  assert.equal(entry.rights.repository_license_path, "LICENSE");
  assert.equal(
    entry.rights.repository_license_sha256,
    "9f64449d4ef2db6b57d6af9d36e5eca5b6de3ece2db14a847ae71d4e0dcbff15",
  );
  assert.equal(entry.rights.asset_specific_rights_evidence, null);
  compatibilityCounts[entry.compatibility.state] += 1;
  runtimeBranches[entry.compatibility.compiled_kernel.branch] += 1;
  assert.equal(entry.compatibility.state, "selectable");
  assert.equal(entry.compatibility.decoder.admitted, true);
  assert.equal(entry.compatibility.compiled_kernel.admitted, true);
  assert.deepEqual(entry.compatibility.reasons, []);
  assert.equal(
    entry.compatibility.assessed_runtime.native_conversion_method,
    "hybrid-static-ir-pcm16-pcm24-44100-to-192000-binary64-v2",
  );
  assert.equal(
    entry.compatibility.assessed_runtime.wasm32_conversion_method,
    "hybrid-static-ir-pcm16-pcm24-44100-to-192000-binary64-wasm32-binary128-v2",
  );
  assert.equal(
    entry.compatibility.assessed_runtime.convolution_method,
    "hybrid-fixed-or-uniform-partitioned-causal-fft-binary64-v2",
  );
  assert.equal(
    entry.compatibility.legacy_output_preservation,
    entry.compatibility.compiled_kernel.branch === "legacy-fixed-v1-bit-identical"
      ? "v1-coefficient-and-fixed-spectrum-bytes-unchanged"
      : null,
  );
}

assert.deepEqual(compatibilityCounts, catalog.compatibility_summary);
assert.deepEqual(compatibilityCounts, {
  selectable: 73,
  "requires-long-kernel": 0,
  "requires-conversion": 0,
});
assert.deepEqual(runtimeBranches, {
  "legacy-fixed-v1-bit-identical": 24,
  "uniform-partitioned-v2": 49,
});
assert.equal(pcm24Count, 1);
assert.equal(
  Object.values(compatibilityCounts).reduce((sum, value) => sum + value, 0),
  73,
);
assert.equal(
  catalog.semantic_method.reviewed_entry_count +
    catalog.entries.filter((entry) =>
      entry.authoring.caption.review_status === "pending-human-audition"
    ).length,
  73,
);

const attributes = spawnSync(
  "git",
  ["check-attr", "filter", "diff", "merge", "text", "--",
    ...catalog.entries.map((entry) => entry.payload.repository_relative_path)],
  { cwd: REPOSITORY, encoding: "utf8" },
);
assert.equal(attributes.status, 0, attributes.stderr);
const attributeLines = attributes.stdout.trim().split("\n");
assert.equal(attributeLines.length, 73 * 4);
for (const line of attributeLines) {
  if (line.endsWith(": filter: lfs")) continue;
  if (line.endsWith(": diff: lfs")) continue;
  if (line.endsWith(": merge: lfs")) continue;
  if (line.endsWith(": text: unset")) continue;
  assert.fail(`unexpected LFS attribute: ${line}`);
}

process.stdout.write(
  `IR authoring catalog: ${catalog.entry_count} exact payloads, ` +
  `${catalog.semantic_method.reviewed_entry_count} human-reviewed, ` +
  `${JSON.stringify(catalog.compatibility_summary)}\n`,
);
