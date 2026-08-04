import { ESO_CANONICAL_SAMPLE_RATE } from "./c-api-abi.js";
import {
  AudioPackageLoadError,
  MAXIMUM_AUDIO_PACKAGE_MANIFEST_BYTES,
  validateAudioPackageArtifactPath,
  validateAudioPackageManifest,
} from "./audio-package-manifest.js";
import { decodeAudioPackageWave } from "./audio-package-wave.js";

export { AudioPackageLoadError } from "./audio-package-manifest.js";

function fail(code, path, message, options) {
  throw new AudioPackageLoadError(code, path, message, options);
}

function resolveManifestUrl(value) {
  if (value instanceof URL) {
    return new URL(value.href);
  }
  if (typeof value !== "string" || value.length === 0) {
    throw new TypeError("packageManifestUrl must be a nonempty URL or string");
  }
  try {
    return new URL(value, globalThis.location?.href);
  } catch (error) {
    throw new TypeError("packageManifestUrl must resolve to an absolute URL", {
      cause: error,
    });
  }
}

function resolveArtifactUrl(manifestUrl, artifact, index) {
  validateAudioPackageArtifactPath(
    artifact.relative_path,
    `artifacts[${index}].relative_path`,
  );
  const packageBase = new URL(".", manifestUrl);
  const artifactUrl = new URL(artifact.relative_path, packageBase);
  if (
    artifactUrl.origin !== packageBase.origin ||
    !artifactUrl.pathname.startsWith(packageBase.pathname) ||
    artifactUrl.search !== "" ||
    artifactUrl.hash !== ""
  ) {
    fail(
      "audio-package-invalid-artifact-path",
      `artifacts[${index}].relative_path`,
      "does not remain beneath the package URL",
    );
  }
  return artifactUrl;
}

async function fetchBytes(url, fetchImplementation, path) {
  let response;
  try {
    response = await fetchImplementation(url.href);
  } catch (error) {
    fail("audio-package-fetch-failed", path, `fetch failed for ${url.href}`, {
      cause: error,
    });
  }
  if (
    !response ||
    response.ok !== true ||
    typeof response.arrayBuffer !== "function"
  ) {
    const status =
      response?.status === undefined
        ? "invalid response"
        : `HTTP ${response.status}`;
    fail("audio-package-fetch-failed", path, `${status} for ${url.href}`);
  }
  if (typeof response.url === "string" && response.url !== "") {
    let finalUrl;
    try {
      finalUrl = new URL(response.url).href;
    } catch {
      fail(
        "audio-package-fetch-failed",
        path,
        "response reports an invalid final URL",
      );
    }
    if (finalUrl !== url.href) {
      fail(
        "audio-package-fetch-failed",
        path,
        "cross-location redirects are not admitted",
      );
    }
  }
  let buffer;
  try {
    buffer = await response.arrayBuffer();
  } catch (error) {
    fail("audio-package-fetch-failed", path, "response body could not be read", {
      cause: error,
    });
  }
  if (!(buffer instanceof ArrayBuffer)) {
    fail(
      "audio-package-fetch-failed",
      path,
      "response body is not an ArrayBuffer",
    );
  }
  return new Uint8Array(buffer);
}

async function sha256Hex(bytes, cryptoImplementation, path) {
  if (!cryptoImplementation?.subtle?.digest) {
    fail(
      "audio-package-crypto-unavailable",
      path,
      "Web Crypto SHA-256 is unavailable",
    );
  }
  let digest;
  try {
    digest = new Uint8Array(
      await cryptoImplementation.subtle.digest("SHA-256", bytes),
    );
  } catch (error) {
    fail("audio-package-crypto-unavailable", path, "SHA-256 digest failed", {
      cause: error,
    });
  }
  return Array.from(digest, (byte) =>
    byte.toString(16).padStart(2, "0"),
  ).join("");
}

