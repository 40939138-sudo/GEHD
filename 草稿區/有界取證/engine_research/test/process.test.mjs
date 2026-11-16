import test from 'node:test';
import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { once } from 'node:events';
import path from 'node:path';
import { createTracker } from '../processes.mjs';

const expected={parentPid:9,image:'C:\\runtime\\node.exe'};
const row=(pid=100,parentPid=9,createdFiletime='100',extra={})=>({pid,parentPid,createdFiletime,image:expected.image,...extra});
const snapshot=(processes,complete=true)=>({complete,processes});
function fixture(...snapshots) {
  let index=0;
  return createTracker('unused',{pollIntervalMs:0,inventory:async(_python,pids)=>{
    assert(index<snapshots.length,'Unexpected inventory read');
    const value=snapshots[index++];return typeof value==='function'?value(pids):value;
  }});
}

test('initial identity requires parent, image and creation; later snapshots cannot repair acquisition',async()=>{
  for(const invalid of [row(100,8),row(100,9,'100',{image:'C:\\other.exe'}),row(100,9,undefined,{createdFiletime:undefined}),row(100,9,'100',{imageError:5})]) {
    const tracker=fixture(snapshot([invalid]),snapshot([row()]));
    await tracker.start(100,expected);
    const result=await tracker.finish(0);
    assert.equal(result.status,'UNVERIFIED');assert.equal(result.known.length,0);
  }
});

test('PID reuse does not adopt the replacement or its descendants',async()=>{
  const tracker=fixture(snapshot([row()]),snapshot([row(100,9,'200'),row(101,100,'250')]));
  await tracker.start(100,expected);
  const result=await tracker.finish(0);
  assert.equal(result.status,'RELEASED');assert.equal(result.known.length,1);
  assert.deepEqual(result.remaining,[]);assert.match(result.scope,/not an operating-system-wide absence claim/);
});

test('unreadable current identity is uncertainty, not release',async()=>{
  const tracker=fixture(snapshot([row()]),snapshot([row(100,9,'100',{image:undefined,imageError:5})]));
  await tracker.start(100,expected);
  const result=await tracker.finish(0);
  assert.equal(result.status,'UNVERIFIED');assert.equal(result.remaining.length,1);
  assert(result.errors.some(e=>e.error==='identity_unreadable'));
});

test('child chains require current proved parents, regardless of snapshot order',async()=>{
  const tracker=fixture(snapshot([row(102,101,'300'),row(101,100,'200'),row()]),snapshot([]));
  await tracker.start(100,expected);
  const result=await tracker.finish(0);
  assert.equal(result.status,'RELEASED');assert.deepEqual(result.known.map(x=>x.pid).sort(),[100,101,102]);
});

test('known child remains owned after parent exit and can establish a current descendant',async()=>{
  const tracker=fixture(snapshot([row(),row(101,100,'200')]),snapshot([row(102,101,'300'),row(101,100,'200')]));
  await tracker.start(100,expected);
  const result=await tracker.finish(0);
  assert.equal(result.status,'STILL_RUNNING');assert.deepEqual(result.remaining.map(x=>x.pid).sort(),[101,102]);
});

test('an exited parent cannot establish a newly seen child; an older child cannot belong to a reused parent',async()=>{
  for(const next of [[row(101,100,'200')],[row(),row(101,100,'50')]]) {
    const tracker=fixture(snapshot([row()]),snapshot(next));
    await tracker.start(100,expected);
    const result=await tracker.finish(0);
    assert.equal(result.known.length,1);assert(!result.remaining.some(x=>x.pid===101));
  }
});

test('same PID and creation with a changed image is not the same identity',async()=>{
  const tracker=fixture(snapshot([row()]),snapshot([row(100,9,'100',{image:'C:\\other.exe'})]));
  await tracker.start(100,expected);
  const result=await tracker.finish(0);
  assert.equal(result.status,'UNVERIFIED');assert.equal(result.known.length,1);
  assert(result.errors.some(e=>e.error==='identity_mismatch'));
});

test('incomplete enumeration cannot establish release',async()=>{
  const tracker=fixture(snapshot([row()]),snapshot([],false));
  await tracker.start(100,expected);
  assert.equal((await tracker.finish(0)).status,'UNVERIFIED');
});

const unreadChild=(extra={})=>({pid:101,parentPid:100,name:'short-lived.exe',identityError:87,...extra});

test('transient candidate is retained in query roots and resolved only by complete later absence',async()=>{
  const tracker=fixture(snapshot([row(),unreadChild()]),pids=>{
    assert(pids.includes(101),'Unread candidate must be queried explicitly after parent exit');
    return snapshot([]);
  });
  await tracker.start(100,expected);
  const result=await tracker.finish(0);
  assert.equal(result.status,'RELEASED');assert.equal(result.errors.length,1);
  assert.equal(result.errors[0].partialIdentity.parentPid,100);
  assert.equal(result.errors[0].observedParentIdentity.createdFiletime,'100');
  assert.equal(result.errors[0].resolution.kind,'absent_from_complete_snapshot');
  assert.equal(result.pendingCandidates.length,0);
  assert(result.finalSnapshot.complete);assert(result.finalSnapshot.queryPids.includes(101));
  assert.deepEqual(result.finalSnapshot.processes,[]);
});

