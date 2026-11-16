// Audition host only: a UAZ-shaped road load and automatic driver, not engine simulation.
// Ratios, shift/clutch policy and torque envelope follow the Godot UAZ powertrain.
// The carrier owns sound; none of these values change its samples or presentation.
const RATIOS = Object.freeze([4.124, 2.641, 1.58, 1]);
const FINAL_DRIVE = 5.125;
const WHEEL_RADIUS = 0.4;
const MASS = 1650; // 469B curb mass + driver and modest gear; matches the Godot UAZ.
const SHIFT_SECONDS = 0.85;
const SHIFT_COOLDOWN = 1;
// UMZ-451M: published 166.7 Nm at 2,200–2,500 RPM and 75 PS at 4,000 RPM.
// Intermediate points are an authored approximation, not a measured dynamometer trace.
const TORQUE_CURVE = Object.freeze([
  [650, 100], [1000, 130], [1500, 150], [2000, 165], [2200, 166.7],
  [2500, 166.7], [3000, 155], [3500, 142], [4000, 131.7], [4300, 115],
]);
const STEP = 1 / 120;
const clamp = (value, lo, hi) => Math.max(lo, Math.min(hi, value));
const approach = (value, target, amount) =>
  value + clamp(target - value, -amount, amount);
const smoothstep = (lo, hi, value) => {
  const t = clamp((value - lo) / (hi - lo), 0, 1);
  return t * t * (3 - 2 * t);
};

export function umz451mTorque(rpm) {
  if (rpm <= TORQUE_CURVE[0][0]) return TORQUE_CURVE[0][1];
  for (let i = 1; i < TORQUE_CURVE.length; ++i) {
    const [rightRpm, rightTorque] = TORQUE_CURVE[i];
    if (rpm <= rightRpm) {
      const [leftRpm, leftTorque] = TORQUE_CURVE[i - 1];
      return leftTorque + (rightTorque - leftTorque) * (rpm - leftRpm) / (rightRpm - leftRpm);
    }
  }
  // Other carriers can cover much higher RPM; the UAZ audition rig still has
  // only the rated engine power, even outside its normal operating range.
  return Math.min(TORQUE_CURVE.at(-1)[1], 55166.367 * 30 / (Math.PI * rpm));
}

export class DrivingModel {
  constructor(minimumRpm, maximumRpm, limiter = null, { idleRpm = 650 } = {}) {
    if (!Number.isFinite(minimumRpm) || !Number.isFinite(maximumRpm) ||
        minimumRpm <= 0 || maximumRpm <= minimumRpm) {
      throw new RangeError("Driving mode requires a finite positive RPM range.");
    }
    if (!Number.isFinite(idleRpm) || idleRpm <= 0) {
      throw new RangeError("Driving idle RPM must be finite and positive.");
    }
    this.minimumRpm = minimumRpm;
    this.maximumRpm = maximumRpm;
    this.idleRpm = clamp(idleRpm, minimumRpm, minimumRpm + (maximumRpm - minimumRpm) * 0.1);
    this.limiter = limiter;
    if (limiter !== null) this.idleRpm = clamp(limiter.idleRpm, minimumRpm, maximumRpm);
    this.span = maximumRpm - this.idleRpm;
    this.reset();
  }

  reset() {
    this.selector = "D";
    this.gear = 1;
    this.speed = 0;
    this.distance = 0;
    this.rpm = this.idleRpm;
    this.launchRpm = this.idleRpm;
    this.throttle = 0;
    this.brake = 0;
    this.pedal = 0;
    this.clutch = 0;
    this.load = 0;
    this.engineThrottle = 0;
    this.shiftElapsed = -1;
    this.targetGear = 1;
    this.shiftCooldown = 0;
    this.accumulator = 0;
    this.limiterTimer = 0;
    this.limiterCutActive = false;
    this.limiterCuts = 0;
    return this.snapshot();
  }

