import assert from "node:assert/strict";
import fs from "node:fs/promises";
import os from "node:os";
import path from "node:path";
import { spawn } from "node:child_process";
import { setTimeout as delay } from "node:timers/promises";

const EXPECTED_WAV_BYTES = 3_840_056;
const EXPECTED_WAV_SHA256 =
  "f7cad8870381669e105a2ac7e092c17e74b2ec2ff9b4747df3befc1329087e11";

function usage() {
  return (
    "usage: node web/tests/integration/workbench-ui-smoke.mjs " +
    "<workbench-url> [chrome-executable]"
  );
}

async function waitUntil(operation, predicate, description, timeoutMs = 20_000) {
  const deadline = Date.now() + timeoutMs;
  let latest;
  let latestError;
  while (Date.now() < deadline) {
    try {
      latest = await operation();
      if (predicate(latest)) {
        return latest;
      }
    } catch (error) {
      latestError = error;
    }
    await delay(50);
  }
  const detail = latestError?.message ?? JSON.stringify(latest);
  throw new Error(`timed out waiting for ${description}: ${detail}`);
}

class CdpSession {
  #socket;
  #nextId = 1;
  #pending = new Map();
  exceptions = [];

  static async connect(url) {
    const socket = new WebSocket(url);
    await new Promise((resolve, reject) => {
      socket.addEventListener("open", resolve, { once: true });
      socket.addEventListener(
        "error",
        () => reject(new Error("Chrome DevTools WebSocket failed to open")),
        { once: true },
      );
    });
    return new CdpSession(socket);
  }

  constructor(socket) {
    this.#socket = socket;
    socket.addEventListener("message", (event) => {
      const message = JSON.parse(String(event.data));
      if (message.id !== undefined) {
        const pending = this.#pending.get(message.id);
        if (!pending) {
          return;
        }
        this.#pending.delete(message.id);
        if (message.error) {
          pending.reject(
            new Error(
              `CDP ${pending.method} failed: ${message.error.message}`,
            ),
          );
        } else {
          pending.resolve(message.result);
        }
        return;
      }
      if (message.method === "Runtime.exceptionThrown") {
        this.exceptions.push(
          message.params.exceptionDetails?.text ?? "page exception",
        );
      }
      if (
        message.method === "Runtime.consoleAPICalled" &&
        message.params.type === "error"
      ) {
        this.exceptions.push(
          message.params.args
            .map((argument) => argument.value ?? argument.description ?? "")
            .join(" "),
        );
      }
    });
  }

  send(method, params = {}) {
    const id = this.#nextId++;
    return new Promise((resolve, reject) => {
      this.#pending.set(id, { resolve, reject, method });
      this.#socket.send(JSON.stringify({ id, method, params }));
    });
  }

  async evaluate(expression) {
    const result = await this.send("Runtime.evaluate", {
      expression,
      awaitPromise: true,
      returnByValue: true,
    });
    if (result.exceptionDetails) {
      throw new Error(
        result.exceptionDetails.exception?.description ??
          result.exceptionDetails.text,
      );
    }
    return result.result.value;
  }

  close() {
    this.#socket.close();
  }
}

async function chromeTarget(port, expectedUrl) {
  return waitUntil(
    async () => {
      const response = await fetch(`http://127.0.0.1:${port}/json/list`);
      if (!response.ok) {
        throw new Error(`Chrome target list returned ${response.status}`);
      }
      const targets = await response.json();
      return (
        targets.find(
          (target) =>
            target.type === "page" &&
            (target.url === expectedUrl || target.url.startsWith(expectedUrl)),
        ) ?? null
      );
    },
    Boolean,
    "the workbench Chrome target",
  );
}

async function pageState(cdp) {
  return cdp.evaluate(`(() => {
    const text = (selector) =>
      document.querySelector(selector)?.textContent?.trim() ?? "";
    const selected = document.querySelector("#bus-select");
    return {
      readyState: document.readyState,
      isolated: globalThis.crossOriginIsolated === true,
      worker: text("#worker-status"),
      build: text("#build-status"),
      session: text("#session-state"),
      diagnostics: text("#diagnostics-list"),
      rpm: text("#rpm-value"),
      throttle: document.querySelector("#throttle-output")?.value ?? "",
      underruns: text("#underrun-value"),
      busCount: selected?.options?.length ?? 0,
      selectedBus: selected?.value ?? "",
      buildDisabled: document.querySelector("#build-button")?.disabled ?? true,
      startDisabled: document.querySelector("#start-button")?.disabled ?? true
    };
  })()`);
}

async function terminate(child) {
  if (child.exitCode !== null || child.signalCode !== null) {
    return;
  }
  const exited = new Promise((resolve) => child.once("exit", resolve));
  child.kill("SIGTERM");
  const terminated = await Promise.race([
    exited.then(() => true),
    delay(2_000).then(() => false),
  ]);
  if (terminated) {
    return;
  }
  child.kill("SIGKILL");
  const killed = await Promise.race([
    exited.then(() => true),
    delay(2_000).then(() => false),
  ]);
  if (!killed) {
    throw new Error("headless Chrome did not exit after SIGKILL");
  }
}

