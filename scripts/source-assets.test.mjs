import assert from "node:assert/strict";
import { test } from "node:test";
import fs from "node:fs/promises";
import os from "node:os";
import path from "node:path";
import { execFileSync } from "node:child_process";
import { digest, extractVerified, fetchAssets, packAssets, validateManifest, verifyFiles } from "./source-assets.mjs";

async function fixture(t) {
  const root = await fs.mkdtemp(path.join(os.tmpdir(), "crankwave-source-assets-"));
  t.after(() => fs.rm(root, { recursive: true, force: true }));
  const source = path.join(root, "source");
  const destination = path.join(root, "destination");
  await fs.mkdir(path.join(source, "reference/fixtures"), { recursive: true });
  await fs.mkdir(path.join(source, "assets/builtin"), { recursive: true });
  await fs.mkdir(destination);
  await fs.writeFile(path.join(source, "reference/fixtures/test.bin"), Buffer.from([0, 1, 2, 255]));
  await fs.writeFile(path.join(source, "assets/builtin/test.wav"), "example audio");
  const paths = ["reference/fixtures/test.bin", "assets/builtin/test.wav"];
  const packed = await packAssets(source, "source-assets-test", paths);
  return { root, source, destination, paths, ...packed };
}

test("fresh fetch verifies and restores bytes, and repeated setup is offline", async (t) => {
  const f = await fixture(t);
  let requests = 0;
  const download = async (url) => {
    requests++;
    assert.equal(url, `https://github.com/SvetlozarValchev/crankwave/releases/download/${f.manifest.release}/${f.manifest.archive.name}`);
    return new Response(await fs.readFile(f.archive));
  };
  await fetchAssets(f.destination, f.manifest, { download });
  await verifyFiles(f.destination, f.manifest);
  await fetchAssets(f.destination, f.manifest, { download });
  assert.equal(requests, 1);
  await fs.unlink(path.join(f.destination, f.paths[0]));
  await fetchAssets(f.destination, f.manifest, { download });
  assert.equal(requests, 1, "missing files must be recoverable from the cached attachment");
  assert.equal(await fs.readFile(path.join(f.destination, f.paths[1]), "utf8"), "example audio");
});

test("pack is reproducible and records changed asset contents", async (t) => {
  const f = await fixture(t);
  const again = await packAssets(f.source, "source-assets-test", [...f.paths].reverse());
  assert.equal(again.manifest.archive.sha256, f.manifest.archive.sha256);
  await fs.writeFile(path.join(f.source, f.paths[0]), "new asset");
  const changed = await packAssets(f.source, "source-assets-next", f.paths);
  assert.notEqual(changed.manifest.archive.name, f.manifest.archive.name);
  assert.notEqual(changed.manifest.archive.sha256, f.manifest.archive.sha256);
});

test("corrupt downloads never install payloads or leave partial downloads", async (t) => {
  const f = await fixture(t);
  await assert.rejects(fetchAssets(f.destination, f.manifest, {
    download: async () => new Response("not the pinned archive"),
  }), /checksum or size mismatch/);
  await assert.rejects(fs.stat(path.join(f.destination, f.paths[0])), { code: "ENOENT" });
  assert.deepEqual(await fs.readdir(path.join(f.destination, ".work/source-assets")), []);
});

test("HTTP failure is actionable and leaves the checkout unchanged", async (t) => {
  const f = await fixture(t);
  await assert.rejects(fetchAssets(f.destination, f.manifest, {
    download: async () => new Response("missing", { status: 404 }),
  }), /HTTP 404/);
  await assert.rejects(fs.stat(path.join(f.destination, f.paths[0])), { code: "ENOENT" });
});

test("changed local assets and symlink parents are not overwritten", async (t) => {
  const f = await fixture(t);
  await fs.mkdir(path.join(f.destination, "assets/builtin"), { recursive: true });
  await fs.writeFile(path.join(f.destination, f.paths[1]), "local edit");
  const download = () => { throw new Error("unexpected network"); };
  await assert.rejects(fetchAssets(f.destination, f.manifest, { download }), /Refusing to overwrite/);
  assert.equal(await fs.readFile(path.join(f.destination, f.paths[1]), "utf8"), "local edit");
  await fs.rm(path.join(f.destination, "assets"), { recursive: true });
  await fs.symlink(path.join(f.source, "assets"), path.join(f.destination, "assets"));
  await assert.rejects(fetchAssets(f.destination, f.manifest, { download }), /parent is not a directory/);
});

test("manifest traversal and duplicate paths are rejected", async (t) => {
  const f = await fixture(t);
  const invalid = structuredClone(f.manifest);
  invalid.files[0].path = "assets/../../outside";
  assert.throws(() => validateManifest(invalid), /Invalid source asset path/);
  invalid.files = [f.manifest.files[0], f.manifest.files[0]];
  assert.throws(() => validateManifest(invalid), /unsorted/);
});

test("archive with a matching digest but wrong members is rejected", async (t) => {
  const f = await fixture(t);
  const invalid = structuredClone(f.manifest);
  invalid.files.pop();
  await assert.rejects(extractVerified(f.archive, f.destination, invalid), /file list/);
});

test("archive symlinks are rejected before extraction", async (t) => {
  const f = await fixture(t);
  const relative = f.paths[0];
  await fs.unlink(path.join(f.source, relative));
  await fs.symlink("/tmp/outside", path.join(f.source, relative));
  const archive = path.join(f.root, "symlink.tar.gz");
  execFileSync("tar", ["-czf", archive, ...f.manifest.files.map((file) => file.path)], { cwd: f.source });
  const invalid = structuredClone(f.manifest);
  invalid.archive.sha256 = await digest(archive);
  invalid.archive.size = (await fs.stat(archive)).size;
  await assert.rejects(extractVerified(archive, f.destination, invalid), /only regular files/);
  assert.deepEqual(await fs.readdir(f.destination), []);
});

test("file checksums are enforced after archive verification", async (t) => {
  const f = await fixture(t);
  const invalid = structuredClone(f.manifest);
  invalid.files[0].sha256 = "0".repeat(64);
  await assert.rejects(extractVerified(f.archive, f.destination, invalid), /missing or changed/);
});
