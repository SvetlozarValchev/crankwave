import { ResponsiveAudioLifecycleCursor } from './responsive-audio-lifecycle-runtime.js';
import { SharedRecordedStarterCursor } from './shared-recorded-starter-runtime.js';

const clamp = (x, lo, hi) => Math.max(lo, Math.min(hi, x));

function weights(lanes, value) {
  const result = lanes.map(lane => ({id: lane.id, weight: 0}));
  if (value <= lanes[0].throttle01) result[0].weight = 1;
  else if (value >= lanes.at(-1).throttle01) result.at(-1).weight = 1;
  else {
    const right = lanes.findIndex(lane => lane.throttle01 >= value), left = right - 1;
    const t = (value - lanes[left].throttle01) / (lanes[right].throttle01 - lanes[left].throttle01);
    result[left].weight = 1-t; result[right].weight = t;
  }
  return result;
}

// Optional audition host for a complete responsive carrier. Starting follows
// the carrier's measured startup telemetry; the existing lifecycle cursor owns
// first-fire/pickup/handoff audio and the shared cursor reuses recorded starter.
// Running RPM/load remain owned by the caller. No engine physics is invented.
export class ResponsiveDriveSession {
  constructor(engine, runtime) {
    if (!runtime.lifecyclePackage || !runtime.sharedRecordedStarterPackage)
      throw new TypeError('Responsive drive needs packaged lifecycle and recorded starter');
    this.engine = engine;
    this.runtime = runtime;
    this.package = runtime.lifecyclePackage;
    this.startupSource = this.package.provenanceSources.find(s => s.role === 'startup');
    if (!this.startupSource?.evidence.points.length)
      throw new TypeError('Packaged startup telemetry is required');
    this.startupFrames = this.package.startup.artifact.frameCount;
    this.lanes = runtime.heldPackage.manifest.domain.load_lanes;
    this.lifecycle = new ResponsiveAudioLifecycleCursor(this.package, {
      audioLatencyFrames: engine.latencyFrames,
      runningFloorRpm: runtime.minimumRpm,
      heldAnchorFloorRpm: runtime.heldMinimumRpm,
      atlasLoadLanes: this.lanes,
      atlasLoadCoordinate: runtime.heldPackage.loadCoordinate,
    });
    this.starter = new SharedRecordedStarterCursor(runtime.sharedRecordedStarterPackage,
      {audioLatencyFrames: engine.latencyFrames});
    this.reset();
  }

  reset({startEngine = true} = {}) {
    this.frame = 0;
    this.startEngine = startEngine;
    this.shutdown = null;
    this.crankRevolutions = 0;
    this.lifecycle.reset(); this.starter.reset();
    const initial = this.startupSource.scenario.initial_state;
    this.lastPhysical = startEngine ? {
      rpm: 0, starter: initial.starter_enabled, ignition: initial.ignition_enabled,
      fuel: initial.fuel_enabled, torque: null,
    } : {rpm: this.engine.operatingPoint.rpm, starter: false, ignition: true, fuel: true, torque: null};
    this.lastPoint = {...this.engine.operatingPoint, throttle01: 0, load01: 0};
    this.engine.setOperatingPoint({...this.lastPoint, rpm: clamp(this.lastPhysical.rpm,
      this.engine.minimumRpm, this.engine.maximumRpm)});
    this.engine.reset();
    this.schedule(this.lastPhysical, this.lastPoint, 0, 0);
  }

  get starting() {
    return this.startEngine && this.shutdown === null &&
      this.lifecycle.diagnostics().outputMode !== 'running';
  }

  capturedPoint(source, frame, fallback) {
    const points = source.evidence.points;
    const right = points.findIndex(p => p.source_frame + 1 >= frame);
    const b = right < 0 ? points.at(-1) : points[right];
    const a = right > 0 ? points[right-1] : null;
    const startFrame = a ? a.source_frame + 1 : 0;
    const t = clamp((frame-startFrame)/(b.source_frame+1-startFrame || 1), 0, 1);
    return {
      rpm: Math.abs((a?.rpm ?? fallback.rpm) + (b.rpm-(a?.rpm ?? fallback.rpm))*t),
      starter: b.starter_enabled, ignition: b.ignition_enabled, fuel: b.fuel_enabled,
      torque: b.indicated_gas_availability === 1 ? b.indicated_gas_torque_nm : null,
    };
  }

