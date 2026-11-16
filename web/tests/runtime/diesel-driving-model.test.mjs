import assert from "node:assert/strict";
import test from "node:test";

import {DieselDrivingModel} from "../../crankwave-harness/diesel-driving-model.js";

const controls = (selector, throttle01 = 0, brake01 = 0) =>
    ({selector, throttle01, brake01});
const run = (driver, seconds, dt = 0.02) => {
  for (let i = 0; i < Math.round(seconds / dt); ++i) driver.advance(dt);
  return driver.operatingPoint();
};

test("released pedal keeps a diesel firing at idle and after reset", () => {
  const driver = new DieselDrivingModel(650, 3400);
  for (const selector of ["D", "N"]) {
    driver.setControls(controls(selector));
    assert.deepEqual(run(driver, 3), {rpm: 800, throttle01: 0, load01: 0.12});
    assert.equal(driver.snapshot().load01, 0.12);
  }
  driver.reset();
  assert.equal(driver.operatingPoint().load01, 0.12);
});

test("neutral lift cuts fuel above idle and restores it as RPM settles", () => {
  const driver = new DieselDrivingModel(650, 3400);
  driver.setControls(controls("N", 1));
  const revved = run(driver, 4);
  assert.ok(revved.rpm > 3000 && revved.load01 > 0.2);
  driver.setControls(controls("N"));
  const cut = run(driver, 0.5);
  assert.ok(cut.rpm > 1500);
  assert.equal(cut.load01, 0);
  const idle = run(driver, 4);
  assert.equal(idle.rpm, 800);
  assert.equal(idle.load01, 0.12);
});

test("diesel fuel mapping stays bounded and follows the fixed driving clock", () => {
  const a = new DieselDrivingModel(650, 3400);
  const b = new DieselDrivingModel(650, 3400);
  a.setControls(controls("D", 0.7));
  b.setControls(controls("D", 0.7));
  for (let i = 0; i < 1500; ++i) {
    const point = a.advance(0.02);
    b.advance(0.01);
    b.advance(0.01);
    assert.ok(point.load01 >= 0 && point.load01 <= 1);
    assert.deepEqual(point, b.operatingPoint());
  }
});
