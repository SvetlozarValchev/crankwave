import assert from "node:assert/strict";
import { createHash, webcrypto } from "node:crypto";
import { readFileSync } from "node:fs";
import test from "node:test";

import { loadRendererRuntimeCompatibility } from "../../runtime/renderer-runtime-compatibility.js";

const MANIFEST_URL = new URL(
  "https://fixtures.invalid/packages/fixture-engine/runtime.json",
);
const CAPTURE_SOURCE_CLOSURE_SHA256 =
  "5287982ab1fe4846fc6d5008f0415f1901adae0fe4ca06a02c48addcab39b6fb";
const ADMITTED_SOURCE_CLOSURE_SHA256 =
  "c3a905712ddf3ae4e506af64e3919f2324df46af13ff4e3dc1a24037b4079d23";
const EVIDENCE_BYTE_COUNT = 18_309;
const EVIDENCE_SHA256 =
  "ee4eed2e02c2177ac487a15d3ecb0c0aed7cf226f5cf6544acc9ddb8f2a9de0a";
const EVIDENCE_PATH = "evidence/renderer-compatibility-parity-report-v1.json";
const CANONICAL_EVIDENCE_BYTES = Uint8Array.from(
  readFileSync(
    new URL(
      "../../../reference/fixtures/responsive-audio/bmw-m52tub28-cleanroom-responsive-audio/evidence/renderer-compatibility-parity-report-v1.json",
      import.meta.url,
    ),
  ),
);
const CANONICAL_EVIDENCE = JSON.parse(
  new TextDecoder().decode(CANONICAL_EVIDENCE_BYTES),
);

function jsonBytes(value) {
  return new TextEncoder().encode(`${JSON.stringify(value)}\n`);
}

function sha256(bytes) {
  return createHash("sha256").update(bytes).digest("hex");
}

function response(
  bytes,
  { redirected = false, url = "" } = {},
) {
  return Object.freeze({
    ok: true,
    status: 200,
    redirected,
    url,
    async arrayBuffer() {
      return bytes.slice().buffer;
    },
  });
}

function fixture({
  mutateDescriptor,
  evidenceBytes = CANONICAL_EVIDENCE_BYTES,
  responseOptions,
} = {}) {
  const descriptor = {
    capture_source_closure_sha256: CAPTURE_SOURCE_CLOSURE_SHA256,
    admitted_source_closure_sha256: ADMITTED_SOURCE_CLOSURE_SHA256,
    evidence_path: EVIDENCE_PATH,
    evidence_byte_count: EVIDENCE_BYTE_COUNT,
    evidence_sha256: EVIDENCE_SHA256,
  };
  mutateDescriptor?.(descriptor);
  const calls = [];
  const fetch = async (url, options) => {
    calls.push({ url, options });
    return response(evidenceBytes, responseOptions);
  };
  return { descriptor, evidenceBytes, fetch, calls };
}

function mutatedCanonicalEvidence(mutator) {
  const evidence = structuredClone(CANONICAL_EVIDENCE);
  mutator(evidence);
  return jsonBytes(evidence);
}

test("the canonical reviewed report loads through its registered edge", async () => {
  assert.equal(CANONICAL_EVIDENCE_BYTES.byteLength, EVIDENCE_BYTE_COUNT);
  assert.equal(sha256(CANONICAL_EVIDENCE_BYTES), EVIDENCE_SHA256);
  assert.deepEqual(Object.keys(CANONICAL_EVIDENCE).sort(), [
    "aggregate",
    "cases",
    "control_profiles",
    "renderer_pair",
    "schema",
    "verdict",
  ]);
  assert.deepEqual(CANONICAL_EVIDENCE.renderer_pair, {
    capture_renderer_source_closure_sha256:
      CAPTURE_SOURCE_CLOSURE_SHA256,
    capture_wasm_sha256:
      "d5c0016086cd3d67eb9daf4fd80c90cfd2eacba2cbd36d5da7c68f68c909c033",
    loader_sha256:
      "769c7942e447acf79e155470d84e94c5e1dd2ad896ce3691b81e0fd5a93d86e6",
    running_renderer_source_closure_sha256:
      ADMITTED_SOURCE_CLOSURE_SHA256,
    running_wasm_sha256:
      "0596bf2535c11368c087474aedcf24da8cfeca67bc266b5729046796b5882963",
  });

  const { descriptor, fetch, calls } = fixture();
  const loaded = await loadRendererRuntimeCompatibility(
    descriptor,
    CAPTURE_SOURCE_CLOSURE_SHA256,
    MANIFEST_URL,
    { fetch, crypto: webcrypto },
  );

  assert.deepEqual(calls, [
    {
      url: new URL(EVIDENCE_PATH, MANIFEST_URL).href,
      options: {
        cache: "no-store",
        credentials: "same-origin",
        redirect: "error",
      },
    },
  ]);
  assert.deepEqual(loaded, {
    captureSourceClosureSha256: CAPTURE_SOURCE_CLOSURE_SHA256,
    admittedSourceClosureSha256: ADMITTED_SOURCE_CLOSURE_SHA256,
    evidenceUrl: new URL(EVIDENCE_PATH, MANIFEST_URL).href,
    evidenceSha256: EVIDENCE_SHA256,
    caseCount: 21,
    processedBlockCount: 14_315,
    audioFrameCount: 54_942_720,
  });
  assert.ok(Object.isFrozen(loaded));
});