  setControls({ selector, throttle01, brake01 }) {
    if (selector !== "N" && selector !== "D") {
      throw new RangeError("Select N or D.");
    }
    for (const value of [throttle01, brake01]) {
      if (!Number.isFinite(value) || value < 0 || value > 1) {
        throw new RangeError("Pedals must lie in [0, 1].");
      }
    }
    if (selector !== this.selector) {
      this.shiftElapsed = -1;
      this.clutch = 0;
      this.shiftCooldown = SHIFT_COOLDOWN;
      // Re-engage a sensible gear at the current road speed, never first at highway speed.
      this.gear = 1;
      while (this.gear < RATIOS.length &&
          this.shaftRpm(this.gear) > this.shiftRpm(0.5)) {
        this.gear += 1;
      }
    }
    this.selector = selector;
    this.throttle = throttle01;
    this.brake = brake01;
    return this.snapshot();
  }

  shaftRpm(gear) {
    return this.speed / WHEEL_RADIUS * 60 / (2 * Math.PI) * RATIOS[gear - 1] * FINAL_DRIVE;
  }

  shiftRpm(pedal) {
    return clamp(2000 + 600 * pedal, this.minimumRpm, this.maximumRpm * 0.94);
  }

  operatingPoint() {
    return { rpm: this.rpm, throttle01: this.engineThrottle, load01: this.load,
      ...(this.limiter === null ? {} : { limiterCutActive: this.limiterCutActive }) };
  }

  snapshot() {
    return {
      selector: this.selector,
      gear: this.selector === "N" ? "N" : String(this.gear),
      speedKph: this.speed * 3.6,
      distanceM: this.distance,
      rpm: this.rpm,
      throttle01: this.throttle,
      brake01: this.brake,
      clutch01: this.clutch,
      load01: this.load,
      shifting: this.shiftElapsed >= 0,
      limiterCutActive: this.limiterCutActive,
      limiterCuts: this.limiterCuts,
    };
  }

  advance(seconds) {
    if (!Number.isFinite(seconds) || seconds < 0 || seconds > 1) {
      throw new RangeError("Driving timestep must lie in [0, 1] seconds.");
    }
    this.accumulator += seconds;
    while (this.accumulator + 1e-12 >= STEP) {
      this.step();
      this.accumulator = Math.max(0, this.accumulator - STEP);
    }
    return this.operatingPoint();
  }

  updateLimiter(rpm) {
    if (this.limiter === null) return;
    const wasCut = this.limiterCutActive;
    this.limiterTimer = Math.max(0, this.limiterTimer - STEP);
    if (rpm > this.limiter.activationRpm) this.limiterTimer = this.limiter.cutSeconds;
    this.limiterCutActive = this.limiterTimer > 0;
    if (this.limiterCutActive && !wasCut) ++this.limiterCuts;
  }

