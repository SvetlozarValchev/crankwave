import assert from 'node:assert/strict';
import test from 'node:test';
import {MotoredTorqueBank,CoupledCoast,BackdriveRadiation,backdriveProfile} from '../../../data/engines/ural4320-mechanical-v8/backdrive-coast.mjs';
const TAU=2*Math.PI;
const bank={mean:()=>-50,ripple:(_rpm,revolutions)=>500*Math.sin(TAU*4*revolutions)};

test('motored torque accepts the original force/flow loader and explicit newer channel counts',()=>{
 const n=2048,geometry={stroke_m:.14,connecting_rod_length_m:.26};
 // For F=-5000*sin(theta), mean F*dx/dtheta=-5000*r/2. The
 // connecting-rod correction integrates to zero over complete revolutions.
 const expected=-5000*(geometry.stroke_m/2)/2;
 for(const declaredChannels of [undefined,2,3]) {
  const channels=declaredChannels??2,samples=new Float32Array(n*2*channels);
  for(let cell=0;cell<2;cell++) for(let i=0;i<n;i++) {
   samples[(cell*n+i)*channels]=-5000*Math.sin(i/n*2*TAU);
   samples[(cell*n+i)*channels+1]=.003;
   if(channels===3)samples[(cell*n+i)*channels+2]=123;
  }
  const runtime={kind:'diesel-mechanical-audio',samples,
   ...(declaredChannels===undefined?{}:{sourceChannels:declaredChannels}),
   manifest:{samples_per_cycle:n,cylinders:[{firing_angle_rad:0}]},
   rows:[550,2100].map((rpm,index)=>({rpm,cells:[{index,fueling_01:0,indicated_work_j:expected*2*TAU}]}))};
  const torque=new MotoredTorqueBank(runtime,geometry);
  assert.ok(Math.abs(torque.mean(1500)-expected)<1e-5);
  assert.ok(Number.isFinite(torque.ripple(1500,.123)));
 }
});

test('engaged coast follows drag and combined inertia; wheel torque feeds the engine at fuel cut',()=>{
 const p=backdriveProfile,rpm0=2050,totalI=p.engineInertiaKgM2+p.vehicleReflectedInertiaKgM2;
 const coupled=new CoupledCoast(bank,rpm0),dt=.02,duration=5;
 let result,previous=rpm0;
 for(let t=0;t<duration-1e-9;t+=dt) {
  result=coupled.advance(dt,0);assert.ok(result.rpm<previous);previous=result.rpm;
  assert.ok(result.backdriveTorqueNm>0&&result.backdriveTorqueNm<result.engineDragNm);
 }
 // Closed-form solution of I*domega/dt=-(a+b*rpm), independent of implementation.
 const a=50+p.frictionConstantNm+p.reflectedRoadDragNm,b=p.frictionNmPerRpm;
 const rate=60*b/(TAU*totalI),expected=(rpm0+a/b)*Math.exp(-rate*duration)-a/b;
 assert.ok(Math.abs(result.rpm-expected)<.01);
 const open=new CoupledCoast(bank,rpm0,{...p,vehicleReflectedInertiaKgM2:0,reflectedRoadDragNm:0});
 let free;
 for(let t=0;t<1-1e-9;t+=dt)free=open.advance(dt,0);
 assert.ok(free.rpm<result.rpm,'one second uncoupled loses more RPM than five seconds coupled');
 assert.equal(free.backdriveTorqueNm,0);
});

test('negative-load radiation uses motored torque, with exact bypass for open coupling or driving torque',()=>{
 const fs=48000,render=(torque,coupling,b=bank)=>{
  const layer=new BackdriveRadiation(b,backdriveProfile,fs),pcm=new Float64Array(fs/2);
  for(let i=0;i<pcm.length;i++)pcm[i]=layer.sample(1500,1500/60*i/fs,torque,coupling);
  return pcm;
 };
 assert.ok(render(120,0).every(x=>x===0));
 assert.ok(render(0,1).every(x=>x===0));
 assert.ok(render(-120,1).every(x=>x===0));
 assert.ok(render(120,1,{...bank,ripple:()=>0}).every(x=>x===0));
 const active=render(120,1);assert.ok(active.some(x=>Math.abs(x)>.01));
 assert.ok(active.every(x=>Number.isFinite(x)&&Math.abs(x)<1));
});