test("an absent compatibility descriptor returns null without dependencies", async () => {
  assert.equal(
    await loadRendererRuntimeCompatibility(
      undefined,
      undefined,
      undefined,
      { fetch: undefined, crypto: undefined },
    ),
    null,
  );
});

test("corrupted evidence fails its registered digest before parsing", async () => {
  const corrupted = CANONICAL_EVIDENCE_BYTES.slice();
  corrupted[corrupted.byteLength - 2] ^= 1;
  const { descriptor, fetch } = fixture({ evidenceBytes: corrupted });

  await assert.rejects(
    loadRendererRuntimeCompatibility(
      descriptor,
      CAPTURE_SOURCE_CLOSURE_SHA256,
      MANIFEST_URL,
      { fetch, crypto: webcrypto },
    ),
    /evidence SHA-256 does not match its descriptor/,
  );
});

test("capture and admission identities fail closed when they disagree", async (t) => {
  await t.test("descriptor capture versus package provenance", async () => {
    const { descriptor, fetch, calls } = fixture();
    await assert.rejects(
      loadRendererRuntimeCompatibility(
        descriptor,
        "88".repeat(32),
        MANIFEST_URL,
        { fetch, crypto: webcrypto },
      ),
      /capture identity must match package provenance/,
    );
    assert.deepEqual(calls, []);
  });

  await t.test("capture and admission must be distinct", async () => {
    const { descriptor, fetch, calls } = fixture({
      mutateDescriptor(value) {
        value.admitted_source_closure_sha256 =
          value.capture_source_closure_sha256;
      },
    });
    await assert.rejects(
      loadRendererRuntimeCompatibility(
        descriptor,
        CAPTURE_SOURCE_CLOSURE_SHA256,
        MANIFEST_URL,
        { fetch, crypto: webcrypto },
      ),
      /must describe two distinct source closures/,
    );
    assert.deepEqual(calls, []);
  });

  await t.test("a report renderer identity cannot be altered", async () => {
    const evidenceBytes = mutatedCanonicalEvidence((value) => {
      value.renderer_pair.running_renderer_source_closure_sha256 =
        `f${value.renderer_pair.running_renderer_source_closure_sha256.slice(1)}`;
    });
    assert.equal(evidenceBytes.byteLength, EVIDENCE_BYTE_COUNT);
    const { descriptor, fetch } = fixture({ evidenceBytes });
    await assert.rejects(
      loadRendererRuntimeCompatibility(
        descriptor,
        CAPTURE_SOURCE_CLOSURE_SHA256,
        MANIFEST_URL,
        { fetch, crypto: webcrypto },
      ),
      /evidence SHA-256 does not match its descriptor/,
    );
  });
});

test("only the exact code-owned registry edge and digest are admitted", async (t) => {
  const cases = [
    {
      name: "unknown capture-to-admission edge",
      mutate(value) {
        value.admitted_source_closure_sha256 = "99".repeat(32);
      },
    },
    {
      name: "unregistered evidence digest",
      mutate(value) {
        value.evidence_sha256 = "aa".repeat(32);
      },
    },
    {
      name: "unregistered evidence byte count",
      mutate(value) {
        value.evidence_byte_count -= 1;
      },
    },
  ];

  for (const entry of cases) {
    await t.test(entry.name, async () => {
      const { descriptor, fetch, calls } = fixture({
        mutateDescriptor: entry.mutate,
      });
      await assert.rejects(
        loadRendererRuntimeCompatibility(
          descriptor,
          CAPTURE_SOURCE_CLOSURE_SHA256,
          MANIFEST_URL,
          { fetch, crypto: webcrypto },
        ),
        /edge is not present in the reviewed registry/,
      );
      assert.deepEqual(calls, []);
    });
  }
});

