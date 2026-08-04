import assert from "node:assert/strict";
import test from "node:test";

import {
  AudioSignalDisposition,
  AudioBusKind,
  ControlCapability,
  EngineCycleState,
  ESO_C_API_VERSION,
  Layout,
  MotionMode,
  SourceRouteKind,
  WASM32_ABI_WORDS,
  audioBusKindName,
  audioSignalDispositionName,
  sourceRouteKindName,
} from "../../web/runtime/c-api-abi.js";
import { EngineSimSession } from "../../web/runtime/c-api-session.js";

class FakeHeap {
  constructor() {
    this.buffer = new ArrayBuffer(1024 * 1024);
    this.bytes = new Uint8Array(this.buffer);
    this.bytes.fill(0xa5);
    this.view = new DataView(this.buffer);
    this.next = 8;
  }

  allocate(size) {
    const pointer = (this.next + 7) & ~7;
    this.next = pointer + Math.max(1, size);
    assert.ok(this.next <= this.buffer.byteLength);
    return pointer;
  }

  free() {}

  decodeUtf8(pointer, byteCount) {
    return new TextDecoder().decode(this.bytes.subarray(pointer, pointer + byteCount));
  }
}

function writeUtf8(heap, pointer, value) {
  const encoded = new TextEncoder().encode(value);
  heap.bytes.set(encoded, pointer);
  heap.bytes[pointer + encoded.byteLength] = 0;
}

function copyToMutableBuffer(heap, bufferPointer, value) {
  const data = heap.view.getUint32(
    bufferPointer + Layout.mutableUtf8Buffer.data,
    true,
  );
  writeUtf8(heap, data, value);
}

function writeEngineTelemetry(view, pointer, engineStep, rpm) {
  const layout = Layout.engineTelemetry;
  view.setBigUint64(pointer + layout.engineStepEndIndex, BigInt(engineStep), true);
  view.setUint32(pointer + layout.validityMask, 0x55aa, true);
  view.setUint32(pointer + layout.ignitionEnabled, 1, true);
  view.setUint32(pointer + layout.fuelEnabled, 1, true);
  view.setFloat64(pointer + layout.engineSpeedRpm, rpm, true);
  view.setFloat64(pointer + layout.requestedThrottle, 0.75, true);
}

