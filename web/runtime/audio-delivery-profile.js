// Runtime delivery rates are separate from the native authoring/canonical
// rate. The 192 kHz path remains the accepted baseline; 48 kHz is the
// experimental game-delivery profile.

export const CRANKWAVE_CANONICAL_DELIVERY_SAMPLE_RATE = 192_000;
export const CRANKWAVE_GAME_DELIVERY_SAMPLE_RATE = 48_000;
export const CRANKWAVE_CANONICAL_DELIVERY_PROFILE = "canonical-192k-v1";
export const CRANKWAVE_GAME_DELIVERY_PROFILE = "game-48k-v1";

const SUPPORTED_DELIVERY_SAMPLE_RATES = new Set([
  CRANKWAVE_GAME_DELIVERY_SAMPLE_RATE,
  CRANKWAVE_CANONICAL_DELIVERY_SAMPLE_RATE,
]);

export function isSupportedAudioDeliverySampleRate(value) {
  return Number.isSafeInteger(value) && SUPPORTED_DELIVERY_SAMPLE_RATES.has(value);
}

export function requireSupportedAudioDeliverySampleRate(value, label = "audio sample rate") {
  if (!isSupportedAudioDeliverySampleRate(value)) {
    throw new TypeError(
      `${label} must be ${CRANKWAVE_GAME_DELIVERY_SAMPLE_RATE} or ` +
        `${CRANKWAVE_CANONICAL_DELIVERY_SAMPLE_RATE} Hz`,
    );
  }
  return value;
}

export function deliveryProfileForAudio(sampleRate, blockFrames = undefined) {
  if (sampleRate === CRANKWAVE_CANONICAL_DELIVERY_SAMPLE_RATE) {
    if (blockFrames !== undefined && blockFrames !== 3_840) return null;
    return CRANKWAVE_CANONICAL_DELIVERY_PROFILE;
  }
  if (sampleRate === CRANKWAVE_GAME_DELIVERY_SAMPLE_RATE) {
    if (blockFrames !== undefined && blockFrames !== 8_192) return null;
    return CRANKWAVE_GAME_DELIVERY_PROFILE;
  }
  return null;
}

export function validateDeliveryProfileMetadata(audio, label = "audio") {
  if (audio?.delivery_profile === undefined) return;
  if (typeof audio.delivery_profile !== "string") {
    throw new TypeError(`${label}.delivery_profile must be a string`);
  }
  const expected = deliveryProfileForAudio(audio.sample_rate_hz, audio.block_frames);
  if (expected === null || audio.delivery_profile !== expected) {
    throw new TypeError(`${label}.delivery_profile does not match its audio geometry`);
  }
}
