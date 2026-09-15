// Ural listening treatment of the existing recorded car starter. Reuses the
// canonical verified source and its playback cursor. No starter synthesis.
// Treatment remains an audition override, separate from the shared asset and
// from the accepted native diesel carrier.
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import {createHash,webcrypto} from 'node:crypto';
import {execFileSync} from 'node:child_process';
import {pathToFileURL,fileURLToPath} from 'node:url';
import {resolve} from 'node:path';
import {loadSharedRecordedStarterRuntime,SharedRecordedStarterCursor} from '../../../web/runtime/shared-recorded-starter-runtime.js';
import {CrankwaveAudioEngine} from '../../../web/runtime/crankwave-audio-engine.js';
import {loadResponsiveAudioCrankwave} from '../../../web/runtime/crankwave-package.js';
import {encodeFloat32Wav} from '../../../web/runtime/wav.js';
import {MotoredTorqueBank,CoupledCoast,BackdriveRadiation,backdriveProfile} from './backdrive-coast.mjs';
const fsHz=192000,smooth=x=>{x=Math.max(0,Math.min(1,x));return x*x*(3-2*x);};
const hash=x=>createHash('sha256').update(x).digest('hex');
const fixture=new URL('../../../reference/fixtures/responsive-audio/shared-recorded-starter/',import.meta.url);
const manifestUrl=new URL('runtime.json',fixture);
const payloadUrl=new URL('audio/recorded-starter.cropped.192000hz.mono.f32le',fixture);
const [carrier,output,pickup='surge',sequence='standard',coast='legacy',...extra]=process.argv.slice(2);
assert.ok(carrier&&output&&['surge','gentle'].includes(pickup)&&['standard','drive'].includes(sequence)&&
  ['legacy','coupled','motion-only'].includes(coast)&&(coast==='legacy'||sequence==='drive')&&!extra.length,
  'usage: node starter-audition.mjs <ural.crankwave> <new-output-directory> [surge|gentle] [standard|drive] [legacy|coupled|motion-only]');
await fs.mkdir(output,{recursive:false});const out=pathToFileURL(resolve(output)+'/');
const shared=await loadSharedRecordedStarterRuntime(manifestUrl,{crypto:webcrypto,fetch:async url=>{
  assert.ok([manifestUrl.href,payloadUrl.href].includes(url));
  const bytes=Uint8Array.from(await fs.readFile(new URL(url)));
  return {ok:true,url,redirected:false,arrayBuffer:async()=>bytes.buffer};
}});
const originalManifest=await fs.readFile(manifestUrl),originalPayload=await fs.readFile(payloadUrl);
const filterGraph='highpass=f=32:poles=2,equalizer=f=180:t=q:w=0.7:g=4,equalizer=f=520:t=q:w=0.9:g=-2,equalizer=f=2400:t=q:w=0.7:g=-3,lowpass=f=4300:poles=2';
const coloredPath=new URL('starter-colored-source.f32le',out);
execFileSync('ffmpeg',['-v','error','-n','-f','f32le','-ar',String(fsHz),'-ac','1','-i',fileURLToPath(payloadUrl),'-af',filterGraph,'-f','f32le','-acodec','pcm_f32le',fileURLToPath(coloredPath)]);
const coloredBytes=await fs.readFile(coloredPath);assert.equal(coloredBytes.length,originalPayload.length);
const colored=new Float32Array(shared.samples.length);
for(let i=0;i<colored.length;i++)colored[i]=coloredBytes.readFloatLE(i*4);
const rms=x=>Math.sqrt(x.reduce((s,v)=>s+v*v,0)/x.length);
// Equalize the whole source once so the heavier treatment isn't just louder.
const eqGain=rms(shared.samples)/rms(colored);for(let i=0;i<colored.length;i++)colored[i]*=eqGain;
const overrides={basePlaybackRate:.74,catchPlaybackRate:.9,speedUpStartRpm:180,speedUpEndRpm:650,
  rpmSmoothingMilliseconds:55,attackMilliseconds:14,catchStarterGain:.35,handoffMilliseconds:220};
