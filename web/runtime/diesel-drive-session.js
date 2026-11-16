import {SharedRecordedStarterCursor} from './shared-recorded-starter-runtime.js';
import {MotoredTorqueBank, BackdriveRadiation} from './diesel-backdrive-runtime.js';

const smooth = x => { x = Math.max(0, Math.min(1, x)); return x*x*(3-2*x); };
export function dieselPickupPoint(curve, seconds) {
  for (let i = 0; i < curve.length - 1; ++i) {
    const a = curve[i], b = curve[i+1];
    if (seconds <= b.seconds) {
      const w = smooth((seconds-a.seconds)/(b.seconds-a.seconds));
      return {rpm:a.rpm+(b.rpm-a.rpm)*w, load01:a.fuel+(b.fuel-a.fuel)*w,
        throttle01:a.fuel+(b.fuel-a.fuel)*w};
    }
  }
  const a = curve.at(-1);
  return {rpm:a.rpm, load01:a.fuel, throttle01:a.fuel};
}

// Explicit optional host lifecycle. The engine still consumes ordinary in-grid
// RPM/fuel points; only the reused recording supplies sub-idle starter audio.
export class DieselDriveSession {
  constructor(engine, runtime) {
    if (!runtime.drivePackage) throw new TypeError('A packaged diesel driving profile is required');
    this.engine = engine;
    this.package = runtime.drivePackage;
    this.profile = this.package.manifest;
    this.bank = new MotoredTorqueBank(runtime, this.package.geometry);
    this.reset();
  }

  reset({startEngine = true} = {}) {
    const e = this.engine, p = this.profile.pickup, fs = e.sampleRate;
    this.frame = 0;
    this.startEngine = startEngine;
    this.entryFrame = Math.round(p.running_seconds*fs);
    this.pickupEndFrame = this.entryFrame + Math.round(p.curve.at(-1).seconds*fs);
    this.reaction = new BackdriveRadiation(this.bank, this.profile.backdrive, fs);
    this.pendingReaction = new Float32Array(e.processFrames);
    this.previousReverse = 0;
    this.previousCoupling = 0;
    if (startEngine) e.setOperatingPoint(dieselPickupPoint(p.curve,0));
    e.reset();
    this.previousRpm = e.operatingPoint.rpm;
    this.crankRevolutions = this.previousRpm*e.latencyFrames/fs/60;
    this.starter = new SharedRecordedStarterCursor(this.package.starter);
    this.starter.reset();
    const state = (frame,starter,rpm,combustionDetected) => this.starter.setState({
      frame,starter,rpm,combustionDetected,ignition:true,fuel:true});
    state(0,false,0,false);
    if (startEngine) {
      state(Math.round(p.crank_seconds*fs),true,p.crank_rpm,false);
      state(this.entryFrame+e.latencyFrames,true,p.curve[0].rpm,true);
      state(this.entryFrame+e.latencyFrames+Math.round(p.release_seconds*fs),false,p.curve.at(-1).rpm,true);
    }
    this.lastPoint = e.operatingPoint;
    this.lastReactionRms = 0;
  }

  get starting() { return this.startEngine && this.frame < this.pickupEndFrame; }

  snapshot() {
    const p = this.profile.pickup;
    const stage = !this.startEngine || this.frame >= this.pickupEndFrame ? 'Running' :
      this.frame < this.entryFrame ? 'Starting' : 'Pickup';
    return {stage, rpm:stage === 'Starting' ? (this.frame < p.crank_seconds*this.engine.sampleRate ? 0 : p.crank_rpm) :
      this.lastPoint.rpm, backdriveRms:this.lastReactionRms, starter:this.starter.diagnostics()};
  }

  process(point) {
    const e = this.engine, n = e.processFrames, fs = e.sampleRate, p = this.profile.pickup;
    const zero = new Float32Array(n);
    const starter = this.starter.mixPair(zero,zero).bakedBlock;
    let running = zero, radiation = zero;
    if (!this.startEngine || this.frame >= this.entryFrame) {
      if (this.starting) point = dieselPickupPoint(p.curve, (this.frame-this.entryFrame)/fs);
      const reverse = point.backdriveTorqueNm ?? 0, coupling = point.coupling01 ?? 0;
      if (!Number.isFinite(reverse) || reverse < 0 || !Number.isFinite(coupling) || coupling < 0 || coupling > 1)
        throw new RangeError('Invalid backdrive controls');
      running = e.process(point, n);
      radiation = this.pendingReaction;
      const next = new Float32Array(n), duration = n/fs, slope = (point.rpm-this.previousRpm)/duration;
      for (let j = 0; j < n; ++j) {
        const dt = (j+.5)/fs, w = dt/duration;
        const phase = this.crankRevolutions+(this.previousRpm*dt+.5*slope*dt*dt)/60;
        next[j] = this.reaction.sample(this.previousRpm+slope*dt,phase,
          this.previousReverse+(reverse-this.previousReverse)*w,
          this.previousCoupling+(coupling-this.previousCoupling)*w);
        if (this.startEngine) running[j] *= smooth(
          (this.frame+j-this.entryFrame-e.latencyFrames)/(p.onset_seconds*fs));
      }
      this.crankRevolutions += (this.previousRpm+point.rpm)*.5*duration/60;
      this.previousRpm = point.rpm;
      this.previousReverse = reverse;
      this.previousCoupling = coupling;
      this.pendingReaction = next;
      this.lastPoint = point;
    }
    const gain = 10**(this.profile.gain_db/20);
    const output = new Float32Array(n);
    let reactionEnergy = 0;
    for (let j = 0; j < n; ++j) {
      // Preserve the audition's two Float32 summation boundaries.
      const combined = Math.fround(running[j]+radiation[j]);
      output[j] = Math.fround(combined+starter[j])*gain;
      if (!Number.isFinite(output[j])) throw new RangeError('Non-finite diesel session output');
      reactionEnergy += radiation[j]**2;
    }
    this.lastReactionRms = Math.sqrt(reactionEnergy/n);
    this.frame += n;
    return output;
  }
}