  schedule(physical, point, frame, slope) {
    const rpm = clamp(physical.rpm, this.engine.minimumRpm, this.engine.maximumRpm);
    this.lifecycle.setState({
      frame, starter: physical.starter, ignition: physical.ignition, fuel: physical.fuel,
      rpm: physical.rpm, runningBedReady: true, requestedThrottle01: point.throttle01,
      manifoldPressurePaAbs: this.engine.loadManifoldPressurePa(rpm, point.load01),
      rpmSlopeRpmPerSecond: slope,
      admissionLaneWeights: weights(this.lanes, point.throttle01),
      runningBedLaneWeights: weights(this.lanes, point.load01),
      unwrappedCrankRevolutions: this.crankRevolutions, indicatedGasTorqueNm: physical.torque,
    });
    this.starter.setState({frame, starter: physical.starter, ignition: physical.ignition,
      fuel: physical.fuel, rpm: physical.rpm,
      combustionDetected: physical.ignition && physical.fuel && physical.torque !== null && physical.torque > 0});
  }

  stopEngine() {
    if (this.shutdown !== null) return;
    const elevated = this.lastPhysical.rpm > this.runtime.heldMinimumRpm * 1.2;
    const role = elevated && this.package.shutdownElevated ? 'shutdown-elevated' : 'shutdown';
    const source = this.package.provenanceSources.find(s => s.role === role);
    const points = source.evidence.points.filter(p => !p.ignition_enabled);
    const nearest = points.reduce((a,b) => Math.abs(b.rpm-this.lastPhysical.rpm) <
      Math.abs(a.rpm-this.lastPhysical.rpm) ? b : a);
    this.shutdown = {source, entry: nearest.source_frame + 1, frame: this.frame};
    this.startEngine = false;
    this.lastPhysical = {...this.lastPhysical, starter: false, ignition: false, torque: null};
    this.schedule(this.lastPhysical, this.lastPoint, this.frame, 0);
  }

  process(point) {
    const e = this.engine, n = e.processFrames, duration = n/e.sampleRate;
    let physical;
    if (this.shutdown) {
      physical = this.capturedPoint(this.shutdown.source,
        this.shutdown.entry + this.frame + n - this.shutdown.frame, this.lastPhysical);
      physical = {...physical, starter: false, ignition: false};
      point = {rpm: clamp(physical.rpm,e.minimumRpm,e.maximumRpm), throttle01: 0, load01: 0};
    } else if (this.starting) {
      physical = this.capturedPoint(this.startupSource, Math.min(this.frame+n,this.startupFrames), {rpm: 0});
      point = {rpm: clamp(physical.rpm,e.minimumRpm,e.maximumRpm), throttle01: 0, load01: 0};
    } else {
      physical = {rpm: point.rpm, starter: false, ignition: true, fuel: true, torque: null};
    }
    const running = e.process(point, n);
    const slope = (physical.rpm-this.lastPhysical.rpm)/duration;
    this.crankRevolutions += (physical.rpm+this.lastPhysical.rpm)*.5*duration/60;
    this.schedule(physical,point,this.frame+n,slope);
    const silence = new Float32Array(n);
    const lifecycle = this.lifecycle.mixPair(silence,running).bakedBlock;
    const output = this.starter.mixPair(silence,lifecycle).bakedBlock;
    this.frame += n;
    this.lastPhysical = physical; this.lastPoint = point;
    return output;
  }

  snapshot() {
    const life = this.lifecycle.diagnostics();
    return {stage: this.shutdown ? (this.lastPhysical.rpm <= 1 ? 'Stopped' : 'Stopping') :
      this.starting ? (life.startupCount ? 'Pickup' : 'Starting') : 'Running',
      rpm: this.lastPhysical.rpm, lifecycle: life, starter: this.starter.diagnostics()};
  }
}
