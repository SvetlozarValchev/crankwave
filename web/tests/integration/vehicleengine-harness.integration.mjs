import assert from "node:assert/strict";
import fs from "node:fs/promises";
import os from "node:os";
import path from "node:path";
import { spawn } from "node:child_process";
import { setTimeout as delay } from "node:timers/promises";

const PACKAGE_TIMEOUT_MS = 120_000;
const PLAYBACK_TIMEOUT_MS = 120_000;
const FORBIDDEN_REQUEST_FRAGMENTS = Object.freeze([
  ".wasm",
  "engine-sim-offline.js",
  "/data/",
  "/packages/",
  "/reference/",
]);

function usage() {
  return (
    "usage: node web/tests/integration/vehicleengine-harness.integration.mjs " +
    "<harness-url> <carrier.vehicleengine> [chrome-executable]"
  );
}

async function requireCarrier(filePath) {
  const absolute = path.resolve(filePath);
  const metadata = await fs.stat(absolute);
  assert.ok(metadata.isFile(), `VEHICLEENGINE carrier is not a file: ${absolute}`);
  assert.ok(metadata.size > 0, `VEHICLEENGINE carrier is empty: ${absolute}`);
  assert.equal(
    path.extname(absolute).toLowerCase(),
    ".vehicleengine",
    `VEHICLEENGINE carrier must use the .vehicleengine extension: ${absolute}`,
  );
  return absolute;
}

