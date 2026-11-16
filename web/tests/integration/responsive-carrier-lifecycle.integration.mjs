// Integration with an actual independently baked carrier, not synthetic PCM.
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import {createHash} from 'node:crypto';
import {loadResponsiveAudioCrankwave} from '../../runtime/crankwave-package.js';
import {CrankwaveAudioEngine} from '../../runtime/crankwave-audio-engine.js';
import {ResponsiveDriveSession} from '../../runtime/responsive-drive-session.js';

const file=process.argv[2], playbackBlockFrames=process.argv[3]===undefined?undefined:Number(process.argv[3]);
if(!file)throw Error('Usage: node responsive-carrier-lifecycle.integration.mjs <carrier.crankwave> [playback-block-frames]');
const loaded=await loadResponsiveAudioCrankwave(await fs.readFile(file));
const e=new CrankwaveAudioEngine(loaded,{playbackBlockFrames}), session=new ResponsiveDriveSession(e,loaded.runtime);
assert.ok(e.limiter && loaded.runtime.lifecyclePackage && loaded.runtime.sharedRecordedStarterPackage);
const initial=[], state={}, sha=chunks=>createHash('sha256').update(Buffer.concat(chunks.map(x=>Buffer.from(x.buffer)))).digest('hex');
const startBlocks=Math.ceil((loaded.runtime.lifecyclePackage.startup.artifact.frameCount+e.latencyFrames)/e.processFrames)+50;
// A heavy engine may legitimately run down for longer than four seconds.
// Verify stop and silence against the actual packaged capture duration, with
// one extra second for the final silence check; do not shorten the rundown.
const shutdownFrames=Math.max(loaded.runtime.lifecyclePackage.shutdown.artifact.frameCount,
  loaded.runtime.lifecyclePackage.shutdownElevated?.artifact.frameCount??0);
const shutdownBlocks=Math.max(200,Math.ceil((shutdownFrames+e.latencyFrames)/e.processFrames)+50);
const offBlock=startBlocks+100, totalBlocks=offBlock+shutdownBlocks;
let tailEnergy=0, peak=0;
for(let block=0;block<totalBlocks;block++){
  if(block===offBlock||block===offBlock+10)session.stopEngine();
  const point=block<startBlocks?{rpm:e.limiter.idleRpm,throttle01:0,load01:0}:
    {rpm:e.limiter.activationRpm,throttle01:1,load01:1,
      limiterCutActive:(block>=startBlocks+25&&block<startBlocks+35)||(block>=startBlocks+50&&block<startBlocks+60)};
  const pcm=session.process(point);
  assert.equal(pcm.length,e.processFrames);
  for(const v of pcm){assert.ok(Number.isFinite(v));peak=Math.max(peak,Math.abs(v));if(block>=totalBlocks-50)tailEnergy+=v*v;}
  if(block<50)initial.push(pcm);
  if(block===startBlocks-1)state.running=session.snapshot();
}
state.stopped=session.snapshot();
assert.equal(state.running.lifecycle.startupCount,1);
assert.ok(state.running.lifecycle.audibleHandoffEndFrame>0,'pickup hands over to running material');
assert.equal(state.running.starter.catchCount,1);
assert.equal(state.running.starter.fallbackCatchCount,0,'catch comes from captured combustion evidence');
assert.equal(state.running.starter.payloadSha256,loaded.runtime.sharedRecordedStarterPackage.payloadSha256,
  'play the verified packaged starter, including delivery-rate/encoding conversions');
assert.equal(state.running.starter.sourceSha256,'818adef5e4737957ddb2ca061a50b05666776ce50c06d866fcc72e9c42767df8',
  'reuse the accepted recorded starter source');
assert.equal(state.stopped.lifecycle.shutdownCount,1,'repeated key-off is idempotent');
assert.equal(state.stopped.stage,'Stopped');
assert.equal(state.stopped.lifecycle.outputMode,'stopped');
assert.ok(tailEnergy<1e-12,'key-off ends in silence');
assert.equal(e.diagnostics().runtime.limiter.triggers,2,'both host cuts play captured limiter performances');
session.reset();
const repeated=[];
for(let i=0;i<50;i++)repeated.push(session.process({rpm:e.limiter.idleRpm,throttle01:0,load01:0}));
assert.equal(sha(initial),sha(repeated),'restart repeats startup deterministically');
session.reset({startEngine:false});
for(let i=0;i<50;i++)session.process({rpm:e.limiter.idleRpm,throttle01:0,load01:0});
assert.equal(session.snapshot().lifecycle.startupCount,0,'running reset skips startup');
console.log(JSON.stringify({file,passed:true,latencyFrames:e.latencyFrames,peak,tailEnergy,startupSha256:sha(initial),
  starterCatchCount:state.running.starter.catchCount,startupHandoffFrame:state.running.lifecycle.audibleHandoffEndFrame,
  limiterCuts:2,shutdownCount:state.stopped.lifecycle.shutdownCount,
  shutdownVerificationSeconds:shutdownBlocks*e.processFrames/e.sampleRate}));
