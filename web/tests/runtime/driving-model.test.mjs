import assert from "node:assert/strict";
import test from "node:test";
import { DrivingModel, umz451mTorque } from "../../crankwave-harness/driving-model.js";

const controls = (selector = "D", throttle01 = 0, brake01 = 0) =>
  ({ selector, throttle01, brake01 });
function run(driver, seconds, cadence = 0.02) {
  for (let i = 0; i < Math.round(seconds / cadence); ++i) driver.advance(cadence);
  return driver.snapshot();
}

test("neutral revs freely and settles to idle without moving the vehicle", () => {
  const driver = new DrivingModel(650, 4200);
  driver.setControls(controls("N", 1));
  const revved = run(driver, 5);
  assert.ok(revved.rpm > 4000 && revved.rpm <= 4200);
  assert.equal(revved.speedKph, 0);
  assert.ok(revved.load01 < 0.2);
  driver.setControls(controls("N"));
  assert.equal(run(driver, 5).rpm, 650);
});

test("launch builds RPM and clutch gradually; gentle pedal can still pull away", () => {
  const driver = new DrivingModel(650, 4200);
  driver.setControls(controls("D", 1));
  const first = run(driver, 0.2);
  assert.ok(first.rpm > 650 && first.rpm < 950, "no immediate 1,650 RPM flare");
  assert.ok(first.clutch01 <= 0.25, "clutch takes up progressively");
  assert.ok(run(driver, 0.3).rpm < 1200);
  assert.ok(run(driver, 0.7).rpm > 1550, "launch still reaches useful torque");
  driver.reset();
  driver.setControls(controls("D", 0.35));
  assert.ok(run(driver, 10).speedKph > 10, "part-throttle launch must overcome road load");
});

test("ordinary automatic driver shifts early without hunting at steady pedal", () => {
  for (const pedal of [0.35, 0.5, 1]) {
    const driver = new DrivingModel(650, 4200);
    driver.setControls(controls("D", pedal));
    let previous = driver.snapshot();
    let shifts = 0;
    for (let i = 0; i < 3000; ++i) {
      driver.advance(0.02);
      const current = driver.snapshot();
      if (current.shifting && !previous.shifting) {
        assert.ok(current.rpm < 2700, "upshift before the old high-RPM schedule");
        shifts++;
      }
      assert.ok(Number(current.gear) >= Number(previous.gear), "no up/down hunting on level acceleration");
      previous = current;
    }
    assert.equal(shifts, 3);
  }
});

test("torque and power stay within the published UMZ-451M rated envelope", () => {
  assert.equal(umz451mTorque(2200), 166.7);
  assert.equal(umz451mTorque(2500), 166.7);
  let peakKw = 0;
  for (let rpm = 650; rpm <= 4300; rpm++) {
    const torque = umz451mTorque(rpm);
    assert.ok(torque > 0 && torque <= 166.7);
    peakKw = Math.max(peakKw, torque * rpm * Math.PI / 30 / 1000);
  }
  assert.ok(peakKw > 55 && peakKw < 55.3);
  for (const rpm of [5000, 8000, 12000]) {
    assert.ok(umz451mTorque(rpm) * rpm * Math.PI / 30 / 1000 < 55.3);
  }
});

test("Drive launches, shifts through four gears with load cuts and RPM drops", () => {
  const driver = new DrivingModel(650, 4200);
  driver.setControls(controls("D", 1));
  const gears = new Set();
  let unloadedShifts = 0;
  let largestRpmDrop = 0;
  let previous = driver.snapshot();
  for (let i = 0; i < 1500; ++i) {
    const point = driver.advance(0.02);
    const current = driver.snapshot();
    assert.ok(point.rpm >= 650 && point.rpm <= 4200);
    assert.ok(point.load01 >= 0 && point.load01 <= 1);
    assert.ok(Math.abs(current.speedKph - previous.speedKph) < 0.6, "shifts must preserve road speed");
    largestRpmDrop = Math.max(largestRpmDrop, previous.rpm - current.rpm);
    if (current.shifting && current.load01 < 0.01) unloadedShifts++;
    gears.add(current.gear);
    previous = current;
  }
  assert.deepEqual([...gears], ["1", "2", "3", "4"]);
  assert.ok(previous.speedKph > 80 && previous.speedKph < 120);
  assert.ok(unloadedShifts > 10);
  assert.ok(largestRpmDrop > 50);
  driver.setControls(controls("D", 0, 1));
  const stopped = run(driver, 12);
  assert.equal(stopped.speedKph, 0);
  assert.equal(stopped.gear, "1");
  assert.equal(stopped.rpm, 650);
});

test("coasting slows down; neutral decouples and re-engaging D keeps speed", () => {
  const driver = new DrivingModel(650, 4200);
  driver.setControls(controls("D", 1));
  const moving = run(driver, 20);
  driver.setControls(controls("D"));
  assert.ok(run(driver, 4).speedKph < moving.speedKph - 1);
  driver.setControls(controls("N"));
  const neutral = run(driver, 4);
  assert.equal(neutral.rpm, 650);
  assert.ok(neutral.speedKph > 40);
  driver.setControls(controls("D", 0.5));
  assert.equal(driver.snapshot().speedKph, neutral.speedKph);
  assert.ok(Number(driver.snapshot().gear) >= 3);
});

test("fixed simulation steps are independent of audio block size", () => {
  const small = new DrivingModel(650, 4200);
  const large = new DrivingModel(650, 4200);
  for (const driver of [small, large]) driver.setControls(controls("D", 0.7));
  assert.deepEqual(run(small, 20, 0.02), run(large, 20, 0.1));
});

test("other carrier RPM ranges stay bounded and invalid controls are atomic", () => {
  for (const [minimum, maximum] of [[500, 12000], [1000, 2000], [5000, 5001]]) {
    const driver = new DrivingModel(minimum, maximum);
    driver.setControls(controls("D", 1));
    for (let i = 0; i < 600; ++i) {
      const point = driver.advance(0.1);
      assert.ok(Number.isFinite(point.rpm) && point.rpm >= minimum && point.rpm <= maximum);
    }
    const before = driver.snapshot();
    assert.throws(() => driver.setControls(controls("N", NaN)));
    assert.deepEqual(driver.snapshot(), before);
    assert.throws(() => driver.advance(Infinity));
    driver.reset();
    assert.equal(driver.snapshot().speedKph, 0);
  }
});

test('captured limiter capability allows repeated neutral cuts above 4500 RPM', () => {
  const limiter={idleRpm:765,activationRpm:4500,cutSeconds:.1};
  for(const cadence of [1/60,1/120,.02]) {
    const driver=new DrivingModel(569.49,4700,limiter);
    driver.setControls(controls('N',1));
    let cuts=0, wasCut=false, maximum=0;
    for(let i=0;i<Math.round(8/cadence);++i) {
      const point=driver.advance(cadence);
      maximum=Math.max(maximum,point.rpm);
      if(point.limiterCutActive && !wasCut) cuts++;
      wasCut=point.limiterCutActive;
      assert.equal(driver.snapshot().speedKph,0);
    }
    assert.ok(cuts>=15);
    // At 60 Hz observation can miss the single 120 Hz overspeed tick.
    assert.ok(maximum>4500-2600*cadence && maximum<4560);
    driver.setControls(controls('N'));
    assert.equal(run(driver,3).rpm,765);
    assert.equal(driver.operatingPoint().limiterCutActive,false);
  }
});