function makeFakeClient({
  busKind = AudioBusKind.engineAuditionMaster,
  sourceRouteKind = SourceRouteKind.unspecified,
  signalDisposition = AudioSignalDisposition.active,
} = {}) {
  const heap = new FakeHeap();
  const capturedControlBatches = [];
  const engineId = "fixture-engine";
  const scenarioId = "fixture-free-vehicle";
  const requestedBus = {
    id: busKind === AudioBusKind.engineAuditionMaster
      ? "master.engine.audition"
      : "route.fixture.dry",
    kind: busKind,
    sourceRouteKind,
    signalDisposition,
  };
  const buses = [requestedBus];
  if (busKind !== AudioBusKind.engineAuditionMaster) {
    buses.push({
      id: "master.engine.audition",
      kind: AudioBusKind.engineAuditionMaster,
      sourceRouteKind: SourceRouteKind.unspecified,
      signalDisposition: AudioSignalDisposition.active,
    });
  }
  const gears = [
    { gearId: 41, authoredOrdinal: 1, ratio: 4.21, semanticId: "gear-1" },
    { gearId: 42, authoredOrdinal: 2, ratio: 2.49, semanticId: "gear-2" },
  ];
  const allCapabilities = Object.values(ControlCapability).reduce(
    (mask, capability) => mask | capability,
    0,
  );

  const module = {
    _eso_session_get_descriptor(_context, _session, pointer) {
      const view = heap.view;
      const layout = Layout.sessionDescriptor;
      heap.bytes.fill(0, pointer, pointer + layout.size);
      view.setUint32(pointer + layout.maximumDeliveryFrames, 4, true);
      view.setUint32(pointer + layout.controlQueueCapacity, 32, true);
      view.setUint32(pointer + layout.maximumTelemetryFrames, 3, true);
      view.setUint32(pointer + layout.maximumCycleEvidence, 4, true);
      view.setBigUint64(pointer + layout.physicsRateNumerator, 20_000n, true);
      view.setBigUint64(pointer + layout.physicsRateDenominator, 1n, true);
      view.setBigUint64(pointer + layout.deliveryRateNumerator, 192_000n, true);
      view.setBigUint64(pointer + layout.deliveryRateDenominator, 1n, true);
      view.setUint32(pointer + layout.physicsFramesPerBlock, 4, true);
      view.setUint32(pointer + layout.deliveryFramesPerBlock, 4, true);
      view.setBigUint64(pointer + layout.totalBlockCount, 2n, true);
      view.setBigUint64(pointer + layout.preparationBlockCount, 0n, true);
      view.setUint32(pointer + layout.audioBusCount, buses.length, true);
      view.setUint32(pointer + layout.liveControlCapabilities, allCapabilities, true);
      view.setUint32(pointer + layout.engineIdBytes, engineId.length, true);
      view.setUint32(pointer + layout.scenarioIdBytes, scenarioId.length, true);
      view.setUint32(pointer + layout.executionKind, 1, true);
      view.setUint32(pointer + layout.motionMode, MotionMode.freeVehicle, true);
      view.setUint32(pointer + layout.forwardGearCount, gears.length, true);
      return 0;
    },
    _eso_session_copy_identity(_context, _session, pointer) {
      const layout = Layout.sessionIdentityBuffers;
      writeUtf8(heap, heap.view.getUint32(pointer + layout.engineData, true), engineId);
      writeUtf8(
        heap,
        heap.view.getUint32(pointer + layout.scenarioData, true),
        scenarioId,
      );
      return 0;
    },
    _eso_session_get_forward_gear_descriptor(
      _context,
      _session,
      index,
      pointer,
    ) {
      const gear = gears[index];
      const layout = Layout.forwardGearDescriptor;
      heap.bytes.fill(0, pointer, pointer + layout.size);
      heap.view.setUint32(pointer + layout.gearId, gear.gearId, true);
      heap.view.setUint32(
        pointer + layout.authoredOrdinal,
        gear.authoredOrdinal,
        true,
      );
      heap.view.setFloat64(pointer + layout.ratio, gear.ratio, true);
      heap.view.setUint32(
        pointer + layout.semanticIdBytes,
        gear.semanticId.length,
        true,
      );
      return 0;
    },
    _eso_session_copy_forward_gear_semantic_id(
      _context,
      _session,
      index,
      buffer,
    ) {
      copyToMutableBuffer(heap, buffer, gears[index].semanticId);
      return 0;
    },
    _eso_session_get_audio_bus_descriptor(_context, _session, index, pointer) {
      const bus = buses[index];
      const layout = Layout.audioBusDescriptor;
      heap.bytes.fill(0, pointer, pointer + layout.size);
      heap.view.setUint32(pointer + layout.kind, bus.kind, true);
      const hasRouteId = bus.sourceRouteKind !== SourceRouteKind.unspecified;
      heap.view.setUint32(pointer + layout.hasRouteId, hasRouteId ? 1 : 0, true);
      heap.view.setUint32(pointer + layout.routeId, hasRouteId ? 71 : 0, true);
      heap.view.setUint32(
        pointer + layout.sourceRouteKind,
        bus.sourceRouteKind,
        true,
      );
      heap.view.setUint32(
        pointer + layout.signalDisposition,
        bus.signalDisposition,
        true,
      );
      heap.view.setUint32(pointer + layout.channelCount, 1, true);
      heap.view.setBigUint64(pointer + layout.sampleRateNumerator, 192_000n, true);
      heap.view.setBigUint64(pointer + layout.sampleRateDenominator, 1n, true);
      heap.view.setUint32(pointer + layout.idBytes, bus.id.length, true);
      return 0;
    },
    _eso_session_copy_audio_bus_id(_context, _session, index, buffer) {
      copyToMutableBuffer(heap, buffer, buses[index].id);
      return 0;
    },
    _eso_session_enqueue_controls(
      _context,
      _session,
      pointer,
      count,
      rejection,
    ) {
      capturedControlBatches.push(
        heap.bytes.slice(pointer, pointer + count * Layout.controlCommand.size),
      );
      heap.bytes.fill(0, rejection, rejection + Layout.controlRejection.size);
      return 0;
    },
    _eso_session_process(
      _context,
      _session,
      audioCopy,
      _audioCount,
      telemetry,
      telemetryCapacity,
      cycleEvidence,
      cycleEvidenceCapacity,
      process,
    ) {
      assert.ok(telemetryCapacity >= 3);
      assert.ok(cycleEvidenceCapacity >= 4);
      const view = heap.view;
      const audio = Layout.audioCopyBuffer;
      const samples = view.getUint32(audioCopy + audio.samples, true);
      view.setFloat32(samples, 0.25, true);
      view.setFloat32(samples + 4, -0.25, true);
      view.setUint32(audioCopy + audio.samplesWritten, 2, true);

      heap.bytes.fill(
        0,
        telemetry,
        telemetry + 3 * Layout.sessionTelemetry.size,
      );
      for (let index = 0; index < 3; ++index) {
        const base = telemetry + index * Layout.sessionTelemetry.size;
        view.setBigUint64(
          base + Layout.sessionTelemetry.physicsStepEnd,
          BigInt(100 + index),
          true,
        );
        writeEngineTelemetry(
          view,
          base + Layout.sessionTelemetry.engine,
          200 + index,
          3000 + index,
        );
      }

      const heldBase = telemetry + Layout.sessionTelemetry.size;
      view.setUint32(heldBase + Layout.sessionTelemetry.hasHeldDyno, 1, true);
      const held = heldBase + Layout.sessionTelemetry.heldDyno;
      view.setFloat64(held + Layout.heldDynoTelemetry.targetEngineSpeedRpm, 3200, true);
      view.setFloat64(
        held + Layout.heldDynoTelemetry.maximumAbsorbingTorqueNm,
        450,
        true,
      );
      view.setFloat64(
        held + Layout.heldDynoTelemetry.maximumDrivingTorqueNm,
        80,
        true,
      );
      view.setFloat64(
        held + Layout.heldDynoTelemetry.requiredActuatorTorqueNm,
        -120,
        true,
      );
      view.setFloat64(
        held + Layout.heldDynoTelemetry.appliedActuatorTorqueNm,
        -120,
        true,
      );
      view.setUint32(held + Layout.heldDynoTelemetry.disposition, 1, true);

      const vehicleBase = telemetry + 2 * Layout.sessionTelemetry.size;
      view.setUint32(vehicleBase + Layout.sessionTelemetry.hasFreeVehicle, 1, true);
      const vehicle = vehicleBase + Layout.sessionTelemetry.freeVehicle;
      view.setFloat64(vehicle + Layout.freeVehicleTelemetry.vehicleSpeedMS, 12.5, true);
      view.setFloat64(vehicle + Layout.freeVehicleTelemetry.vehicleDistanceM, 42, true);
      view.setUint32(
        vehicle + Layout.freeVehicleTelemetry.hasSelectedForwardGear,
        1,
        true,
      );
      view.setUint32(
        vehicle + Layout.freeVehicleTelemetry.selectedForwardGearOrdinal,
        2,
        true,
      );
      view.setFloat64(
        vehicle + Layout.freeVehicleTelemetry.clutchEngagement01,
        0.8,
        true,
      );
      view.setFloat64(
        vehicle + Layout.freeVehicleTelemetry.serviceBrakeApplication01,
        0.1,
        true,
      );
      view.setUint32(
        vehicle + Layout.freeVehicleTelemetry.clutchDisposition,
        5,
        true,
      );
      view.setFloat64(
        vehicle + Layout.freeVehicleTelemetry.clutchTorqueCapacityNm,
        500,
        true,
      );
      view.setFloat64(
        vehicle + Layout.freeVehicleTelemetry.appliedAverageClutchTorqueOnEngineNm,
        -125,
        true,
      );
      view.setUint32(
        vehicle + Layout.freeVehicleTelemetry.roadLoadDisposition,
        1,
        true,
      );
      view.setFloat64(
        vehicle + Layout.freeVehicleTelemetry.requestedRoadLoadForceN,
        750,
        true,
      );
      view.setFloat64(
        vehicle + Layout.freeVehicleTelemetry.appliedAverageRoadLoadForceN,
        725,
        true,
      );

      heap.bytes.fill(
        0,
        cycleEvidence,
        cycleEvidence + 4 * Layout.completedCycleEvidence.size,
      );
      const cycle = Layout.completedCycleEvidence;
      const boundary = Layout.cycleBoundaryEvidence;
      const control = Layout.cycleControlEvidence;
      const net = Layout.cycleNetShaftEvidence;
      view.setBigUint64(
        cycleEvidence + cycle.completedCycleOrdinal,
        9_007_199_254_740_993n,
        true,
      );
      const start = cycleEvidence + cycle.startBoundary;
      view.setBigInt64(start + boundary.cycleOrdinal, -9_007_199_254_740_993n, true);
      view.setBigUint64(
        start + boundary.leftPhysicsFrame,
        9_007_199_254_740_994n,
        true,
      );
      view.setBigUint64(
        start + boundary.rightPhysicsFrame,
        9_007_199_254_740_995n,
        true,
      );
      view.setFloat64(start + boundary.fractionFromLeft01, 0.125, true);
      view.setFloat64(start + boundary.thetaUnwrappedRad, -12.5, true);
      view.setFloat64(start + boundary.timeS, 1.25, true);
      view.setFloat64(start + boundary.deliveryFrame, 240_000.5, true);
      const end = cycleEvidence + cycle.endBoundary;
      view.setBigInt64(end + boundary.cycleOrdinal, -9_007_199_254_740_992n, true);
      view.setBigUint64(
        end + boundary.leftPhysicsFrame,
        9_007_199_254_741_000n,
        true,
      );
      view.setBigUint64(
        end + boundary.rightPhysicsFrame,
        9_007_199_254_741_001n,
        true,
      );
      view.setFloat64(end + boundary.fractionFromLeft01, 0.875, true);
      view.setFloat64(end + boundary.thetaUnwrappedRad, 0.06637061435917246, true);
      view.setFloat64(end + boundary.timeS, 1.3, true);
      view.setFloat64(end + boundary.deliveryFrame, 249_600.25, true);
      view.setFloat64(cycleEvidence + cycle.durationS, 0.05, true);
      view.setFloat64(cycleEvidence + cycle.meanEngineSpeedRpm, 2400.5, true);
      const requested = cycleEvidence + cycle.requestedThrottle;
      view.setFloat64(requested + control.timeWeightedMean01, 0.6, true);
      view.setFloat64(requested + control.minimum01, 0.5, true);
      view.setFloat64(requested + control.maximum01, 0.75, true);
      view.setUint32(requested + control.changeCount, 2, true);
      const resolved = cycleEvidence + cycle.resolvedEngineThrottle;
      view.setFloat64(resolved + control.timeWeightedMean01, 0.55, true);
      view.setFloat64(resolved + control.minimum01, 0.45, true);
      view.setFloat64(resolved + control.maximum01, 0.7, true);
      view.setUint32(resolved + control.changeCount, 3, true);
      const plate = cycleEvidence + cycle.intakePlatePosition;
      view.setFloat64(plate + control.timeWeightedMean01, 0.42, true);
      view.setFloat64(plate + control.minimum01, 0.35, true);
      view.setFloat64(plate + control.maximum01, 0.5, true);
      view.setUint32(plate + control.changeCount, 4, true);
      const shaft = cycleEvidence + cycle.instantaneousNetShaft;
      view.setFloat64(shaft + net.angularWorkJ, 123.5, true);
      view.setFloat64(shaft + net.cycleMeanTorqueNm, 9.827113, true);
      view.setUint32(shaft + net.availability, 1, true);
      view.setUint32(shaft + net.completeness, 1, true);
      view.setUint32(shaft + net.unavailableReason, 0, true);
      view.setBigUint64(shaft + net.includedTerms, 9_007_199_254_740_997n, true);
      view.setBigUint64(shaft + net.omittedTerms, 4n, true);
      view.setUint32(cycleEvidence + cycle.startStateFlags, 0x03, true);
      view.setUint32(cycleEvidence + cycle.endStateFlags, 0x13, true);
      view.setUint32(cycleEvidence + cycle.stateTransitionFlags, 0x10, true);

      const info = Layout.processInfo;
      heap.bytes.fill(0, process, process + info.size);
      view.setUint32(process + info.kind, 1, true);
      view.setUint32(process + info.blockPhase, 2, true);
      view.setBigUint64(process + info.blockOrdinal, 0n, true);
      view.setBigUint64(process + info.firstPhysicsFrame, 0n, true);
      view.setUint32(process + info.physicsFrameCount, 4, true);
      view.setBigUint64(process + info.firstDeliveryFrame, 0n, true);
      view.setUint32(process + info.deliveryFrameCount, 4, true);
      view.setUint32(process + info.telemetryWritten, 3, true);
      view.setUint32(process + info.cycleEvidenceWritten, 1, true);
      view.setUint32(process + info.liveControlsAccepted, 1, true);
      return 0;
    },
    _eso_destroy_session() {
      return 0;
    },
  };

  const client = {
    module,
    heap,
    context: 1,
    assertStatus(status) {
      assert.equal(status, 0);
    },
  };
  return { client, capturedControlBatches };
}

