import assert from "node:assert/strict";
import fs from "node:fs/promises";
import os from "node:os";
import path from "node:path";
import { spawn } from "node:child_process";
import { setTimeout as delay } from "node:timers/promises";

const PRESET_ID = "bmw-m52tub28-cleanroom-lifecycle-ab";
const SCENARIO_ID =
  "bmw-m52tub28-cleanroom-interactive-lifecycle-0rpm";

function usage() {
  return (
    "usage: node web/tests/integration/lifecycle-baked-routing.integration.mjs " +
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
      if (predicate(latest)) return latest;
    } catch (error) {
      latestError = error;
    }
    await delay(50);
  }
  throw new Error(
    `timed out waiting for ${description}: ${
      latestError?.message ?? JSON.stringify(latest)
    }`,
  );
}

class CdpSession {
  #socket;
  #nextId = 1;
  #pending = new Map();
  pageErrors = [];
  networkFailures = [];
  badResponses = [];

  static async connect(url) {
    const socket = new WebSocket(url);
    await new Promise((resolve, reject) => {
      socket.addEventListener("open", resolve, { once: true });
      socket.addEventListener("error", reject, { once: true });
    });
    return new CdpSession(socket);
  }

  constructor(socket) {
    this.#socket = socket;
    socket.addEventListener("message", (event) => {
      const message = JSON.parse(String(event.data));
      if (message.id !== undefined) {
        const pending = this.#pending.get(message.id);
        if (!pending) return;
        this.#pending.delete(message.id);
        if (message.error) pending.reject(new Error(message.error.message));
        else pending.resolve(message.result);
        return;
      }
      if (message.method === "Runtime.exceptionThrown") {
        this.pageErrors.push(
          message.params.exceptionDetails?.exception?.description ??
            message.params.exceptionDetails?.text ??
            "page exception",
        );
      }
      if (
        message.method === "Runtime.consoleAPICalled" &&
        message.params.type === "error"
      ) {
        this.pageErrors.push(
          message.params.args
            .map((argument) => argument.value ?? argument.description ?? "")
            .join(" "),
        );
      }
      if (message.method === "Network.loadingFailed") {
        const failure = message.params.errorText ?? "network failure";
        if (failure !== "net::ERR_ABORTED") this.networkFailures.push(failure);
      }
      if (
        message.method === "Network.responseReceived" &&
        message.params.response.status >= 400 &&
        !message.params.response.url.endsWith("/favicon.ico")
      ) {
        this.badResponses.push({
          status: message.params.response.status,
          url: message.params.response.url,
        });
      }
    });
  }

