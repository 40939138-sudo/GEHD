import fs from 'node:fs/promises';
import path from 'node:path';
import {spawn} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {run,bytesAtRva,peInfo,relocationOffsets,sha} from './run.mjs';
const [fixture,output,runtime]=process.argv.slice(2);
if(![fixture,output,runtime].every(x=>x&&path.isAbsolute(x))||/[^\x20-\x7e]/.test(output))throw Error('Absolute fixture/runtime and ASCII output required');
await fs.mkdir(output);
const binary=await fs.readFile(fixture), identity=sha(binary),summary={fixture,fixtureSha256:identity,cases:[]};
summary.codeRoot=path.dirname(fileURLToPath(import.meta.url));summary.sources={};
await fs.mkdir(path.join(output,'source-snapshot'));
for(const name of ['observer.js','run.mjs','qualify.mjs','fixture.cpp']) {
  const bytes=await fs.readFile(path.join(summary.codeRoot,name));summary.sources[name]=sha(bytes);
  await fs.writeFile(path.join(output,'source-snapshot',name),bytes,{flag:'wx'});
}
const wait=async test=>{const end=Date.now()+20000;while(Date.now()<end){if(await test())return;await new Promise(r=>setTimeout(r,25));}throw Error('Fixture timeout');};
const exists=p=>fs.access(p).then(()=>true,()=>false);
for(const scenario of ['baseline','normal','event_cap','buffer_cap','wrong_module','wrong_hash','wrong_bytes','wrong_caller']) {
  const dir=path.join(output,scenario);await fs.mkdir(dir);
  const child=spawn(fixture,[dir],{windowsHide:true,stdio:'ignore'});let exit;
  child.on('exit',(code,signal)=>exit={code,signal});child.on('error',error=>exit={error:String(error)});
  const item={scenario,pid:child.pid,checks:{}};
  try {
    await wait(()=>exists(path.join(dir,'bindings.json')));
    const b=JSON.parse(await fs.readFile(path.join(dir,'bindings.json'),'utf8'));
    const p={schema:'GEHD_SOURCE_PARAMETER_OBSERVER_V1',pid:child.pid,durationMs:1800,
      maxEvents:300,maxBufferBytes:1024,maxHashBytes:65536,
      modules:{main:{path:fixture,sha256:identity,imageSize:peInfo(binary).imageSize,imageBase:peInfo(binary).imageBase}},
      loader:{module:'main',rva:b.loader,bytes:bytesAtRva(binary,b.loader,16).toString('hex'),relocations:relocationOffsets(binary,b.loader,16)},
      api:{module:'main',rva:b.api,bytes:bytesAtRva(binary,b.api,16).toString('hex'),relocations:relocationOffsets(binary,b.api,16)},
      caller:{module:'main',returnRva:b.caller,bytes:bytesAtRva(binary,b.caller-8,8).toString('hex'),relocations:relocationOffsets(binary,b.caller-8,8)}};
    if(scenario==='event_cap')p.maxEvents=2;
    if(scenario==='buffer_cap')p.maxBufferBytes=3;
    if(scenario==='wrong_module'){await fs.mkdir(path.join(dir,'alternate'));const other=path.join(dir,'alternate/other.exe');await fs.copyFile(fixture,other);p.modules.main.path=other;}
    if(scenario==='wrong_hash')p.modules.main.sha256='0'.repeat(64);
    if(scenario==='wrong_bytes')p.api.bytes='00'.repeat(12);
    if(scenario==='wrong_caller')p.caller.bytes='00'.repeat(8);
    let result,rejected;
    if(scenario==='baseline')await fs.writeFile(path.join(dir,'go1'),'');
    else try{result=await run(p,path.join(dir,'observation'),runtime,()=>fs.writeFile(path.join(dir,'go1'),''));}
      catch(error){rejected=String(error);}
    item.rejected=rejected;
    if(result){item.captureStatus=result.status;item.terminal=result.terminal;
      item.checks.detached=result.detached===true;item.checks.clientClosed=result.clientClosed===true;
      item.checks.aliveAfterDetach=exit===undefined;item.checks.moduleUnchanged=result.modulesUnchanged===true;}
    if(scenario==='normal') {
      if(!result?.payloads)throw Error('Missing capture: '+(result?.error??rejected));
      const events=result.payloads, enters=events.filter(e=>e.kind==='api_enter'),leaves=events.filter(e=>e.kind==='api_leave');
      const owners=events.filter(e=>e.kind==='loader_enter'),ends=events.filter(e=>e.kind==='loader_leave');
      const pair=e=>leaves.find(x=>x.id===e.id&&x.loaderId===e.loaderId&&x.thread===e.thread);
      item.checks.complete=result.captureComplete===true;
      item.checks.allCallsPaired=enters.length===10&&leaves.length===10&&enters.every(e=>!!pair(e));
      item.checks.loaderPairs=owners.length===13&&ends.length===13&&owners.every(e=>ends.some(x=>x.id===e.id&&x.thread===e.thread));
      item.checks.nested=owners.some(e=>e.parent!==null&&owners.some(x=>x.id===e.parent&&x.thread===e.thread));
      item.checks.multithread=new Set(owners.map(e=>e.thread)).size===3;
      item.checks.exactSlot=enters.every(e=>BigInt(e.outputSlot)===BigInt(e.owner)+0x44n);
      const hash=v=>sha(Buffer.from(v)), first=enters.filter(e=>e.inputSha256===hash([1,2,3,4]));
      item.checks.sameBytesSeparateOwners=first.length===3&&new Set(first.map(e=>e.owner)).size===3;
      item.checks.knownReturnedPointer=first.every(e=>pair(e)?.returnedTexture==='0x10001100'&&pair(e).parameterLink===true);
      item.checks.inputIdentity=enters.every(e=>e.size===4&&e.replacementAttempted===false);
      item.checks.parameters=enters.every(e=>JSON.stringify(e.parameters)===JSON.stringify([0xfffffffe,0xfffffffe,0xfffffffd,0,0,1,1,0xffffffff,0]));
      const failed=enters.find(e=>e.inputSha256===hash([4,2,3,4]));
      item.checks.failedOutputNotAdopted=pair(failed)?.reason==='failed_hresult'&&pair(failed).returnedTexture===null&&!pair(failed).parameterLink;
      const empty=enters.find(e=>e.inputSha256===hash([5,2,3,4]));
      item.checks.successNullNotAdopted=pair(empty)?.reason==='success_null_output'&&!pair(empty).parameterLink;
      const changed=enters.find(e=>e.inputSha256===hash([6,2,3,4]));
      item.checks.changedInputNotAdopted=pair(changed)?.reason==='input_changed'&&!pair(changed).parameterLink;
      item.checks.wrongSlotRejected=events.some(e=>e.kind==='api_rejected'&&e.reason==='owner_slot_mismatch');
      item.checks.wrongCallerRejected=events.some(e=>e.kind==='api_rejected'&&e.reason==='caller_mismatch');
      item.checks.noScopeInvalidPointersIgnored=events.some(e=>e.kind==='api_rejected'&&e.reason==='no_loader_scope');
      item.checks.reusedAddressNoGeneration=owners.every(e=>e.lifetimeGeneration===null)&&new Set(owners.map(e=>e.id)).size===owners.length&&new Set(owners.map(e=>e.owner)).size<owners.length;
      item.checks.noConsumptionClaims=leaves.every(e=>e.inputBytesConsumedProven===false&&e.resourceContentProven===false&&e.sharingProven===false);
      item.checks.alOnly=ends.some(e=>e.al===0)&&ends.some(e=>e.al===1);
    } else if(scenario==='event_cap')item.checks.expectedStop=result?.terminal?.reason==='event_cap'&&result.status==='STOPPED';
    else if(scenario==='buffer_cap')item.checks.expectedStop=result?.terminal?.reason==='invalid_observation'&&result.status==='STOPPED';
    else if(scenario==='wrong_module')item.checks.rejected=result?.status==='STOPPED'&&/not ready/.test(result.error);
    else if(scenario!=='baseline')item.checks.rejected=typeof rejected==='string'&&/mismatch/.test(rejected);
  }catch(error){item.error=String(error.stack??error);}
  finally {
    await fs.writeFile(path.join(dir,'go1'),'');await fs.writeFile(path.join(dir,'go2'),'');
    try{await wait(()=>Promise.resolve(exit!==undefined));}catch(error){child.kill();item.error??=String(error);
      try{await wait(()=>Promise.resolve(exit!==undefined));}catch(cleanupError){item.cleanupError=String(cleanupError);}}
    item.exit=exit;item.checks.normalExit=exit?.code===0;
    try{item.behavior=JSON.parse(await fs.readFile(path.join(dir,'first.json'),'utf8'));
      item.checks.nativeBehavior=item.behavior.ok===true&&JSON.parse(await fs.readFile(path.join(dir,'done.json'),'utf8')).ok===true;
      if(scenario!=='baseline')item.checks.matchesBaseline=JSON.stringify(item.behavior)===JSON.stringify(summary.cases[0].behavior);
    }catch(error){item.error??=String(error);}
    item.checks.binaryUnchanged=sha(await fs.readFile(fixture))===identity;
    item.status=!item.error&&Object.values(item.checks).every(v=>v===true)?'PASS':'FAIL';
    await fs.writeFile(path.join(dir,'qualification.json'),JSON.stringify(item,null,2)+'\n',{flag:'wx'});
    summary.cases.push(item);console.log(JSON.stringify({scenario,status:item.status,failed:Object.entries(item.checks).filter(([,v])=>v!==true).map(([k])=>k),error:item.error}));
  }
}
summary.status=summary.cases.every(c=>c.status==='PASS')?'PASS':'FAIL';
summary.limits=['Synthetic x86 ABI and pointers; no D3DX device, COM lifetime, pixel or GE deployment qualification.',
  'No resolver/cache/table/material hooks; parameter witness alone cannot prove candidate sharing.',
  'Input equality at endpoints does not exclude intermediate writers or prove bytes consumed.'];
await fs.writeFile(path.join(output,'summary.json'),JSON.stringify(summary,null,2)+'\n',{flag:'wx'});
if(summary.status!=='PASS')process.exitCode=1;