test("frozen wasm32 ABI is the exact v7 layout", () => {
  assert.equal(ESO_C_API_VERSION, 7);
  assert.deepEqual(
    WASM32_ABI_WORDS,
    [7, 4, 4, 4, 8, 1, 40, 104, 24, 48, 696, 296],
  );
  assert.equal(Layout.diagnosticInfo.size, 56);
  assert.equal(Layout.engineTelemetry.size, 536);
  assert.equal(Layout.sessionTelemetry.engine, 8);
  assert.equal(Layout.sessionTelemetry.heldDyno, 552);
  assert.equal(Layout.sessionTelemetry.freeVehicle, 600);
  assert.equal(Layout.processInfo.size, 96);
  assert.equal(Layout.completedCycleEvidence.startBoundary, 8);
  assert.equal(Layout.completedCycleEvidence.endBoundary, 64);
  assert.equal(Layout.completedCycleEvidence.instantaneousNetShaft, 232);
  assert.equal(Layout.completedCycleEvidence.size, 296);
});

test("audio bus kinds, route kinds, and signal dispositions are exact", () => {
  assert.deepEqual(AudioBusKind, {
    sourceRouteDry: 1,
    sourceRouteConfiguredTransfer: 2,
    sourceRouteSelected: 3,
    engineRawMaster: 4,
    engineAuditionMaster: 5,
  });
  assert.deepEqual(SourceRouteKind, {
    unspecified: 0,
    exhaustOutlet: 1,
    intakeInlet: 2,
    mechanicalEngine: 3,
    mechanicalStarter: 4,
  });
  assert.deepEqual(AudioSignalDisposition, {
    active: 1,
    declaredSilent: 2,
  });
  assert.equal(audioBusKindName(AudioBusKind.sourceRouteDry), "source-route-dry");
  assert.equal(
    audioBusKindName(AudioBusKind.sourceRouteConfiguredTransfer),
    "source-route-configured-transfer",
  );
  assert.equal(
    audioBusKindName(AudioBusKind.sourceRouteSelected),
    "source-route-selected",
  );
  assert.equal(sourceRouteKindName(SourceRouteKind.exhaustOutlet), "exhaust-outlet");
  assert.equal(sourceRouteKindName(SourceRouteKind.intakeInlet), "intake-inlet");
  assert.equal(
    sourceRouteKindName(SourceRouteKind.mechanicalEngine),
    "mechanical-engine",
  );
  assert.equal(
    sourceRouteKindName(SourceRouteKind.mechanicalStarter),
    "mechanical-starter",
  );
  assert.equal(
    audioSignalDispositionName(AudioSignalDisposition.active),
    "active",
  );
  assert.equal(
    audioSignalDispositionName(AudioSignalDisposition.declaredSilent),
    "declared-silent",
  );
});

