// Render the packaged engine, recorded starter, pickup and coupled coast through
// the same session used by the harness. Revs/gear pulls are prescribed controls;
// the final in-gear rundown is integrated from native motored gas work.
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import path from 'node:path';
import {createHash, webcrypto} from 'node:crypto';
import {execFileSync} from 'node:child_process';
import {loadResponsiveAudioCrankwave} from '../../../web/runtime/crankwave-package.js';
import {CrankwaveAudioEngine} from '../../../web/runtime/crankwave-audio-engine.js';
import {DieselDriveSession} from '../../../web/runtime/diesel-drive-session.js';
import {CoupledCoast} from '../../../web/runtime/diesel-backdrive-runtime.js';
import {encodeFloat32Wav} from '../../../web/runtime/wav.js';

const [carrier, output, baseline, ...extra] = process.argv.slice(2);
assert.ok(carrier && output && !extra.length,
  'usage: node audition.mjs <drive.crankwave> <new-output-directory> [baseline-drive.crankwave]');
await fs.mkdir(output, {recursive:false});
const rate = 192000;
const smooth = x => { x=Math.max(0,Math.min(1,x)); return x*x*(3-2*x); };
const hash = x => createHash('sha256').update(x).digest('hex');
const statistics = pcm => {
  let power=0,peak=0;
  for (const x of pcm) {
    assert.ok(Number.isFinite(x) && Math.abs(x)<1, 'Non-finite or clipped audio');
    power+=x*x; peak=Math.max(peak,Math.abs(x));
  }
  return {seconds:pcm.length/rate,peak,rms:Math.sqrt(power/pcm.length)};
};
async function save(name, pcm) {
  const file=path.join(output,`${name}-192k.wav`);
  await fs.writeFile(file,encodeFloat32Wav(pcm,rate,1),{flag:'wx'});
  execFileSync('ffmpeg',['-v','error','-n','-i',file,'-ar','48000','-c:a','pcm_s16le',
    path.join(output,`${name}-48k.wav`)]);
}
async function load(file) {
  const bytes=await fs.readFile(file);
  const loaded=await loadResponsiveAudioCrankwave(bytes,{crypto:webcrypto});
  const engine=new CrankwaveAudioEngine(loaded);
  assert.equal(engine.sampleRate,rate);
  return {loaded,engine,session:new DieselDriveSession(engine,loaded.runtime),sha256:hash(bytes)};
}

