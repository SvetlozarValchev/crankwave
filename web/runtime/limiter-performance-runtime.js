import { isSupportedAudioDeliverySampleRate } from "./audio-delivery-profile.js";
import {
  decodeAudioPayload,
  PCM16_SCALE_ENCODING,
  validatePcm16Scale,
} from "./pcm-payload.js";

// Native captured exhaust during limiter cuts and combustion recovery. All
// samples stay on their original time axis; host RPM never pitches this layer.
export async function loadLimiterPerformance(url, { fetch, crypto }) {
  const response = await fetch(url.href);
  if (!response.ok) throw new Error('Cannot load limiter performance manifest.');
  const manifest = JSON.parse(new TextDecoder().decode(await response.arrayBuffer()));
  const integer = v => Number.isSafeInteger(v) && v >= 0;
  if (manifest.schema !== 'crankwave/limiter-performance-v1' || !isSupportedAudioDeliverySampleRate(manifest.sample_rate_hz) ||
      !['float32le', PCM16_SCALE_ENCODING].includes(manifest.encoding ?? 'float32le') ||
      !Number.isFinite(manifest.idle_rpm) || manifest.idle_rpm <= 0 ||
      !Number.isFinite(manifest.activation_rpm) || manifest.activation_rpm <= 0 ||
      !Number.isFinite(manifest.cut_seconds) || manifest.cut_seconds <= 0 || manifest.cut_seconds > 2 ||
      !Number.isFinite(manifest.maximum_rpm) || manifest.maximum_rpm <= manifest.activation_rpm ||
      !integer(manifest.frame_count) || manifest.frame_count <= 0 || manifest.frame_count > manifest.sample_rate_hz * 60 ||
      !integer(manifest.fade_frames) || manifest.fade_frames <= 0 || manifest.fade_frames > 1920 ||
      !Array.isArray(manifest.clips) || manifest.clips.length < 1 || manifest.clips.length > 64 ||
      !Array.isArray(manifest.routes) || manifest.routes.length < 1)
    throw new Error('Invalid limiter performance geometry.');
  let end = 0;
  for (const clip of manifest.clips) {
    const coordinates = ['start_frame', 'hold_start_frame', 'hold_end_frame', 'release_frame', 'end_frame'].map(k => clip[k]);
    if (coordinates.some(v => !integer(v)) || coordinates[0] !== end ||
        coordinates.some((v, i) => i && v <= coordinates[i - 1]) ||
        clip.hold_end_frame - clip.hold_start_frame <= 2 * manifest.fade_frames ||
        clip.end_frame - clip.release_frame <= manifest.fade_frames)
      throw new Error('Invalid limiter performance clip.');
    end = clip.end_frame;
  }
  if (end !== manifest.frame_count) throw new Error('Limiter clip coverage is incomplete.');
  const routes = [];
  const ids = new Set();
  for (const route of manifest.routes) {
    if (typeof route.bus_id !== 'string' || ids.has(route.bus_id) ||
        !/^[a-zA-Z0-9_-]+\.(?:f32|pcm16le)$/.test(route.file))
      throw new Error('Invalid limiter route.');
    ids.add(route.bus_id);
    const response = await fetch(new URL(route.file, url).href);
    if (!response.ok) throw new Error('Missing limiter dry PCM.');
    const bytes = await response.arrayBuffer();
    const digest = Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256', bytes)), v => v.toString(16).padStart(2, '0')).join('');
    const encoding = manifest.encoding ?? 'float32le';
    if (encoding === PCM16_SCALE_ENCODING) {
      validatePcm16Scale(route.quantization?.scale, `limiter route ${route.bus_id} quantization.scale`);
    }
    const samples = decodeAudioPayload(
      new Uint8Array(bytes),
      manifest.frame_count,
      encoding,
      route.quantization,
      `limiter route ${route.bus_id}`,
    );
    if (digest !== route.sha256) throw new Error('Limiter dry PCM hash/length mismatch.');
    if (!samples.every(Number.isFinite)) throw new Error('Non-finite limiter PCM.');
    if (!Array.isArray(route.reference_rms) || route.reference_rms.length !== manifest.clips.length ||
        route.reference_rms.some(x => !Number.isFinite(x) || x <= 0)) throw new Error('Invalid limiter running reference RMS.');
    routes.push(Object.freeze({ busId: route.bus_id, samples, referenceRms: route.reference_rms }));
  }
  return Object.freeze({ manifest, routes: Object.freeze(routes) });
}

export class LimiterPerformanceCursor {
  constructor(package_) {
    this.package = package_;
    this.reset();
  }
  reset() {
    this.cut = false;
    this.clipIndex = -1;
    this.position = 0;
    this.active = false;
    this.age = 0;
    this.triggerCount = 0;
    this.referencePower = this.package.routes.map(() => 0);
    this.captureGain = this.package.routes.map(() => 1);
  }
  setCut(cut) {
    if (cut === this.cut) return;
    this.cut = cut;
    if (cut) {
      this.clipIndex = (this.clipIndex + 1) % this.package.manifest.clips.length;
      this.position = this.package.manifest.clips[this.clipIndex].start_frame;
      this.active = true;
      this.age = 0;
      ++this.triggerCount;
      this.captureGain = this.package.routes.map((route, i) =>
        Math.sqrt(this.referencePower[i]) / route.referenceRms[this.clipIndex]);
    } else if (this.active) {
      // Recorded release is selected explicitly, so downhill overspeed can
      // hold motoring indefinitely without playing the capture's next firing.
      this.position = this.package.manifest.clips[this.clipIndex].release_frame;
    }
  }
  mix(routes, cut, sourceScale = 1) {
    this.setCut(cut);
    const { clips, fade_frames: fade } = this.package.manifest;
    const clip = this.active ? clips[this.clipIndex] : null;
    const alpha = 1 - Math.exp(-1 / (this.package.manifest.sample_rate_hz * .025));
    for (let frame = 0; frame < routes[0].length; ++frame) {
      for (let r = 0; r < routes.length; ++r) {
        const base = routes[r][frame] / sourceScale;
        this.referencePower[r] += (base * base - this.referencePower[r]) * alpha;
      }
      if (!this.active) continue;
      let wrapAmount = 0;
      if (this.cut && this.position >= clip.hold_end_frame - fade) {
        if (this.position >= clip.hold_end_frame) this.position = clip.hold_start_frame + fade;
        else wrapAmount = (this.position - (clip.hold_end_frame - fade)) / fade;
      }
      let gain = Math.min(1, this.age / fade);
      if (!this.cut) gain = Math.min(gain, (clip.end_frame - this.position) / fade);
      for (let route = 0; route < routes.length; ++route) {
        const pcm = this.package.routes[route].samples;
        let value = pcm[this.position];
        if (wrapAmount > 0) {
          const other = clip.hold_start_frame + this.position - (clip.hold_end_frame - fade);
          value += (pcm[other] - value) * wrapAmount;
        }
        routes[route][frame] += (value * sourceScale * this.captureGain[route] - routes[route][frame]) * gain;
      }
      ++this.age;
      ++this.position;
      if (!this.cut && this.position >= clip.end_frame) this.active = false;
    }
    return routes;
  }
}
