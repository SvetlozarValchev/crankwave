// Capture device-rate PCM from the real standalone harness worker's output ring.
// The device sink is silent and drains faster than wall time. Rendering, package
// loading, lifecycle, driving, limiter and resampling remain the browser's code.
import fs from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import assert from 'node:assert/strict';
import {spawn} from 'node:child_process';
import {setTimeout as delay} from 'node:timers/promises';
import {createHash} from 'node:crypto';

const [url, carrier, output, ...extra] = process.argv.slice(2);
const totalSeconds=extra.length===0?40:Number(extra[1]);
if (!output || (extra.length && (extra.length!==2 || extra[0]!=='--seconds')) ||
    !Number.isInteger(totalSeconds) || totalSeconds<40 || totalSeconds>120)
  throw Error('Usage: node record_harness.mjs <localhost-harness-url> <carrier.crankwave> <new-directory> [--seconds 40..120]');
assert.ok(['localhost','127.0.0.1'].includes(new URL(url).hostname));
await fs.mkdir(output);
const profile = await fs.mkdtemp(path.join(os.tmpdir(),'crankwave-capture-'));
const chrome = spawn('google-chrome',['--headless=new','--no-sandbox','--disable-gpu','--mute-audio',
  '--autoplay-policy=no-user-gesture-required','--remote-debugging-port=0',`--user-data-dir=${profile}`,'about:blank'],
  {stdio:['ignore','ignore','ignore']});
