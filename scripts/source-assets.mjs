#!/usr/bin/env node

// Source assets are pinned release attachments. No Git LFS or npm dependencies.
import { createHash, randomUUID } from "node:crypto";
import { createReadStream, createWriteStream } from "node:fs";
import fs from "node:fs/promises";
import path from "node:path";
import { execFile, spawn } from "node:child_process";
import { promisify } from "node:util";
import { Readable } from "node:stream";
import { pipeline } from "node:stream/promises";
import { createGzip } from "node:zlib";
import { fileURLToPath } from "node:url";

const exec = promisify(execFile);
const ROOT = fileURLToPath(new URL("../", import.meta.url));
const LOCK = "source-assets.lock.json";
const RELEASES = "https://github.com/SvetlozarValchev/crankwave/releases/download/";

function requirePath(name) {
  if (typeof name !== "string" || !/^(assets|reference)\/[A-Za-z0-9._+/@-]+$/.test(name) ||
      name.split("/").some((part) => !part || part === "." || part === "..")) {
    throw new Error(`Invalid source asset path: ${name}`);
  }
}

export function validateManifest(manifest) {
  if (manifest.version !== 1 || !Array.isArray(manifest.files) || !manifest.files.length ||
      !/^[a-zA-Z0-9][a-zA-Z0-9._-]*$/.test(manifest.release) ||
      !/^crankwave-source-assets-[a-f0-9]{16}\.tar\.gz$/.test(manifest.archive?.name) ||
      !/^[a-f0-9]{64}$/.test(manifest.archive?.sha256) ||
      !Number.isSafeInteger(manifest.archive?.size) || manifest.archive.size <= 0) {
    throw new Error("Invalid source asset manifest");
  }
  let previous = "";
  for (const file of manifest.files) {
    requirePath(file.path);
    if (file.path <= previous || !/^[a-f0-9]{64}$/.test(file.sha256) ||
        !Number.isSafeInteger(file.size) || file.size < 0) {
      throw new Error(`Invalid or unsorted source asset entry: ${file.path}`);
    }
    previous = file.path;
  }
  return manifest;
}

export async function digest(filename) {
  const hash = createHash("sha256");
  for await (const chunk of createReadStream(filename)) hash.update(chunk);
  return hash.digest("hex");
}

async function statOptional(filename) {
  try { return await fs.lstat(filename); }
  catch (error) { if (error.code === "ENOENT") return null; throw error; }
}

// Check every parent, including parents of missing files, before installing.
async function assetPath(root, relative) {
  requirePath(relative);
  let current = root;
  for (const part of relative.split("/").slice(0, -1)) {
    current = path.join(current, part);
    const stat = await statOptional(current);
    if (stat && !stat.isDirectory()) throw new Error(`Asset parent is not a directory: ${current}`);
  }
  return path.join(root, relative);
}

async function matches(filename, expected) {
  const stat = await statOptional(filename);
  return !!stat?.isFile() && stat.size === expected.size &&
    await digest(filename) === expected.sha256;
}

export async function verifyFiles(root, manifest) {
  for (const file of manifest.files) {
    if (!await matches(await assetPath(root, file.path), file)) {
      throw new Error(`Source asset missing or changed: ${file.path}\nRun: node scripts/source-assets.mjs fetch`);
    }
  }
}

export async function extractVerified(archive, destination, manifest) {
  if (!await matches(archive, manifest.archive)) throw new Error("Source asset archive checksum or size mismatch");
  const { stdout: listing } = await exec("tar", ["-tzf", archive], { maxBuffer: 8 * 1024 * 1024 });
  const names = listing.trimEnd().split("\n");
  const expected = new Set(manifest.files.map((file) => file.path));
  if (names.length !== expected.size || new Set(names).size !== names.length ||
      names.some((name) => !expected.has(name))) {
    throw new Error("Source asset archive does not match the manifest file list");
  }
  const { stdout: details } = await exec("tar", ["-tvzf", archive], { maxBuffer: 8 * 1024 * 1024 });
  if (details.trimEnd().split("\n").some((line) => !line.startsWith("-"))) {
    throw new Error("Source asset archive must contain only regular files");
  }
  await exec("tar", ["-xzf", archive, "-C", destination, "--no-same-owner", "--no-same-permissions"]);
  await verifyFiles(destination, manifest);
}

