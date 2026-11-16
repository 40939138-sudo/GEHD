import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { PassThrough } from 'node:stream';
import { runReaQueries } from '../runner.mjs';
import { fileSha, readJson } from '../io.mjs';
import { createTracker } from '../processes.mjs';
const here=path.dirname(fileURLToPath(import.meta.url));
const project=process.env.GE_TEST_PROJECT_ROOT;
assert(project,'GE_TEST_PROJECT_ROOT must explicitly select fixture evidence root/dependencies');
const artifacts=path.join(project,'toolchain/engine-research-controller-r1/qualification');
await fs.mkdir(artifacts,{recursive:true});
async function scenario(name,extra={}) {
  const dir=await fs.mkdtemp(path.join(artifacts,name.replaceAll(':','_')+'-'));
  const target=path.join(dir,'synthetic.bin');await fs.writeFile(target,'SYNTHETIC TEST DATA ONLY');
  const plan={target,targetSha256:await fileSha(target),timeoutMs:2000,
    cleanup:{budgetMs:5000,exportTimeoutMs:1000,closeTimeoutMs:1000},
    calls:[{name:'open_binary',arguments:{path:target}},
      {id:'analysis',name:'analyze_function',arguments:{procedure:'0x1000'},...extra.step}],...extra.plan};
  if(extra.additionalCall)plan.calls.push(extra.additionalCall);
  const planPath=path.join(dir,'plan.json');await fs.writeFile(planPath,JSON.stringify(plan));
  let serverEntry=path.join(here,'mock_rea_server.mjs');
  if(extra.serverSource){serverEntry=path.join(dir,'synthetic-lifecycle-server.mjs');await fs.writeFile(serverEntry,extra.serverSource);}
  const events=[],input=new PassThrough();
  const result=await runReaQueries({planPath,output:path.join(dir,'run'),
    runtimeRoot:path.join(project,'草稿區/有界取證/rea-toolchain/runtime'),
    python:path.join(project,'toolchain/ai-texture-runtime/Scripts/python.exe'),
    serverEntry,serverArgs:[],input,...(extra.trackerFactory?{trackerFactory:extra.trackerFactory}:{}),
    testEnv:{GE_MOCK_SCENARIO:name,GE_MOCK_LOG:path.join(dir,'mock.jsonl'),GE_MOCK_DELAY_MS:'300'},
    onEvent:async e=>{events.push(e);if(extra.onEvent)await extra.onEvent(e,input,dir);}});
  input.destroy();return {dir,result,events,plan};
}
test('real SDK roundtrip records persisted responses and owned exit, old plan timeout applies',async()=>{
  const r=await scenario('success');assert.equal(r.result.status,'COMPLETE',JSON.stringify(r.result));
  assert.equal(r.result.resourceRelease,'RELEASED');assert.equal(r.result.targetAfterSha256,r.plan.targetSha256);
  assert.equal(r.result.calls[1].effectiveTimeoutMs,2000);assert(r.result.evidenceExported);
  const response=r.events.find(e=>e.type==='response_saved'&&e.stepId==='analysis');
  assert.equal((await readJson(path.join(r.dir,'run',response.file))).isError,undefined);
});
test('per-step timeout sends cancel and preserves unknown instead of fabricating response',async()=>{
  const r=await scenario('delay_ignore_cancel',{step:{timeoutMs:60}});
  assert.equal(r.result.status,'FAILED');assert.equal(r.result.unknownRequests.length,1);
  assert.equal(r.result.calls.length,1);assert(!r.result.evidenceExported);
  const log=await fs.readFile(path.join(r.dir,'mock.jsonl'),'utf8');assert.match(log,/cancel/);
  assert.equal(r.events.find(e=>e.stepId==='analysis'&&e.type==='request_sent').effectiveTimeoutMs,60);
});
test('allowed tool rejection persists and next followup can run',async()=>{
  let sent=false;
  const r=await scenario('tool_refusal',{step:{allowError:true},plan:{followup:true},
    onEvent:(e,input)=>{if(e.type==='ready'){if(!sent){sent=true;input.write(JSON.stringify({name:'procedure_assembly',arguments:{procedure:'0x1000'}})+'\n');}else input.end('finish\n');}}});
  assert.equal(r.result.status,'COMPLETE');assert.equal(r.result.calls[1].isError,true);assert.equal(r.result.calls.length,3);
});
test('cleanup error does not erase usable responses or grant resource release',async()=>{
  const r=await scenario('close_error');assert.equal(r.result.status,'FAILED');assert.equal(r.result.resourceRelease,'UNVERIFIED');
  assert.equal(r.result.calls[1].isError,false);assert(r.result.closeError);
});
test('disconnect retains unanswered request and does not auto retry',async()=>{
  const r=await scenario('disconnect');assert.equal(r.result.status,'FAILED');assert.equal(r.result.unknownRequests.length,1);
  assert.equal(r.events.filter(e=>e.type==='request_sent'&&e.stepId==='analysis').length,1);
});
test('acquisition deadline is rechecked after readiness work before an open is sent',async()=>{
  const r=await scenario('success',{plan:{acquisitionDeadlineMs:Date.now()+1000},onEvent:async e=>{
    if(e.type==='request_sent'&&e.starting==='open_binary')await new Promise(resolve=>setTimeout(resolve,1050));
  }});
  assert.equal(r.result.status,'FAILED');assert.equal(r.result.requests[0].dispatched,false);
  assert.equal(r.result.calls.length,0);assert.equal(r.result.resourceRelease,'RELEASED');
});