test("completed-cycle state bits are exact", () => {
  assert.deepEqual(EngineCycleState, {
    ignitionEnabled: 1 << 0,
    fuelEnabled: 1 << 1,
    starterEnabled: 1 << 2,
    dynoEnabled: 1 << 3,
    limiterEnabled: 1 << 4,
    limiterCutActive: 1 << 5,
  });
});

test("session distinguishes declared-silent intake topology from active audio", () => {
  const { client } = makeFakeClient({
    busKind: AudioBusKind.sourceRouteDry,
    sourceRouteKind: SourceRouteKind.intakeInlet,
    signalDisposition: AudioSignalDisposition.declaredSilent,
  });
  const session = new EngineSimSession(client, 1n);
  try {
    assert.equal(session.buses[0].sourceRouteKind, "intake-inlet");
    assert.equal(session.buses[0].routeId, 71);
    assert.equal(session.buses[0].signalDisposition, "declared-silent");
  } finally {
    session.dispose();
  }
});

test("session rejects a master bus carrying source-route identity", () => {
  const { client } = makeFakeClient({
    busKind: AudioBusKind.engineRawMaster,
    sourceRouteKind: SourceRouteKind.exhaustOutlet,
    signalDisposition: AudioSignalDisposition.active,
  });
  assert.throws(
    () => new EngineSimSession(client, 1n),
    /invalid audio source-route descriptor/,
  );
});

