import assert from "node:assert/strict";
import test from "node:test";

import {
  AudioBusKind,
  ControlCapability,
  ESO_C_API_VERSION,
  Layout,
  MotionMode,
  WASM32_ABI_WORDS,
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

function makeFakeClient() {
  const heap = new FakeHeap();
  const capturedControlBatches = [];
  const engineId = "fixture-engine";
  const scenarioId = "fixture-free-vehicle";
  const busId = "master.engine.audition";
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
      view.setBigUint64(pointer + layout.physicsRateNumerator, 20_000n, true);
      view.setBigUint64(pointer + layout.physicsRateDenominator, 1n, true);
      view.setBigUint64(pointer + layout.deliveryRateNumerator, 192_000n, true);
      view.setBigUint64(pointer + layout.deliveryRateDenominator, 1n, true);
      view.setUint32(pointer + layout.physicsFramesPerBlock, 4, true);
      view.setUint32(pointer + layout.deliveryFramesPerBlock, 4, true);
      view.setBigUint64(pointer + layout.totalBlockCount, 2n, true);
      view.setBigUint64(pointer + layout.preparationBlockCount, 0n, true);
      view.setUint32(pointer + layout.audioBusCount, 1, true);
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
    _eso_session_get_audio_bus_descriptor(_context, _session, _index, pointer) {
      const layout = Layout.audioBusDescriptor;
      heap.bytes.fill(0, pointer, pointer + layout.size);
      heap.view.setUint32(pointer + layout.kind, AudioBusKind.engineAuditionMaster, true);
      heap.view.setUint32(pointer + layout.channelCount, 1, true);
      heap.view.setBigUint64(pointer + layout.sampleRateNumerator, 192_000n, true);
      heap.view.setBigUint64(pointer + layout.sampleRateDenominator, 1n, true);
      heap.view.setUint32(pointer + layout.idBytes, busId.length, true);
      return 0;
    },
    _eso_session_copy_audio_bus_id(_context, _session, _index, buffer) {
      copyToMutableBuffer(heap, buffer, busId);
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
      process,
    ) {
      assert.ok(telemetryCapacity >= 3);
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

test("frozen wasm32 ABI is the exact v4 layout", () => {
  assert.equal(ESO_C_API_VERSION, 4);
  assert.deepEqual(WASM32_ABI_WORDS, [4, 4, 4, 4, 8, 1, 40, 104, 24, 40, 696]);
  assert.equal(Layout.diagnosticInfo.size, 56);
  assert.equal(Layout.engineTelemetry.size, 536);
  assert.equal(Layout.sessionTelemetry.engine, 8);
  assert.equal(Layout.sessionTelemetry.heldDyno, 552);
  assert.equal(Layout.sessionTelemetry.freeVehicle, 600);
});

test("session decodes motion, gear inventory, and nullable telemetry sidecars", () => {
  const { client } = makeFakeClient();
  const session = new EngineSimSession(client, 1n);
  try {
    assert.equal(session.descriptor.motionMode, "free-vehicle");
    assert.equal(session.descriptor.motionModeCode, MotionMode.freeVehicle);
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
