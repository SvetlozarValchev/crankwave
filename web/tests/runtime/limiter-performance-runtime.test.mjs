import test from 'node:test';
import assert from 'node:assert/strict';
import {createHash, webcrypto} from 'node:crypto';
import {LimiterPerformanceCursor, loadLimiterPerformance} from '../../runtime/limiter-performance-runtime.js';

function fixture() {
  const samples = Float32Array.from({length: 16000}, (_, i) => .1 * Math.sin(i * .04));
  const manifest = {schema:'crankwave/limiter-performance-v1', engine:'test', sample_rate_hz:192000,
    idle_rpm:765, activation_rpm:4500, cut_seconds:.1, maximum_rpm:4700,
    frame_count:samples.length, fade_frames:120,
    clips:[{start_frame:0,hold_start_frame:1000,hold_end_frame:5000,release_frame:6000,end_frame:8000},
      {start_frame:8000,hold_start_frame:9000,hold_end_frame:13000,release_frame:14000,end_frame:16000}],
    routes:[{bus_id:'dry',file:'route-0.f32',sha256:createHash('sha256').update(new Uint8Array(samples.buffer)).digest('hex'),reference_rms:[.1/Math.SQRT2,.1/Math.SQRT2]}]};
  return {manifest,routes:[{busId:'dry',samples,referenceRms:manifest.routes[0].reference_rms}]};
}
function block(cursor, cut, frames=3840, scale=1) {
  return cursor.mix([Float32Array.from({length:frames},(_,i)=> scale*.01*Math.sin(i*.03))], cut, scale)[0];
}
const power = samples => samples.reduce((sum,x)=>sum+x*x,0)/samples.length;

test('captured cuts remain audible through long overspeed, recover, vary, and reset',()=>{
  const cursor=new LimiterPerformanceCursor(fixture());
  for(let i=0;i<10;++i) block(cursor,false);
  const reference=power(block(cursor,false));
  for(let i=0;i<100;++i) {
    const samples=block(cursor,true);
    assert.ok(samples.every(Number.isFinite));
    assert.ok(power(samples)>reference*.1 && power(samples)<reference*4, 'matches running level without a silent gate');
  }
  assert.equal(cursor.triggerCount,1);
  assert.equal(cursor.clipIndex,0);
  block(cursor,false,4000);
  assert.equal(cursor.active,false);
  const base=Float32Array.from({length:200},(_,i)=>.01*Math.sin(i*.03));
  assert.deepEqual(cursor.mix([base.slice()],false)[0],base);
  block(cursor,true);
  assert.equal(cursor.clipIndex,1);
  assert.equal(cursor.triggerCount,2);
  cursor.reset();
  assert.equal(cursor.triggerCount,0);
  assert.equal(cursor.active,false);
});

test('presentation source scaling preserves captured level and timing',()=>{
  const a=new LimiterPerformanceCursor(fixture()), b=new LimiterPerformanceCursor(fixture());
  for(let i=0;i<10;++i) {block(a,false);block(b,false,3840,3);}
  const normal=block(a,true), scaled=block(b,true,3840,3);
  for(let i=0;i<normal.length;++i) assert.ok(Math.abs(scaled[i]-normal[i]*3)<1e-7);
  assert.equal(a.position,b.position);
});

test('loader checks PCM integrity, markers and reference levels',async()=>{
  const pkg=fixture();
  const url=new URL('https://bank.invalid/limiter/runtime.json');
  const fetch=async value=>new Response(value.endsWith('.json')?JSON.stringify(pkg.manifest):pkg.routes[0].samples.buffer);
  assert.equal((await loadLimiterPerformance(url,{fetch,crypto:webcrypto})).routes.length,1);
  pkg.manifest.routes[0].reference_rms[0]=0;
  await assert.rejects(loadLimiterPerformance(url,{fetch,crypto:webcrypto}),/reference RMS/);
  pkg.manifest.routes[0].reference_rms[0]=.1;
  pkg.routes[0].samples[12]=.123;
  await assert.rejects(loadLimiterPerformance(url,{fetch,crypto:webcrypto}),/hash\/length/);
  pkg.manifest.clips[1].start_frame=7999;
  await assert.rejects(loadLimiterPerformance(url,{fetch,crypto:webcrypto}),/clip/);
});
