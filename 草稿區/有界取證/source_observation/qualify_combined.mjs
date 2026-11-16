import fs from 'node:fs/promises';import path from 'node:path';import {spawn} from 'node:child_process';import {fileURLToPath} from 'node:url';
import {run,bytesAtRva,peInfo,relocationOffsets,sha,resolverHookKeys,resolverCallerKeys} from './run.mjs';
const [fixture,output,runtime,dll]=process.argv.slice(2);
if(![fixture,output,runtime,dll].every(x=>x&&path.isAbsolute(x))||/[^\x20-\x7e]/.test(output))throw Error('Absolute inputs and ASCII output required');
await fs.mkdir(output);const binary=await fs.readFile(fixture),apiBinary=await fs.readFile(dll),here=path.dirname(fileURLToPath(import.meta.url));
const summary={fixture,fixtureSha256:sha(binary),api:dll,apiSha256:sha(apiBinary),sources:{},cases:[]};await fs.mkdir(path.join(output,'source-snapshot'));
for(const name of ['run.mjs','combined_observer.js','combined_fixture.cpp','qualify_combined.mjs','observer.js','handoff_observer.js','resolver_observer.js',
  'handoff_fixture.cpp','material_fixture.cpp','resolver_fixture.cpp','d3dx_fixture.cpp']){
  const b=await fs.readFile(path.join(here,name));summary.sources[name]=sha(b);await fs.writeFile(path.join(output,'source-snapshot',name),b,{flag:'wx'});}