  send(method, params = {}) {
    const id = this.#nextId++;
    return new Promise((resolve, reject) => {
      this.#pending.set(id, { resolve, reject });
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

const mutedProbe = String.raw`(() => {
  const probe = globalThis.__ESO_LIFECYCLE_B_ROUTING = {
    explicitlyMuted: true,
    audioContextCount: 0,
    workletNodeCount: 0,
    worker: null,
    workerMessages: [],
    workerErrors: [],
    telemetry: null,
    lifecycle: null,
    ring: null,
    drainTimer: null,
  };

  const NativeWorker = globalThis.Worker;
  globalThis.Worker = new Proxy(NativeWorker, {
    construct(Target, arguments_) {
      const worker = new Target(...arguments_);
      probe.worker = worker;
      worker.addEventListener("message", (event) => {
        const message = event.data;
        if (message?.type === "telemetry") {
          const frame = message.frames?.at?.(-1) ?? null;
          if (frame) {
            probe.telemetry = {
              rpm: frame.engineSpeedRpm,
              throttle: frame.requestedThrottle01,
              ignition: frame.ignitionEnabled,
              starter: frame.starterEnabled,
              gasTorque:
                frame.torque?.instantaneousIndicatedGas?.availability === 1
                  ? frame.torque.instantaneousIndicatedGas.valueNm
                  : null,
            };
          }
        } else if (message?.type === "audio-atlas-status") {
          probe.lifecycle = message.diagnostics?.lifecycle ?? null;
        } else if (message?.type === "error") {
          probe.workerErrors.push(
            message.error?.message ?? message.message ?? "worker error",
          );
        }
        if (
          ["audio-atlas-status", "comparison-mode", "error"].includes(
            message?.type,
          )
        ) {
          probe.workerMessages.push({
            type: message.type,
            mode: message.mode ?? message.comparisonMode ?? null,
            status: message.status ?? null,
            forced: message.forced ?? null,
            reason: message.reason ?? null,
          });
        }
      });
      return worker;
    },
  });

  class MutedAudioContext {
    constructor() {
      ++probe.audioContextCount;
      this.sampleRate = 48_000;
      this.state = "suspended";
      this.destination = Object.freeze({ explicitlyMuted: true });
      this.audioWorklet = { addModule: async () => {} };
    }
    async resume() { this.state = "running"; }
    async close() { this.state = "closed"; }
  }

  probe.drain = () => {
    if (!probe.ring) return;
    const ring = probe.ring;
    const header = new Int32Array(
      ring.sharedBuffer,
      0,
      ring.headerBytes / Int32Array.BYTES_PER_ELEMENT,
    );
    const available = Math.max(
      0,
      Math.min(ring.capacityFrames, Atomics.load(header, ring.indices.availableFrames)),
    );
    const read = Atomics.load(header, ring.indices.readFrame);
    Atomics.store(
      header,
      ring.indices.readFrame,
      (read + available) % ring.capacityFrames,
    );
    Atomics.sub(header, ring.indices.availableFrames, available);
    ring.maximumUnderrunFrames = Math.max(
      ring.maximumUnderrunFrames,
      Atomics.load(header, ring.indices.underrunFrames),
    );
    ring.maximumUnderrunEvents = Math.max(
      ring.maximumUnderrunEvents,
      Atomics.load(header, ring.indices.underrunEvents),
    );
  };

  class MutedWorkletNode {
    constructor(_context, _name, options) {
      ++probe.workletNodeCount;
      const ring = options.processorOptions;
      probe.ring = {
        sharedBuffer: ring.sharedBuffer,
        headerBytes: ring.headerBytes,
        capacityFrames: ring.capacityFrames,
        indices: ring.indices,
        maximumUnderrunFrames: 0,
        maximumUnderrunEvents: 0,
      };
      clearInterval(probe.drainTimer);
      probe.drainTimer = setInterval(probe.drain, 4);
      this.port = { postMessage() {} };
      this.onprocessorerror = null;
    }
    connect() { return this; }
    disconnect() {}
  }

  Object.defineProperty(globalThis, "AudioContext", {
    configurable: true,
    value: MutedAudioContext,
  });
  Object.defineProperty(globalThis, "webkitAudioContext", {
    configurable: true,
    value: MutedAudioContext,
  });
  Object.defineProperty(globalThis, "AudioWorkletNode", {
    configurable: true,
    value: MutedWorkletNode,
  });

  probe.requestStatus = () => probe.worker?.postMessage({
    type: "status",
    requestId: "lifecycle-b-routing-" + Date.now(),
  });
})();`;

async function pageState(cdp) {
  return cdp.evaluate(`(() => {
    const probe = globalThis.__ESO_LIFECYCLE_B_ROUTING;
    probe?.drain();
    const text = (selector) =>
      document.querySelector(selector)?.textContent?.trim() ?? "";
    return {
      readyState: document.readyState,
      isolated: globalThis.crossOriginIsolated === true,
      explicitlyMuted: probe?.explicitlyMuted === true,
      packageId: document.querySelector("#package-select")?.value ?? "",
      scenario: document.querySelector("#scenario-editor")?.value ?? "",
      build: text("#build-status"),
      session: text("#session-state"),
      bakedStatus: text("#baked-audition-status"),
      bakedDetail: text("#baked-audition-detail"),
      selectedMode:
        document.querySelector(".comparison-mode.is-selected")?.dataset
          ?.comparisonMode ?? null,
      buildDisabled: document.querySelector("#build-button")?.disabled ?? true,
      loadDisabled:
        document.querySelector("#load-package-button")?.disabled ?? true,
      startDisabled: document.querySelector("#start-button")?.disabled ?? true,
      bakedDisabled:
        document.querySelector('[data-comparison-mode="baked-b"]')?.disabled ??
        true,
      telemetry: probe?.telemetry ?? null,
      lifecycle: probe?.lifecycle ?? null,
      audioContextCount: probe?.audioContextCount ?? null,
      workletNodeCount: probe?.workletNodeCount ?? null,
      ring: probe?.ring
        ? {
            maximumUnderrunFrames: probe.ring.maximumUnderrunFrames,
            maximumUnderrunEvents: probe.ring.maximumUnderrunEvents,
          }
        : null,
      workerErrors: [...(probe?.workerErrors ?? [])],
    };
  })()`);
}

async function setThrottle(cdp, percent) {
  await cdp.evaluate(`(() => {
    const throttle = document.querySelector("#throttle-input");
    throttle.value = ${JSON.stringify(String(percent))};
    throttle.dispatchEvent(new Event("input", { bubbles: true }));
    return true;
  })()`);
}

async function holdStarter(cdp, held) {
  await cdp.evaluate(`(() => {
    document.querySelector("#starter-button").dispatchEvent(
      new KeyboardEvent(${JSON.stringify(held ? "keydown" : "keyup")}, {
        key: " ",
        bubbles: true,
        cancelable: true,
        repeat: false,
      }),
    );
    return true;
  })()`);
}

async function toggleIgnition(cdp) {
  await cdp.evaluate(
    'document.querySelector("#ignition-button").click(); true',
  );
}

async function requestLifecycleStatus(cdp) {
  await cdp.evaluate(
    "globalThis.__ESO_LIFECYCLE_B_ROUTING.requestStatus(); true",
  );
  await delay(40);
}

async function waitForStoppedLifecycle(cdp, startupCount, shutdownCount) {
  return waitUntil(
    async () => {
      await requestLifecycleStatus(cdp);
      return pageState(cdp);
    },
    (state) =>
      state.lifecycle?.startupCount >= startupCount &&
      state.lifecycle?.shutdownCount >= shutdownCount &&
      state.lifecycle?.outputMode === "stopped" &&
      state.lifecycle?.activeEvent === null,
    `lifecycle stop ${shutdownCount}`,
    25_000,
  );
}

async function runStartCycle(cdp, { throttle, starterFirst, ordinal }) {
  await setThrottle(cdp, throttle);
  await waitUntil(
    () => pageState(cdp),
    (state) =>
      Math.abs((state.telemetry?.throttle ?? -1) - throttle / 100) < 0.005,
    `${throttle}% throttle telemetry`,
  );

  if (starterFirst) {
    await holdStarter(cdp, true);
    await waitUntil(
      () => pageState(cdp),
      (state) =>
        state.telemetry?.starter === true &&
        state.telemetry?.ignition === false &&
        state.telemetry?.rpm > 180,
      `starter-first crank ${ordinal}`,
    );
    await toggleIgnition(cdp);
  } else {
    await toggleIgnition(cdp);
    await waitUntil(
      () => pageState(cdp),
      (state) =>
        state.telemetry?.ignition === true &&
        state.telemetry?.starter === false,
      `ignition-first arming ${ordinal}`,
    );
    await holdStarter(cdp, true);
  }

  const caught = await waitUntil(
    () => pageState(cdp),
    (state) =>
      state.telemetry?.starter === true &&
      state.telemetry?.ignition === true &&
      state.telemetry?.rpm > 900 &&
      (state.telemetry?.gasTorque ?? -Infinity) > 0,
    `combustion catch ${ordinal}`,
    25_000,
  );
  assert.equal(caught.selectedMode, "baked-b");

  await delay(250);
  await holdStarter(cdp, false);
  await waitUntil(
    () => pageState(cdp),
    (state) => state.telemetry?.starter === false && state.telemetry?.rpm > 700,
    `starter release ${ordinal}`,
  );
  await delay(500);
  await toggleIgnition(cdp);
  await waitUntil(
    () => pageState(cdp),
    (state) =>
      state.telemetry?.ignition === false && state.telemetry?.rpm < 1,
    `physical shutdown ${ordinal}`,
    25_000,
  );
  return waitForStoppedLifecycle(cdp, ordinal, ordinal);
}

async function chromePage(port) {
  return waitUntil(
    async () => {
      const response = await fetch(`http://127.0.0.1:${port}/json/list`);
      return (await response.json()).find((target) => target.type === "page");
    },
    Boolean,
    "Chrome page target",
  );
}

async function terminate(child) {
  if (child.exitCode !== null || child.signalCode !== null) return;
  const exited = new Promise((resolve) => child.once("exit", resolve));
  child.kill("SIGTERM");
  if (await Promise.race([exited.then(() => true), delay(2_000).then(() => false)])) {
    return;
  }
  child.kill("SIGKILL");
  await exited;
}

async function main() {
  if (process.argv.length < 3 || process.argv.length > 4) {
    throw new Error(usage());
  }
  const workbenchUrl = new URL(process.argv[2]).href;
  const chromeExecutable = process.argv[3] ?? "google-chrome";
  const profile = await fs.mkdtemp(
    path.join(os.tmpdir(), "engine-sim-offline-lifecycle-b-"),
  );
  const chrome = spawn(
    chromeExecutable,
    [
      "--headless=new",
      "--disable-gpu",
      "--no-sandbox",
      "--mute-audio",
      "--disable-audio-output",
      "--autoplay-policy=no-user-gesture-required",
      "--remote-debugging-address=127.0.0.1",
      "--remote-debugging-port=0",
      `--user-data-dir=${profile}`,
      "about:blank",
    ],
    { stdio: ["ignore", "ignore", "ignore"] },
  );

  let cdp;
  try {
    const portText = await waitUntil(
      () =>
        fs
          .readFile(path.join(profile, "DevToolsActivePort"), "utf8")
          .catch(() => ""),
      (text) => /^\d+/u.test(text),
      "Chrome DevTools port",
    );
    const port = Number(portText.split(/\r?\n/u)[0]);
    const target = await chromePage(port);
    cdp = await CdpSession.connect(target.webSocketDebuggerUrl);
    await Promise.all([
      cdp.send("Runtime.enable"),
      cdp.send("Page.enable"),
      cdp.send("Network.enable"),
    ]);
    await cdp.send("Page.addScriptToEvaluateOnNewDocument", {
      source: mutedProbe,
    });
    await cdp.send("Page.navigate", { url: workbenchUrl });

    await waitUntil(
      () => pageState(cdp),
      (state) =>
        state.readyState === "complete" &&
        state.isolated &&
        state.explicitlyMuted &&
        !state.loadDisabled &&
        !state.buildDisabled,
      "isolated muted workbench",
      30_000,
    );
    await cdp.evaluate(`(() => {
      const select = document.querySelector("#package-select");
      select.value = ${JSON.stringify(PRESET_ID)};
      select.dispatchEvent(new Event("change", { bubbles: true }));
      document.querySelector("#load-package-button").click();
      return true;
    })()`);
    await waitUntil(
      () => pageState(cdp),
      (state) =>
        state.packageId === PRESET_ID &&
        state.scenario.includes(SCENARIO_ID) &&
        !state.loadDisabled,
      "BMW lifecycle preset documents",
    );
    await cdp.evaluate(
      'document.querySelector("#build-button").click(); true',
    );
    await waitUntil(
      () => pageState(cdp),
      (state) =>
        state.build === "Build admitted" &&
        state.bakedStatus === "Baked active" &&
        !state.startDisabled &&
        !state.bakedDisabled,
      "admitted lifecycle B package",
      45_000,
    );
    await cdp.evaluate(
      'document.querySelector(\'[data-comparison-mode="baked-b"]\').click(); true',
    );
    await waitUntil(
      () => pageState(cdp),
      (state) => state.selectedMode === "baked-b",
      "Baked B selection",
    );
    const bSelectionOrdinal = await cdp.evaluate(
      "globalThis.__ESO_LIFECYCLE_B_ROUTING.workerMessages.length",
    );
    await cdp.evaluate(
      'document.querySelector("#start-button").click(); true',
    );
    await waitUntil(
      () => pageState(cdp),
      (state) =>
        state.session === "Running" &&
        state.selectedMode === "baked-b" &&
        state.telemetry !== null &&
        state.ring !== null,
      "running muted lifecycle session",
      45_000,
    );

    await runStartCycle(cdp, {
      throttle: 17,
      starterFirst: false,
      ordinal: 1,
    });
    await runStartCycle(cdp, {
      throttle: 63,
      starterFirst: true,
      ordinal: 2,
    });
    await requestLifecycleStatus(cdp);

    const final = await pageState(cdp);
    const messagesAfterB = await cdp.evaluate(
      `globalThis.__ESO_LIFECYCLE_B_ROUTING.workerMessages.slice(${bSelectionOrdinal})`,
    );
    const sourceFallbacks = messagesAfterB.filter(
      (message) =>
        message.type === "comparison-mode" && message.mode === "source-a",
    );

    assert.equal(final.selectedMode, "baked-b");
    assert.equal(sourceFallbacks.length, 0, "B routing fell back to source A");
    assert.ok(final.lifecycle?.startupCount >= 2);
    assert.ok(final.lifecycle?.shutdownCount >= 2);
    assert.equal(final.lifecycle?.outputMode, "stopped");
    assert.equal(final.lifecycle?.activeEvent, null);
    assert.equal(final.ring?.maximumUnderrunFrames, 0);
    assert.equal(final.ring?.maximumUnderrunEvents, 0);
    assert.equal(final.audioContextCount, 1);
    assert.equal(final.workletNodeCount, 1);
    assert.deepEqual(final.workerErrors, []);
    assert.deepEqual(cdp.pageErrors, []);
    assert.deepEqual(cdp.networkFailures, []);
    assert.deepEqual(cdp.badResponses, []);

    process.stdout.write(
      `${JSON.stringify({
        presetId: PRESET_ID,
        selectedMode: final.selectedMode,
        throttlePercents: [17, 63],
        startupOrders: ["ignition-first", "starter-first"],
        startupCount: final.lifecycle.startupCount,
        shutdownCount: final.lifecycle.shutdownCount,
        underrunFrames: final.ring.maximumUnderrunFrames,
        underrunEvents: final.ring.maximumUnderrunEvents,
        sourceFallbacks: sourceFallbacks.length,
        explicitlyMuted: final.explicitlyMuted,
      })}\n`,
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
  process.stderr.write(`lifecycle B routing failure: ${error.stack}\n`);
  process.exitCode = 1;
});