  step() {
    const pedalTarget = this.brake > 0.1 ? 0 : this.throttle;
    this.pedal = approach(this.pedal, pedalTarget, STEP * (pedalTarget > this.pedal ? 1.25 : 5));
    this.shiftCooldown = Math.max(0, this.shiftCooldown - STEP);
    let driveForce = 0;
    let engineBrake = 0;
    if (this.selector === "N") {
      this.launchRpm = this.idleRpm;
      this.updateLimiter(this.rpm);
      const target = this.limiter === null ? this.idleRpm + this.pedal * this.span * 0.98
        : this.limiterCutActive ? this.idleRpm
        : this.idleRpm + this.pedal * (this.limiter.activationRpm + 500 - this.idleRpm);
      this.rpm = approach(this.rpm, target, STEP * (this.limiter === null ? this.span * (target > this.rpm ? 0.7 : 0.4) : 2600));
      this.clutch = 0;
      this.engineThrottle = this.pedal;
      this.load = this.pedal * 0.15;
    } else {
      let shaft = this.shaftRpm(this.gear);
      if (this.shiftElapsed < 0 && this.shiftCooldown === 0) {
        let next = this.gear;
        const upshiftRpm = this.shiftRpm(this.pedal);
        if (this.gear < RATIOS.length && shaft > upshiftRpm) {
          next += 1;
        } else if (this.gear > 1 && shaft < 1050 + 250 * this.pedal &&
            this.shaftRpm(this.gear - 1) < upshiftRpm - 200) {
          next -= 1;
        }
        if (next !== this.gear) {
          this.targetGear = next;
          this.shiftElapsed = 0;
        }
      }
      let shiftClutch = 1;
      if (this.shiftElapsed >= 0) {
        this.shiftElapsed += STEP;
        if (this.shiftElapsed >= 0.25) this.gear = this.targetGear;
        shaft = this.shaftRpm(this.gear);
        shiftClutch = smoothstep(0.4, SHIFT_SECONDS, this.shiftElapsed);
        if (this.shiftElapsed >= SHIFT_SECONDS) {
          this.shiftElapsed = -1;
          this.shiftCooldown = SHIFT_COOLDOWN;
        }
      }
      // Ease the automatic driver's launch demand instead of immediately commanding
      // the full ~1,000 RPM boost. Shaft-driven RPM still follows the actual wheels.
      const requestedLaunchRpm = this.idleRpm + this.pedal * this.span * 0.28;
      this.launchRpm = approach(this.launchRpm, requestedLaunchRpm,
        STEP * this.span * (requestedLaunchRpm > this.launchRpm ? 0.25 : 0.4));
      const launchRpm = this.launchRpm;
      const target = this.shiftElapsed >= 0 ? Math.max(this.idleRpm, shaft) : Math.max(launchRpm, shaft);
      this.rpm = approach(this.rpm, Math.min(target, this.maximumRpm), STEP * this.span * 1.55);
      const launchClutch = Math.max(clamp(shaft / launchRpm, 0, 1),
        0.8 * smoothstep(0, 0.3, this.pedal));
      const targetClutch = launchClutch * shiftClutch;
      const launchCoupling = smoothstep(requestedLaunchRpm, requestedLaunchRpm * 1.4, shaft);
      const clutchRate = targetClutch < this.clutch || this.shiftElapsed >= 0
        ? 5 : 1 + 4 * launchCoupling;
      this.clutch = approach(this.clutch, targetClutch, STEP * clutchRate);
      this.updateLimiter(this.clutch === 1 ? shaft : this.rpm);
      const governor = this.limiter !== null ? 1 : 1 - smoothstep(this.maximumRpm - this.span * 0.05, this.maximumRpm, shaft);
      this.engineThrottle = this.pedal * shiftClutch * governor;
      const demand = Math.pow(this.engineThrottle, 1.5);
      const combustion = this.limiterCutActive ? 0 : 1;
      this.load = demand * this.clutch;
      const overrunTorque = 8 + 24 * clamp(this.rpm / 4300, 0, 1);
      // The curve already describes net shaft torque. Overrun fades out just off
      // closed throttle; subtracting it across the whole pedal range double-counts
      // losses and can prevent a lightly throttled vehicle from pulling away.
      const netTorque = umz451mTorque(this.rpm) * demand * combustion -
        overrunTorque * (1 - smoothstep(0, 0.15, this.engineThrottle * combustion));
      const reduction = RATIOS[this.gear - 1] * FINAL_DRIVE * 0.85 / WHEEL_RADIUS;
      driveForce = Math.max(0, netTorque) * this.clutch * reduction;
      engineBrake = Math.max(0, -netTorque) * this.clutch * reduction;
    }
    const roadForce = MASS * 9.81 * 0.015 + 0.5 * 1.225 * 0.6 * 2.65 * this.speed * this.speed;
    this.speed = Math.max(0, this.speed + (driveForce - engineBrake - roadForce - this.brake * 9000) / MASS * STEP);
    this.distance += this.speed * STEP;
  }
}
