#!/usr/bin/env node

import fs from "node:fs/promises";
import http from "node:http";
import path from "node:path";
import process from "node:process";
import { fileURLToPath } from "node:url";

const scriptDirectory = path.dirname(fileURLToPath(import.meta.url));
const repositoryDirectory = path.resolve(scriptDirectory, "..");
const defaults = {
  host: "127.0.0.1",
  port: 4173,
  root: path.join(
    repositoryDirectory,
    ".work",
    "browser-workbench",
    "build",
    "workbench",
  ),
  rootWasExplicit: false,
};

function parseArguments(arguments_) {
  const result = { ...defaults };
  for (let index = 0; index < arguments_.length; index += 1) {
    const argument = arguments_[index];
    if (argument === "--host" && arguments_[index + 1]) {
      result.host = arguments_[++index];
    } else if (argument === "--port" && arguments_[index + 1]) {
      result.port = Number(arguments_[++index]);
    } else if (argument === "--root" && arguments_[index + 1]) {
      result.root = path.resolve(arguments_[++index]);
      result.rootWasExplicit = true;
    } else if (argument === "--help") {
      console.log(
        "Usage: node scripts/serve-workbench.mjs [--root <directory>] " +
          "[--host 127.0.0.1] [--port 4173]\n" +
          "Default root: .work/browser-workbench/build/workbench\n" +
          "Build it first with: scripts/build-workbench.sh",
      );
      process.exit(0);
    } else {
      throw new Error(`Unknown argument: ${argument}`);
    }
  }
  if (
    !Number.isInteger(result.port) ||
    result.port < 0 ||
    result.port > 65535
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
  [".wasm", "application/wasm"],
  [".wav", "audio/wav"],
]);

function routePath(pathname, rootDirectory) {
  if (pathname === "/") {
    return path.join(rootDirectory, "web", "index.html");
  }
  const allowedPrefixes = ["/web/", "/data/", "/reference/"];
  let decoded;
  try {
    decoded = decodeURIComponent(pathname);
  } catch {
    return null;
  }
  const prefix = allowedPrefixes.find((candidate) =>
    decoded.startsWith(candidate),
  );
  if (!prefix) {
    return null;
  }
  const relative = decoded.slice(prefix.length);
  const segments = relative.split("/");
  if (
    segments.some(
      (segment) =>
        segment === "." ||
        segment === ".." ||
        segment.includes("\0"),
    )
  ) {
    return null;
  }
  const allowedDirectory = path.resolve(rootDirectory, prefix.slice(1));
  const candidate = path.resolve(allowedDirectory, relative);
  if (
    candidate !== allowedDirectory &&
    !candidate.startsWith(`${allowedDirectory}${path.sep}`)
  ) {
    return null;
  }
  return candidate;
}

function writeHeaders(
  response,
  status,
  contentType = "text/plain; charset=utf-8",
  additionalHeaders = {},
) {
  response.writeHead(status, {
    "Cache-Control": "no-store",
    "Content-Type": contentType,
    "Cross-Origin-Embedder-Policy": "require-corp",
    "Cross-Origin-Opener-Policy": "same-origin",
    "Cross-Origin-Resource-Policy": "same-origin",
    "Referrer-Policy": "no-referrer",
    "X-Content-Type-Options": "nosniff",
    ...additionalHeaders,
  });
}

async function serve(request, response, rootDirectory) {
  try {
    if (request.method !== "GET" && request.method !== "HEAD") {
      writeHeaders(response, 405);
      response.end("Method not allowed\n");
      return;
    }
    const url = new URL(request.url, "http://localhost");
    const candidate = routePath(url.pathname, rootDirectory);
    if (!candidate) {
      writeHeaders(response, 404);
      response.end("Not found\n");
      return;
    }

    const realCandidate = await fs.realpath(candidate);
    if (
      realCandidate !== rootDirectory &&
      !realCandidate.startsWith(`${rootDirectory}${path.sep}`)
    ) {
      writeHeaders(response, 404);
      response.end("Not found\n");
      return;
    }
    const bytes = await fs.readFile(realCandidate);
    const type =
      mimeTypes.get(path.extname(realCandidate).toLowerCase()) ??
      "application/octet-stream";
    writeHeaders(response, 200, type, {
      "Content-Length": bytes.byteLength,
    });
    response.end(request.method === "HEAD" ? undefined : bytes);
  } catch (error) {
    if (error?.code === "ENOENT") {
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
let rootDirectory;
try {
  rootDirectory = await fs.realpath(options.root);
  const rootMetadata = await fs.stat(rootDirectory);
  if (!rootMetadata.isDirectory()) {
    throw new Error("path is not a directory");
  }
} catch (error) {
  if (!options.rootWasExplicit) {
    console.error(
      `Staged workbench not found at ${options.root}.\n` +
        "Run scripts/build-workbench.sh before starting the server.",
    );
  } else {
    console.error(`Cannot serve --root ${options.root}: ${error.message}`);
  }
  process.exit(1);
}
const server = http.createServer((request, response) => {
  void serve(request, response, rootDirectory);
});
server.on("error", (error) => {
  console.error(`Workbench server failed: ${error.message}`);
  process.exitCode = 1;
});
server.listen(options.port, options.host, () => {
  const address = server.address();
  const listeningPort =
    typeof address === "object" && address !== null ? address.port : options.port;
  console.log(
    `Engine Sim Offline workbench: http://${options.host}:${listeningPort}/`,
  );
  console.log(`Serving files from: ${rootDirectory}`);
  console.log("Cross-origin isolation headers are enabled; press Ctrl+C to stop.");
});