async function waitUntil(
  operation,
  predicate,
  description,
  timeoutMilliseconds = 20_000,
) {
  const deadline = Date.now() + timeoutMilliseconds;
  let latest;
  let latestError;
  while (Date.now() < deadline) {
    try {
      latest = await operation();
      if (predicate(latest)) return latest;
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
  #targetKinds = new Map();
  #workerAdmission = [];

  exceptions = [];
  consoleErrors = [];
  logErrors = [];
  targetErrors = [];
  requests = [];
  responses = [];
  networkFailures = [];

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
      this.#accept(JSON.parse(String(event.data)));
    });
  }

  #context(message) {
    const kind = message.sessionId
      ? this.#targetKinds.get(message.sessionId) ?? "attached target"
      : "page";
    return `${kind}${message.sessionId ? ` ${message.sessionId}` : ""}`;
  }

  #accept(message) {
    if (message.id !== undefined) {
      const pending = this.#pending.get(message.id);
      if (!pending) return;
      this.#pending.delete(message.id);
      if (message.error) {
        pending.reject(
          new Error(`CDP ${pending.method} failed: ${message.error.message}`),
        );
      } else {
        pending.resolve(message.result);
      }
      return;
    }

    if (message.method === "Target.attachedToTarget") {
      const { sessionId, targetInfo } = message.params;
      this.#targetKinds.set(sessionId, targetInfo.type);
      if (targetInfo.type === "worker" || targetInfo.type === "shared_worker") {
        const admission = Promise.all([
          this.send("Runtime.enable", {}, sessionId),
          this.send("Network.enable", {}, sessionId),
        ])
          .then(() => this.send("Runtime.runIfWaitingForDebugger", {}, sessionId))
          .catch((error) => {
            this.targetErrors.push(
              `${targetInfo.type} admission failed: ${error.message}`,
            );
          });
        this.#workerAdmission.push(admission);
      } else {
        void this.send("Runtime.runIfWaitingForDebugger", {}, sessionId).catch(
          (error) => {
            this.targetErrors.push(
              `${targetInfo.type} admission failed: ${error.message}`,
            );
          },
        );
      }
      return;
    }

    if (message.method === "Runtime.exceptionThrown") {
      const details = message.params.exceptionDetails;
      this.exceptions.push(
        `${this.#context(message)}: ${
          details.exception?.description ?? details.text ?? "uncaught exception"
        }`,
      );
      return;
    }
    if (
      message.method === "Runtime.consoleAPICalled" &&
      message.params.type === "error"
    ) {
      this.consoleErrors.push(
        `${this.#context(message)}: ${message.params.args
          .map((argument) => argument.value ?? argument.description ?? "")
          .join(" ")}`,
      );
      return;
    }
    if (
      message.method === "Log.entryAdded" &&
      message.params.entry.level === "error"
    ) {
      this.logErrors.push(
        `${this.#context(message)}: ${message.params.entry.text}`,
      );
      return;
    }
    if (message.method === "Network.requestWillBeSent") {
      this.requests.push({
        requestId: message.params.requestId,
        url: message.params.request.url,
        type: message.params.type,
        sessionId: message.sessionId ?? null,
      });
      return;
    }
    if (message.method === "Network.responseReceived") {
      this.responses.push({
        url: message.params.response.url,
        status: message.params.response.status,
        type: message.params.type,
        headers: message.params.response.headers,
        sessionId: message.sessionId ?? null,
      });
      return;
    }
    if (message.method === "Network.loadingFailed") {
      this.networkFailures.push({
        url:
          this.requests.findLast(
            (request) =>
              request.requestId === message.params.requestId &&
              request.sessionId === (message.sessionId ?? null),
          )?.url ?? null,
        type: message.params.type,
        errorText: message.params.errorText,
        canceled: message.params.canceled === true,
        sessionId: message.sessionId ?? null,
      });
      return;
    }
    if (
      message.method === "Inspector.targetCrashed" ||
      message.method === "Target.targetCrashed"
    ) {
      this.targetErrors.push(
        `${this.#context(message)}: ${message.method}`,
      );
    }
  }

  send(method, params = {}, sessionId = undefined) {
    const id = this.#nextId++;
    return new Promise((resolve, reject) => {
      this.#pending.set(id, { resolve, reject, method });
      this.#socket.send(
        JSON.stringify({
          id,
          method,
          params,
          ...(sessionId === undefined ? {} : { sessionId }),
        }),
      );
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

  async settleWorkers() {
    await Promise.all(this.#workerAdmission);
  }

  close() {
    this.#socket.close();
  }
}

async function chromeTarget(port) {
  return waitUntil(
    async () => {
      const response = await fetch(`http://127.0.0.1:${port}/json/list`);
      assert.equal(response.status, 200);
      const targets = await response.json();
      return targets.find((target) => target.type === "page") ?? null;
    },
    Boolean,
    "the headless Chrome page target",
  );
}

function fakeAudioBootstrap() {
  return `(() => {
    const stats = {
      contexts: 0,
      resumes: 0,
      modules: [],
      nodes: 0,
      connections: 0,
      portMessages: 0
    };
    class DisabledAudioWorklet {
      async addModule(url) {
        stats.modules.push(String(url));
      }
    }
    class DisabledAudioPort {
      postMessage() {
        stats.portMessages += 1;
      }
      addEventListener() {}
      removeEventListener() {}
      start() {}
      close() {}
    }
    class DisabledAudioWorkletNode {
      constructor(context, name, options) {
        this.context = context;
        this.name = name;
        this.options = options;
        this.port = new DisabledAudioPort();
        stats.nodes += 1;
      }
      connect(destination) {
        stats.connections += 1;
        return destination;
      }
      disconnect() {}
    }
    class DisabledAudioContext {
      constructor() {
        this.sampleRate = 48_000;
        this.state = "suspended";
        this.destination = Object.freeze({ kind: "disabled-test-output" });
        this.audioWorklet = new DisabledAudioWorklet();
        stats.contexts += 1;
      }
      async resume() {
        this.state = "running";
        stats.resumes += 1;
      }
      async close() {
        this.state = "closed";
      }
    }
    Object.defineProperties(globalThis, {
      AudioContext: {
        configurable: true,
        value: DisabledAudioContext
      },
      webkitAudioContext: {
        configurable: true,
        value: DisabledAudioContext
      },
      AudioWorkletNode: {
        configurable: true,
        value: DisabledAudioWorkletNode
      },
      __vehicleengineHarnessTestAudio: {
        configurable: false,
        value: stats
      }
    });
  })();`;
}

async function pageState(cdp) {
  return cdp.evaluate(`(() => {
    const text = (selector) =>
      document.querySelector(selector)?.textContent?.trim() ?? "";
    const value = (selector) => document.querySelector(selector)?.value ?? "";
    return {
      readyState: document.readyState,
      isolated: globalThis.crossOriginIsolated === true,
      secure: globalThis.isSecureContext === true,
      security: text("#security-status"),
      package: text("#package-status"),
      packageMessage: text("#package-message"),
      audio: text("#audio-status"),
      audioMessage: text("#audio-message"),
      engine: text("#engine-id"),
      coverage: text("#rpm-coverage"),
      packageRate: text("#package-rate"),
      deviceRate: text("#device-rate"),
      buffer: text("#buffer-output"),
      level: value("#level-output"),
      meterWidth: document.querySelector("#level-meter")?.style?.width ?? "",
      rpmInput: value("#rpm-input"),
      rpmMinimum: document.querySelector("#rpm-input")?.min ?? "",
      rpmMaximum: document.querySelector("#rpm-input")?.max ?? "",
      rpm: value("#rpm-output"),
      throttleInput: value("#throttle-input"),
      throttle: value("#throttle-output"),
      loadInput: value("#load-input"),
      load: value("#load-output"),
      manifoldPressure: text("#map-output"),
      playDisabled: document.querySelector("#play-button")?.disabled ?? true,
      errorMessages: [...document.querySelectorAll(".message.is-error")]
        .map((element) => element.textContent.trim()),
      fakeAudio: globalThis.__vehicleengineHarnessTestAudio ?? null
    };
  })()`);
}

function normalizedHeaders(headers) {
  return Object.fromEntries(
    Object.entries(headers).map(([name, value]) => [name.toLowerCase(), value]),
  );
}

function assertIsolatedDocument(cdp, harnessUrl) {
  const expected = new URL(harnessUrl);
  const documentResponse = cdp.responses.findLast((response) => {
    if (response.type !== "Document") return false;
    const actual = new URL(response.url);
    return (
      actual.origin === expected.origin && actual.pathname === expected.pathname
    );
  });
  assert.ok(documentResponse, "the harness document response was not recorded");
  assert.equal(documentResponse.status, 200);
  const headers = normalizedHeaders(documentResponse.headers);
  assert.equal(headers["cross-origin-embedder-policy"], "require-corp");
  assert.equal(headers["cross-origin-opener-policy"], "same-origin");
  assert.equal(headers["cross-origin-resource-policy"], "same-origin");
  assert.match(headers["content-security-policy"] ?? "", /default-src 'self'/u);
}

function assertNetworkIsolation(cdp, harnessUrl) {
  const expectedOrigin = new URL(harnessUrl).origin;
  const forbidden = [];
  for (const request of cdp.requests) {
    const lower = request.url.toLowerCase();
    const fragment = FORBIDDEN_REQUEST_FRAGMENTS.find((candidate) =>
      lower.includes(candidate),
    );
    if (fragment) forbidden.push(`${request.url} (${fragment})`);

    if (request.url.startsWith("http://") || request.url.startsWith("https://")) {
      const url = new URL(request.url);
      assert.equal(
        url.origin,
        expectedOrigin,
        `harness made a cross-origin request: ${request.url}`,
      );
      assert.ok(
        url.pathname === "/" ||
          url.pathname === "/index.html" ||
          url.pathname.startsWith("/harness/") ||
          url.pathname.startsWith("/runtime/"),
        `harness escaped its server route allowlist: ${request.url}`,
      );
    }
  }
  assert.deepEqual(
    forbidden,
    [],
    `forbidden runtime requests: ${forbidden.join(", ")}`,
  );

  const failedResponses = cdp.responses.filter(
    (response) => response.status < 200 || response.status >= 400,
  );
  assert.deepEqual(
    failedResponses,
    [],
    "harness received an unsuccessful response",
  );
  assert.deepEqual(cdp.networkFailures, [], "harness had a failed network load");
  for (const response of cdp.responses) {
    if (!response.url.startsWith(expectedOrigin)) continue;
    const headers = normalizedHeaders(response.headers);
    assert.equal(
      headers["cross-origin-embedder-policy"],
      "require-corp",
      `missing COEP isolation on ${response.url}`,
    );
    assert.equal(
      headers["cross-origin-opener-policy"],
      "same-origin",
      `missing COOP isolation on ${response.url}`,
    );
    assert.equal(
      headers["cross-origin-resource-policy"],
      "same-origin",
      `missing CORP isolation on ${response.url}`,
    );
  }
}

async function setCarrierFile(cdp, carrierPath) {
  await cdp.send("DOM.enable");
  const document = await cdp.send("DOM.getDocument", { depth: -1, pierce: true });
  const input = await cdp.send("DOM.querySelector", {
    nodeId: document.root.nodeId,
    selector: "#package-file-input",
  });
  assert.ok(input.nodeId > 0, "package file input was not found");
  await cdp.send("DOM.setFileInputFiles", {
    files: [carrierPath],
    nodeId: input.nodeId,
  });

  await delay(150);
  if ((await pageState(cdp)).package === "No package") {
    await cdp.evaluate(`(() => {
      document.querySelector("#package-file-input")
        .dispatchEvent(new Event("change", { bubbles: true }));
      return true;
    })()`);
  }
}

async function terminate(child) {
  if (child.exitCode !== null || child.signalCode !== null) return;
  const exited = new Promise((resolve) => child.once("exit", resolve));
  child.kill("SIGTERM");
  if (
    await Promise.race([
      exited.then(() => true),
      delay(2_000).then(() => false),
    ])
  ) {
    return;
  }
  child.kill("SIGKILL");
  const killed = await Promise.race([
    exited.then(() => true),
    delay(2_000).then(() => false),
  ]);
  if (!killed) throw new Error("headless Chrome did not exit after SIGKILL");
}

async function main() {
  if (process.argv.length < 4 || process.argv.length > 5) {
    throw new Error(usage());
  }
  const harnessUrl = new URL(process.argv[2]).href;
  assert.ok(
    harnessUrl.startsWith("http://127.0.0.1:") ||
      harnessUrl.startsWith("http://localhost:"),
    "the harness integration must target a localhost HTTP server",
  );
  const carrierPath = await requireCarrier(process.argv[3]);
  const chromeExecutable = process.argv[4] ?? "google-chrome";
  const profile = await fs.mkdtemp(
    path.join(os.tmpdir(), "vehicleengine-harness-chrome-"),
  );
  const chrome = spawn(
    chromeExecutable,
    [
      "--headless=new",
      "--disable-gpu",
      "--mute-audio",
      "--no-sandbox",
      "--autoplay-policy=no-user-gesture-required",
      "--remote-debugging-address=127.0.0.1",
      "--remote-debugging-port=0",
      `--user-data-dir=${profile}`,
      "about:blank",
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
    const target = await chromeTarget(port);
    cdp = await CdpSession.connect(target.webSocketDebuggerUrl);
    await Promise.all([
      cdp.send("Runtime.enable"),
      cdp.send("Page.enable"),
      cdp.send("Network.enable"),
      cdp.send("Log.enable"),
      cdp.send("Target.setAutoAttach", {
        autoAttach: true,
        waitForDebuggerOnStart: true,
        flatten: true,
      }),
    ]);
    await cdp.send("Page.addScriptToEvaluateOnNewDocument", {
      source: fakeAudioBootstrap(),
    });
    await cdp.send("Page.navigate", { url: harnessUrl });

    const admitted = await waitUntil(
      () => pageState(cdp),
      (state) =>
        state.readyState === "complete" &&
        state.isolated &&
        state.secure &&
        state.security === "Isolated audio ready",
      "the isolated VEHICLEENGINE harness",
      30_000,
    );
    assert.deepEqual(admitted.errorMessages, []);
    assert.ok(
      admitted.fakeAudio,
      "output-disabled AudioContext was not injected",
    );
    assertIsolatedDocument(cdp, harnessUrl);

    await setCarrierFile(cdp, carrierPath);
    const ready = await waitUntil(
      () => pageState(cdp),
      (state) =>
        state.package === "Package ready" &&
        state.engine !== "—" &&
        !state.playDisabled,
      "complete carrier verification and package admission",
      PACKAGE_TIMEOUT_MS,
    );
    assert.deepEqual(ready.errorMessages, []);
    assert.match(ready.coverage, /RPM$/u);
    assert.match(ready.packageRate, /kHz$/u);
    assert.match(ready.manifoldPressure, /kPa abs$/u);
    const initialMap = ready.manifoldPressure;

    await cdp.evaluate(
      `document.querySelector("#play-button").click(); true`,
    );
    const running = await waitUntil(
      () => pageState(cdp),
      (state) => {
        const bufferedMilliseconds = Number.parseInt(state.buffer, 10);
        const decibels = Number.parseFloat(state.level);
        const meterPercent = Number.parseFloat(state.meterWidth);
        return (
          state.audio === "Audio running" &&
          Number.isFinite(bufferedMilliseconds) &&
          bufferedMilliseconds > 0 &&
          Number.isFinite(decibels) &&
          Number.isFinite(meterPercent) &&
          meterPercent > 0
        );
      },
      "nonzero buffered VEHICLEENGINE audio",
      PLAYBACK_TIMEOUT_MS,
    );
    assert.deepEqual(running.errorMessages, []);
    assert.match(running.deviceRate, /kHz$/u);
    assert.equal(running.fakeAudio.contexts, 1);
    assert.equal(running.fakeAudio.resumes, 1);
    assert.equal(running.fakeAudio.nodes, 1);
    assert.equal(running.fakeAudio.connections, 1);
    assert.deepEqual(running.fakeAudio.modules, [
      "/harness/audio-worklet.js",
    ]);

    const controlled = await cdp.evaluate(`(() => {
      const rpm = document.querySelector("#rpm-input");
      const throttle = document.querySelector("#throttle-input");
      const load = document.querySelector("#load-input");
      const minimum = Number(rpm.min);
      const maximum = Number(rpm.max);
      rpm.value = String(Math.round(minimum + (maximum - minimum) * 0.63));
      throttle.value = "73";
      load.value = "81";
      for (const input of [rpm, throttle, load]) {
        input.dispatchEvent(new Event("input", { bubbles: true }));
      }
      return { rpm: rpm.value, throttle: throttle.value, load: load.value };
    })()`);
    const updated = await waitUntil(
      () => pageState(cdp),
      (state) =>
        state.rpmInput === controlled.rpm &&
        state.throttleInput === "73" &&
        state.loadInput === "81" &&
        state.rpm.includes("RPM") &&
        state.throttle === "73%" &&
        state.load === "81%" &&
        state.manifoldPressure.endsWith("kPa abs") &&
        state.manifoldPressure !== initialMap,
      "RPM, throttle, load, and derived MAP control update",
      30_000,
    );
    assert.deepEqual(updated.errorMessages, []);

    await cdp.settleWorkers();
    assertNetworkIsolation(cdp, harnessUrl);
    assert.deepEqual(cdp.exceptions, []);
    assert.deepEqual(cdp.consoleErrors, []);
    assert.deepEqual(cdp.logErrors, []);
    assert.deepEqual(cdp.targetErrors, []);

    process.stdout.write(
      JSON.stringify({
        engine: updated.engine,
        coverage: updated.coverage,
        packageSampleRate: updated.packageRate,
        outputSampleRate: updated.deviceRate,
        bufferedAudio: updated.buffer,
        outputLevel: updated.level,
        rpm: updated.rpm,
        throttle: updated.throttle,
        load: updated.load,
        manifoldPressure: updated.manifoldPressure,
        networkResponses: cdp.responses.map(({ url, status, type }) => ({
          url,
          status,
          type,
        })),
      }) + "\n",
    );
  } catch (error) {
    if (chrome.exitCode !== null && chrome.exitCode !== 0) {
      error.message += `\nChrome exited ${chrome.exitCode}: ${chromeStderr}`;
    }
    throw error;
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
  process.stderr.write(`VEHICLEENGINE harness integration failure: ${error.stack}\n`);
  process.exitCode = 1;
});
