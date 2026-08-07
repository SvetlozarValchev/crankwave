#!/usr/bin/env node

import fs from "node:fs/promises";
import http from "node:http";
import path from "node:path";
import process from "node:process";
import { fileURLToPath } from "node:url";

const scriptDirectory = path.dirname(fileURLToPath(import.meta.url));
const repositoryDirectory = path.resolve(scriptDirectory, "..");
const defaultOptions = Object.freeze({
  host: "127.0.0.1",
  port: 4173,
});

function parseArguments(arguments_) {
  const result = { ...defaultOptions };
  for (let index = 0; index < arguments_.length; index += 1) {
    const argument = arguments_[index];
    if (argument === "--host" && arguments_[index + 1]) {
      result.host = arguments_[++index];
    } else if (argument === "--port" && arguments_[index + 1]) {
      result.port = Number(arguments_[++index]);
    } else if (argument === "--help") {
      console.log(
        "Usage: node scripts/serve-revengine-harness.mjs " +
          "[--host 127.0.0.1] [--port 4173]\n\n" +
          "Serves only the standalone REVENGINE harness and browser runtime.\n" +
          "No workbench build or WebAssembly module is required.",
      );
      process.exit(0);
    } else {
      throw new Error(`Unknown argument: ${argument}`);
    }
  }
  if (
    !Number.isInteger(result.port) ||
    result.port < 0 ||
    result.port > 65_535
  ) {
    throw new Error("--port must be an integer from 0 through 65535");
  }
  return result;
}

const mimeTypes = new Map([
  [".css", "text/css; charset=utf-8"],
  [".html", "text/html; charset=utf-8"],
  [".js", "text/javascript; charset=utf-8"],
  [".json", "application/json; charset=utf-8"],
  [".mjs", "text/javascript; charset=utf-8"],
  [".svg", "image/svg+xml"],
]);

// Exact transitive playback closure.  In particular, the standalone server
// does not expose the C API, browser engine runtime, renderer loader, or WASM
// heap modules that happen to live beside these files in the source tree.
const runtimeFiles = new Set([
  "device-resampler.js",
  "directional-phase-cell.js",
  "dry-directional-phase-runtime.js",
  "held-phase-texture-runtime.js",
  "held-texture-presentation-runtime.js",
  "pcm-ring-buffer.js",
  "renderer-runtime-compatibility.js",
  "responsive-audio-lifecycle-runtime.js",
  "revengine-audio-engine.js",
  "revengine-package.js",
  "shared-recorded-starter-runtime.js",
  "state-phase-texture-runtime.js",
  "steady-transient-envelope.js",
]);

function beneath(candidate, directory) {
  return candidate === directory || candidate.startsWith(`${directory}${path.sep}`);
}

function relativeRequestPath(pathname, prefix) {
  let decoded;
  try {
    decoded = decodeURIComponent(pathname);
  } catch {
    return null;
  }
  if (!decoded.startsWith(prefix) || decoded.includes("\0")) return null;
  const relative = decoded.slice(prefix.length);
  if (
    relative.length === 0 ||
    relative.startsWith("/") ||
    relative.endsWith("/") ||
    relative.split("/").some((segment) => segment === "." || segment === "..") ||
    !/^[A-Za-z0-9._/-]+$/u.test(relative)
  ) {
    return null;
  }
  return relative;
}

function routePath(pathname, roots) {
  if (pathname === "/" || pathname === "/index.html") {
    return {
      candidate: path.join(roots.harness, "index.html"),
      allowedRoot: roots.harness,
    };
  }
  const routes = [
    ["/harness/", roots.harness],
    ["/runtime/", roots.runtime],
  ];
  for (const [prefix, allowedRoot] of routes) {
    const relative = relativeRequestPath(pathname, prefix);
    if (relative === null) continue;
    if (prefix === "/runtime/" && !runtimeFiles.has(relative)) return null;
    const candidate = path.resolve(allowedRoot, relative);
    if (!beneath(candidate, allowedRoot)) return null;
    return { candidate, allowedRoot };
  }
  return null;
}

function writeHeaders(
  response,
  status,
  contentType = "text/plain; charset=utf-8",
  additionalHeaders = {},
) {
  response.writeHead(status, {
    "Cache-Control": "no-store",
    "Content-Security-Policy":
      "default-src 'self'; script-src 'self'; style-src 'self'; " +
      "worker-src 'self'; connect-src 'self'; img-src 'self'; " +
      "object-src 'none'; base-uri 'none'; frame-ancestors 'none'",
    "Content-Type": contentType,
    "Cross-Origin-Embedder-Policy": "require-corp",
    "Cross-Origin-Opener-Policy": "same-origin",
    "Cross-Origin-Resource-Policy": "same-origin",
    "Permissions-Policy": "camera=(), geolocation=(), microphone=()",
    "Referrer-Policy": "no-referrer",
    "X-Content-Type-Options": "nosniff",
    ...additionalHeaders,
  });
}

async function serve(request, response, roots) {
  try {
    if (request.method !== "GET" && request.method !== "HEAD") {
      writeHeaders(response, 405);
      response.end("Method not allowed\n");
      return;
    }
    const url = new URL(request.url, "http://localhost");
    const route = routePath(url.pathname, roots);
    if (route === null) {
      writeHeaders(response, 404);
      response.end("Not found\n");
      return;
    }
    const realCandidate = await fs.realpath(route.candidate);
    if (!beneath(realCandidate, route.allowedRoot)) {
      writeHeaders(response, 404);
      response.end("Not found\n");
      return;
    }
    const metadata = await fs.stat(realCandidate);
    if (!metadata.isFile()) {
      writeHeaders(response, 404);
      response.end("Not found\n");
      return;
    }
    const bytes = await fs.readFile(realCandidate);
    const contentType =
      mimeTypes.get(path.extname(realCandidate).toLowerCase()) ??
      "application/octet-stream";
    writeHeaders(response, 200, contentType, {
      "Content-Length": bytes.byteLength,
    });
    response.end(request.method === "HEAD" ? undefined : bytes);
  } catch (error) {
    if (error?.code === "ENOENT" || error?.code === "ENOTDIR") {
      writeHeaders(response, 404);
      response.end("Not found\n");
      return;
    }
    writeHeaders(response, 500);
    response.end("Internal server error\n");
    console.error(error);
  }
}

const options = parseArguments(process.argv.slice(2));
const roots = Object.freeze({
  harness: await fs.realpath(
    path.join(repositoryDirectory, "web", "revengine-harness"),
  ),
  runtime: await fs.realpath(path.join(repositoryDirectory, "web", "runtime")),
});
const server = http.createServer((request, response) => {
  void serve(request, response, roots);
});
server.on("error", (error) => {
  console.error(`REVENGINE harness server failed: ${error.message}`);
  process.exitCode = 1;
});
server.listen(options.port, options.host, () => {
  const address = server.address();
  const port =
    typeof address === "object" && address !== null ? address.port : options.port;
  console.log(`REVENGINE audio harness: http://${options.host}:${port}/`);
  console.log("Serving only the harness and its JavaScript runtime dependencies.");
  console.log("Cross-origin isolation is enabled; press Ctrl+C to stop.");
});
