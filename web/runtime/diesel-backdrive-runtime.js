// Authored engaged-coast model shared by the audition and packaged driving host. The native motored pressure capture supplies
// gas torque; reflected vehicle inertia supplies the rundown and reaction load.
// Acoustic radiation is authored, not an identified Ural transmission model.
const assert = {
 ok(value) { if (!value) throw new RangeError('Invalid diesel backdrive input'); },
 equal(a, b) { if (a !== b) throw new RangeError('Diesel backdrive identity mismatch'); }
};
import {DieselBiquad} from './diesel-mechanical-runtime.js';
const TAU=2*Math.PI;
const smooth=x=>{x=Math.max(0,Math.min(1,x));return x*x*(3-2*x);};
export const backdriveProfile=Object.freeze({
 engineInertiaKgM2:3.2,vehicleReflectedInertiaKgM2:27,
 frictionConstantNm:35,frictionNmPerRpm:.025,reflectedRoadDragNm:20,
 takeupSeconds:.22,referenceBackdriveTorqueNm:140,
 radiationHighpassHz:65,radiationLowpassHz:750,
 modes:[{frequencyHz:140,q:.85,gainPcmPerNm:.00015},
   {frequencyHz:320,q:1.1,gainPcmPerNm:.00010},{frequencyHz:580,q:.9,gainPcmPerNm:.000045}]
});

export class MotoredTorqueBank {
 constructor(runtime,geometry) {
  assert.equal(runtime.kind,'diesel-mechanical-audio');
  const n=runtime.manifest.samples_per_cycle,rad=geometry.stroke_m/2,rod=geometry.connecting_rod_length_m;
  // The original v1-v11 loader exposes interleaved force/flow without a stride
  // field. Newer loaders supply the channel count explicitly.
  const channels=runtime.sourceChannels??2;
  assert.ok(rad>0&&rod>rad);
  this.n=n;
  this.rows=runtime.rows.map(row=>{
   assert.equal(row.cells[0].fueling_01,0);
   const torque=new Float64Array(n),cell=row.cells[0].index;
   for(let i=0;i<n;i++) {
    for(const c of runtime.manifest.cylinders) {
     const phase=((i/n-c.firing_angle_rad/(2*TAU))%1+1)%1;
     const angle=phase*2*TAU,s=Math.sin(angle),co=Math.cos(angle);
     const lever=rad*s+rad*rad*s*co/Math.sqrt(rod*rod-rad*rad*s*s);
     const at=phase*n,left=Math.floor(at),w=at-left;
     const force=runtime.samples[(cell*n+left)*channels]*(1-w)+
       runtime.samples[(cell*n+(left+1)%n)*channels]*w;
     // Pressure force times dx/dtheta. Physical sum does not use microphone gains.
     torque[i]+=force*lever;
    }
   }
   const mean=torque.reduce((sum,x)=>sum+x,0)/n;
   // Integral over 720 degrees agrees with the native per-cylinder gas work.
   const expected=row.cells[0].indicated_work_j*runtime.manifest.cylinders.length/(2*TAU);
   assert.ok(Math.abs(mean-expected)<.01*Math.max(1,Math.abs(expected)));
   return {rpm:row.rpm,torque,mean};
  });
 }
 weights(rpm) {
  assert.ok(rpm>=this.rows[0].rpm&&rpm<=this.rows.at(-1).rpm);
  let i=0;while(i<this.rows.length-2&&rpm>this.rows[i+1].rpm)i++;
  const a=this.rows[i],b=this.rows[i+1],w=(rpm-a.rpm)/(b.rpm-a.rpm);return {a,b,w};
 }
 mean(rpm) {const {a,b,w}=this.weights(rpm);return a.mean+(b.mean-a.mean)*w;}
 ripple(rpm,revolutions) {
  const {a,b,w}=this.weights(rpm),phase=((revolutions/2)%1+1)%1,x=phase*this.n,i=Math.floor(x),f=x-i;
  const sample=row=>row.torque[i]*(1-f)+row.torque[(i+1)%this.n]*f-row.mean;
  return sample(a)*(1-w)+sample(b)*w;
 }
}

export class CoupledCoast {
 constructor(bank,initialRpm,profile=backdriveProfile) {
  this.bank=bank;this.profile=profile;this.rpm=initialRpm;this.elapsed=0;
 }
 advance(seconds,fuel) {
  assert.ok(Number.isFinite(seconds)&&seconds>0&&seconds<=.1);
  const p=this.profile;let reverseTorque=0,engineDrag=0;
  for(let left=seconds;left>1e-10;) {
   const dt=Math.min(left,1/1000);left-=dt;
   engineDrag=-this.bank.mean(this.rpm)+p.frictionConstantNm+p.frictionNmPerRpm*this.rpm;
   // Fuel release traverses the drive/coast transition; after fuel cut the
   // combined engine+reflected-vehicle inertia loses energy through gas work,
   // authored mechanical friction and road resistance. No added exhaust brake.
   const dragAmount=1-smooth(fuel/.82);
   const decel=-(engineDrag+p.reflectedRoadDragNm)*dragAmount/
     (p.engineInertiaKgM2+p.vehicleReflectedInertiaKgM2);
   this.rpm+=decel*dt*60/TAU;this.elapsed+=dt;
   reverseTorque=Math.max(0,(engineDrag*p.vehicleReflectedInertiaKgM2-
     p.engineInertiaKgM2*p.reflectedRoadDragNm)*dragAmount/
     (p.engineInertiaKgM2+p.vehicleReflectedInertiaKgM2));
  }
  return {rpm:this.rpm,backdriveTorqueNm:reverseTorque,engineDragNm:engineDrag,
    coupling01:smooth(this.elapsed/p.takeupSeconds),elapsed:this.elapsed};
 }
}

export class BackdriveRadiation {
 constructor(bank,profile=backdriveProfile,sampleRate=192000) {
  this.bank=bank;this.profile=profile;this.sampleRate=sampleRate;
  this.hp=new DieselBiquad('highpass',profile.radiationHighpassHz,Math.SQRT1_2,sampleRate);
  this.lp=new DieselBiquad('lowpass',profile.radiationLowpassHz,Math.SQRT1_2,sampleRate);
  this.modes=profile.modes.map(m=>({...m,filter:new DieselBiquad('bandpass',m.frequencyHz,m.q,sampleRate)}));
 }
 sample(rpm,revolutions,backdriveTorqueNm,coupling01) {
  const x=this.hp.process(this.bank.ripple(rpm,revolutions));
  let out=0;for(const m of this.modes)out+=m.filter.process(x)*m.gainPcmPerNm;
  out=this.lp.process(out);
  // Continue warming the pressure-driven filters with zero output while the
  // clutch is open or torque is positive. No noise generator or extra combustion.
  const amount=Math.min(1.5,Math.max(0,backdriveTorqueNm)/this.profile.referenceBackdriveTorqueNm)*coupling01;
  return amount===0?0:out*amount;
 }
}