const heavy={...shared,samples:colored,settings:{...shared.settings,...overrides}};
const controls=[],duration=sequence==='drive'?48:24,start=3.02,n=Math.round(duration*fsHz),running=new Float32Array(n);
const carrierBytes=await fs.readFile(carrier),loaded=await loadResponsiveAudioCrankwave(carrierBytes,{crypto:webcrypto});
const engine=new CrankwaveAudioEngine(loaded);
assert.equal(engine.engineId,'ural4320-mechanical-v8');assert.equal(engine.sampleRate,fsHz);
let torqueBank=null,reaction=null,coastModel=null,previousBackdrive=0,previousCoupling=0;
const driveline=new Float32Array(n);
if(coast!=='legacy') {
 const definitionBytes=await fs.readFile(new URL('engine.json',import.meta.url));
 assert.equal(hash(definitionBytes),loaded.runtime.manifest.source_sha256,'geometry must match the native carrier source');
 torqueBank=new MotoredTorqueBank(loaded.runtime,JSON.parse(definitionBytes).engine.cylinder);
 reaction=new BackdriveRadiation(torqueBank);
}
// A brief fueled pickup, one overshoot and a small settling dip. These are
// prescribed controls for the existing engine, not extra startup sound layers.
const pickupCurve=[
 {seconds:0,rpm:550,fuel:.42},
 {seconds:.12,rpm:900,fuel:.62},
 {seconds:.30,rpm:1150,fuel:.48},
 {seconds:.50,rpm:1000,fuel:.18},
 {seconds:.95,rpm:735,fuel:.09},
 {seconds:1.20,rpm:690,fuel:.12},
 {seconds:1.60,rpm:725,fuel:.12}
];
function pickupPoint(dt) {
 if(pickup==='gentle')return {rpm:dt<.3?550+245*smooth(dt/.3):dt<.9?795-70*smooth((dt-.3)/.6):725,
   fuel:.12+.08*(1-smooth(dt/.45))};
 let a=pickupCurve[0];
 for(const b of pickupCurve.slice(1)) {
  if(dt<=b.seconds) {
   const w=smooth((dt-a.seconds)/(b.seconds-a.seconds));
   return {rpm:a.rpm+(b.rpm-a.rpm)*w,fuel:a.fuel+(b.fuel-a.fuel)*w};
  }
  a=b;
 }
 return {rpm:a.rpm,fuel:a.fuel};
}
// A prescribed listening performance, not the harness's UAZ vehicle rig. Free
// revs use short fuel requests; gear pulls sustain fuel, and the shift releases
// it while RPM falls. Gear labels document intent, not simulated transmission state.
const driveCurve=[
 [8,725,.12,'neutral'],[8.18,800,.40,'neutral'],[8.65,1300,.24,'neutral'],
 [8.85,1300,0,'neutral'],[10.3,850,0,'neutral'],[10.8,725,.12,'neutral'],
 [11.7,725,.12,'neutral'],[11.9,850,.48,'neutral'],[12.55,1650,.28,'neutral'],
 [12.75,1650,0,'neutral'],[14.4,850,0,'neutral'],[14.9,725,.12,'neutral'],
 [15.8,725,.12,'neutral'],[16.0,850,.54,'neutral'],[16.75,1900,.32,'neutral'],
 [17.0,1900,0,'neutral'],[18.35,850,0,'neutral'],[18.85,725,.12,'neutral'],
 [21,725,.12,'first'],[21.5,1020,.46,'first'],[22.0,870,.65,'first'],
 [23,1100,.74,'first'],[28.8,2000,.80,'first'],[30,2050,.80,'shift-1-2'],
 [30.12,2035,0,'shift-1-2'],[30.95,1160,0,'shift-1-2'],[31.22,1140,0,'shift-1-2'],
 [31.5,1150,.68,'second'],[38.8,2050,.82,'second'],[39,2050,.82,'coast-second'],
 [39.12,2040,0,'coast-second'],[48,1050,0,'coast-second']
];
function drivePoint(t) {
 let a=driveCurve[0];
 for(const b of driveCurve.slice(1)) {
  if(t<b[0]) {
   const progress=(t-a[0])/(b[0]-a[0]);
   // Long loaded pulls and overrun retain momentum instead of stopping their
   // RPM slope at every keyframe. Short pedal/clutch gestures remain smooth.
   const w=b[0]-a[0]>3?progress:smooth(progress);
   return {rpm:a[1]+(b[1]-a[1])*w,fuel:a[2]+(b[2]-a[2])*w,stage:a[3]};
  }
  a=b;
 }
 return {rpm:a[1],fuel:a[2],stage:a[3]};
}
const driveSections=[
 {start:0,end:3.04,label:'Recorded starter'},
 {start:3.04,end:4.64,label:'Engine pickup'},
 {start:4.64,end:8,label:'Idle'},
 {start:8,end:11.7,label:'Stationary rev 1 and pause'},
 {start:11.7,end:15.8,label:'Stationary rev 2 and pause'},
 {start:15.8,end:21,label:'Stationary rev 3 and pause'},
 {start:21,end:23,label:'Pull away in first'},
 {start:23,end:30,label:'Accelerate in first'},
 {start:30,end:31.5,label:'Lift, shift pause, engage second'},
 {start:31.5,end:39,label:'Accelerate in second'},
 {start:39,end:48,label:'Lift and coast in second'}
];
const initialFuel=pickupPoint(0).fuel;
engine.setOperatingPoint({rpm:550,load01:initialFuel,throttle01:initialFuel});engine.reset();
let previousRpm=550,crankRevolutions=550*engine.latencyFrames/fsHz/60;
for(let i=0;i<Math.round((duration-start)*50);i++) {
 const time=start+i/50,dt=time-start;
 const point=pickupPoint(dt);let rpm=point.rpm,load01=point.fuel;
 let stage='pickup-and-idle';
 if(sequence==='drive'&&time>=8) {
  const point=drivePoint(time);rpm=point.rpm;load01=point.fuel;stage=point.stage;
 } else if(sequence==='standard') {
  if(time>=8&&time<12){rpm=725+1375*(time-8)/4;load01=.12+.73*smooth((time-8)/.7);}
  else if(time>=12&&time<13){rpm=2100;load01=.85;}
  else if(time>=13&&time<19){rpm=2100-1375*(time-13)/6;load01=rpm>950?0:.12*(950-rpm)/225;}
 }
 let coastState=null;
 if(torqueBank&&time>=39) {
  coastModel??=new CoupledCoast(torqueBank,previousRpm);
  coastState=coastModel.advance(.02,load01);rpm=coastState.rpm;
 }
 const c={rpm,load01,throttle01:load01};running.set(engine.process(c),Math.round(time*fsHz));
 if(reaction) {
  const reverse=coastState?.backdriveTorqueNm??0,coupling=coastState?.coupling01??0;
  const slope=(rpm-previousRpm)/.02,base=Math.round(time*fsHz)+engine.latencyFrames;
  for(let j=0;j<engine.processFrames;j++) {
   const dt=(j+.5)/fsHz,w=dt/.02;
   const phase=crankRevolutions+(previousRpm*dt+.5*slope*dt*dt)/60;
   const sample=reaction.sample(previousRpm+slope*dt,phase,
     previousBackdrive+(reverse-previousBackdrive)*w,previousCoupling+(coupling-previousCoupling)*w);
   if(base+j<n&&coast==='coupled')driveline[base+j]=sample;
  }
  crankRevolutions+=(previousRpm+rpm)*.5*.02/60;
  previousBackdrive=reverse;previousCoupling=coupling;
 }
 previousRpm=rpm;
 controls.push({time_s:time,...c,...(sequence==='drive'?{stage}:{}),...(coastState?{backdrive:coastState}:{})});
}
// Smooth the first native running onset. RPM below 550 is starter playback
// telemetry only; no native cranking or first-combustion solver is claimed.
const latency=engine.latencyFrames,entry=Math.round(start*fsHz);
const onsetSeconds=pickup==='surge'?.055:.16;
for(let i=0;i<Math.round(onsetSeconds*fsHz);i++)running[entry+latency+i]*=smooth(i/(onsetSeconds*fsHz));
function starterTrack(package_,length,catchEnabled=true,blockFrames=3840) {
 const c=new SharedRecordedStarterCursor(package_);c.reset();
 const state=(frame,starter,rpm,combustionDetected)=>c.setState({frame,starter,rpm,combustionDetected,ignition:true,fuel:true});
 state(0,false,0,false);state(Math.round(.4*fsHz),true,155,false);
 if(catchEnabled) {
  // Schedule to the audible native onset, accounting for the engine's fixed latency.
  state(entry+latency,true,550,true);state(entry+latency+Math.round(.16*fsHz),false,725,true);
 }
 const pcm=new Float32Array(length);
 for(let at=0;at<length;at+=blockFrames) {
  const size=Math.min(blockFrames,length-at),zero=new Float32Array(size);
  pcm.set(c.mixPair(zero,zero).bakedBlock,at);
 }
 return {pcm,diagnostics:c.diagnostics()};
}
const starter=starterTrack(heavy,n),engineAndDriveline=coast==='coupled'?Float32Array.from(running,(v,i)=>v+driveline[i]):running;
const mix=Float32Array.from(engineAndDriveline,(v,i)=>v+starter.pcm[i]);
const replay=starterTrack(heavy,n,true,8191);assert.deepEqual(replay.pcm,starter.pcm,'recorded playback replay independent of read boundaries');
assert.ok(starter.pcm.subarray(4*fsHz).every(v=>v===0),'recorded starter releases completely');
assert.deepEqual(mix.subarray(4*fsHz),engineAndDriveline.subarray(4*fsHz),'starter leaves later running PCM exact');
let peak=0;for(const v of mix){assert.ok(Number.isFinite(v)&&Math.abs(v)<1);peak=Math.max(peak,Math.abs(v));}
// Retain the preceding full-drive listening gain for a fair coast comparison.
const mixRms=rms(mix),gain=coast==='legacy'?Math.min(10**(-23/20)/mixRms,.85/peak):
 Math.min(10**(-5.678079962336634/20),.85/peak);