export async function fetchAssets(root, manifest, { download = fetch } = {}) {
  validateManifest(manifest);
  const missing = [];
  for (const file of manifest.files) {
    const filename = await assetPath(root, file.path);
    if (await matches(filename, file)) continue;
    if (await statOptional(filename)) {
      throw new Error(`Refusing to overwrite changed asset: ${file.path}\nMove it aside, then run fetch again.`);
    }
    missing.push(file);
  }
  if (!missing.length) return "All source assets are already verified; no download needed.";

  const cache = path.join(root, ".work/source-assets");
  await fs.mkdir(cache, { recursive: true });
  const archive = path.join(cache, manifest.archive.name);
  if (!await matches(archive, manifest.archive)) {
    const temporary = `${archive}.${randomUUID()}.part`;
    try {
      const url = `${RELEASES}${manifest.release}/${manifest.archive.name}`;
      console.log(`Downloading ${url}`);
      const response = await download(url, { signal: AbortSignal.timeout(15 * 60 * 1000) });
      if (!response.ok || !response.body) throw new Error(`Asset download failed: HTTP ${response.status}`);
      await pipeline(Readable.fromWeb(response.body), createWriteStream(temporary, { flags: "wx" }));
      if (!await matches(temporary, manifest.archive)) throw new Error("Downloaded asset archive checksum or size mismatch");
      await fs.rename(temporary, archive);
    } finally {
      await fs.rm(temporary, { force: true });
    }
  }

  const stage = await fs.mkdtemp(path.join(cache, "extract-"));
  try {
    await extractVerified(archive, stage, manifest);
    for (const file of missing) {
      const target = await assetPath(root, file.path);
      await fs.mkdir(path.dirname(target), { recursive: true });
      // COPYFILE_EXCL also protects a file created since the preflight above.
      await fs.copyFile(path.join(stage, file.path), target, 1);
    }
  } finally {
    await fs.rm(stage, { recursive: true, force: true });
  }
  return `Installed and verified ${missing.length} source assets from the release attachment.`;
}

export async function packAssets(root, release, paths) {
  if (!/^[a-zA-Z0-9][a-zA-Z0-9._-]*$/.test(release)) throw new Error("Invalid asset release tag");
  const files = [];
  for (const relative of [...new Set(paths)].sort()) {
    const filename = await assetPath(root, relative);
    const stat = await fs.lstat(filename);
    if (!stat.isFile()) throw new Error(`Source asset is not a regular file: ${relative}`);
    const prefix = Buffer.alloc(48);
    const handle = await fs.open(filename);
    try { await handle.read(prefix, 0, prefix.length, 0); } finally { await handle.close(); }
    if (prefix.toString().startsWith("version https://git-lfs.github.com/spec/v1")) {
      throw new Error(`Cannot package an unmaterialized LFS pointer: ${relative}`);
    }
    files.push({ path: relative, size: stat.size, sha256: await digest(filename) });
  }
  const identity = createHash("sha256").update(JSON.stringify(files)).digest("hex").slice(0, 16);
  const name = `crankwave-source-assets-${identity}.tar.gz`;
  const output = path.join(root, ".work/source-assets/releases", release);
  await fs.mkdir(output, { recursive: true });
  const archive = path.join(output, name);
  const tar = spawn("tar", ["--format=posix", "--pax-option=delete=atime,delete=ctime", "--mtime=@0",
    "--owner=0", "--group=0", "--numeric-owner", "--no-recursion", "-cf", "-", "--null", "-T", "-"],
  { cwd: root, stdio: ["pipe", "pipe", "pipe"] });
  let errorText = "";
  tar.stderr.on("data", (chunk) => { errorText += chunk; });
  const finished = new Promise((resolve, reject) => {
    tar.on("error", reject);
    tar.on("close", (code) => code === 0 ? resolve() : reject(new Error(`tar failed: ${errorText}`)));
  });
  tar.stdin.end(files.map((file) => file.path).join("\0") + "\0");
  await Promise.all([finished, pipeline(tar.stdout, createGzip({ level: 6 }), createWriteStream(archive))]);
  const manifest = validateManifest({ version: 1, release,
    archive: { name, size: (await fs.stat(archive)).size, sha256: await digest(archive) }, files });
  const stage = await fs.mkdtemp(path.join(output, "verify-"));
  try { await extractVerified(archive, stage, manifest); }
  finally { await fs.rm(stage, { recursive: true, force: true }); }
  return { manifest, archive };
}

async function main() {
  const [command, release, ...additionalPaths] = process.argv.slice(2);
  const manifest = JSON.parse(await fs.readFile(path.join(ROOT, LOCK), "utf8"));
  if (command === "pack" && release) {
    const result = await packAssets(ROOT, release, [...manifest.files.map((file) => file.path), ...additionalPaths]);
    await fs.writeFile(path.join(ROOT, LOCK), JSON.stringify(result.manifest, null, 2) + "\n");
    console.log(`Updated ${LOCK}\nAttachment: ${result.archive}\nSHA-256: ${result.manifest.archive.sha256}`);
  } else if (command === "fetch" && !release) {
    console.log(await fetchAssets(ROOT, manifest));
  } else if (command === "verify" && !release) {
    validateManifest(manifest);
    await verifyFiles(ROOT, manifest);
    console.log(`Verified ${manifest.files.length} source assets.`);
  } else {
    throw new Error("Usage: node scripts/source-assets.mjs fetch | verify | pack <new-release-tag> [additional-asset-path ...]");
  }
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  main().catch((error) => { console.error(error.message); process.exitCode = 1; });
}