test("malformed descriptors and unknown members fail closed", async (t) => {
  const descriptorCases = [
    {
      name: "non-object descriptor",
      descriptor: [],
      error: /runtime_renderer_compatibility must be an object/,
    },
    {
      name: "missing required member",
      mutate(value) {
        delete value.evidence_byte_count;
      },
      error: /is missing member evidence_byte_count/,
    },
    {
      name: "unknown member",
      mutate(value) {
        value.unreviewed_override = true;
      },
      error: /has unknown member unreviewed_override/,
    },
    {
      name: "noncanonical digest",
      mutate(value) {
        value.evidence_sha256 = "A".repeat(64);
      },
      error: /must be a nonzero lowercase SHA-256 digest/,
    },
  ];

  for (const entry of descriptorCases) {
    await t.test(entry.name, async () => {
      const built = fixture({ mutateDescriptor: entry.mutate });
      await assert.rejects(
        loadRendererRuntimeCompatibility(
          entry.descriptor ?? built.descriptor,
          CAPTURE_SOURCE_CLOSURE_SHA256,
          MANIFEST_URL,
          { fetch: built.fetch, crypto: webcrypto },
        ),
        entry.error,
      );
      assert.deepEqual(built.calls, []);
    });
  }
});

test("evidence URLs remain same-origin and beneath the manifest", async (t) => {
  const pathCases = [
    ["parent traversal", "../outside.json"],
    ["absolute scheme", "https://attacker.invalid/evidence.json"],
    ["encoded traversal", "evidence/%2e%2e/outside.json"],
    ["backslash traversal", "evidence\\..\\outside.json"],
    ["query suffix", `${EVIDENCE_PATH}?replacement=1`],
    ["fragment suffix", `${EVIDENCE_PATH}#replacement`],
  ];

  for (const [name, evidencePath] of pathCases) {
    await t.test(name, async () => {
      const { descriptor, fetch, calls } = fixture({
        mutateDescriptor(value) {
          value.evidence_path = evidencePath;
        },
      });
      await assert.rejects(
        loadRendererRuntimeCompatibility(
          descriptor,
          CAPTURE_SOURCE_CLOSURE_SHA256,
          MANIFEST_URL,
          { fetch, crypto: webcrypto },
        ),
        /evidence_path must stay beneath its manifest/,
      );
      assert.deepEqual(calls, []);
    });
  }
});

test("redirected or retargeted evidence responses are rejected", async (t) => {
  const expectedUrl = new URL(EVIDENCE_PATH, MANIFEST_URL).href;
  const responseCases = [
    ["redirect flag", { redirected: true, url: expectedUrl }],
    [
      "different effective URL",
      {
        redirected: false,
        url: "https://fixtures.invalid/packages/other/evidence.json",
      },
    ],
  ];

  for (const [name, responseOptions] of responseCases) {
    await t.test(name, async () => {
      const { descriptor, fetch } = fixture({ responseOptions });
      await assert.rejects(
        loadRendererRuntimeCompatibility(
          descriptor,
          CAPTURE_SOURCE_CLOSURE_SHA256,
          MANIFEST_URL,
          { fetch, crypto: webcrypto },
        ),
        /renderer compatibility evidence fetch failed/,
      );
    });
  }
});

test("malformed or unknown report members invalidate the reviewed bytes", async (t) => {
  const evidenceCases = [
    {
      name: "unknown top-level member",
      mutate(value) {
        value.bogusx = value.schema;
        delete value.schema;
      },
    },
    {
      name: "unsupported verdict",
      mutate(value) {
        value.verdict = `x${value.verdict.slice(1)}`;
      },
    },
    {
      name: "unknown case member",
      mutate(value) {
        value.cases[0].surprise = value.cases[0].scenario;
        delete value.cases[0].scenario;
      },
    },
    {
      name: "undeclared control profile",
      mutate(value) {
        value.cases[0].control_profile = "x".repeat(
          value.cases[0].control_profile.length,
        );
      },
    },
    {
      name: "invalid case count",
      mutate(value) {
        value.aggregate.case_count = 22;
      },
    },
    {
      name: "duplicate case IDs",
      mutate(value) {
        value.cases[16].id = value.cases[0].id;
      },
    },
    {
      name: "invalid aggregate count",
      mutate(value) {
        value.aggregate.audio_frames += 1;
      },
    },
  ];

  for (const entry of evidenceCases) {
    await t.test(entry.name, async () => {
      const evidenceBytes = mutatedCanonicalEvidence(entry.mutate);
      assert.equal(evidenceBytes.byteLength, EVIDENCE_BYTE_COUNT);
      const { descriptor, fetch } = fixture({ evidenceBytes });
      await assert.rejects(
        loadRendererRuntimeCompatibility(
          descriptor,
          CAPTURE_SOURCE_CLOSURE_SHA256,
          MANIFEST_URL,
          { fetch, crypto: webcrypto },
        ),
        /evidence SHA-256 does not match its descriptor/,
      );
    });
  }
});
