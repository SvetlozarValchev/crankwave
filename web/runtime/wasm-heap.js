export class WasmHeapExhaustedError extends Error {
  constructor(byteCount, purpose) {
    super(`the wasm heap could not allocate ${byteCount} bytes for ${purpose}`);
    this.name = "WasmHeapExhaustedError";
    this.byteCount = byteCount;
    this.purpose = purpose;
  }
}

export class WasmHeap {
  #module;
  #buffer;
  #view;
  #encoder = new TextEncoder();
  #decoder = new TextDecoder("utf-8", { fatal: true });

  constructor(module) {
    if (
      typeof module?._malloc !== "function" ||
      typeof module?._free !== "function" ||
      !(module.HEAPU8 instanceof Uint8Array)
    ) {
      throw new TypeError("the Emscripten module does not expose its heap");
    }
    this.#module = module;
    this.#buffer = module.HEAPU8.buffer;
    this.#view = new DataView(this.#buffer);
  }

  get module() {
    return this.#module;
  }

  get buffer() {
    this.#refreshMemory();
    return this.#buffer;
  }

  get bytes() {
    this.#refreshMemory();
    return this.#module.HEAPU8;
  }

  get view() {
    this.#refreshMemory();
    return this.#view;
  }

  allocate(byteCount, purpose = "runtime data") {
    if (!Number.isSafeInteger(byteCount) || byteCount < 0) {
      throw new RangeError("wasm allocation size must be a non-negative safe integer");
    }
    const allocationSize = Math.max(1, byteCount);
    const pointer = this.#module._malloc(allocationSize);
    if (pointer === 0) {
      throw new WasmHeapExhaustedError(allocationSize, purpose);
    }
    this.bytes.fill(0, pointer, pointer + allocationSize);
    return pointer;
  }

  free(pointer) {
    if (pointer !== 0) {
      this.#module._free(pointer);
    }
  }

  encodeUtf8(text) {
    if (typeof text !== "string") {
      throw new TypeError("JSON and identifiers must be UTF-8 strings");
    }
    return this.#encoder.encode(text);
  }

  decodeUtf8(pointer, byteCount) {
    if (byteCount === 0) {
      return "";
    }
    return this.#decoder.decode(this.bytes.subarray(pointer, pointer + byteCount));
  }

  copyIn(bytes, purpose = "input bytes") {
    const source = asUint8Array(bytes);
    const pointer = this.allocate(source.byteLength, purpose);
    this.bytes.set(source, pointer);
    return { pointer, byteLength: source.byteLength };
  }

  copyOut(pointer, byteCount) {
    return this.bytes.slice(pointer, pointer + byteCount);
  }

  #refreshMemory() {
    if (this.#module.HEAPU8.buffer !== this.#buffer) {
      this.#buffer = this.#module.HEAPU8.buffer;
      this.#view = new DataView(this.#buffer);
    }
  }
}

export function asUint8Array(value) {
  if (value instanceof Uint8Array) {
    return value;
  }
  if (
    value instanceof ArrayBuffer ||
    (typeof SharedArrayBuffer !== "undefined" &&
      value instanceof SharedArrayBuffer)
  ) {
    return new Uint8Array(value);
  }
  if (ArrayBuffer.isView(value)) {
    return new Uint8Array(value.buffer, value.byteOffset, value.byteLength);
  }
  throw new TypeError("asset payload must be an ArrayBuffer or typed-array view");
}

export function withWasmAllocations(heap, allocations, operation) {
  const pointers = [];
  try {
    for (const allocation of allocations) {
      pointers.push(heap.allocate(allocation.bytes, allocation.purpose));
    }
    return operation(pointers);
  } finally {
    for (let index = pointers.length - 1; index >= 0; --index) {
      heap.free(pointers[index]);
    }
  }
}
