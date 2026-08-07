import assert from "node:assert/strict";
import { createHash, webcrypto } from "node:crypto";
import fs from "node:fs/promises";
import path from "node:path";
import test from "node:test";
import { fileURLToPath } from "node:url";

import {
  loadSharedRecordedStarterRuntime,
  resolveSharedRecordedStarterManifestUrl,
  SharedRecordedStarterCursor,
} from "../../runtime/shared-recorded-starter-runtime.js";

const FIXTURE_ROOT = fileURLToPath(
  new URL(
    "../../../reference/fixtures/responsive-audio/shared-recorded-starter/",
    import.meta.url,
  ),
);
const MANIFEST_URL = new URL(
  "https://fixtures.invalid/packages/shared-recorded-starter/runtime.json",
);
const PAYLOAD_PATH =
  "audio/recorded-starter.cropped.192000hz.mono.f32le";

function response(bytes, url, { redirected = false } = {}) {
  const copy = Uint8Array.from(bytes);
  return Object.freeze({
    ok: true,
    status: 200,
    url,
    redirected,
    async arrayBuffer() {
      return copy.buffer;
    },
  });
}

async function canonicalBytes(url) {
  const parsed = new URL(url);
  const packagePrefix = "/packages/shared-recorded-starter/";
  assert.ok(parsed.pathname.startsWith(packagePrefix));
  const relativePath = parsed.pathname.slice(packagePrefix.length);
  assert.ok(relativePath === "runtime.json" || relativePath === PAYLOAD_PATH);
  return fs.readFile(path.join(FIXTURE_ROOT, relativePath));
}

function canonicalFetch({ mutate, redirected = false, retarget = false } = {}) {
  const calls = [];
  const fetch = async (url, options) => {
    calls.push(Object.freeze({ url, options }));
    let bytes = Uint8Array.from(await canonicalBytes(url));
    if (typeof mutate === "function") bytes = mutate(url, bytes);
    return response(
      bytes,
      retarget ? `${url}?retargeted=1` : url,
      { redirected },
    );
  };
  return { calls, fetch };
}

test("canonical commissioned starter loads with exact identities and DSP", async () => {
  const fixture = canonicalFetch();
  const package_ = await loadSharedRecordedStarterRuntime(MANIFEST_URL, {
    fetch: fixture.fetch,
    crypto: webcrypto,
  });

  assert.equal(package_.kind, "shared-recorded-starter");
  assert.equal(package_.manifest.id, "shared-recorded-starter-licensed");
  assert.equal(package_.manifest.rights.status, "licensed");
  assert.equal(package_.manifest.rights.audition_only, false);
  assert.equal(package_.manifest.rights.modification_authorized, true);
  assert.equal(package_.manifest.rights.redistribution_authorized, true);
  assert.equal(package_.licensee, "SvetlozarValchev");
  assert.equal(package_.samples.length, 259_318);
  assert.equal(
    package_.manifestSha256,
    "1fb698a9c304ecee323361b059dbfc615ab82c06357e01b815faa8f3a008365e",
  );
  assert.equal(
    package_.payloadSha256,
    "1949863ca58aef11146d4a842609ef217f6b7df4ba6db38f478eb918cef2964a",
  );
  assert.deepEqual(package_.settings, {
    defaultEnabled: true,
    sourceGain: 0.5,
    speedUpStartRpm: 500,
    speedUpEndRpm: 760,
    basePlaybackRate: 0.8,
    catchPlaybackRate: 1.05,
    speedUpCurve: 1.6,
    rpmSmoothingMilliseconds: 0,
    attackMilliseconds: 8,
    preCatchEngineGain: 0.903125,
    catchRpm: 400,
    ignitionDuckLeadMilliseconds: 30,
    catchStarterGain: 0.2,
    engineCatchGain: 1,
    handoffMilliseconds: 160,
    catchOffsetMilliseconds: 0,
  });
  assert.deepEqual(
    fixture.calls.map(({ options }) => options),
    [
      { cache: "no-store", redirect: "error" },
      { cache: "no-store", redirect: "error" },
    ],
  );

  const manifestText = await fs.readFile(
    path.join(FIXTURE_ROOT, "runtime.json"),
    "utf8",
  );
  assert.equal(manifestText.includes("/home/"), false);
  assert.equal(Object.hasOwn(package_.manifest.provenance.source, "original_path"), false);
});

test("recorded starter ends its post-catch tail after the authored 160 ms", async () => {
  const fixture = canonicalFetch();
  const package_ = await loadSharedRecordedStarterRuntime(MANIFEST_URL, {
    fetch: fixture.fetch,
    crypto: webcrypto,
  });
  const cursor = new SharedRecordedStarterCursor(package_);
  cursor.setState({
    starter: true,
    ignition: false,
    fuel: true,
    rpm: 250,
    combustionDetected: false,
    frame: 0,
  });
  cursor.setState({
    starter: true,
    ignition: true,
    fuel: true,
    rpm: 800,
    combustionDetected: true,
    frame: 1_000,
  });

  const handoffEndFrame = 1_000 + 30 * 192 + 160 * 192;
  cursor.mixPair(
    new Float32Array(handoffEndFrame),
    new Float32Array(handoffEndFrame),
  );
  assert.deepEqual(
    {
      handoffStartFrame: cursor.diagnostics().handoffStartFrame,
      handoffEndFrame: cursor.diagnostics().handoffEndFrame,
      active: cursor.diagnostics().active,
    },
    {
      handoffStartFrame: 1_000 + 30 * 192,
      handoffEndFrame,
      active: true,
    },
  );

  cursor.mixPair(new Float32Array(1), new Float32Array(1));
  assert.equal(cursor.diagnostics().active, false);
  assert.equal(cursor.diagnostics().starterGain, 0);
});

