import {createHash} from "node:crypto";

export const digest = (bytes) => createHash("sha256").update(bytes).digest("hex");
export const jsonBytes = (value) => Buffer.from(`${JSON.stringify(value)}\n`);

export function packTestCarrier(entries) {
  const ordered = [...entries].sort(([a], [b]) => a < b ? -1 : a > b ? 1 : 0);
  const indexBytes = ordered.reduce((n, [name]) => n + 56 + Buffer.byteLength(name), 0);
  const payloadStart = 128 + indexBytes;
  const out = Buffer.alloc(
      payloadStart + ordered.reduce((n, [, bytes]) => n + bytes.length, 0));
  out.write("CRKWAVE1");
  out.writeUInt16LE(1, 8);
  out.writeUInt16LE(128, 10);
  out.writeUInt32LE(ordered.length, 16);
  out.writeUInt32LE(56, 20);
  for (const [offset, value] of [
           [24, 128], [32, indexBytes], [40, payloadStart],
           [48, out.length - payloadStart], [56, out.length]])
    out.writeBigUInt64LE(BigInt(value), offset);
  let index = 128;
  let payload = payloadStart;
  for (const [name, bytes] of ordered) {
    const path = Buffer.from(name);
    out.writeUInt16LE(path.length, index);
    out.writeBigUInt64LE(BigInt(payload), index + 8);
    out.writeBigUInt64LE(BigInt(bytes.length), index + 16);
    Buffer.from(digest(bytes), "hex").copy(out, index + 24);
    path.copy(out, index + 56);
    Buffer.from(bytes).copy(out, payload);
    index += 56 + path.length;
    payload += bytes.length;
  }
  Buffer.from(digest(out.subarray(128, payloadStart)), "hex").copy(out, 64);
  Buffer.from(digest(out.subarray(payloadStart)), "hex").copy(out, 96);
  return out;
}