const current=await load(carrier),p=current.session.profile;
// Preserve the approved performance's gestures; scale its RPM range to this
// profile's idle and shift speed. This is not a measured road recording trace.
const rpm = r => p.vehicle.idle_rpm+(r-725)*(p.vehicle.shift_rpm-p.vehicle.idle_rpm)/(2050-725);
const curve=[
  [8,725,.12,'neutral'],[8.18,800,.40,'neutral'],[8.65,1300,.24,'neutral'],
  [8.85,1300,0,'neutral'],[10.3,850,0,'neutral'],[10.8,725,.12,'neutral'],
  [11.7,725,.12,'neutral'],[11.9,850,.48,'neutral'],[12.55,1650,.28,'neutral'],
  [12.75,1650,0,'neutral'],[14.4,850,0,'neutral'],[14.9,725,.12,'neutral'],
  [15.8,725,.12,'neutral'],[16,850,.54,'neutral'],[16.75,1900,.32,'neutral'],
  [17,1900,0,'neutral'],[18.35,850,0,'neutral'],[18.85,725,.12,'neutral'],
  [21,725,.12,'first'],[21.5,1020,.46,'first'],[22,870,.65,'first'],
  [23,1100,.74,'first'],[28.8,2000,.80,'first'],[30,2050,.80,'shift-1-2'],
  [30.12,2035,0,'shift-1-2'],[30.95,1160,0,'shift-1-2'],[31.22,1140,0,'shift-1-2'],
  [31.5,1150,.68,'second'],[38.8,2050,.82,'second'],[39,2050,.82,'coast-second'],
  [39.12,2040,0,'coast-second'],[48,1050,0,'coast-second']
].map(([t,r,f,stage])=>[t,rpm(r),f,stage]);
function point(t) {
  if(t<8) return {rpm:p.vehicle.idle_rpm,load01:.12,stage:'starter-pickup-idle'};
  for(let i=0;i<curve.length-1;i++) {
    const a=curve[i],b=curve[i+1];
    if(t<b[0]) {
      const progress=(t-a[0])/(b[0]-a[0]),w=b[0]-a[0]>3?progress:smooth(progress);
      return {rpm:a[1]+(b[1]-a[1])*w,load01:a[2]+(b[2]-a[2])*w,stage:a[3]};
    }
  }
  throw new RangeError('Performance ended');
}
const pcm=new Float32Array(48*rate),controls=[];
let coast;
for(let i=0;i<48*50;i++) {
  const t=i/50,c=point(t);
  if(t>=39) {
    coast??=new CoupledCoast(current.session.bank,controls.at(-1).rpm,p.backdrive);
    Object.assign(c,coast.advance(.02,c.load01));
  }
  c.throttle01=c.load01;
  pcm.set(current.session.process(c),i*3840);
  controls.push({time_s:t,...c,sessionStage:current.session.snapshot().stage});
}
const report={engine:current.engine.engineId,carrierSha256:current.sha256,
  gainDb:p.gain_db,processing:'Exact packaged gain; no listening normalization or added engine samples.',
  full:statistics(pcm),coastStartRpm:controls[39*50].rpm,coastEndRpm:controls.at(-1).rpm,
  sections:[{start:0,end:8,label:'Recorded starter, pickup and idle'},
    {start:8,end:21,label:'Three stationary revs'},
    {start:21,end:30,label:'Launch and first gear'},
    {start:30,end:31.5,label:'Lift and shift pause'},
    {start:31.5,end:39,label:'Second gear'},
    {start:39,end:48,label:'Wheel-driven coast in second'}]};
await save('start-pickup-idle-revs-drive-coast',pcm);
await fs.writeFile(path.join(output,'controls.json'),JSON.stringify(controls,null,2)+'\n');

if(baseline) {
  const old=await load(baseline),sections=[];
  assert.equal(old.engine.engineId,'ural4320-mechanical-v8');
  assert.equal(current.engine.engineId,'ural4320-kamaz740-1980');
  report.baselineSha256=old.sha256;
  for(const [label,r,f,reverse] of [['idle',null,.12,0],['loaded 1500 RPM',1500,.65,0],
    ['in-gear fuel cut 1800 RPM',1800,0,100]]) {
    for(const item of [old,current]) {
      const c={rpm:r??item.session.profile.vehicle.idle_rpm,load01:f,throttle01:f,
        backdriveTorqueNm:reverse,coupling01:reverse?1:0};
      item.engine.setOperatingPoint(c);item.session.reset({startEngine:false});
      const held=new Float32Array(4*rate);
      for(let i=0;i<225;i++) {
        const block=item.session.process(c);
        if(i>=25) held.set(block,(i-25)*3840);
      }
      // Smooth only the boundaries of cut excerpts for the comparison.
      for(let i=0;i<.02*rate;i++) {
        const w=smooth(i/(.02*rate-1));held[i]*=w;held[held.length-1-i]*=w;
      }
      sections.push({pcm:held,label:`${item.engine.engineId}: ${label}`,rpm:c.rpm,
        fuel:f,...statistics(held)});
    }
  }
  const comparison=new Float32Array(26.5*rate);
  report.comparison=sections.map(({pcm,...s},i)=>{
    comparison.set(pcm,Math.round(i*4.5*rate));
    return {...s,start:i*4.5,end:i*4.5+4};
  });
  await save('yamz-then-kamaz-idle-load-coast',comparison);
}
await fs.writeFile(path.join(output,'audition.json'),JSON.stringify(report,null,2)+'\n');
console.log(JSON.stringify(report,null,2));