function decodeManifest(manifestBytes) {
  if (
    manifestBytes.byteLength === 0 ||
    manifestBytes.byteLength > MAXIMUM_AUDIO_PACKAGE_MANIFEST_BYTES
  ) {
    fail(
      "audio-package-invalid-manifest",
      "package.json",
      "manifest must contain between 1 byte and 16 MiB",
    );
  }
  if (
    manifestBytes.byteLength >= 3 &&
    manifestBytes[0] === 0xef &&
    manifestBytes[1] === 0xbb &&
    manifestBytes[2] === 0xbf
  ) {
    fail(
      "audio-package-invalid-manifest",
      "package.json",
      "UTF-8 BOM is not admitted",
    );
  }
  let manifestText;
  try {
    manifestText = new TextDecoder("utf-8", { fatal: true }).decode(
      manifestBytes,
    );
  } catch (error) {
    fail("audio-package-invalid-manifest", "package.json", "is not valid UTF-8", {
      cause: error,
    });
  }
  let manifest;
  try {
    manifest = JSON.parse(manifestText);
  } catch (error) {
    fail("audio-package-invalid-manifest", "package.json", "is not valid JSON", {
      cause: error,
    });
  }
  validateAudioPackageManifest(manifest);
  return manifest;
}

function deepFreezeJson(value) {
  if (value !== null && typeof value === "object" && !Object.isFrozen(value)) {
    for (const child of Object.values(value)) {
      deepFreezeJson(child);
    }
    Object.freeze(value);
  }
  return value;
}

async function loadArtifact(
  manifestUrl,
  artifact,
  index,
  fetchImplementation,
  cryptoImplementation,
) {
  const url = resolveArtifactUrl(manifestUrl, artifact, index);
  const path = `artifacts[${index}]`;
  const bytes = await fetchBytes(url, fetchImplementation, path);
  if (bytes.byteLength !== artifact.byte_count) {
    fail(
      "audio-package-artifact-size-mismatch",
      `${path}.byte_count`,
      `expected ${artifact.byte_count} bytes but fetched ${bytes.byteLength}`,
    );
  }
  const actualSha256 = await sha256Hex(bytes, cryptoImplementation, path);
  if (actualSha256 !== artifact.payload_sha256) {
    fail(
      "audio-package-artifact-digest-mismatch",
      `${path}.payload_sha256`,
      `expected ${artifact.payload_sha256} but fetched ${actualSha256}`,
    );
  }
  const decoded = decodeAudioPackageWave(
    bytes,
    artifact,
    ESO_CANONICAL_SAMPLE_RATE,
  );
  return Object.freeze({
    id: artifact.id,
    relativePath: artifact.relative_path,
    url: url.href,
    descriptor: artifact,
    bytes,
    pcm: decoded.samples,
    sampleRate: decoded.sampleRate,
    channelCount: decoded.channelCount,
    frameCount: decoded.frameCount,
    formatChunkBytes: decoded.formatChunkBytes,
    dataByteOffset: decoded.dataByteOffset,
  });
}

export async function loadAudioPackage(
  packageManifestUrl,
  {
    fetch: fetchImplementation = globalThis.fetch,
    crypto: cryptoImplementation = globalThis.crypto,
  } = {},
) {
  if (typeof fetchImplementation !== "function") {
    throw new TypeError("audio-package loading requires a fetch function");
  }
  const manifestUrl = resolveManifestUrl(packageManifestUrl);
  const manifest = decodeManifest(
    await fetchBytes(manifestUrl, fetchImplementation, "package.json"),
  );
  const loadedArtifacts = await Promise.all(
    manifest.artifacts.map((artifact, index) =>
      loadArtifact(
        manifestUrl,
        artifact,
        index,
        fetchImplementation,
        cryptoImplementation,
      ),
    ),
  );

  const artifactsById = Object.create(null);
  for (const artifact of loadedArtifacts) {
    artifactsById[artifact.id] = artifact;
  }
  Object.freeze(loadedArtifacts);
  Object.freeze(artifactsById);
  deepFreezeJson(manifest);
  return Object.freeze({
    manifestUrl: manifestUrl.href,
    manifest,
    artifacts: loadedArtifacts,
    artifactsById,
    artifact(id) {
      return Object.hasOwn(artifactsById, id) ? artifactsById[id] : null;
    },
  });
}
