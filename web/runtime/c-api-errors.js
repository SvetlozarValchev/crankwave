import {
  Layout,
  errorStageName,
  statusName,
} from "./c-api-abi.js";
import { withWasmAllocations } from "./wasm-heap.js";

export class CrankwaveRuntimeError extends Error {
  constructor(message, detail = {}) {
    super(message);
    this.name = "CrankwaveRuntimeError";
    Object.assign(this, detail);
  }

  toJSON() {
    return {
      name: this.name,
      message: this.message,
      operation: this.operation ?? null,
      status: this.status ?? null,
      statusName: this.statusName ?? null,
      stage: this.stage ?? null,
      stageName: this.stageName ?? null,
      code: this.code ?? null,
      detailCode: this.detailCode ?? null,
      diagnostics: this.diagnostics ?? [],
    };
  }
}

function readStringSet(heap, sizes, layout, invoke) {
  const allocations = sizes.map((entry) => ({
    bytes: entry.bytes + 1,
    purpose: entry.purpose,
  }));
  allocations.push({ bytes: layout.size, purpose: "C API text-buffer layout" });
  return withWasmAllocations(heap, allocations, (pointers) => {
    const bufferPointer = pointers[pointers.length - 1];
    const view = heap.view;
    for (let index = 0; index < sizes.length; ++index) {
      const entry = sizes[index];
      view.setUint32(bufferPointer + entry.dataOffset, pointers[index], true);
      view.setUint32(bufferPointer + entry.capacityOffset, entry.bytes + 1, true);
    }
    const status = invoke(bufferPointer);
    if (status !== 0) {
      throw new CrankwaveRuntimeError(
        `copying structured C API error text failed with ${statusName(status)}`,
        { status, statusName: statusName(status) },
      );
    }
    return pointers
      .slice(0, sizes.length)
      .map((pointer, index) => heap.decodeUtf8(pointer, sizes[index].bytes));
  });
}

function readRelatedDiagnostic(module, heap, context, diagnosticIndex, relatedIndex) {
  const info = Layout.relatedDiagnosticInfo;
  return withWasmAllocations(
    heap,
    [{ bytes: info.size, purpose: "related diagnostic metadata" }],
    ([infoPointer]) => {
      const status = module._crankwave_context_get_related_diagnostic(
        context,
        diagnosticIndex,
        relatedIndex,
        infoPointer,
      );
      if (status !== 0) {
        throw new CrankwaveRuntimeError(
          `reading related diagnostic failed with ${statusName(status)}`,
          { status, statusName: statusName(status) },
        );
      }
      const view = heap.view;
      const sizes = [
        {
          bytes: view.getUint32(infoPointer + info.jsonPointerBytes, true),
          dataOffset: Layout.diagnosticTextBuffers.jsonPointerData,
          capacityOffset: Layout.diagnosticTextBuffers.jsonPointerCapacity,
          purpose: "related diagnostic JSON pointer",
        },
        {
          bytes: view.getUint32(infoPointer + info.subjectKindBytes, true),
          dataOffset: Layout.diagnosticTextBuffers.subjectKindData,
          capacityOffset: Layout.diagnosticTextBuffers.subjectKindCapacity,
          purpose: "related diagnostic subject kind",
        },
        {
          bytes: view.getUint32(infoPointer + info.subjectIdBytes, true),
          dataOffset: Layout.diagnosticTextBuffers.subjectIdData,
          capacityOffset: Layout.diagnosticTextBuffers.subjectIdCapacity,
          purpose: "related diagnostic subject id",
        },
        {
          bytes: view.getUint32(infoPointer + info.messageBytes, true),
          dataOffset: Layout.diagnosticTextBuffers.messageData,
          capacityOffset: Layout.diagnosticTextBuffers.messageCapacity,
          purpose: "related diagnostic message",
        },
      ];
      const [jsonPointer, subjectKind, subjectId, message] = readStringSet(
        heap,
        sizes,
        Layout.diagnosticTextBuffers,
        (buffers) =>
          module._crankwave_context_copy_related_diagnostic_text(
            context,
            diagnosticIndex,
            relatedIndex,
            buffers,
          ),
      );
      return {
        hasSubject: view.getUint32(infoPointer + info.hasSubject, true) !== 0,
        jsonPointer,
        subjectKind,
        subjectId,
        message,
      };
    },
  );
}