async function main() {
  if (process.argv.length < 3 || process.argv.length > 4) {
    throw new Error(usage());
  }
  const workbenchUrl = new URL(process.argv[2]).href;
  const chromeExecutable = process.argv[3] ?? "google-chrome";
  const profile = await fs.mkdtemp(
    path.join(os.tmpdir(), "engine-sim-offline-chrome-"),
  );
  const chrome = spawn(
    chromeExecutable,
    [
      "--headless=new",
      "--disable-gpu",
      "--no-sandbox",
      "--autoplay-policy=no-user-gesture-required",
      "--remote-debugging-address=127.0.0.1",
      "--remote-debugging-port=0",
      `--user-data-dir=${profile}`,
      workbenchUrl,
    ],
    { stdio: ["ignore", "ignore", "pipe"] },
  );
  let chromeStderr = "";
  chrome.stderr.setEncoding("utf8");
  chrome.stderr.on("data", (chunk) => {
    chromeStderr += chunk;
  });

  let cdp;
  try {
    const activePort = path.join(profile, "DevToolsActivePort");
    const portText = await waitUntil(
      async () => fs.readFile(activePort, "utf8"),
      (text) => /^\d+/u.test(text),
      "Chrome DevTools port",
    );
    const port = Number(portText.split(/\r?\n/u)[0]);
    const target = await chromeTarget(port, workbenchUrl);
    cdp = await CdpSession.connect(target.webSocketDebuggerUrl);
    await cdp.send("Runtime.enable");
    await cdp.send("Page.enable");

    const loaded = await waitUntil(
      () => pageState(cdp),
      (state) =>
        state.readyState === "complete" &&
        state.isolated &&
        state.worker === "WASM ABI 1" &&
        !state.buildDisabled,
      "isolated workbench and WASM Worker",
    );
    assert.match(loaded.diagnostics, /No diagnostics reported/u);

    await cdp.evaluate(
      `document.querySelector("#build-button").click(); true`,
    );
    const built = await waitUntil(
      () => pageState(cdp),
      (state) =>
        state.build === "Build admitted" &&
        state.session === "Ready" &&
        state.busCount === 8 &&
        !state.startDisabled,
      "the compiled BMW workbench session",
    );
    assert.equal(built.selectedBus, "7");
    assert.match(built.diagnostics, /No diagnostics reported/u);

    await cdp.evaluate(`(() => {
      const createObjectUrl = URL.createObjectURL.bind(URL);
      window.__esoSmokeDownload = null;
      URL.createObjectURL = (blob) => {
        window.__esoSmokeDownload = blob.arrayBuffer().then(async (buffer) => {
          const digest = new Uint8Array(
            await crypto.subtle.digest("SHA-256", buffer)
          );
          return {
            bytes: buffer.byteLength,
            sha256: [...digest]
              .map((value) => value.toString(16).padStart(2, "0"))
              .join("")
          };
        });
        return createObjectUrl(blob);
      };
      document.querySelector("#export-button").click();
      return true;
    })()`);
    const exported = await waitUntil(
      () =>
        cdp.evaluate(
          `window.__esoSmokeDownload === null
            ? null
            : window.__esoSmokeDownload`,
        ),
      (value) => value !== null,
      "the full canonical browser WAV export",
      45_000,
    );
    assert.deepEqual(exported, {
      bytes: EXPECTED_WAV_BYTES,
      sha256: EXPECTED_WAV_SHA256,
    });

    await cdp.evaluate(
      `document.querySelector("#start-button").click(); true`,
    );
    await waitUntil(
      () => pageState(cdp),
      (state) =>
        state.session === "Running" &&
        state.rpm !== "—" &&
        state.throttle === "10%",
      "primed live BMW playback",
      20_000,
    );
    await delay(500);
    let running = await pageState(cdp);
    assert.equal(running.underruns, "0");

    await cdp.evaluate(`(() => {
      const throttle = document.querySelector("#throttle-input");
      throttle.value = "20";
      throttle.dispatchEvent(new Event("input", { bubbles: true }));
      return true;
    })()`);
    await waitUntil(
      () => pageState(cdp),
      (state) => state.throttle === "20%",
      "the accepted 20% throttle command",
    );
    await delay(500);
    running = await pageState(cdp);
    assert.equal(running.throttle, "20%");
    assert.equal(running.underruns, "0");

    await cdp.evaluate(
      `document.querySelector("#stop-button").click(); true`,
    );
    await waitUntil(
      () => pageState(cdp),
      (state) => state.session === "Paused",
      "paused live session",
    );

    const selectedRoute = await cdp.evaluate(`(() => {
      const buses = document.querySelector("#bus-select");
      const route = [...buses.options].find(
        (option) => option.value !== buses.value
      );
      buses.value = route.value;
      buses.dispatchEvent(new Event("change", { bubbles: true }));
      return route.value;
    })()`);
    const routeReady = await waitUntil(
      () => pageState(cdp),
      (state) =>
        state.session === "Ready" &&
        state.build === "Build admitted" &&
        state.selectedBus === selectedRoute,
      "route-bus session replacement",
    );
    assert.match(routeReady.diagnostics, /No diagnostics reported/u);
    assert.deepEqual(cdp.exceptions, []);

    process.stdout.write(
      JSON.stringify({
        worker: routeReady.worker,
        buses: routeReady.busCount,
        wavBytes: exported.bytes,
        wavSha256: exported.sha256,
        startupUnderruns: Number(running.underruns),
        throttle: running.throttle,
        selectedRoute,
      }) + "\n",
    );
  } finally {
    cdp?.close();
    await terminate(chrome);
    await fs.rm(profile, {
      recursive: true,
      force: true,
      maxRetries: 10,
      retryDelay: 100,
    });
  }
}

main().catch((error) => {
  process.stderr.write(`workbench UI smoke failure: ${error.stack}\n`);
  process.exitCode = 1;
});
