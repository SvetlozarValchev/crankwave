import assert from 'node:assert/strict';
import test from 'node:test';
import fs from 'node:fs';
import {validateDieselDrive} from '../../runtime/diesel-drive-runtime.js';
import {DieselCoupledDrivingModel} from '../../crankwave-harness/diesel-coupled-driving-model.js';

const authored=JSON.parse(fs.readFileSync(new URL('../../../data/engines/ural4320-mechanical-v8/drive.json',import.meta.url)));
const profile=structuredClone(authored);
profile.source_sha256='a'.repeat(64);
profile.starter.processed_sha256='b'.repeat(64);
const runtime={sampleRate:192000,batchFrames:3840,minimumRpm:550,maximumRpm:2275,
  manifest:{engine:profile.engine,source_sha256:profile.source_sha256,cylinders:[{}]},
  // Synthetic constant gas torque permits an independent analytical comparison.
  rows:[550,2275].map(rpm=>({rpm,cells:[
    {fueling_01:0,indicated_work_j:-50*4*Math.PI},
    {fueling_01:1,indicated_work_j:800*4*Math.PI}]})),
  drivePackage:{manifest:profile}};
const bank={mean:()=>-50};

test('driving profile rejects stale source identity and invalid audio, asset, or physical controls',()=>{
  validateDieselDrive(profile,runtime);
  for(const change of [
    m=>m.source_sha256='c'.repeat(64), m=>m.engine='another-engine',
    m=>m.starter.processed_path='../elsewhere.f32le', m=>m.starter.overrides.basePlaybackRate=0,
    m=>m.pickup.curve[1].seconds=0, m=>m.pickup.curve[1].rpm=0,
    m=>m.backdrive.engineInertiaKgM2=0, m=>m.vehicle.shaft_rpm_per_mps.reverse(),
    m=>m.vehicle.inertia_reference_gear=.5, m=>m.unknown=true]) {
    const invalid=structuredClone(profile); change(invalid);
    assert.throws(()=>validateDieselDrive(invalid,runtime),/Invalid diesel driving package/);
  }
  assert.throws(()=>validateDieselDrive(profile,{...runtime,batchFrames:8192}),/audio clock/);
});

function coastingDriver() {
  const driver=new DieselCoupledDrivingModel(runtime,bank);
  driver.gear=2;driver.rpm=2050;driver.speed=2050/180;driver.clutch=1;driver.fuel=0;
  driver.setControls({selector:'D',throttle01:0,brake01:0});
  return driver;
}

test('in-gear coast follows coupled momentum and preserves crank-to-wheel speed',()=>{
  const driver=coastingDriver();
  let previous=driver.rpm;
  for(let i=0;i<250;i++) {
    const point=driver.advance(.02);
    assert.equal(point.load01,0);
    assert.ok(point.rpm<previous);
    assert.ok(Math.abs(point.rpm-driver.speed*180)<1e-10);
    assert.ok(point.backdriveTorqueNm>0);
    previous=point.rpm;
  }
  const b=profile.backdrive,total=b.engineInertiaKgM2+b.vehicleReflectedInertiaKgM2;
  const a=50+b.frictionConstantNm+b.reflectedRoadDragNm, rate=60*b.frictionNmPerRpm/(2*Math.PI*total);
  const expected=(2050+a/b.frictionNmPerRpm)*Math.exp(-rate*5)-a/b.frictionNmPerRpm;
  assert.ok(Math.abs(driver.rpm-expected)<.01);
  assert.equal(driver.coupling,1);
});

test('neutral and shifting remove backdrive; sustained braking reaches a fueled idle',()=>{
  const driver=coastingDriver();
  driver.advance(.2);
  driver.setControls({selector:'N',throttle01:0,brake01:0});
  for(let i=0;i<100;i++) {
    const point=driver.advance(.02);
    assert.equal(point.backdriveTorqueNm,0);
  }
  assert.equal(driver.rpm,profile.vehicle.idle_rpm);
  assert.equal(driver.clutch,0);assert.equal(driver.coupling,0);assert.equal(driver.fuel,.12);
  const shifting=coastingDriver();
  shifting.shiftElapsed=0;shifting.targetGear=3;
  assert.equal(shifting.advance(.02).backdriveTorqueNm,0);
  const braking=coastingDriver();
  braking.setControls({selector:'D',throttle01:0,brake01:1});
  for(let i=0;i<1000;i++) {
    const point=braking.advance(.02);
    assert.ok(Number.isFinite(point.rpm)&&point.rpm>=runtime.minimumRpm&&point.rpm<=runtime.maximumRpm);
  }
  assert.equal(braking.speed,0);assert.equal(braking.fuel,.12);assert.equal(braking.backdrive,0);
});

test('automatic shifts leave time to pull in each gear and release the clutch between them',()=>{
  const d=new DieselCoupledDrivingModel(runtime,bank);
  d.setControls({selector:'D',throttle01:1,brake01:0});
  const starts=[];let shifting=false;
  for(let i=0;i<2500;i++) {
    d.advance(.02);
    if(d.shiftElapsed>=0&&!shifting) starts.push(i/50);
    if(d.shiftElapsed>=0&&d.shiftElapsed<.8) assert.equal(d.backdrive,0);
    shifting=d.shiftElapsed>=0;
  }
  assert.ok(starts.length>=2);
  for(let i=1;i<starts.length;i++) assert.ok(starts[i]-starts[i-1]>=profile.vehicle.minimum_gear_seconds-.021);
});