test("recorded starter cursor reports licensed availability and mixes A/B equally", async () => {
  const fixture = canonicalFetch();
  const package_ = await loadSharedRecordedStarterRuntime(MANIFEST_URL, {
    fetch: fixture.fetch,
    crypto: webcrypto,
  });
  const cursor = new SharedRecordedStarterCursor(package_);
  cursor.setState({
    starter: true,
    ignition: false,
    fuel: true,
    rpm: 250,
    combustionDetected: false,
    frame: 0,
  });
  const silence = new Float32Array(4_096);
  const mixed = cursor.mixPair(silence, silence);

  assert.deepEqual(mixed.sourceBlock, mixed.bakedBlock);
  assert.ok(mixed.sourceBlock.some((sample) => sample !== 0));
  assert.deepEqual(
    {
      licenseStatus: cursor.diagnostics().licenseStatus,
      licensee: cursor.diagnostics().licensee,
      auditionOnly: cursor.diagnostics().auditionOnly,
      modificationAuthorized: cursor.diagnostics().modificationAuthorized,
      redistributionAuthorized:
        cursor.diagnostics().redistributionAuthorized,
      active: cursor.diagnostics().active,
    },
    {
      licenseStatus: "licensed",
      licensee: "SvetlozarValchev",
      auditionOnly: false,
      modificationAuthorized: true,
      redistributionAuthorized: true,
      active: true,
    },
  );
});

test("responsive packages may reference one direct child or sibling manifest", () => {
  const responsive = new URL(
    "https://fixtures.invalid/packages/bmw-m52tub28-cleanroom-responsive-audio/runtime.json",
  );
  assert.equal(
    resolveSharedRecordedStarterManifestUrl(
      responsive,
      "../shared-recorded-starter/runtime.json",
    ).href,
    MANIFEST_URL.href,
  );
  assert.equal(
    resolveSharedRecordedStarterManifestUrl(
      new URL("https://fixtures.invalid/package/runtime.json"),
      "shared-recorded-starter/runtime.json",
    ).href,
    "https://fixtures.invalid/package/shared-recorded-starter/runtime.json",
  );

  for (const rejected of [
    "../../shared-recorded-starter/runtime.json",
    "../nested/shared-recorded-starter/runtime.json",
    "../shared_recorded_starter/runtime.json",
    "../shared-recorded-starter/other.json",
    "../shared-recorded-starter/runtime.json?version=1",
    "../shared-recorded-starter/runtime.json#fragment",
    "..\\shared-recorded-starter\\runtime.json",
    "/packages/shared-recorded-starter/runtime.json",
    "https://elsewhere.invalid/packages/shared-recorded-starter/runtime.json",
    "../%73hared-recorded-starter/runtime.json",
  ]) {
    assert.throws(
      () => resolveSharedRecordedStarterManifestUrl(responsive, rejected),
      /direct child or sibling package/u,
      rejected,
    );
  }
});

test("manifest and payload corruption fail their independent SHA-256 checks", async () => {
  const manifestMutation = canonicalFetch({
    mutate(url, bytes) {
      if (url === MANIFEST_URL.href) bytes[bytes.length - 1] = 0x20;
      return bytes;
    },
  });
  await assert.rejects(
    loadSharedRecordedStarterRuntime(MANIFEST_URL, {
      fetch: manifestMutation.fetch,
      crypto: webcrypto,
    }),
    /manifest SHA-256 mismatch/u,
  );

  const payloadMutation = canonicalFetch({
    mutate(url, bytes) {
      if (url.endsWith(PAYLOAD_PATH)) bytes[0] ^= 0x01;
      return bytes;
    },
  });
  await assert.rejects(
    loadSharedRecordedStarterRuntime(MANIFEST_URL, {
      fetch: payloadMutation.fetch,
      crypto: webcrypto,
    }),
    /payload SHA-256 mismatch/u,
  );
});

test("redirected or retargeted starter fetches fail closed", async () => {
  for (const fixture of [
    canonicalFetch({ redirected: true }),
    canonicalFetch({ retarget: true }),
  ]) {
    await assert.rejects(
      loadSharedRecordedStarterRuntime(MANIFEST_URL, {
        fetch: fixture.fetch,
        crypto: webcrypto,
      }),
      /redirected or retargeted/u,
    );
  }
});

test("canonical fixture manifest digest is stable", async () => {
  const bytes = await fs.readFile(path.join(FIXTURE_ROOT, "runtime.json"));
  assert.equal(bytes.length, 3_120);
  assert.equal(
    createHash("sha256").update(bytes).digest("hex"),
    "1fb698a9c304ecee323361b059dbfc615ab82c06357e01b815faa8f3a008365e",
  );
});
