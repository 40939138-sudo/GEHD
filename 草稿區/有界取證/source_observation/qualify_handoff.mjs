import fs from 'node:fs/promises';
import path from 'node:path';
import {spawn} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {run,bytesAtRva,peInfo,relocationOffsets,sha} from './run.mjs';
const [fixture,output,runtime]=process.argv.slice(2);
if(![fixture,output,runtime].every(x=>x&&path.isAbsolute(x))||/[^\x20-\x7e]/.test(output))throw Error('Absolute inputs and ASCII output required');
await fs.mkdir(output);const binary=await fs.readFile(fixture),identity=sha(binary),here=path.dirname(fileURLToPath(import.meta.url));
const summary={fixture,fixtureSha256:identity,sources:{},cases:[]};await fs.mkdir(path.join(output,'source-snapshot'));
for(const name of ['run.mjs','handoff_observer.js','handoff_fixture.cpp','qualify_handoff.mjs']){const b=await fs.readFile(path.join(here,name));summary.sources[name]=sha(b);await fs.writeFile(path.join(output,'source-snapshot',name),b,{flag:'wx'});}
const exists=p=>fs.access(p).then(()=>true,()=>false);
const wait=async test=>{const end=Date.now()+20000;while(Date.now()<end){if(await test())return;await new Promise(r=>setTimeout(r,25));}throw Error('Fixture timeout');};
for(const scenario of ['baseline','normal','event_cap','invalid_list','wrong_bytes','wrong_caller']) {
  const dir=path.join(output,scenario);await fs.mkdir(dir);const child=spawn(fixture,[dir,scenario],{windowsHide:true,stdio:'ignore'});let exit;
  child.on('exit',(code,signal)=>exit={code,signal});child.on('error',error=>exit={error:String(error)});const item={scenario,pid:child.pid,checks:{}};
  try {
    await wait(()=>exists(path.join(dir,'bindings.json')));const b=JSON.parse(await fs.readFile(path.join(dir,'bindings.json'),'utf8'));
    const info=peInfo(binary),point=rva=>({module:'main',rva,bytes:bytesAtRva(binary,rva,16).toString('hex'),relocations:relocationOffsets(binary,rva,16)});
    const caller=returnRva=>({module:'main',returnRva,bytes:bytesAtRva(binary,returnRva-8,8).toString('hex'),relocations:relocationOffsets(binary,returnRva-8,8)});
    const p={schema:'GEHD_OBJECT_HANDOFF_V1',pid:child.pid,durationMs:1800,maxEvents:200,maxBufferBytes:1,maxHashBytes:1,maxListItems:32,
      modules:{main:{path:fixture,sha256:identity,imageSize:info.imageSize,imageBase:info.imageBase}},reader:point(b.reader),callback:point(b.callback),append:point(b.append),
      callbackCaller:caller(b.callbackCaller),appendCaller:caller(b.appendCaller)};
    if(scenario==='event_cap')p.maxEvents=3;
    if(scenario==='wrong_bytes')p.append.bytes='00'.repeat(16);
    if(scenario==='wrong_caller')p.callbackCaller.bytes='00'.repeat(8);
    let r,rejected;
    if(scenario==='baseline')await fs.writeFile(path.join(dir,'go1'),'');
    else try{r=await run(p,path.join(dir,'observation'),runtime,()=>fs.writeFile(path.join(dir,'go1'),''));}catch(e){rejected=String(e);}
    if(r){item.captureStatus=r.status;item.terminal=r.terminal;item.checks.detached=r.unloaded===true&&r.detached===true&&r.clientClosed===true&&exit===undefined;item.checks.moduleUnchanged=r.modulesUnchanged===true;}
    if(scenario==='normal') {
      if(!r?.payloads)throw Error('No capture: '+(r?.error??rejected));
      const es=r.payloads,readers=es.filter(e=>e.kind==='reader_enter'),readerEnds=es.filter(e=>e.kind==='reader_leave'),
        cb=es.filter(e=>e.kind==='callback_enter'),cbEnds=es.filter(e=>e.kind==='callback_leave'),
        enters=es.filter(e=>e.kind==='append_enter'),ends=es.filter(e=>e.kind==='append_leave');
      item.checks.complete=r.captureComplete===true;
      item.checks.readers=readers.length===13&&readerEnds.length===13&&readers.every(e=>readerEnds.some(x=>x.id===e.id&&x.thread===e.thread));
      item.checks.callbacks=cb.length===13&&cbEnds.length===13&&cb.every(e=>cbEnds.some(x=>x.id===e.id&&x.readerId===e.readerId&&x.thread===e.thread));
      item.checks.appends=enters.length===14&&ends.length===14&&enters.every(e=>ends.some(x=>x.id===e.id&&x.readerId===e.readerId&&x.thread===e.thread&&x.index===e.index&&x.tableEntry===e.entity&&x.appendObserved));
      item.checks.partialReader=readerEnds.filter(e=>e.al===0).length===1&&readerEnds.every(e=>e.allEntriesProven===false)&&readerEnds.filter(e=>e.al===0).every(e=>ends.some(x=>x.readerId===e.id&&x.appendObserved));
      const relations=Object.fromEntries([...new Set(ends.map(e=>e.relation))].map(k=>[k,ends.filter(e=>e.relation===k).length]));item.relations=relations;
      item.checks.branches=relations.callback_entity_preserved===8&&relations.entity_changed_after_callback===2&&relations.empty_callback_then_append===1&&relations.callback_absent===1&&relations.callback_output_mismatch===1&&relations.callback_unobserved===1;
      item.checks.wrongCallbackRejected=es.filter(e=>e.kind==='callback_rejected'&&e.reason==='caller_or_target').length===1;
      item.checks.nested=readers.some(e=>e.parent!==null&&readers.some(x=>x.id===e.parent&&x.thread===e.thread));
      item.checks.threads=new Set(readers.map(e=>e.thread)).size===3;
      item.checks.wrongTableRejected=es.filter(e=>e.kind==='append_rejected'&&e.reason==='caller_or_table').length===1;
      item.checks.outOfScopeRejected=es.filter(e=>/^(callback|append)_rejected$/.test(e.kind)&&e.reason==='no_reader_scope').length===2;
      item.checks.unmatchedNotAdopted=readerEnds.filter(e=>e.unmatchedCallback!==null).length===1;
      item.checks.noOverclaims=ends.every(e=>e.sourceProven===false&&e.sharingProven===false&&e.materialUseProven===false&&e.lifetimeGeneration===null&&e.atomicSnapshot===false)&&cbEnds.every(e=>e.candidateProven===false);
      await wait(()=>exists(path.join(dir,'first.json')));const native=JSON.parse(await fs.readFile(path.join(dir,'first.json'),'utf8'));
      item.checks.nativeEntries=native.records.length===readerEnds.length&&[...new Set(native.records.map(n=>n.thread))].every(thread=>{
        // Per-thread completion order survives stack-address reuse and nested reader calls.
        const ns=native.records.filter(n=>n.thread===thread),rs=readerEnds.filter(e=>e.thread===thread);
        return ns.length===rs.length&&ns.every((n,i)=>{
          const reader=readers.find(e=>e.id===rs[i].id),observed=enters.filter(e=>e.readerId===reader.id).map(e=>e.entity);
          return reader.table===n.table&&JSON.stringify(observed)===JSON.stringify(n.name==='wrong_table'?[]:n.entities);
        });});
      item.checks.addressReuseNoGeneration=new Set(readers.map(e=>e.table)).size<readers.length&&readers.every(e=>e.lifetimeGeneration===null);
    }else if(scenario==='event_cap')item.checks.expectedStop=r?.status==='STOPPED'&&r.terminal?.reason==='event_cap';
    else if(scenario==='invalid_list')item.checks.expectedStop=r?.status==='STOPPED'&&r.terminal?.reason==='invalid_observation'&&/list_bound/.test(r.terminal.detail);
    else if(scenario!=='baseline')item.checks.rejected=typeof rejected==='string'&&/mismatch/.test(rejected);
  }catch(e){item.error=String(e.stack??e);}
  finally {
    await fs.writeFile(path.join(dir,'go1'),'');await fs.writeFile(path.join(dir,'go2'),'');
    try{await wait(()=>Promise.resolve(exit!==undefined));}catch(e){child.kill();item.error??=String(e);try{await wait(()=>Promise.resolve(exit!==undefined));}catch(e2){item.cleanupError=String(e2);}}
    item.exit=exit;item.checks.normalExit=exit?.code===0;
    try{const a=JSON.parse(await fs.readFile(path.join(dir,'first.json'),'utf8')),b=JSON.parse(await fs.readFile(path.join(dir,'after.json'),'utf8'));
      item.checks.nativeBehavior=a.ok===true&&b.ok===true&&a.errors===0&&b.errors===0;
      const stable=x=>x.records.map(r=>({name:r.name,entities:r.entities})).sort((a,b)=>a.name.localeCompare(b.name));
      item.nativeBehavior=stable(a);
      if(scenario!=='invalid_list')item.checks.afterMatches=JSON.stringify(stable(a))===JSON.stringify(stable(b));
      if(scenario!=='baseline'&&scenario!=='invalid_list')item.checks.baselineMatches=JSON.stringify(stable(a))===JSON.stringify(summary.cases[0].nativeBehavior);
    }catch(e){item.error??=String(e);}
    item.checks.binaryUnchanged=sha(await fs.readFile(fixture))===identity;
    item.status=!item.error&&Object.values(item.checks).every(x=>x===true)?'PASS':'FAIL';summary.cases.push(item);
    await fs.writeFile(path.join(dir,'qualification.json'),JSON.stringify(item,null,2)+'\n',{flag:'wx'});
    console.log(JSON.stringify({scenario,status:item.status,failed:Object.entries(item.checks).filter(([,v])=>!v).map(([k])=>k),error:item.error,relations:item.relations}));
  }
}
summary.status=summary.cases.every(c=>c.status==='PASS')?'PASS':'FAIL';
summary.limits=['Synthetic x86 ABI and opaque entity pointers, not GE or COM.',
  'Callback-to-table only; no resolver provenance, material use, resource generation or source join.',
  'Snapshots are not atomic; reader AL is not proof that all entries succeeded.'];
await fs.writeFile(path.join(output,'summary.json'),JSON.stringify(summary,null,2)+'\n',{flag:'wx'});
if(summary.status!=='PASS')process.exitCode=1;