test('later readable child may become owned but does not erase its historical unread observation',async()=>{
  const tracker=fixture(snapshot([row(),unreadChild()]),snapshot([row(),row(101,100,'150')]),snapshot([]));
  await tracker.start(100,expected);await tracker.sample();
  const result=await tracker.finish(0);
  assert.equal(result.status,'RELEASED');assert(result.known.some(x=>x.pid===101));
  assert.equal(result.errors[0].resolution.kind,'absent_from_complete_snapshot');
});

test('unread candidate without creation is not resolved by a present reused PID',async()=>{
  const replacement=row(101,77,'300');
  const tracker=fixture(snapshot([row(),unreadChild()]),snapshot([replacement]));
  await tracker.start(100,expected);
  const result=await tracker.finish(0);
  assert.equal(result.status,'UNVERIFIED');assert.equal(result.pendingCandidates.length,1);
  assert.equal(result.errors[0].resolution,undefined);assert(!result.known.some(x=>x.pid===101));
});

test('known candidate creation can prove replacement without adopting replacement or new descendants',async()=>{
  const partial=row(101,100,'150',{image:undefined,imageError:5});
  const tracker=fixture(snapshot([row(),partial]),snapshot([row(101,77,'300'),row(102,101,'350')]));
  await tracker.start(100,expected);
  const result=await tracker.finish(0);
  assert.equal(result.status,'RELEASED');assert.equal(result.known.length,1);
  assert.equal(result.errors[0].resolution.kind,'creation_replaced');
  assert.equal(result.errors[0].resolution.replacementIdentity.createdFiletime,'300');
});

test('an incomplete absence cannot resolve a pending candidate',async()=>{
  const tracker=fixture(snapshot([row(),unreadChild()]),snapshot([],false));
  await tracker.start(100,expected);
  const result=await tracker.finish(0);
  assert.equal(result.status,'UNVERIFIED');assert.equal(result.pendingCandidates.length,1);
  assert.equal(result.errors[0].resolution,undefined);
});

test('a creation rejected by snapshot timing cannot establish the original candidate identity',async()=>{
  const partial=row(101,100,'150',{identityError:'created_after_snapshot_start'});
  const tracker=fixture(snapshot([row(),partial]),snapshot([row(101,77,'300')]));
  await tracker.start(100,expected);
  const result=await tracker.finish(0);
  assert.equal(result.status,'UNVERIFIED');assert.equal(result.pendingCandidates[0].createdFiletime,null);
  assert.equal(result.errors[0].resolution,undefined);
});

test('already observed candidate descendant remains pending after unread parent disappears',async()=>{
  const child=row(102,101,'180');
  const tracker=fixture(snapshot([row(),unreadChild(),child]),pids=>{
    assert(pids.includes(101));assert(pids.includes(102));return snapshot([child]);
  });
  await tracker.start(100,expected);
  const result=await tracker.finish(0);
  assert.equal(result.status,'UNVERIFIED');assert.equal(result.known.length,1);
  assert.deepEqual(result.pendingCandidates.map(x=>x.pid),[102]);
  assert(result.errors.find(x=>x.pid===101).resolution);
});

test('initial root acquisition failure stays fatal even after complete absence',async()=>{
  const tracker=fixture(snapshot([row(100,9,'100',{image:undefined})]),snapshot([]));
  await tracker.start(100,expected);
  const result=await tracker.finish(0);
  assert.equal(result.status,'UNVERIFIED');assert.equal(result.known.length,0);
  assert.equal(result.errors[0].error,'initial_identity_unreadable');assert.equal(result.errors[0].resolution,undefined);
});

test('temporarily unread owned identity can resolve on proved creation replacement',async()=>{
  const tracker=fixture(snapshot([row()]),snapshot([row(100,9,'100',{image:undefined,imageError:5})]),snapshot([row(100,9,'200')]));
  await tracker.start(100,expected);await tracker.sample();
  const result=await tracker.finish(0);
  assert.equal(result.status,'RELEASED');assert.equal(result.known.length,1);
  assert.equal(result.known[0].createdFiletime,'100');
  assert.equal(result.errors[0].resolution.kind,'creation_replaced');
});

test('real owned short Node process is identified and its observed identity is released',{
  skip:process.platform!=='win32',timeout:15000
},async t=>{
  const project=process.env.GE_TEST_PROJECT_ROOT;
  assert(project,'GE_TEST_PROJECT_ROOT must select the existing Python dependency');
  const python=path.join(project,'toolchain/ai-texture-runtime/Scripts/python.exe');
  const child=spawn(process.execPath,['-e',"setTimeout(()=>process.exit(0),8000);process.stdin.resume();process.stdin.on('end',()=>process.exit(0));"],{
    windowsHide:true,stdio:['pipe','ignore','pipe']
  });
  const closed=once(child,'close');
  const tracker=createTracker(python,{pollIntervalMs:0});
  try {
    await once(child,'spawn');await tracker.start(child.pid);
    const live=await tracker.sample();
    assert(live.complete);assert(live.processes.some(x=>x.pid===child.pid&&x.parentPid===process.pid&&x.createdFiletime));
    child.stdin.end();await closed;
    const result=await tracker.finish(1000);
    assert.equal(result.status,'RELEASED',JSON.stringify(result));
    const root=result.known.find(x=>x.pid===child.pid);
    assert(root);assert.equal(root.image.toLowerCase(),process.execPath.toLowerCase());
    t.diagnostic(JSON.stringify({status:result.status,known:result.known,scope:result.scope}));
  }finally {
    if(child.exitCode===null && child.signalCode===null)child.kill();
    await closed.catch(()=>{});
  }
});