test('callback failure stops dispatch but cannot bypass cleanup or final receipt',async()=>{
  const r=await scenario('success',{additionalCall:{name:'procedure_assembly',arguments:{procedure:'0x1000'}},
    onEvent:e=>{if(e.type==='response_saved'&&e.stepId==='analysis')throw new Error('synthetic callback rejection');}});
  assert.equal(r.result.status,'FAILED');assert.equal(r.result.calls.length,2);
  assert(r.result.closeSucceeded);assert(r.result.clientClosed);assert.equal(r.result.resourceRelease,'RELEASED');
  assert(r.result.eventErrors.some(e=>e.stage==='callback'&&e.type==='response_saved'));
  assert.equal((await readJson(path.join(r.dir,'run/result.json'))).status,'FAILED');
  const log=(await fs.readFile(path.join(r.dir,'mock.jsonl'),'utf8')).trim().split('\n').map(JSON.parse);
  assert(!log.some(x=>x.kind==='request'&&x.message.method==='tools/call'&&x.message.params?.name==='procedure_assembly'));
});

test('lifecycle journal IO failure still closes transport and records the unsent request',async()=>{
  const r=await scenario('success',{onEvent:async(e,input,dir)=>{
    if(e.type==='connected'){
      const journal=path.join(dir,'run/lifecycle.jsonl');
      await fs.rename(journal,journal+'.before-failure');await fs.mkdir(journal);
    }
  }});
  assert.equal(r.result.status,'FAILED');assert(r.result.clientClosed);assert.equal(r.result.resourceRelease,'RELEASED');
  assert.equal(r.result.calls.length,0);assert.equal(r.result.requests[0].dispatched,false);
  assert(r.result.eventErrors.some(e=>e.stage==='journal'));
  assert.equal((await readJson(path.join(r.dir,'run/result.json'))).status,'FAILED');
});

test('received response with failed persistence is distinct from an unanswered call',async()=>{
  const r=await scenario('success',{additionalCall:{name:'procedure_assembly',arguments:{procedure:'0x1000'}},
    onEvent:async(e,input,dir)=>{if(e.type==='request_sent'&&e.stepId==='analysis')
      await fs.mkdir(path.join(dir,'run/001-analyze_function.json.writing'));}});
  assert.equal(r.result.status,'FAILED');assert.equal(r.result.unknownRequests.length,0);
  const receipt=r.result.requests.find(x=>x.id==='analysis');
  assert(receipt.dispatched);assert(receipt.responseReceived);assert.equal(receipt.responseSaved,false);assert(receipt.storageError);
  assert.equal(r.result.calls.length,1);assert(r.result.evidenceExported);assert(r.result.closeSucceeded);
  const bundle=await readJson(path.join(r.dir,'run/evidence.json'));
  assert(bundle.synthetic);assert(bundle.records.some(x=>x.operation==='analyze_function'));
});

// These generated servers only exercise SDK/lifecycle failures; they never launch REA.
function lifecycleServer(mode){return `// SYNTHETIC LIFECYCLE TEST ONLY\n
import {createInterface} from 'node:readline';
const lines=createInterface({input:process.stdin});
const send=(id,value)=>process.stdout.write(JSON.stringify({jsonrpc:'2.0',id,...value})+'\\n');
lines.on('line',line=>{const m=JSON.parse(line);if(m.method==='initialize'){
 if(${JSON.stringify(mode)}==='reject')send(m.id,{error:{code:-32000,message:'synthetic initialize rejection'}});
 else if(${JSON.stringify(mode)}!=='connect_timeout')send(m.id,{result:{protocolVersion:'2025-11-25',capabilities:{tools:{}},serverInfo:{name:'synthetic-lifecycle',version:'1'}}});
}});
lines.on('close',()=>process.exit(0));\n`;}

