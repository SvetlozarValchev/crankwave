import {
  ESO_CANONICAL_SAMPLE_RATE,
  ProcessKind,
  SessionExecutionKind,
} from "./c-api-abi.js";
import { EngineSimRuntimeError } from "./c-api-errors.js";
import { concatenateFloat32, encodeFloat32Wav } from "./wav.js";

export const MAXIMUM_CANONICAL_EXPORT_FRAMES =
  ESO_CANONICAL_SAMPLE_RATE * 180;

function nextTask() {
  return new Promise((resolve) => setTimeout(resolve, 0));
}

function normalizeExportControls(session, controls) {
  if (controls === undefined) {
    return [];
  }
  if (!Array.isArray(controls)) {
    throw new TypeError("export controls must be an array");
  }
  const defaultFrame = session.firstAudibleDeliveryFrame;
  return controls
    .map((control, index) => ({
      kind: control.kind,
      value: control.value,
      deliveryFrame:
        control.deliveryFrame === undefined
          ? defaultFrame
          : BigInt(control.deliveryFrame),
      sourceOrder: index,
    }))
    .sort((left, right) => {
      if (left.deliveryFrame < right.deliveryFrame) {
        return -1;
      }
      if (left.deliveryFrame > right.deliveryFrame) {
        return 1;
      }
      return left.sourceOrder - right.sourceOrder;
    });
}

export async function runCanonicalExport({
  program,
  busIndex,
  controls,
  onProgress = () => {},
}) {
  const session = program.createSession(
    SessionExecutionKind.finiteScenario,
  );
  try {
    const bus = session.buses[busIndex];
    if (!bus) {
      throw new EngineSimRuntimeError(
        `audio bus index ${busIndex} is outside the compiled session`,
        {
          operation: "export-wav",
          detailCode: "browser-runtime-audio-bus-index-invalid",
          diagnostics: [],
        },
      );
    }
    const descriptor = session.descriptor;
    if (
      descriptor.executionKindCode !==
        SessionExecutionKind.finiteScenario ||
      descriptor.totalBlockCountBigInt === null
    ) {
      throw new EngineSimRuntimeError(
        "canonical WAV export requires a finite authored-scenario session",
        {
          operation: "export-wav",
          detailCode: "browser-runtime-export-session-not-finite",
          diagnostics: [],
        },
      );
    }
    const predictedFrames =
      (descriptor.totalBlockCountBigInt -
        descriptor.preparationBlockCountBigInt) *
      BigInt(descriptor.deliveryFramesPerBlock);
    if (predictedFrames > BigInt(MAXIMUM_CANONICAL_EXPORT_FRAMES)) {
      throw new EngineSimRuntimeError(
        `export contains ${predictedFrames} canonical frames; the browser limit is ` +
          `${MAXIMUM_CANONICAL_EXPORT_FRAMES}`,
        {
          operation: "export-wav",
          detailCode: "browser-runtime-export-limit",
          diagnostics: [],
        },
      );
    }

    const normalizedControls = normalizeExportControls(session, controls);
    if (normalizedControls.length !== 0) {
      session.enqueueControls(normalizedControls);
    }

    const chunks = [];
    let totalSamples = 0;
    let processedBlocks = 0n;
    for (;;) {
      const block = session.processBlock(busIndex);
      if (block.process.kindCode === ProcessKind.completed) {
        break;
      }
      ++processedBlocks;
      if (block.audible) {
        chunks.push(block.samples);
        totalSamples += block.samples.length;
      }
      if ((processedBlocks & 7n) === 0n) {
        onProgress({
          completedBlocks: processedBlocks.toString(10),
          totalBlocks: descriptor.totalBlockCount,
          canonicalFrames:
            Math.floor(totalSamples / bus.channelCount).toString(10),
        });
        await nextTask();
      }
    }

    const pcm = concatenateFloat32(chunks, totalSamples);
    const wav = encodeFloat32Wav(
      pcm,
      ESO_CANONICAL_SAMPLE_RATE,
      bus.channelCount,
    );
    return {
      pcm,
      wav,
      bus,
      executionKind: descriptor.executionKind,
      sampleRate: ESO_CANONICAL_SAMPLE_RATE,
      channelCount: bus.channelCount,
      frameCount: pcm.length / bus.channelCount,
      completedBlocks: processedBlocks.toString(10),
    };
  } finally {
    session.dispose();
  }
}