const listening=Float32Array.from(mix,v=>v*gain);
const fade=Math.round(.02*fsHz);
const fadeEnds=pcm=>{for(let i=0;i<fade;i++){pcm[i]*=smooth(i/fade);pcm[pcm.length-1-i]*=smooth(i/fade);}return pcm;};
fadeEnds(listening);
async function save(name,pcm,listeningCopy=false) {
 const path=new URL(`${name}-192k.wav`,out);await fs.writeFile(path,encodeFloat32Wav(pcm,fsHz,1),{flag:'wx'});
 if(listeningCopy)execFileSync('ffmpeg',['-v','error','-n','-i',fileURLToPath(path),'-ar','48000','-c:a','pcm_s16le',fileURLToPath(new URL(`${name}-48k.wav`,out))]);
}
const auditionName=sequence==='drive'?'start-revs-first-second-coast':'start-idle-revs-coast';
await save('running-raw',running);await save('recorded-starter-raw',starter.pcm);await save(`${auditionName}-raw`,mix);
if(coast!=='legacy')await save('backdrive-raw',driveline);
await save(auditionName,listening,true);await save('start-and-idle',fadeEnds(listening.slice(0,7*fsHz)),true);
// Direct starter-only A/B: same canonical recording, original then Ural treatment.
const parts=[],abGains=[];
for(const package_ of [shared,heavy]) {
 const pcm=starterTrack(package_,Math.round(4.4*fsHz),false).pcm.slice(Math.round(.4*fsHz));
 let peak=0;for(const v of pcm)peak=Math.max(peak,Math.abs(v));
 const g=Math.min(10**(-25/20)/rms(pcm),.85/peak);for(let i=0;i<pcm.length;i++)pcm[i]*=g;
 parts.push(fadeEnds(pcm));abGains.push(20*Math.log10(g));
}
const ab=new Float32Array(Math.round(8.4*fsHz));ab.set(parts[0]);ab.set(parts[1],Math.round(4.4*fsHz));
await save('default-then-heavy-starter',ab,true);
assert.deepEqual(await fs.readFile(manifestUrl),originalManifest);assert.deepEqual(await fs.readFile(payloadUrl),originalPayload);
assert.deepEqual(await fs.readFile(carrier),carrierBytes);
await fs.writeFile(new URL('controls.json',out),JSON.stringify(controls,null,2)+'\n');
const metadata={carrierSha256:hash(carrierBytes),starterSourceManifestSha256:hash(originalManifest),starterSourcePayloadSha256:hash(originalPayload),
 starterSource:shared.manifest.provenance.source,rightsNotice:shared.rightsNotice,
 treatment:{filterGraph,eqGainDb:20*Math.log10(eqGain),settingsOverrides:overrides,pitchSemitones:12*Math.log2(overrides.basePlaybackRate)},
 pickup:{mode:pickup,onsetSeconds,curve:pickup==='surge'?pickupCurve:null,
   description:pickup==='surge'?'Fuel burst lifts 550 to 1150 RPM, drops through 690, settles at 725 within 1.6 s.':'Previous gentle 550 to 795 to 725 RPM handoff.'},
 sequence,coast,seconds:duration,rms:mixRms,peak,listeningGainDb:20*Math.log10(gain),abGainsDb:abGains,starterDiagnostics:starter.diagnostics,
 ...(coast!=='legacy'?{backdriveProfile,backdriveEnd:controls.at(-1).backdrive,
   coastMethod:'Cycle-mean native motored gas work plus authored engine friction/road drag decelerates engine and reflected vehicle inertia together. Phase-aligned native motored torque ripple excites authored casing modes only under backdrive load. Starter and earlier engine rendering are unchanged.',
   backdriveRadiationRms:rms(driveline),coastGainReferenceDb:-5.678079962336634}:{}),
 verifiedCanonicalSource:true,originalFilesUnchanged:true,starterFromRecordingOnly:true,proceduralStarterAdded:false,
 partitionReplayExact:true,starterInactiveAfter4Seconds:true,
 ...(coast==='legacy'?{enginePcmAfter4SecondsExact:true}:{}),
 sections:sequence==='drive'?driveSections:`0.4-3.02 s recorded cranking; around 3.05 s native engine catches; ${pickup==='surge'?'pickup peaks near 3.35 s, settles near 4.65 s':'gentle pickup settles near 4 s'}; idle until 8 s; rev 8-12 s, hold 12-13 s; coast 13-19 s; idle 19-24 s.`,
 ...(sequence==='drive'?{driveCurve,driveMethod:coast==='legacy'?
   'Authored RPM/fueling/clutch-handoff performance; no measured gear ratios or vehicle/road/transmission simulation. Continuous engine rendering with no reset at revs or shifts.':
   'Authored running/shift performance until 39 s; final overrun uses cycle-mean native gas work and authored friction, road drag and reflected vehicle inertia. Fixed gear, rigid coupling; no solved backlash, flexible shafts, tires, slope or actual Ural gear ratio.'}:{}),
 abSections:'Default recorded starter 0-4 s; same recording treated for Ural 4.4-8.4 s, each matched once to -25 dBFS RMS.',
 mastering:coast==='legacy'?'One constant gain across the full start/idle/rev/coast audition to -23 dBFS RMS, 20 ms endpoint fades. No per-section leveling.':
   'One constant gain matching the preceding 48-second drive audition, 20 ms endpoint fades. No per-section leveling.',
 status:'Separate listening treatment. The shared starter asset and .crankwave carrier remain unchanged. Reuses existing starter cursor with local audition overrides; not embedded in diesel runtime lifecycle.',
 limitations:'Pitch/cadence and EQ are artistic settings, not measured Ural starter specifications. Native running controls stay within the capture grid; catch is an authored handoff, not a cold-start simulation.'};
await fs.writeFile(new URL('audition.json',out),JSON.stringify(metadata,null,2)+'\n');
console.log(JSON.stringify(metadata,null,2));
