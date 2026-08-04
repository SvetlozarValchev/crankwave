import assert from "node:assert/strict";
import fs from "node:fs/promises";
import os from "node:os";
import path from "node:path";
import { spawn } from "node:child_process";
import { setTimeout as delay } from "node:timers/promises";

const EXPECTED_WAV_BYTES = 4_224_056;
const EXPECTED_WAV_SHA256 =
  "1c67f7d7075f5647960ff635a550aa9004e9b11eb4585f3257d3389c5c0d1b93";
const FINITE_EXECUTION_KIND = "1";
const OPEN_ENDED_EXECUTION_KIND = "2";

function repositoryPackageExpectations(
  packagePrefix,
  engineId,
  idleRpm,
  dynoRange,
  freeRevScenarioId = `${engineId}-warm-running-free-rev-${idleRpm}rpm`,
) {
  return [
    {
      packageId: `${packagePrefix}-free-rev`,
      engineId,
      scenarioId: freeRevScenarioId,
      executionKind: OPEN_ENDED_EXECUTION_KIND,
    },
    {
      packageId: `${packagePrefix}-held-idle`,
      engineId,
      scenarioId: `${engineId}-held-idle-region-${idleRpm}rpm`,
      executionKind: FINITE_EXECUTION_KIND,
    },
    {
      packageId: `${packagePrefix}-dyno`,
      engineId,
      scenarioId: `${engineId}-inertial-dyno-${dynoRange}rpm`,
      executionKind: FINITE_EXECUTION_KIND,
    },
  ];
}

const NEW_REPOSITORY_PACKAGES = Object.freeze([
  ...repositoryPackageExpectations(
    "sequoia-3ur-fe",
    "sequoia-3ur-fe-cleanroom",
    650,
    "650-6000",
  ),
  ...repositoryPackageExpectations(
    "harley-evolution-1340",
    "harley-evolution-1340-cleanroom",
    800,
    "800-5000",
  ),
  {
    packageId: "harley-shovelhead-free-rev",
    engineId: "shovelhead-bank-local-heads",
    scenarioId: "shovelhead-bank-local-heads-warm-running-free-rev-1000rpm",
    executionKind: OPEN_ENDED_EXECUTION_KIND,
  },
  {
    packageId: "harley-shovelhead-source-pull",
    engineId: "shovelhead-bank-local-heads",
    scenarioId: "shovelhead-bank-local-heads-source-pull-1000-5000rpm",
    executionKind: FINITE_EXECUTION_KIND,
  },
  ...repositoryPackageExpectations(
    "bmw-m52tub28",
    "bmw-m52tub28-cleanroom",
    700,
    "700-6500",
    "bmw-m52tub28-cleanroom-warm-running-free-rev-700rpm-10khz-preview",
  ),
  {
    packageId: "bmw-m52tub28-cold-start",
    engineId: "bmw-m52tub28-cleanroom",
    scenarioId: "bmw-m52tub28-cleanroom-cold-start-crank-catch-0rpm",
    executionKind: FINITE_EXECUTION_KIND,
  },
  {
    packageId: "bmw-m52tub28-canonical-crank",
    engineId: "bmw-m52tub28-cleanroom",
    scenarioId: "bmw-m52tub28-cleanroom-canonical-crank-only-0rpm",
    executionKind: FINITE_EXECUTION_KIND,
  },
  {
    packageId: "bmw-m52tub28-held-dyno-pull-lift",
    engineId: "bmw-m52tub28-cleanroom",
    scenarioId:
      "bmw-m52tub28-cleanroom-held-dyno-pull-lift-1500-6500rpm",
    executionKind: FINITE_EXECUTION_KIND,
  },
  {
    packageId: "bmw-m52tub28-canonical-load-cycle",
    engineId: "bmw-m52tub28-cleanroom",
    scenarioId:
      "bmw-m52tub28-cleanroom-canonical-loaded-rise-part-load-coast-1500-4500rpm",
    executionKind: FINITE_EXECUTION_KIND,
  },
  {
    packageId: "bmw-m52tub28-canonical-shutdown",
    engineId: "bmw-m52tub28-cleanroom",
    scenarioId:
      "bmw-m52tub28-cleanroom-canonical-key-off-shutdown-700rpm",
    executionKind: FINITE_EXECUTION_KIND,
  },
  {
    packageId: "bmw-m52tub28-launch-first-second",
    engineId: "bmw-m52tub28-cleanroom",
    scenarioId: "bmw-m52tub28-cleanroom-free-vehicle-launch-first-second",
    executionKind: FINITE_EXECUTION_KIND,
  },
  {
    packageId: "bmw-m52tub28-fifth-gear-pull-lift",
    engineId: "bmw-m52tub28-cleanroom",
    scenarioId:
      "bmw-m52tub28-cleanroom-free-vehicle-fifth-gear-pull-lift-1500rpm",
    executionKind: FINITE_EXECUTION_KIND,
  },
  {
    packageId: "honda-b18c5-held-below-vtec",
    engineId: "honda-b18c5-cleanroom",
    scenarioId: "honda-b18c5-cleanroom-held-below-vtec-5400rpm",
    executionKind: FINITE_EXECUTION_KIND,
  },
  {
    packageId: "honda-b18c5-dyno",
    engineId: "honda-b18c5-cleanroom",
    scenarioId: "honda-b18c5-cleanroom-inertial-dyno-5000-8000rpm",
    executionKind: FINITE_EXECUTION_KIND,
  },
  {
    packageId: "honda-b18c5-held-above-vtec",
    engineId: "honda-b18c5-cleanroom",
    scenarioId: "honda-b18c5-cleanroom-held-above-vtec-7000rpm",
    executionKind: FINITE_EXECUTION_KIND,
  },
  {
    packageId: "kohler-ch750-governed-load-step",
    engineId: "kohler-ch750-cleanroom",
    scenarioId: "kohler-ch750-cleanroom-governed-load-step-2740rpm",
    executionKind: FINITE_EXECUTION_KIND,
    throttlePresentation: {
      label: "Governor setpoint",
      minimum: "1,600 rpm",
      maximum: "3,500 rpm",
      authoredValue: "2,740 rpm",
    },
  },
]);