test('fast connect rejection is handled while process tracking is still pending',async()=>{
  let finishCalled=false;
  const r=await scenario('success',{serverSource:lifecycleServer('reject'),
    trackerFactory:python=>{const tracker=createTracker(python);return{
      start:async pid=>{await tracker.start(pid);await new Promise(resolve=>setTimeout(resolve,200));},
      finish:async ms=>{finishCalled=true;return tracker.finish(ms);}
    };}});
  assert.equal(r.result.status,'FAILED');assert.match(r.result.error,/synthetic initialize rejection/);
  assert(finishCalled);assert(r.result.clientClosed);assert.equal(r.result.calls.length,0);
  assert.equal((await readJson(path.join(r.dir,'run/result.json'))).status,'FAILED');
});

for(const mode of ['connect_timeout','list_timeout'])test(mode+' uses explicit lifecycle timeout and cleans up',async()=>{
  const r=await scenario('success',{serverSource:lifecycleServer(mode),plan:{lifecycleTimeoutMs:100}});
  assert.equal(r.result.status,'FAILED');assert.equal(r.result.lifecycleTimeoutMs,100);
  assert.match(r.result.error,/timeout|deadline/i);assert(r.result.clientClosed);assert.equal(r.result.calls.length,0);
  assert(r.events.some(e=>e.type==='cleanup_started'));
});

test('opened target mismatch blocks analysis and retains the original open response',async()=>{
  const r=await scenario('success',{onEvent:async(e,input,dir)=>{
    if(e.type==='connected')await fs.writeFile(path.join(dir,'synthetic.bin'),'CHANGED SYNTHETIC TARGET');
  }});
  assert.equal(r.result.status,'FAILED');assert.match(r.result.error,/Opened target identity/);
  assert.equal(r.result.calls.length,1);assert(r.result.requests[0].responseSaved);assert(!r.result.openedTargetVerified);
  assert(r.result.closeSucceeded);assert(r.result.targetError);
  assert((await readJson(path.join(r.dir,'run/000-open_binary.json'))).structuredContent.result.synthetic);
});

test('cleanup callback and tracker failures cannot suppress result publication',async()=>{
  const r=await scenario('success',{
    trackerFactory:python=>{const tracker=createTracker(python);return{
      start:pid=>tracker.start(pid),finish:async ms=>{await tracker.finish(ms);throw new Error('synthetic tracker finish rejection');}
    };},
    onEvent:e=>{if(e.type==='cleanup_started'||e.type==='runner_finished')throw new Error('synthetic cleanup callback rejection');}
  });
  assert.equal(r.result.status,'FAILED');assert(r.result.closeSucceeded);assert(r.result.clientClosed);
  assert.equal(r.result.resourceRelease,'UNVERIFIED');assert(r.result.cleanupErrors.some(e=>e.stage==='process release'));
  assert.equal((await readJson(path.join(r.dir,'run/result.json'))).status,'FAILED');
});

test('final result IO failure is returned after cleanup and not reported as a saved receipt',async()=>{
  const r=await scenario('success',{onEvent:async(e,input,dir)=>{
    if(e.type==='connected')await fs.mkdir(path.join(dir,'run/result.json.writing'));
  }});
  assert.equal(r.result.status,'FAILED');assert(r.result.resultError);assert(r.result.closeSucceeded);assert(r.result.clientClosed);
  assert(r.events.find(e=>e.type==='runner_finished').finalReceiptPending);
  await assert.rejects(fs.readFile(path.join(r.dir,'run/result.json')),{code:'ENOENT'});
});

test('tracker start rejection still invokes tracker finish and transport close',async()=>{
  let finishCalled=false;
  const r=await scenario('success',{trackerFactory:python=>{const tracker=createTracker(python);return{
    start:async pid=>{await tracker.start(pid);throw new Error('synthetic tracker start rejection');},
    finish:async ms=>{finishCalled=true;return tracker.finish(ms);}
  };}});
  assert.equal(r.result.status,'FAILED');assert.match(r.result.error,/tracker start rejection/);
  assert(finishCalled);assert(r.result.clientClosed);
});