async function until(fn, predicate, label, timeout=120000) {
  const end=Date.now()+timeout; let last;
  while(Date.now()<end) {try {last=await fn(); if(predicate(last))return last;} catch(e){last=e.message;} await delay(100);}
  throw Error(`Timed out ${label}: ${JSON.stringify(last)}`);
}
let socket;
try {
  const port = await until(()=>fs.readFile(path.join(profile,'DevToolsActivePort'),'utf8'),Boolean,'Chrome port');
  const targets=await (await fetch(`http://127.0.0.1:${port.split('\n')[0]}/json/list`)).json();
  socket = new WebSocket(targets.find(t=>t.type==='page').webSocketDebuggerUrl);
  await new Promise((resolve,reject)=>{socket.onopen=resolve;socket.onerror=reject;});
  let next=0;const pending=new Map(), errors=[];
  socket.onmessage=event=>{
    const m=JSON.parse(event.data);
    if(m.id){const p=pending.get(m.id);pending.delete(m.id);m.error?p.reject(Error(JSON.stringify(m.error))):p.resolve(m.result);}
    else if(m.method==='Runtime.exceptionThrown')errors.push(m.params.exceptionDetails);
  };
  const send=(method,params={})=>new Promise((resolve,reject)=>{
    const id=++next;pending.set(id,{resolve,reject});socket.send(JSON.stringify({id,method,params}));
  });
  const evaluate=async expression=>{
    const r=await send('Runtime.evaluate',{expression,awaitPromise:true,returnByValue:true});
    if(r.exceptionDetails)throw Error(JSON.stringify(r.exceptionDetails));return r.result.value;
  };
  await send('Runtime.enable');await send('Page.enable');
  await send('Page.addScriptToEvaluateOnNewDocument',{source:`(() => {
    const c=globalThis.capture={frames:0, chunks:[], events:[], stats:[], errors:[], done:false, next:0, command:0};
    const schedule=[
      [7,'set-driver',{selector:'N',throttle01:.35,brake01:0}],
      [10,'set-driver',{selector:'N',throttle01:0,brake01:0}],
      [13,'set-driver',{selector:'N',throttle01:1,brake01:0}],
      [18,'set-driver',{selector:'N',throttle01:0,brake01:0}],
      [21,'set-driver',{selector:'D',throttle01:.75,brake01:0}],
      [31,'set-driver',{selector:'D',throttle01:0,brake01:0}],
      [36,'key-off',null],
    ];
    const NativeWorker=Worker;
    globalThis.Worker=new Proxy(NativeWorker,{construct(Target,args){
      const worker=new Target(...args);c.worker=worker;
      worker.addEventListener('message',e=>{
        const m=e.data;
        if(m.type==='stats')c.stats.push({capturedFrames:c.frames,...m});
        if(m.type==='playback-error'||m.ok===false)c.errors.push(m);
        if(m.type==='reply'&&typeof m.requestId==='string'&&m.requestId.startsWith('capture-'))c.events.push({frame:c.frames,reply:m});
      });return worker;
    }});
    class Context {
      constructor(){this.sampleRate=48000;this.state='suspended';this.destination={};this.audioWorklet={addModule:async()=>{}};}
      async resume(){this.state='running';}async close(){this.state='closed';}
    }
    class Node {
      constructor(context,name,options){this.config=options.processorOptions;this.port={postMessage(){},addEventListener(){},removeEventListener(){},start(){},close(){}};}
      connect(destination){
        const cfg=this.config,h=new Int32Array(cfg.sharedBuffer,0,cfg.headerBytes/4),pcm=new Float32Array(cfg.sharedBuffer,cfg.headerBytes);
        this.timer=setInterval(()=>{
          if(c.done||Atomics.load(h,cfg.indices.producerState)!==1)return;
          const count=Math.min(960,Atomics.load(h,cfg.indices.availableFrames),${totalSeconds}*48000-c.frames);
          if(!count)return;
          const start=Atomics.load(h,cfg.indices.readFrame),chunk=new Float32Array(count);
          for(let i=0;i<count;i++)chunk[i]=pcm[(start+i)%cfg.capacityFrames];
          c.chunks.push(chunk);c.frames+=count;
          Atomics.store(h,cfg.indices.readFrame,(start+count)%cfg.capacityFrames);
          Atomics.sub(h,cfg.indices.availableFrames,count);
          while(c.next<schedule.length&&c.frames>=schedule[c.next][0]*48000){
            const [seconds,type,controls]=schedule[c.next++];
            c.events.push({frame:c.frames,seconds,type,controls});
            c.worker.postMessage({type,controls,requestId:'capture-'+(++c.command)});
          }
          if(c.frames===${totalSeconds}*48000){c.done=true;clearInterval(this.timer);}
        },1);return destination;
      }
      disconnect(){clearInterval(this.timer);}
    }
    globalThis.AudioContext=globalThis.webkitAudioContext=Context;globalThis.AudioWorkletNode=Node;
  })();`});
  await send('Page.navigate',{url});
  await until(()=>evaluate('document.querySelector("#security-status")?.textContent.trim()'),v=>v==='Isolated audio ready','isolated harness');
  const doc=await send('DOM.getDocument');
  const input=await send('DOM.querySelector',{nodeId:doc.root.nodeId,selector:'#package-file-input'});
  await send('DOM.setFileInputFiles',{nodeId:input.nodeId,files:[path.resolve(carrier)]});
  await until(()=>evaluate('document.querySelector("#package-status").textContent.trim()'),v=>v==='Package ready','carrier load');
  await evaluate('document.querySelector("#neutral-button").click(); document.querySelector("#play-button").click(); true');
  await until(()=>evaluate('({done:capture.done,frames:capture.frames,errors:capture.errors})'),v=>v.done||v.errors.length,`${totalSeconds} seconds of harness PCM`);
  const state=await evaluate('({frames:capture.frames,events:capture.events,stats:capture.stats,errors:capture.errors})');
  await fs.writeFile(path.join(output,'capture-state.json'),JSON.stringify(state,null,2)+'\n');
  assert.deepEqual(state.errors,[]);assert.deepEqual(errors,[]);assert.equal(state.frames,totalSeconds*48000);
  const chunks=await evaluate(`capture.chunks.map(chunk=>{
    const bytes=new Uint8Array(chunk.buffer);let s='';
    for(let i=0;i<bytes.length;i+=8192)s+=String.fromCharCode(...bytes.subarray(i,i+8192));
    return btoa(s);
  })`);
  const pcm=Buffer.concat(chunks.map(s=>Buffer.from(s,'base64')));
  assert.equal(pcm.length,state.frames*4);
  let peak=0,energy=0;for(let i=0;i<state.frames;i++){
    const v=pcm.readFloatLE(i*4);assert.ok(Number.isFinite(v));peak=Math.max(peak,Math.abs(v));energy+=v*v;
  }
  const gain=Math.min(1,.94/Math.max(peak,1e-9));
  const wav=Buffer.alloc(44+state.frames*2);
  wav.write('RIFF');wav.writeUInt32LE(wav.length-8,4);wav.write('WAVEfmt ',8);wav.writeUInt32LE(16,16);
  wav.writeUInt16LE(1,20);wav.writeUInt16LE(1,22);wav.writeUInt32LE(48000,24);wav.writeUInt32LE(96000,28);
  wav.writeUInt16LE(2,32);wav.writeUInt16LE(16,34);wav.write('data',36);wav.writeUInt32LE(state.frames*2,40);
  for(let i=0;i<state.frames;i++)wav.writeInt16LE(Math.round(pcm.readFloatLE(i*4)*gain*32767),44+i*2);
  await fs.writeFile(path.join(output,'harness-48k.wav'),wav);
  await fs.writeFile(path.join(output,'harness-48k.f32le'),pcm);
  await fs.writeFile(path.join(output,'capture.json'),JSON.stringify({...state,peak,rms:Math.sqrt(energy/state.frames),listening_gain:gain,
    duration_seconds:totalSeconds,
    carrier_sha256:createHash('sha256').update(await fs.readFile(carrier)).digest('hex'),
    capture_method:'Actual standalone browser worker and device resampler; PCM copied from output ring by silent accelerated sink. Control timing includes normal audio lead.'},null,2)+'\n');
  console.log(JSON.stringify({output,frames:state.frames,peak,listeningGain:gain,stats:state.stats.length}));
} finally {
  socket?.close();
  const stopped=new Promise(resolve=>chrome.once('exit',resolve));chrome.kill('SIGTERM');
  await Promise.race([stopped,delay(2000)]);
  if(chrome.exitCode===null && chrome.signalCode===null){chrome.kill('SIGKILL');await Promise.race([stopped,delay(2000)]);}
  await fs.rm(profile,{recursive:true,force:true,maxRetries:10,retryDelay:200});
}