function usage() {
  return (
    "usage: node web/tests/integration/workbench-ui-smoke.mjs " +
    "<workbench-url> [chrome-executable]"
  );
}

function uiDurationSeconds(value) {
  const match = /^(\d+):(\d+(?:\.\d+)?)$/u.exec(value);
  return match ? Number(match[1]) * 60 + Number(match[2]) : Number.NaN;
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
    const json = (selector) => {
      try {
        return JSON.parse(document.querySelector(selector)?.value ?? "");
      } catch {
        return null;
      }
    };
    const selected = document.querySelector("#bus-select");
    const executionKindSelector =
      document.querySelector("#execution-kind-select");
    const executionKindRect = executionKindSelector?.getBoundingClientRect();
    const engineDocument = json("#engine-editor");
    const scenarioDocument = json("#scenario-editor");
    return {
      readyState: document.readyState,
      isolated: globalThis.crossOriginIsolated === true,
      worker: text("#worker-status"),
      build: text("#build-status"),
      session: text("#session-state"),
      sessionTitle: text("#session-title"),
      sessionSubtitle: text("#session-subtitle"),
      motionMode: text("#motion-mode-badge"),
      startLabel: text("#start-button-label"),
      stopLabel: text("#stop-button-label"),
      restartLabel: text("#restart-button-label"),
      diagnostics: text("#diagnostics-list"),
      rpm: text("#rpm-value"),
      elapsed: text("#elapsed-value"),
      throttleLabel: text("#throttle-label"),
      throttle: document.querySelector("#throttle-output")?.value ?? "",
      throttleMinimumLabel: text("#throttle-minimum-label"),
      throttleMaximumLabel: text("#throttle-maximum-label"),
      exportLabel: text("#export-button"),
      bakedAuditionStatus: text("#baked-audition-status"),
      bakedAuditionDetail: text("#baked-audition-detail"),
      comparisonMode:
        document.querySelector(".comparison-mode.is-selected")?.dataset
          ?.comparisonMode ?? null,
      bakedComparisonDisabled:
        document.querySelector('[data-comparison-mode="baked-b"]')?.disabled ??
        true,
      underruns: text("#underrun-value"),
      busCount: selected?.options?.length ?? 0,
      selectedBus: selected?.value ?? "",
      selectedPackage:
        document.querySelector("#package-select")?.value ?? "",
      executionKindInput:
        executionKindSelector?.value ?? "",
      executionKindVisible:
        executionKindRect !== undefined &&
        executionKindRect.width > 0 &&
        executionKindRect.left >= 0 &&
        executionKindRect.right <= window.innerWidth,
      authoredEngineId: engineDocument?.engine?.identity?.id ?? "",
      authoredScenarioId: scenarioDocument?.id ?? "",
      buildDisabled: document.querySelector("#build-button")?.disabled ?? true,
      startDisabled: document.querySelector("#start-button")?.disabled ?? true,
      starterDisabled:
        document.querySelector("#starter-button")?.disabled ?? true,
      heldDynoControlsHidden:
        document.querySelector("#held-dyno-controls")?.hidden ?? true,
      dynoTargetDisabled:
        document.querySelector("#held-dyno-target-rpm-input")?.disabled ?? true,
      dynoTargetInput:
        document.querySelector("#held-dyno-target-rpm-input")?.value ?? "",
      dynoTargetTelemetry: text("#dyno-target-value"),
      dynoLimitsTelemetry: text("#dyno-limits-value"),
      dynoDisposition: text("#dyno-disposition-value"),
      vehicleControlsHidden:
        document.querySelector("#free-vehicle-controls")?.hidden ?? true,
      vehicleGearDisabled:
        document.querySelector("#vehicle-gear-select")?.disabled ?? true,
      vehicleGearOptions:
        document.querySelector("#vehicle-gear-select")?.options?.length ?? 0,
      vehicleGearInput:
        document.querySelector("#vehicle-gear-select")?.value ?? "",
      vehicleClutchInput:
        document.querySelector("#vehicle-clutch-input")?.value ?? "",
      vehicleBrakeInput:
        document.querySelector("#vehicle-brake-input")?.value ?? "",
      vehicleGearTelemetry: text("#vehicle-gear-value"),
      vehicleClutchTelemetry: text("#vehicle-clutch-value"),
      vehicleSpeedTelemetry: text("#vehicle-speed-value"),
      restartDisabled:
        document.querySelector("#restart-button")?.disabled ?? true
    };
  })()`);
}

async function verifyRepositoryPackage(cdp, expectation) {
  const packageId = JSON.stringify(expectation.packageId);
  await cdp.evaluate(`(() => {
    const packages = document.querySelector("#package-select");
    packages.value = ${packageId};
    packages.dispatchEvent(new Event("change", { bubbles: true }));
    document.querySelector("#load-package-button").click();
    return true;
  })()`);
  const loaded = await waitUntil(
    () => pageState(cdp),
    (state) =>
      state.selectedPackage === expectation.packageId &&
      state.authoredEngineId === expectation.engineId &&
      state.authoredScenarioId === expectation.scenarioId &&
      state.executionKindInput === expectation.executionKind &&
      !state.buildDisabled,
    `the fetched ${expectation.packageId} repository package`,
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
      state.sessionTitle === expectation.engineId &&
      state.sessionSubtitle.startsWith(`${expectation.scenarioId} ·`) &&
      !state.buildDisabled,
    `the compiled ${expectation.packageId} repository package`,
    30_000,
  );
  assert.equal(built.selectedPackage, expectation.packageId);
  assert.equal(built.authoredEngineId, expectation.engineId);
  assert.equal(built.authoredScenarioId, expectation.scenarioId);
  assert.equal(built.executionKindInput, expectation.executionKind);
  assert.match(
    built.sessionSubtitle,
    expectation.executionKind === OPEN_ENDED_EXECUTION_KIND
      ? /continuous bench/u
      : /finite procedure/u,
  );
  if (expectation.throttlePresentation) {
    assert.equal(
      built.throttleLabel,
      expectation.throttlePresentation.label,
    );
    assert.equal(
      built.throttleMinimumLabel,
      expectation.throttlePresentation.minimum,
    );
    assert.equal(
      built.throttleMaximumLabel,
      expectation.throttlePresentation.maximum,
    );
    assert.equal(
      built.throttle,
      expectation.throttlePresentation.authoredValue,
    );
  }
  assert.match(built.diagnostics, /No diagnostics reported/u);
  return expectation.packageId;
}

async function selectExecutionKindAndRebuild(cdp, executionKind) {
  await cdp.evaluate(`(() => {
    const selector = document.querySelector("#execution-kind-select");
    selector.value = ${JSON.stringify(executionKind)};
    selector.dispatchEvent(new Event("change", { bubbles: true }));
    document.querySelector("#build-button").click();
    return true;
  })()`);
  return waitUntil(
    () => pageState(cdp),
    (state) =>
      state.build === "Build admitted" &&
      state.session === "Ready" &&
      state.executionKindInput === executionKind &&
      (executionKind === OPEN_ENDED_EXECUTION_KIND
        ? /continuous bench/u.test(state.sessionSubtitle)
        : /finite procedure/u.test(state.sessionSubtitle)) &&
      !state.buildDisabled,
    executionKind === OPEN_ENDED_EXECUTION_KIND
      ? "the rebuilt interactive bench"
      : "the rebuilt authored finite procedure",
    30_000,
  );
}

async function verifySourceOnlyAudition(cdp) {
  const ready = await pageState(cdp);
  assert.equal(ready.session, "Ready");
  assert.equal(ready.bakedAuditionStatus, "Baked unavailable");
  assert.equal(ready.comparisonMode, "source-a");
  assert.equal(ready.bakedComparisonDisabled, true);
  assert.equal(ready.startDisabled, false);
  assert.equal(
    ready.bakedAuditionDetail,
    "Baked B is unavailable pending clean-room atlas integration. Source A remains live.",
  );

  await cdp.evaluate(
    `document.querySelector("#start-button").click(); true`,
  );
  await waitUntil(
    () => pageState(cdp),
    (state) =>
      state.session === "Running" &&
      state.rpm !== "—" &&
      state.bakedAuditionStatus === "Baked unavailable" &&
      state.comparisonMode === "source-a" &&
      state.bakedComparisonDisabled,
    "the primed BMW Source A session",
    30_000,
  );
  const source = await waitUntil(
    () => pageState(cdp),
    (state) =>
      state.session === "Running" &&
      uiDurationSeconds(state.elapsed) >= 1 &&
      state.comparisonMode === "source-a",
    "one second of Source A playback",
    30_000,
  );
  assert.equal(source.underruns, "0");
  await cdp.evaluate(
    `document.querySelector("#stop-button").click(); true`,
  );
  await waitUntil(
    () => pageState(cdp),
    (state) => state.session === "Paused",
    "paused Source A session",
  );
  return {
    bakedAudition: source.bakedAuditionDetail,
    sharedOutputUnderruns: Number(source.underruns),
  };
}

async function verifyFiniteProcedure(cdp) {
  const ready = await pageState(cdp);
  assert.equal(ready.executionKindInput, FINITE_EXECUTION_KIND);
  assert.match(ready.sessionSubtitle, /finite procedure/u);
  assert.equal(ready.startLabel, "Run procedure");

  await cdp.evaluate(
    `document.querySelector("#start-button").click(); true`,
  );
  const completed = await waitUntil(
    () => pageState(cdp),
    (state) =>
      state.session === "Procedure complete" &&
      state.restartLabel === "Run again" &&
      !state.restartDisabled,
    "the named finite procedure completion",
    30_000,
  );
  assert.equal(completed.executionKindInput, FINITE_EXECUTION_KIND);
}

async function verifyInteractiveFreeEngine(cdp) {
  const ready = await pageState(cdp);
  assert.equal(ready.executionKindInput, OPEN_ENDED_EXECUTION_KIND);
  assert.equal(ready.motionMode, "Free Engine");
  assert.equal(ready.startLabel, "Start");

  await cdp.evaluate(
    `document.querySelector("#start-button").click(); true`,
  );
  await waitUntil(
    () => pageState(cdp),
    (state) =>
      state.session === "Running" &&
      state.rpm !== "—" &&
      !state.starterDisabled,
    "the interactive Shovelhead FreeEngine bench",
    20_000,
  );
  await cdp.evaluate(`(() => {
    const throttle = document.querySelector("#throttle-input");
    throttle.value = "50";
    throttle.dispatchEvent(new Event("input", { bubbles: true }));
    return true;
  })()`);
  const controlled = await waitUntil(
    () => pageState(cdp),
    (state) =>
      state.session === "Running" &&
      state.throttle === "50%" &&
      state.rpm !== "—",
    "the interactive Shovelhead throttle command",
  );
  assert.match(controlled.diagnostics, /No diagnostics reported/u);

  await cdp.evaluate(
    `document.querySelector("#stop-button").click(); true`,
  );
  await waitUntil(
    () => pageState(cdp),
    (state) => state.session === "Paused",
    "the paused interactive Shovelhead bench",
  );
}

async function verifyHeldDynoBench(cdp) {
  const ready = await pageState(cdp);
  assert.equal(ready.motionMode, "Held Dyno");
  assert.equal(ready.heldDynoControlsHidden, false);
  assert.equal(ready.vehicleControlsHidden, true);
  assert.equal(ready.dynoTargetDisabled, true);
  assert.equal(ready.startLabel, "Start");
  assert.equal(ready.stopLabel, "Stop");
  assert.equal(ready.restartDisabled, true);

  await cdp.evaluate(
    `document.querySelector("#start-button").click(); true`,
  );
  await waitUntil(
    () => pageState(cdp),
    (state) =>
      state.session === "Running" &&
      !state.dynoTargetDisabled &&
      state.dynoTargetTelemetry !== "—" &&
      state.dynoDisposition !== "—",
    "released HeldDyno workbench telemetry",
    20_000,
  );
  await cdp.evaluate(`(() => {
    const values = [
      ["#held-dyno-target-rpm-input", "3200"],
      ["#held-dyno-absorbing-torque-input", "800"],
      ["#held-dyno-driving-torque-input", "0"]
    ];
    for (const [selector, value] of values) {
      const input = document.querySelector(selector);
      input.value = value;
      input.dispatchEvent(new Event("input", { bubbles: true }));
    }
    return true;
  })()`);
  const controlled = await waitUntil(
    () => pageState(cdp),
    (state) =>
      state.dynoTargetTelemetry === "3,200 RPM" &&
      state.dynoLimitsTelemetry.includes("800 N·m absorb") &&
      state.dynoLimitsTelemetry.includes("0 N·m drive"),
    "atomic HeldDyno target and torque-limit batch",
  );
  assert.equal(controlled.dynoTargetInput, "3200");

  await cdp.evaluate(
    `document.querySelector("#stop-button").click(); true`,
  );
  await waitUntil(
    () => pageState(cdp),
    (state) => state.session === "Paused",
    "paused HeldDyno bench",
  );
  const paused = await pageState(cdp);
  assert.equal(paused.startLabel, "Resume");
  assert.equal(paused.restartLabel, "Restart");
  assert.equal(paused.restartDisabled, false);
}

async function verifyFreeVehicleBench(cdp) {
  const ready = await pageState(cdp);
  assert.equal(ready.motionMode, "Free Vehicle");
  assert.equal(ready.heldDynoControlsHidden, true);
  assert.equal(ready.vehicleControlsHidden, false);
  assert.equal(ready.vehicleGearOptions, 6);
  assert.equal(ready.vehicleGearDisabled, true);
  assert.equal(ready.startLabel, "Start");
  assert.equal(ready.stopLabel, "Stop");

  await cdp.evaluate(
    `document.querySelector("#start-button").click(); true`,
  );
  await waitUntil(
    () => pageState(cdp),
    (state) =>
      state.session === "Running" &&
      !state.vehicleGearDisabled &&
      state.vehicleSpeedTelemetry !== "—",
    "released FreeVehicle workbench telemetry",
    20_000,
  );
  await cdp.evaluate(`(() => {
    const gear = document.querySelector("#vehicle-gear-select");
    gear.value = "2";
    gear.dispatchEvent(new Event("change", { bubbles: true }));
    const clutch = document.querySelector("#vehicle-clutch-input");
    clutch.value = "50";
    clutch.dispatchEvent(new Event("input", { bubbles: true }));
    const brake = document.querySelector("#vehicle-brake-input");
    brake.value = "25";
    brake.dispatchEvent(new Event("input", { bubbles: true }));
    return true;
  })()`);
  const controlled = await waitUntil(
    () => pageState(cdp),
    (state) =>
      state.vehicleGearTelemetry.startsWith("2 · gear-2 · 2.49:1") &&
      state.vehicleClutchTelemetry.startsWith("50 %"),
    "atomic FreeVehicle gear, clutch, and brake batch",
  );
  assert.equal(controlled.vehicleGearInput, "2");
  assert.equal(controlled.vehicleClutchInput, "50");
  assert.equal(controlled.vehicleBrakeInput, "25");

  await cdp.evaluate(
    `document.querySelector("#stop-button").click(); true`,
  );
  const paused = await waitUntil(
    () => pageState(cdp),
    (state) => state.session === "Paused" && !state.restartDisabled,
    "paused FreeVehicle bench",
  );
  const pausedElapsed = uiDurationSeconds(paused.elapsed);
  assert.equal(paused.startLabel, "Resume");
  assert.equal(paused.restartLabel, "Restart");
  await cdp.evaluate(
    `document.querySelector("#restart-button").click(); true`,
  );
  const restarted = await waitUntil(
    () => pageState(cdp),
    (state) =>
      state.session === "Running" &&
      uiDurationSeconds(state.elapsed) < pausedElapsed,
    "fresh FreeVehicle bench restart",
    20_000,
  );
  assert.equal(restarted.stopLabel, "Stop");
  await cdp.evaluate(
    `document.querySelector("#stop-button").click(); true`,
  );
  await waitUntil(
    () => pageState(cdp),
    (state) => state.session === "Paused",
    "paused restarted FreeVehicle bench",
  );
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
        state.worker === "WASM ABI 7" &&
        !state.buildDisabled,
      "isolated workbench and WASM Worker",
    );
    assert.equal(loaded.executionKindVisible, true);
    assert.match(loaded.diagnostics, /No diagnostics reported/u);

    await cdp.evaluate(
      `document.querySelector("#build-button").click(); true`,
    );
    const built = await waitUntil(
      () => pageState(cdp),
      (state) =>
        state.build === "Build admitted" &&
        state.session === "Ready" &&
        state.busCount === 11 &&
        !state.startDisabled,
      "the compiled BMW workbench session",
    );
    assert.equal(built.selectedBus, "10");
    assert.equal(built.throttleLabel, "Throttle");
    assert.equal(built.throttleMinimumLabel, "Closed");
    assert.equal(built.throttleMaximumLabel, "Wide open");
    assert.equal(built.throttle, "10%");
    assert.equal(built.executionKindInput, OPEN_ENDED_EXECUTION_KIND);
    assert.match(built.sessionSubtitle, /continuous bench/u);
    assert.equal(built.exportLabel, "Export authored scenario WAV");
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
    assert.equal(running.starterDisabled, false);

    await cdp.evaluate(`(() => {
      const throttle = document.querySelector("#throttle-input");
      throttle.value = "20";
      throttle.dispatchEvent(new Event("input", { bubbles: true }));
      const resistance = document.querySelector("#external-resistance-input");
      resistance.value = "0";
      resistance.dispatchEvent(new Event("input", { bubbles: true }));
      return true;
    })()`);
    await waitUntil(
      () => pageState(cdp),
      (state) => state.throttle === "20%",
      "the accepted 20% throttle command",
    );
    running = await waitUntil(
      () => pageState(cdp),
      (state) =>
        state.session === "Running" &&
        uiDurationSeconds(state.elapsed) >= 6,
      "open-ended playback beyond the 5.5 second authored horizon",
      15_000,
    );
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
    const paused = await pageState(cdp);
    const pausedElapsed = uiDurationSeconds(paused.elapsed);
    assert.ok(Number.isFinite(pausedElapsed) && pausedElapsed >= 6);
    await delay(300);
    assert.equal(
      uiDurationSeconds((await pageState(cdp)).elapsed),
      pausedElapsed,
      "Stop advanced the paused session clock",
    );

    await cdp.evaluate(
      `document.querySelector("#start-button").click(); true`,
    );
    const resumed = await waitUntil(
      () => pageState(cdp),
      (state) =>
        state.session === "Running" &&
        state.throttle === "20%" &&
        uiDurationSeconds(state.elapsed) > pausedElapsed,
      "resumed live session with retained state",
    );
    assert.equal(resumed.underruns, "0");

    await cdp.evaluate(
      `document.querySelector("#stop-button").click(); true`,
    );
    await waitUntil(
      () => pageState(cdp),
      (state) => state.session === "Paused" && !state.restartDisabled,
      "second paused live session",
    );
    await cdp.evaluate(
      `document.querySelector("#restart-button").click(); true`,
    );
    const restarted = await waitUntil(
      () => pageState(cdp),
      (state) =>
        state.session === "Running" &&
        state.throttle === "10%" &&
        uiDurationSeconds(state.elapsed) < pausedElapsed,
      "freshly restarted live session",
      20_000,
    );
    assert.equal(restarted.underruns, "0");

    await cdp.evaluate(
      `document.querySelector("#stop-button").click(); true`,
    );
    await waitUntil(
      () => pageState(cdp),
      (state) => state.session === "Paused",
      "paused restarted session",
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

    await cdp.evaluate(`(() => {
      const packages = document.querySelector("#package-select");
      packages.value = "raspy-muscle-620-free-rev";
      packages.dispatchEvent(new Event("change", { bubbles: true }));
      document.querySelector("#load-package-button").click();
      return true;
    })()`);
    await waitUntil(
      () => pageState(cdp),
      (state) =>
        state.selectedPackage === "raspy-muscle-620-free-rev" &&
        state.authoredEngineId === "raspy-muscle-620-cleanroom" &&
        state.authoredScenarioId ===
          "raspy-muscle-620-cleanroom-warm-running-free-rev-800rpm" &&
        !state.buildDisabled,
      "the repository V8 package",
    );
    await cdp.evaluate(
      `document.querySelector("#build-button").click(); true`,
    );
    const v8Built = await waitUntil(
      () => pageState(cdp),
      (state) =>
        state.build === "Build admitted" &&
        state.session === "Ready" &&
        state.sessionTitle === "raspy-muscle-620-cleanroom" &&
        state.busCount === 8 &&
        /continuous bench/u.test(state.sessionSubtitle) &&
        !state.startDisabled,
      "the compiled 6.2L V8 workbench session",
      30_000,
    );
    assert.equal(v8Built.executionKindInput, OPEN_ENDED_EXECUTION_KIND);
    assert.match(v8Built.diagnostics, /No diagnostics reported/u);

    await cdp.evaluate(
      `document.querySelector("#start-button").click(); true`,
    );
    const v8Running = await waitUntil(
      () => pageState(cdp),
      (state) =>
        state.session === "Running" &&
        state.sessionTitle === "raspy-muscle-620-cleanroom" &&
        state.rpm !== "—",
      "primed live 6.2L V8 playback",
      30_000,
    );
    assert.equal(v8Running.underruns, "0");
    assert.equal(v8Running.starterDisabled, false);
    await cdp.evaluate(
      `document.querySelector("#stop-button").click(); true`,
    );
    await waitUntil(
      () => pageState(cdp),
      (state) => state.session === "Paused",
      "paused live 6.2L V8 session",
    );

    const verifiedPackages = [];
    let sourceOnlyAudition = null;
    for (const expectation of NEW_REPOSITORY_PACKAGES) {
      verifiedPackages.push(
        await verifyRepositoryPackage(cdp, expectation),
      );
      if (expectation.packageId === "bmw-m52tub28-held-dyno-pull-lift") {
        await selectExecutionKindAndRebuild(
          cdp,
          OPEN_ENDED_EXECUTION_KIND,
        );
        await verifyHeldDynoBench(cdp);
      }
      if (expectation.packageId === "bmw-m52tub28-free-rev") {
        sourceOnlyAudition = await verifySourceOnlyAudition(cdp);
      }
      if (expectation.packageId === "bmw-m52tub28-canonical-shutdown") {
        await verifyFiniteProcedure(cdp);
      }
      if (expectation.packageId === "harley-shovelhead-source-pull") {
        await verifyFiniteProcedure(cdp);
      }
      if (expectation.packageId === "harley-shovelhead-free-rev") {
        await verifyInteractiveFreeEngine(cdp);
      }
      if (expectation.packageId === "bmw-m52tub28-launch-first-second") {
        await selectExecutionKindAndRebuild(
          cdp,
          OPEN_ENDED_EXECUTION_KIND,
        );
        await verifyFreeVehicleBench(cdp);
      }
    }
    assert.deepEqual(cdp.exceptions, []);

    process.stdout.write(
      JSON.stringify({
        worker: routeReady.worker,
        buses: routeReady.busCount,
        wavBytes: exported.bytes,
        wavSha256: exported.sha256,
        sourceSharedUnderruns: Number(running.underruns),
        throttle: running.throttle,
        selectedRoute,
        v8Engine: v8Running.sessionTitle,
        v8StartupUnderruns: Number(v8Running.underruns),
        sourceOnlyAudition,
        verifiedPackages,
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
