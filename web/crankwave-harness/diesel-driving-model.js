import {DrivingModel} from "./driving-model.js";

const clamp01 = (x) => Math.max(0, Math.min(1, x));
const smoothstep = (lo, hi, x) => {
  const u = clamp01((x - lo) / (hi - lo));
  return u * u * (3 - 2 * u);
};

// Audition-host policy, separate from the baked engine physics. The vehicle rig
// supplies RPM and shaft-load demand; a diesel needs fuel to overcome its own
// losses even when shaft load and pedal are zero. These are authored defaults,
// not an identified pump/governor or a measured friction map.
export class DieselDrivingModel {
  constructor(minimumRpm, maximumRpm) {
    this.driver = new DrivingModel(minimumRpm, maximumRpm, null, {idleRpm: 800});
  }

  operatingPoint() {
    const point = this.driver.operatingPoint();
    const rpmFraction = clamp01((point.rpm - this.driver.idleRpm) / this.driver.span);
    const noLoadFuel = 0.12 + 0.10 * rpmFraction;
    const idleAssist =
        1 - smoothstep(this.driver.idleRpm + 50, this.driver.idleRpm + 250, point.rpm);
    const pedalDemand = smoothstep(0, 0.05, point.throttle01);
    const baseline = noLoadFuel * Math.max(idleAssist, pedalDemand);
    // A released pedal above idle reaches zero fuel. Near idle the assist
    // smoothly restores firing; manual mode still accepts an explicit fuel cut.
    return {...point, load01: baseline + (1 - baseline) * point.load01};
  }

  snapshot() {
    return {...this.driver.snapshot(), load01: this.operatingPoint().load01};
  }

  setControls(controls) {
    this.driver.setControls(controls);
    return this.snapshot();
  }

  reset() {
    this.driver.reset();
    return this.snapshot();
  }

  advance(seconds) {
    this.driver.advance(seconds);
    return this.operatingPoint();
  }
}