test("session decodes motion, gear inventory, and nullable telemetry sidecars", () => {
  const { client } = makeFakeClient();
  const session = new EngineSimSession(client, 1n);
  try {
    assert.equal(session.descriptor.motionMode, "free-vehicle");
    assert.equal(session.descriptor.motionModeCode, MotionMode.freeVehicle);
    assert.equal(session.descriptor.maximumCycleEvidencePerProcessCall, 4);
    assert.equal(session.buses[0].signalDisposition, "active");
    assert.equal(
      session.buses[0].signalDispositionCode,
      AudioSignalDisposition.active,
    );
    assert.deepEqual(session.forwardGears, [
      {
        index: 0,
        gearId: 41,
        authoredOrdinal: 1,
        semanticId: "gear-1",
        ratio: 4.21,
      },
      {
        index: 1,
        gearId: 42,
        authoredOrdinal: 2,
        semanticId: "gear-2",
        ratio: 2.49,
      },
    ]);
    assert.equal(session.descriptor.forwardGears, session.forwardGears);

    const block = session.processBlock();
    assert.equal(block.telemetry.length, 3);
    assert.deepEqual(
      {
        physicsStepEnd: block.telemetry[0].physicsStepEnd,
        engineStepEndIndex: block.telemetry[0].engineStepEndIndex,
        engineSpeedRpm: block.telemetry[0].engineSpeedRpm,
        heldDyno: block.telemetry[0].heldDyno,
        freeVehicle: block.telemetry[0].freeVehicle,
      },
      {
        physicsStepEnd: "100",
        engineStepEndIndex: "200",
        engineSpeedRpm: 3000,
        heldDyno: null,
        freeVehicle: null,
      },
    );
    assert.deepEqual(block.telemetry[1].heldDyno, {
      targetEngineSpeedRpm: 3200,
      maximumAbsorbingTorqueNm: 450,
      maximumDrivingTorqueNm: 80,
      requiredActuatorTorqueNm: -120,
      appliedActuatorTorqueNm: -120,
      disposition: "tracking",
      dispositionCode: 1,
    });
    assert.equal(block.telemetry[1].freeVehicle, null);
    assert.equal(block.telemetry[2].heldDyno, null);
    assert.deepEqual(block.telemetry[2].freeVehicle, {
      vehicleSpeedMS: 12.5,
      vehicleDistanceM: 42,
      selectedForwardGearOrdinal: 2,
      clutchEngagement01: 0.8,
      serviceBrakeApplication01: 0.1,
      clutchDisposition: "tracking",
      clutchDispositionCode: 5,
      clutchTorqueCapacityNm: 500,
      appliedAverageClutchTorqueOnEngineNm: -125,
      finalClutchSlipRadS: null,
      roadLoadDisposition: "moving",
      roadLoadDispositionCode: 1,
      requestedRoadLoadForceN: 750,
      appliedAverageRoadLoadForceN: 725,
    });
    assert.equal(block.completedCycles.length, 1);
    assert.deepEqual(block.completedCycles[0], {
      completedCycleOrdinal: "9007199254740993",
      startBoundary: {
        cycleOrdinal: "-9007199254740993",
        leftPhysicsFrame: "9007199254740994",
        rightPhysicsFrame: "9007199254740995",
        fractionFromLeft01: 0.125,
        thetaUnwrappedRad: -12.5,
        timeS: 1.25,
        deliveryFrame: 240000.5,
      },
      endBoundary: {
        cycleOrdinal: "-9007199254740992",
        leftPhysicsFrame: "9007199254741000",
        rightPhysicsFrame: "9007199254741001",
        fractionFromLeft01: 0.875,
        thetaUnwrappedRad: 0.06637061435917246,
        timeS: 1.3,
        deliveryFrame: 249600.25,
      },
      durationS: 0.05,
      meanEngineSpeedRpm: 2400.5,
      requestedThrottle: {
        timeWeightedMean01: 0.6,
        minimum01: 0.5,
        maximum01: 0.75,
        changeCount: 2,
      },
      resolvedEngineThrottle: {
        timeWeightedMean01: 0.55,
        minimum01: 0.45,
        maximum01: 0.7,
        changeCount: 3,
      },
      intakePlatePosition: {
        timeWeightedMean01: 0.42,
        minimum01: 0.35,
        maximum01: 0.5,
        changeCount: 4,
      },
      instantaneousNetShaft: {
        angularWorkJ: 123.5,
        cycleMeanTorqueNm: 9.827113,
        availability: 1,
        completeness: 1,
        unavailableReason: 0,
        includedTerms: "9007199254740997",
        omittedTerms: "4",
      },
      startStateFlags: 0x03,
      endStateFlags: 0x13,
      stateTransitionFlags: 0x10,
    });
  } finally {
    session.dispose();
  }
});