const exists=p=>fs.access(p).then(()=>true,()=>false);
const wait=async test=>{const until=Date.now()+30000;while(Date.now()<until){if(await test())return;await new Promise(r=>setTimeout(r,25));}throw Error('Fixture timeout');};
const module=(path,b)=>({path,sha256:sha(b),imageSize:peInfo(b).imageSize,imageBase:peInfo(b).imageBase});
const point=(module,b,rva)=>({module,rva,bytes:bytesAtRva(b,rva,16).toString('hex'),relocations:relocationOffsets(b,rva,16)});
const caller=returnRva=>({module:'main',returnRva,bytes:bytesAtRva(binary,returnRva-8,8).toString('hex'),relocations:relocationOffsets(binary,returnRva-8,8)});
for(const scenario of ['baseline','observed','event_cap','boundary_cap','buffer_cap','wrong_lifetime_bytes','live_mismatch']){
 const dir=path.join(output,scenario);await fs.mkdir(dir);const child=spawn(fixture,[dir,scenario],{windowsHide:true,stdio:'ignore'});let exit;
 child.on('exit',(code,signal)=>exit={code,signal});child.on('error',error=>exit={error:String(error)});const item={scenario,pid:child.pid,checks:{}};
 try{
  await wait(async()=>await exists(path.join(dir,'bindings.json'))||exit!==undefined);if(exit!==undefined)throw Error('Fixture startup '+JSON.stringify(exit));
  const b=JSON.parse(await fs.readFile(path.join(dir,'bindings.json'),'utf8'));
  const p={schema:'GEHD_COMBINED_OBSERVER_V1',pid:child.pid,durationMs:3500,maxEvents:1500,maxBufferBytes:1024,maxHashBytes:16384,maxListItems:32,
   modules:{main:module(fixture,binary),api:{...module(dll,apiBinary),livePath:path.join(process.env.SystemRoot,'System32','d3dx9_43.dll')}},
   source:{loader:point('main',binary,b.loader),api:point('api',apiBinary,b.api),caller:caller(b.caller)},
   reference:{reader:point('main',binary,b.reader),callback:point('main',binary,b.callback),append:point('main',binary,b.append),
    callbackCaller:caller(b.callbackCaller),appendCaller:caller(b.appendCaller),resolver:{},materials:{}},lifetime:{}};
  for(const k of ['create','clear','destroy'])p.lifetime[k]=point('main',binary,b[k]);
  for(const k of resolverHookKeys)p.reference.resolver[k]=point('main',binary,b.resolver[k]);for(const k of resolverCallerKeys)p.reference.resolver[k]=caller(b.resolver[k]);
  for(const k of ['fixed','user','readIndex','lookup','append'])p.reference.materials[k]=point('main',binary,b.materials[k]);
  for(const kind of ['fixed','user'])for(const role of ['Read','Lookup','Append'])p.reference.materials[kind+role+'Caller']=caller(b.materials[kind+role+'Caller']);
  if(scenario==='event_cap')p.maxEvents=30;if(scenario==='buffer_cap')p.maxBufferBytes=1;
  if(scenario==='boundary_cap')p.maxEvents=summary.cases.find(c=>c.scenario==='observed').firstBoundarySequence-1;
  if(scenario==='wrong_lifetime_bytes')p.lifetime.create.bytes='00'.repeat(16);
  let r,rejected;
  if(scenario==='baseline')await fs.writeFile(path.join(dir,'go1'),'');
  else try{r=await run(p,path.join(dir,'observation'),runtime,()=>fs.writeFile(path.join(dir,'go1'),''));}catch(e){rejected=String(e);}
  if(r){item.captureStatus=r.status;item.terminal=r.terminal;item.checks.detached=r.unloaded===true&&r.detached===true&&r.clientClosed===true&&exit===undefined;item.checks.modulesUnchanged=r.modulesUnchanged===true;}
  if(scenario==='observed'){
   if(!r?.payloads)throw Error('No capture '+(r?.error??rejected));await wait(()=>exists(path.join(dir,'first.json')));
   const native=JSON.parse(await fs.readFile(path.join(dir,'first.json'),'utf8')),es=r.payloads,joins=es.filter(e=>e.kind==='source_reference_sample'),data=es.filter(e=>e.sequence!==undefined&&e.kind!=='terminal');
   item.checks.complete=r.captureComplete===true;
   item.checks.oneWindow=r.initial.filter(m=>m.type==='send'&&m.payload?.kind==='ready').length===1&&es.filter(e=>e.kind==='terminal').length===1&&
    r.terminal.reason==='window_end'&&Object.values(r.terminal.domains).every(x=>x.reason==='window_end'&&x.pending.length===0);
   item.checks.globalOrder=data.every((e,i)=>e.sequence===i+1)&&new Set(data.map(e=>e.sequence)).size===data.length;
   item.checks.distinctDomains=['source','reference','lifetime','join'].every(d=>data.some(e=>e.domain===d));
   item.checks.apiFileIdentity=r.initial.some(m=>m.payload?.kind==='ready'&&m.payload.domains.source.moduleBindings.some(x=>x.key==='api'&&x.redirectedFileHashChecked));
   const ends=es.filter(e=>e.domain==='reference'&&e.kind==='reader_leave'),starts=es.filter(e=>e.domain==='reference'&&e.kind==='reader_enter');
   const res=es.filter(e=>e.kind==='resolver_leave'),material=es.filter(e=>e.kind==='material_leave');
   item.checks.referenceCounts=ends.length===9&&res.length===9&&material.length===18&&material.every(e=>e.appendSequenceMatches);
   item.checks.fiveSourceCalls=es.filter(e=>e.domain==='source'&&e.kind==='api_leave').length===5&&es.filter(e=>e.domain==='source'&&e.kind==='loader_leave').length===5;
   item.checks.samples=joins.length===45;
   const grouped=native.records.map((n,i)=>{const rstart=starts.find(x=>x.id===ends[i].id),rindex=data.indexOf(rstart),last=i+1<native.records.length?data.indexOf(starts.find(x=>x.id===ends[i+1].id)):data.length;
    return {native:n,samples:data.slice(rindex,last).filter(e=>e.kind==='source_reference_sample')};});
   const named=name=>grouped.find(g=>g.native.name===name);
   for(const name of ['a','same','reloaded','reused_loaded'])item.checks[name+'_joined']=named(name)?.samples.length===5&&named(name).samples.every(e=>e.parameterPathObserved&&e.owner===named(name).native.owner&&e.texture===named(name).native.texture&&e.surface===named(name).native.surface);
   const reasons={preexisting:'owner_window_unknown',cleared:'no_current_source',failed_load:'no_current_source',fields_changed:'resource_fields_changed',reused_unloaded:'no_current_source'};
   for(const [name,reason] of Object.entries(reasons))item.checks[name+'_not_joined']=named(name)?.samples.length===5&&named(name).samples.every(e=>!e.parameterPathObserved&&e.reason===reason&&e.sourceApiId===null);
   item.checks.reuseNewGeneration=named('a').native.owner===named('reused_loaded').native.owner&&named('a').samples[0].observedGeneration!==named('reused_loaded').samples[0].observedGeneration;
   item.checks.sameInputSeparateOwners=named('a').samples[0].inputSha256===named('same').samples[0].inputSha256&&named('a').native.owner!==named('same').native.owner&&named('a').native.texture!==named('same').native.texture;
   item.checks.reloadNewSource=named('a').samples[0].observedGeneration===named('reloaded').samples[0].observedGeneration&&named('a').samples[0].sourceApiId!==named('reloaded').samples[0].sourceApiId&&named('a').samples[0].inputSha256!==named('reloaded').samples[0].inputSha256;
   item.checks.lifecycleBoundaries=es.filter(e=>e.kind==='owner_boundary'&&e.action==='create_leave').length===3&&es.filter(e=>e.kind==='owner_boundary'&&e.action==='destroy_leave').length===3;
   item.checks.sourcesCorrelated=joins.filter(e=>e.parameterPathObserved).every(j=>es.some(e=>e.domain==='source'&&e.kind==='api_enter'&&e.id===j.sourceApiId&&e.loaderId===j.sourceLoaderId&&e.inputSha256===j.inputSha256)&&es.some(e=>e.domain==='reference'&&e.id===j.referenceId&&e.kind===j.referenceKind));
   item.checks.noOverclaims=joins.every(e=>!e.completeLifetimeCoverage&&!e.unobservedWritersExcluded&&!e.resourceContentProven&&!e.sharingProven);
   item.events=data.length;item.joinedSamples=joins.filter(e=>e.parameterPathObserved).length;
   item.firstBoundarySequence=es.find(e=>e.kind==='owner_boundary'&&e.action==='create_enter').sequence;
  }else if(scenario==='event_cap')item.checks.expectedStop=r?.terminal?.reason==='event_cap'&&r.captureComplete===false&&r.terminal.events===30;
  else if(scenario==='boundary_cap')item.checks.pendingLifetimeRetained=r?.terminal?.reason==='event_cap'&&r.captureComplete===false&&r.terminal.pending.some(id=>id.startsWith('lifetime:'));
  else if(scenario==='buffer_cap')item.checks.expectedStop=r?.terminal?.reason==='invalid_observation'&&/buffer_bound/.test(r.terminal.detail)&&r.captureComplete===false;
  else if(scenario==='wrong_lifetime_bytes')item.checks.rejected=typeof rejected==='string'&&/mismatch/.test(rejected);
  else if(scenario==='live_mismatch')item.checks.failedSecondDomainClosedFirst=r?.terminal?.reason==='preflight_rejected'&&!r.captureComplete&&
    r.terminal.domains.source.reason==='window_end'&&r.terminal.domains.reference.reason==='preflight_rejected'&&!r.initial.some(m=>m.payload?.kind==='ready');
 }catch(error){item.error=String(error.stack??error);}
 finally{
  await fs.writeFile(path.join(dir,'go1'),'');await fs.writeFile(path.join(dir,'go2'),'');
  try{await wait(()=>Promise.resolve(exit!==undefined));}catch(error){child.kill();item.error??=String(error);try{await wait(()=>Promise.resolve(exit!==undefined));}catch(e){item.cleanupError=String(e);}}
  item.exit=exit;item.checks.normalExit=exit?.code===0;
  try{
   const first=JSON.parse(await fs.readFile(path.join(dir,'first.json'),'utf8')),after=JSON.parse(await fs.readFile(path.join(dir,'after.json'),'utf8'));
   item.checks.nativeBehavior=first.ok===true&&after.ok===true;
   const closed=JSON.parse(await fs.readFile(path.join(dir,'exit.json'),'utf8'));item.checks.deviceReleased=closed.code===0&&closed.deviceRemainingRefs===0;
   item.hashes={};for(const phase of ['first','after'])for(const name of ['a','same','b','reused']){
    const raw=await fs.readFile(path.join(dir,phase+'-'+name+'.raw')),expected=Buffer.alloc(84);for(let i=0;i<84;i+=4)expected.writeUInt32LE(name==='b'?0xffa1b2c3:0xff123456,i);
    item.checks[phase+'_'+name+'_bytes']=raw.equals(expected);item.hashes[phase+'_'+name]=sha(raw);
   }
   if(scenario!=='baseline')item.checks.matchesBaseline=JSON.stringify(item.hashes)===JSON.stringify(summary.cases[0].hashes);
  }catch(error){item.error??=String(error);}
  item.checks.binaryUnchanged=sha(await fs.readFile(fixture))===summary.fixtureSha256&&sha(await fs.readFile(dll))===summary.apiSha256;
  item.status=!item.error&&Object.values(item.checks).every(x=>x===true)?'PASS':'FAIL';summary.cases.push(item);
  await fs.writeFile(path.join(dir,'qualification.json'),JSON.stringify(item,null,2)+'\n',{flag:'wx'});console.log(JSON.stringify({scenario,status:item.status,failed:Object.entries(item.checks).filter(([,v])=>!v).map(([k])=>k),error:item.error,events:item.events,joined:item.joinedSamples}));
 }
 if(item.status!=='PASS')break;
}
summary.status=summary.cases.length===7&&summary.cases.every(c=>c.status==='PASS')?'PASS':'FAIL';
summary.limits=['Self-owned real D3DX resources and synthetic resolver/material ABI. No game, draw or visible pixels.',
 'Generations cover only the supplied constructor/clear/destructor boundaries; no global lifetime or unknown-writer proof.',
 'Joined samples witness current field equality and observed transfer paths; not API byte-consumption or resource-content proof.'];
await fs.writeFile(path.join(output,'summary.json'),JSON.stringify(summary,null,2)+'\n',{flag:'wx'});if(summary.status!=='PASS')process.exitCode=1;
