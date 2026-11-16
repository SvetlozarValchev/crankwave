// Fresh native engine bake plus a self-contained, explicitly authored driving profile.
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import path from 'node:path';
import {execFileSync} from 'node:child_process';
import {createHash, webcrypto} from 'node:crypto';
import {loadResponsiveAudioCrankwave} from '../web/runtime/crankwave-package.js';
import {loadSharedRecordedStarterRuntime} from '../web/runtime/shared-recorded-starter-runtime.js';
import {validateDieselDrive} from '../web/runtime/diesel-drive-runtime.js';

const [cli, definition, tuning, outputDirectory, ...extra] = process.argv.slice(2);
assert.ok(cli && definition && tuning && outputDirectory && !extra.length,
  'usage: node tools/bake-diesel-drive.mjs <crankwave-cli> <engine.json> <drive.json> <new-output-directory>');
const hash = bytes => createHash('sha256').update(bytes).digest('hex');
const directory = path.resolve(outputDirectory);
await fs.mkdir(directory, {recursive:false});
const run = args => JSON.parse(execFileSync(path.resolve(cli), [...args, '--result-format','json'],
  {encoding:'utf8',timeout:180000,maxBuffer:4*1024*1024}));
const base = path.join(directory, 'engine-only.crankwave');
const bake = run(['bake-crankwave','--engine',path.resolve(definition),'--output',base]);
assert.equal(bake.ok, true);
const bytes = await fs.readFile(base);
const loaded = await loadResponsiveAudioCrankwave(bytes, {crypto:webcrypto});
assert.equal(loaded.runtime.kind, 'diesel-mechanical-audio');
assert.equal(loaded.runtime.drivePackage, undefined);
const manifest = JSON.parse(await fs.readFile(tuning, 'utf8'));
manifest.source_sha256 = loaded.runtime.manifest.source_sha256;
const packageDirectory = path.join(directory, 'package');
for (const entry of loaded.package.entries) {
  const target = path.join(packageDirectory, entry.path);
  await fs.mkdir(path.dirname(target), {recursive:true});
  await fs.writeFile(target, bytes.subarray(entry.offset, entry.offset + entry.byteCount), {flag:'wx'});
}
const root = path.join(packageDirectory, 'diesel-drive');
await fs.mkdir(root);
const fixture = new URL('../reference/fixtures/responsive-audio/shared-recorded-starter/', import.meta.url);
const shared = await loadSharedRecordedStarterRuntime(new URL('runtime.json', fixture), {
  crypto:webcrypto, fetch:async url => {
    const buffer = Uint8Array.from(await fs.readFile(new URL(url)));
    return {ok:true,url:String(url),redirected:false,arrayBuffer:async()=>buffer.buffer};
  }
});
const relativePcm = 'audio/recorded-starter.cropped.192000hz.mono.f32le';
for (const member of ['runtime.json',relativePcm]) {
  const target = path.join(root, 'shared-starter', member);
  await fs.mkdir(path.dirname(target), {recursive:true});
  await fs.copyFile(new URL(member,fixture), target, fs.constants.COPYFILE_EXCL);
}
const treatedPath = path.join(root, manifest.starter.processed_path);
assert.equal(manifest.starter.processed_path, 'starter-heavy.f32le');
execFileSync('ffmpeg',['-v','error','-n','-f','f32le','-ar','192000','-ac','1','-i',
  path.join(root,'shared-starter',relativePcm),'-af',manifest.starter.filter_graph,
  '-f','f32le','-acodec','pcm_f32le',treatedPath]);
const treated = await fs.readFile(treatedPath);
assert.equal(treated.length, shared.samples.length * 4);
let originalEnergy = 0, treatedEnergy = 0;
for (let i = 0; i < shared.samples.length; ++i) {
  originalEnergy += shared.samples[i] ** 2;
  treatedEnergy += treated.readFloatLE(i * 4) ** 2;
}
const equalizationGain = Math.sqrt(originalEnergy / treatedEnergy);
for (let i = 0; i < shared.samples.length; ++i) treated.writeFloatLE(treated.readFloatLE(i*4)*equalizationGain, i*4);
await fs.writeFile(treatedPath, treated);
manifest.starter.processed_sha256 = hash(treated);
validateDieselDrive(manifest, loaded.runtime);
await fs.writeFile(path.join(root,'runtime.json'), JSON.stringify(manifest,null,2)+'\n', {flag:'wx'});
const output = path.join(directory, `${manifest.engine}-drive.crankwave`);
const pack = run(['pack-crankwave','--package-directory',packageDirectory,'--output',output]);
assert.equal(pack.ok, true);
const verify = run(['verify-crankwave','--input',output]);
assert.equal(verify.result.verified, true);
const finalBytes = await fs.readFile(output);
const final = await loadResponsiveAudioCrankwave(finalBytes, {crypto:webcrypto});
assert.deepEqual(final.runtime.drivePackage.manifest, manifest);
for (const entry of loaded.package.entries) assert.equal(
  final.package.entries.find(e=>e.path === entry.path)?.sha256, entry.sha256,
  `Native engine content changed: ${entry.path}`);
assert.equal(hash(await fs.readFile(base)), hash(bytes));
const report = {output,sha256:hash(finalBytes),base_sha256:hash(bytes),bake,verify,
  originalEngineEntriesUnchanged:true,starterSourceSha256:shared.sourceSha256,
  starterPayloadSha256:shared.payloadSha256,equalizationGain,manifest};
await fs.writeFile(path.join(directory,'bake.json'),JSON.stringify(report,null,2)+'\n',{flag:'wx'});
console.log(JSON.stringify({output,sha256:report.sha256,bytes:finalBytes.length},null,2));
