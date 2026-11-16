import fs from 'node:fs/promises';
import path from 'node:path';
import {spawn} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {run,bytesAtRva,peInfo,relocationOffsets,sha} from './run.mjs';
const [fixture,output,runtime]=process.argv.slice(2);
if(![fixture,output,runtime].every(x=>x&&path.isAbsolute(x))||/[^\x20-\x7e]/.test(output))throw Error('Absolute inputs and ASCII output required');
await fs.mkdir(output);const binary=await fs.readFile(fixture),identity=sha(binary),here=path.dirname(fileURLToPath(import.meta.url));
const summary={fixture,fixtureSha256:identity,sources:{},cases:[]};await fs.mkdir(path.join(output,'source-snapshot'));
for(const name of ['run.mjs','handoff_observer.js','handoff_fixture.cpp','material_fixture.cpp','qualify_material.mjs']){
  const b=await fs.readFile(path.join(here,name));summary.sources[name]=sha(b);await fs.writeFile(path.join(output,'source-snapshot',name),b,{flag:'wx'});}
const exists=p=>fs.access(p).then(()=>true,()=>false);
const wait=async test=>{const end=Date.now()+20000;while(Date.now()<end){if(await test())return;await new Promise(r=>setTimeout(r,25));}throw Error('Fixture timeout');};
for(const scenario of ['baseline','normal','event_cap','invalid_list','wrong_bytes','wrong_caller']) {
  const dir=path.join(output,scenario);await fs.mkdir(dir);const child=spawn(fixture,[dir,scenario],{windowsHide:true,stdio:'ignore'});let exit;
  child.on('exit',(code,signal)=>exit={code,signal});child.on('error',error=>exit={error:String(error)});const item={scenario,pid:child.pid,checks:{}};
  try {
    await wait(()=>exists(path.join(dir,'bindings.json')));const b=JSON.parse(await fs.readFile(path.join(dir,'bindings.json'),'utf8')),info=peInfo(binary);
    const point=rva=>({module:'main',rva,bytes:bytesAtRva(binary,rva,16).toString('hex'),relocations:relocationOffsets(binary,rva,16)});
    const caller=returnRva=>({module:'main',returnRva,bytes:bytesAtRva(binary,returnRva-8,8).toString('hex'),relocations:relocationOffsets(binary,returnRva-8,8)});
    const p={schema:'GEHD_OBJECT_HANDOFF_V1',pid:child.pid,durationMs:1800,maxEvents:500,maxBufferBytes:1,maxHashBytes:1,maxListItems:32,
      modules:{main:{path:fixture,sha256:identity,imageSize:info.imageSize,imageBase:info.imageBase}},reader:point(b.reader),callback:point(b.callback),append:point(b.append),
      callbackCaller:caller(b.callbackCaller),appendCaller:caller(b.appendCaller),materials:{}};
    for(const k of ['fixed','user','readIndex','lookup','append'])p.materials[k]=point(b.materials[k]);
    for(const kind of ['fixed','user'])for(const role of ['Read','Lookup','Append']){const k=kind+role+'Caller';p.materials[k]=caller(b.materials[k]);}
    if(scenario==='event_cap')p.maxEvents=3;
    if(scenario==='wrong_bytes')p.materials.lookup.bytes='00'.repeat(16);
    if(scenario==='wrong_caller')p.materials.userLookupCaller.bytes='00'.repeat(8);
    let r,rejected;if(scenario==='baseline')await fs.writeFile(path.join(dir,'go1'),'');
    else try{r=await run(p,path.join(dir,'observation'),runtime,()=>fs.writeFile(path.join(dir,'go1'),''));}catch(e){rejected=String(e);}
    if(r){item.captureStatus=r.status;item.terminal=r.terminal;item.checks.detached=r.unloaded===true&&r.detached===true&&r.clientClosed===true&&exit===undefined;item.checks.moduleUnchanged=r.modulesUnchanged===true;}
    if(scenario==='normal') {
      if(!r?.payloads)throw Error('No capture: '+(r?.error??rejected));const es=r.payloads;
      const starts=es.filter(e=>e.kind==='material_enter'),ends=es.filter(e=>e.kind==='material_leave'),reads=es.filter(e=>e.kind==='material_index'),
        lookups=es.filter(e=>e.kind==='material_lookup'),appends=es.filter(e=>e.kind==='material_append');
      item.checks.complete=r.captureComplete===true;
      item.checks.materialPairs=starts.length===11&&ends.length===11&&starts.every(e=>ends.some(x=>x.id===e.id&&x.thread===e.thread));
      item.checks.helperCounts=reads.length===18&&lookups.length===17&&appends.length===18;
      item.checks.correlated=appends.every(a=>reads.some(x=>x.id===a.readId&&x.materialId===a.materialId&&x.thread===a.thread&&x.index===a.index)&&
        (a.lookupId===null||lookups.some(x=>x.id===a.lookupId&&x.materialId===a.materialId&&x.thread===a.thread&&x.index===a.index)));
      item.checks.nativeAppends=appends.every(a=>a.appendObserved===true);
      item.selections=Object.fromEntries([...new Set(appends.map(a=>a.selection))].map(k=>[k,appends.filter(a=>a.selection===k).length]));
      item.checks.selectionBranches=JSON.stringify(item.selections)===JSON.stringify({table_value_preserved:13,sentinel_no_table_selection:1,index_incomplete_or_unobserved:1,lookup_not_adopted:2,changed_after_lookup:1});
      item.checks.boundedLookup=lookups.filter(l=>l.boundedSlotObserved).length===14&&lookups.filter(l=>l.boundedSlotObserved).every(l=>l.entity===(l.index===0?'0x10001100':'0x10001200'));
      item.checks.finalListDiffers=ends.filter(e=>!e.appendSequenceMatches).length===2;
      item.checks.noOverclaims=ends.every(e=>e.allEntriesProven===false&&e.shaderUseProven===false&&e.sharingProven===false&&e.lifetimeGeneration===null&&e.atomicSnapshot===false)&&appends.every(e=>e.sourceProven===false&&e.shaderUseProven===false&&e.lifetimeGeneration===null);
      item.checks.threeThreads=new Set(starts.map(e=>e.thread)).size===3;
      item.checks.readerEvidenceSameWindow=es.filter(e=>e.kind==='append_leave'&&e.appendObserved).length===2&&es.some(e=>e.kind==='reader_leave');
      await wait(()=>exists(path.join(dir,'first.json')));const native=JSON.parse(await fs.readFile(path.join(dir,'first.json'),'utf8'));
      item.checks.nativeFinalLists=native.records.length===ends.length&&[...new Set(native.records.map(n=>n.thread))].every(thread=>{
        const ns=native.records.filter(n=>n.thread===thread),out=ends.filter(e=>e.thread===thread);
        return ns.length===out.length&&ns.every((n,i)=>{const start=starts.find(e=>e.id===out[i].id);return start.owner===n.owner&&start.table===n.table&&start.materialKind===n.kind&&out[i].finalList===n.list&&JSON.stringify(out[i].entities)===JSON.stringify(n.entities);});});
      const ids=name=>native.records.filter(n=>n.name===name).map(n=>{
        const ns=native.records.filter(x=>x.thread===n.thread),i=ns.indexOf(n);return ends.filter(e=>e.thread===n.thread)[i].id;});
      item.checks.specificNegativeBranches=['short','wrong_slot','wrong_table','changed','sentinel'].every(name=>{
        const expected={short:'index_incomplete_or_unobserved',wrong_slot:'lookup_not_adopted',wrong_table:'lookup_not_adopted',changed:'changed_after_lookup',sentinel:'sentinel_no_table_selection'}[name];
        return ids(name).every(id=>appends.some(a=>a.materialId===id&&a.selection===expected));});
      item.checks.finalMismatchOwners=['wrong_destination','changed_final'].every(name=>ids(name).every(id=>ends.some(e=>e.id===id&&!e.appendSequenceMatches)));
    }else if(scenario==='event_cap')item.checks.expectedStop=r?.status==='STOPPED'&&r.terminal?.reason==='event_cap';
    else if(scenario==='invalid_list')item.checks.expectedStop=r?.status==='STOPPED'&&r.terminal?.reason==='invalid_observation'&&/list_bound/.test(r.terminal.detail);
    else if(scenario!=='baseline')item.checks.rejected=typeof rejected==='string'&&/mismatch/.test(rejected);
  }catch(e){item.error=String(e.stack??e);}
  finally {
    await fs.writeFile(path.join(dir,'go1'),'');await fs.writeFile(path.join(dir,'go2'),'');
    try{await wait(()=>Promise.resolve(exit!==undefined));}catch(e){child.kill();item.error??=String(e);try{await wait(()=>Promise.resolve(exit!==undefined));}catch(e2){item.cleanupError=String(e2);}}
    item.exit=exit;item.checks.normalExit=exit?.code===0;
    try{const a=JSON.parse(await fs.readFile(path.join(dir,'first.json'),'utf8')),b=JSON.parse(await fs.readFile(path.join(dir,'after.json'),'utf8'));
      item.checks.nativeBehavior=a.ok===true&&b.ok===true;
      const stable=x=>x.records.map(r=>({name:r.name,kind:r.kind,entities:r.entities})).sort((a,b)=>a.name.localeCompare(b.name));item.nativeBehavior=stable(a);
      if(scenario!=='invalid_list')item.checks.afterMatches=JSON.stringify(stable(a))===JSON.stringify(stable(b));
      if(scenario!=='baseline'&&scenario!=='invalid_list')item.checks.baselineMatches=JSON.stringify(stable(a))===JSON.stringify(summary.cases[0].nativeBehavior);
    }catch(e){item.error??=String(e);}
    item.checks.binaryUnchanged=sha(await fs.readFile(fixture))===identity;item.status=!item.error&&Object.values(item.checks).every(x=>x===true)?'PASS':'FAIL';summary.cases.push(item);
    await fs.writeFile(path.join(dir,'qualification.json'),JSON.stringify(item,null,2)+'\n',{flag:'wx'});
    console.log(JSON.stringify({scenario,status:item.status,failed:Object.entries(item.checks).filter(([,v])=>!v).map(([k])=>k),error:item.error,selections:item.selections}));
  }
}
summary.status=summary.cases.every(c=>c.status==='PASS')?'PASS':'FAIL';
summary.limits=['Synthetic entity pointers and two fixed material ABI profiles, not GE or COM.',
  'No sampler/shader, resolver provenance or resource generations. Full list snapshots are non-atomic.',
  'Observed index/helper/append/final-list relations do not establish complete import or pixel consumption.'];
await fs.writeFile(path.join(output,'summary.json'),JSON.stringify(summary,null,2)+'\n',{flag:'wx'});if(summary.status!=='PASS')process.exitCode=1;