test("all twelve controls use canonical typed command fields", () => {
  const { client, capturedControlBatches } = makeFakeClient();
  const session = new EngineSimSession(client, 1n);
  try {
    session.enqueueControls([
      { kind: "throttle", value: 0.5, deliveryFrame: 12n },
      { kind: "ignition", value: true, deliveryFrame: 12n },
      { kind: "fuel", value: false, deliveryFrame: 12n },
      { kind: "limiter", value: true, deliveryFrame: 12n },
      { kind: "external-resisting-torque", value: 20, deliveryFrame: 12n },
      { kind: "starter", value: false, deliveryFrame: 12n },
      { kind: "held-dyno-target-engine-speed", value: 3500, deliveryFrame: 12n },
      {
        kind: "held-dyno-maximum-absorbing-torque",
        value: 450,
        deliveryFrame: 12n,
      },
      {
        kind: "held-dyno-maximum-driving-torque",
        value: 80,
        deliveryFrame: 12n,
      },
      { kind: "vehicle-selected-forward-gear", value: 2, deliveryFrame: 12n },
      { kind: "vehicle-clutch-engagement", value: 0.75, deliveryFrame: 12n },
      {
        kind: "vehicle-service-brake-application",
        value: 0.25,
        deliveryFrame: 12n,
      },
    ]);
    assert.equal(capturedControlBatches.length, 1);
    const bytes = capturedControlBatches[0];
    const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
    const booleanKinds = new Set([2, 3, 4, 6]);
    const scalarKinds = new Set([1, 5, 7, 8, 9, 11, 12]);
    for (let index = 0; index < 12; ++index) {
      const base = index * Layout.controlCommand.size;
      const kind = view.getUint32(base + Layout.controlCommand.kind, true);
      assert.equal(kind, index + 1);
      assert.equal(
        view.getBigUint64(base + Layout.controlCommand.deliveryFrame, true),
        12n,
      );
      assert.equal(
        view.getBigUint64(base + Layout.controlCommand.sequence, true),
        BigInt(index + 1),
      );
      assert.equal(view.getUint32(base + Layout.controlCommand.reserved, true), 0);
      if (booleanKinds.has(kind)) {
        assert.equal(view.getFloat64(base + Layout.controlCommand.scalarValue, true), 0);
        assert.equal(view.getUint32(base + Layout.controlCommand.idValue, true), 0);
      } else if (scalarKinds.has(kind)) {
        assert.equal(view.getUint32(base + Layout.controlCommand.enabled, true), 0);
        assert.equal(view.getUint32(base + Layout.controlCommand.idValue, true), 0);
      } else {
        assert.equal(kind, 10);
        assert.equal(view.getUint32(base + Layout.controlCommand.enabled, true), 0);
        assert.equal(view.getFloat64(base + Layout.controlCommand.scalarValue, true), 0);
        assert.equal(view.getUint32(base + Layout.controlCommand.idValue, true), 2);
      }
    }
    assert.throws(
      () =>
        session.enqueueControls([
          {
            kind: "vehicle-selected-forward-gear",
            value: 3,
            deliveryFrame: 13n,
          },
        ]),
      /published authored ordinal/u,
    );
  } finally {
    session.dispose();
  }
});