function readDiagnostic(module, heap, context, diagnosticIndex) {
  const info = Layout.diagnosticInfo;
  return withWasmAllocations(
    heap,
    [{ bytes: info.size, purpose: "diagnostic metadata" }],
    ([infoPointer]) => {
      const status = module._crankwave_context_get_diagnostic(
        context,
        diagnosticIndex,
        infoPointer,
      );
      if (status !== 0) {
        throw new CrankwaveRuntimeError(
          `reading diagnostic failed with ${statusName(status)}`,
          { status, statusName: statusName(status) },
        );
      }
      const view = heap.view;
      const metadata = {
        severity: view.getUint32(infoPointer + info.severity, true),
        code: view.getUint32(infoPointer + info.code, true),
        hasSubject: view.getUint32(infoPointer + info.hasSubject, true) !== 0,
        hasSourcePosition:
          view.getUint32(infoPointer + info.hasSourcePosition, true) !== 0,
        sourceByteOffset: view.getBigUint64(
          infoPointer + info.sourceByteOffset,
          true,
        ),
        sourceLine: view.getUint32(infoPointer + info.sourceLine, true),
        sourceColumn: view.getUint32(infoPointer + info.sourceColumn, true),
        relatedCount: view.getUint32(infoPointer + info.relatedCount, true),
      };
      const sizes = [
        {
          bytes: view.getUint32(infoPointer + info.jsonPointerBytes, true),
          dataOffset: Layout.diagnosticTextBuffers.jsonPointerData,
          capacityOffset: Layout.diagnosticTextBuffers.jsonPointerCapacity,
          purpose: "diagnostic JSON pointer",
        },
        {
          bytes: view.getUint32(infoPointer + info.subjectKindBytes, true),
          dataOffset: Layout.diagnosticTextBuffers.subjectKindData,
          capacityOffset: Layout.diagnosticTextBuffers.subjectKindCapacity,
          purpose: "diagnostic subject kind",
        },
        {
          bytes: view.getUint32(infoPointer + info.subjectIdBytes, true),
          dataOffset: Layout.diagnosticTextBuffers.subjectIdData,
          capacityOffset: Layout.diagnosticTextBuffers.subjectIdCapacity,
          purpose: "diagnostic subject id",
        },
        {
          bytes: view.getUint32(infoPointer + info.messageBytes, true),
          dataOffset: Layout.diagnosticTextBuffers.messageData,
          capacityOffset: Layout.diagnosticTextBuffers.messageCapacity,
          purpose: "diagnostic message",
        },
      ];
      const [jsonPointer, subjectKind, subjectId, message] = readStringSet(
        heap,
        sizes,
        Layout.diagnosticTextBuffers,
        (buffers) =>
          module._crankwave_context_copy_diagnostic_text(
            context,
            diagnosticIndex,
            buffers,
          ),
      );
      const related = [];
      for (let index = 0; index < metadata.relatedCount; ++index) {
        related.push(
          readRelatedDiagnostic(module, heap, context, diagnosticIndex, index),
        );
      }
      return {
        ...metadata,
        jsonPointer,
        subjectKind,
        subjectId,
        message,
        related,
      };
    },
  );
}

export function readContextError(module, heap, context, operation, fallbackStatus) {
  const info = Layout.errorInfo;
  return withWasmAllocations(
    heap,
    [{ bytes: info.size, purpose: "C API error metadata" }],
    ([infoPointer]) => {
      const inspectStatus = module._crankwave_context_get_last_error(context, infoPointer);
      if (inspectStatus !== 0) {
        return new CrankwaveRuntimeError(
          `${operation} failed with ${statusName(fallbackStatus)}`,
          {
            operation,
            status: fallbackStatus,
            statusName: statusName(fallbackStatus),
            diagnostics: [],
          },
        );
      }
      const view = heap.view;
      const status = view.getUint32(infoPointer + info.status, true);
      const stage = view.getUint32(infoPointer + info.stage, true);
      const code = view.getUint32(infoPointer + info.code, true);
      const detailBytes = view.getUint32(infoPointer + info.detailBytes, true);
      const messageBytes = view.getUint32(infoPointer + info.messageBytes, true);
      const diagnosticCount = view.getUint32(
        infoPointer + info.diagnosticCount,
        true,
      );
      const [detailCode, message] = readStringSet(
        heap,
        [
          {
            bytes: detailBytes,
            dataOffset: Layout.errorTextBuffers.detailData,
            capacityOffset: Layout.errorTextBuffers.detailCapacity,
            purpose: "C API error detail code",
          },
          {
            bytes: messageBytes,
            dataOffset: Layout.errorTextBuffers.messageData,
            capacityOffset: Layout.errorTextBuffers.messageCapacity,
            purpose: "C API error message",
          },
        ],
        Layout.errorTextBuffers,
        (buffers) =>
          module._crankwave_context_copy_last_error_text(context, buffers),
      );
      const diagnostics = [];
      for (let index = 0; index < diagnosticCount; ++index) {
        diagnostics.push(readDiagnostic(module, heap, context, index));
      }
      return new CrankwaveRuntimeError(
        message || `${operation} failed with ${statusName(status)}`,
        {
          operation,
          status,
          statusName: statusName(status),
          stage,
          stageName: errorStageName(stage),
          code,
          detailCode,
          diagnostics,
        },
      );
    },
  );
}
