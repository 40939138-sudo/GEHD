import fs from 'node:fs/promises';
import path from 'node:path';
import {spawn} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {run,bytesAtRva,peInfo,relocationOffsets,sha,resolverHookKeys,resolverCallerKeys} from './run.mjs';
const [fixture,output,runtime]=process.argv.slice(2);
if(![fixture,output,runtime].every(x=>x&&path.isAbsolute(x))||/[^\x20-\x7e]/.test(output))throw Error('Absolute inputs and ASCII output required');
await fs.mkdir(output);const binary=await fs.readFile(fixture),identity=sha(binary),here=path.dirname(fileURLToPath(import.meta.url));
const summary={fixture,fixtureSha256:identity,sources:{},cases:[]};await fs.mkdir(path.join(output,'source-snapshot'));
for(const name of ['run.mjs','handoff_observer.js','resolver_observer.js','handoff_fixture.cpp','resolver_fixture.cpp','qualify_resolver.mjs']){
  const b=await fs.readFile(path.join(here,name));summary.sources[name]=sha(b);await fs.writeFile(path.join(output,'source-snapshot',name),b,{flag:'wx'});}
const exists=p=>fs.access(p).then(()=>true,()=>false);
const wait=async test=>{const end=Date.now()+20000;while(Date.now()<end){if(await test())return;await new Promise(r=>setTimeout(r,25));}throw Error('Fixture timeout');};
for(const scenario of ['baseline','normal','event_cap','invalid_list','wrong_bytes','wrong_caller']){
  const dir=path.join(output,scenario);await fs.mkdir(dir);const child=spawn(fixture,[dir,scenario],{windowsHide:true,stdio:'ignore'});let exit;
  child.on('exit',(code,signal)=>exit={code,signal});child.on('error',error=>exit={error:String(error)});const item={scenario,pid:child.pid,checks:{}};
  try{
    await wait(()=>exists(path.join(dir,'bindings.json')));const b=JSON.parse(await fs.readFile(path.join(dir,'bindings.json'),'utf8')),info=peInfo(binary);
    const point=rva=>({module:'main',rva,bytes:bytesAtRva(binary,rva,16).toString('hex'),relocations:relocationOffsets(binary,rva,16)});
    const caller=returnRva=>({module:'main',returnRva,bytes:bytesAtRva(binary,returnRva-8,8).toString('hex'),relocations:relocationOffsets(binary,returnRva-8,8)});
    const p={schema:'GEHD_OBJECT_HANDOFF_V1',pid:child.pid,durationMs:2500,maxEvents:1500,maxBufferBytes:1,maxHashBytes:1,maxListItems:32,
      modules:{main:{path:fixture,sha256:identity,imageSize:info.imageSize,imageBase:info.imageBase}},reader:point(b.reader),callback:point(b.callback),append:point(b.append),
      callbackCaller:caller(b.callbackCaller),appendCaller:caller(b.appendCaller),resolver:{}};
    for(const k of resolverHookKeys)p.resolver[k]=point(b.resolver[k]);for(const k of resolverCallerKeys)p.resolver[k]=caller(b.resolver[k]);
    if(scenario==='event_cap')p.maxEvents=5;
    if(scenario==='wrong_bytes')p.resolver.holder.bytes='00'.repeat(16);
    if(scenario==='wrong_caller')p.resolver.cacheReturnCopy.bytes='00'.repeat(8);
    let r,rejected;if(scenario==='baseline')await fs.writeFile(path.join(dir,'go1'),'');
    else try{r=await run(p,path.join(dir,'observation'),runtime,()=>fs.writeFile(path.join(dir,'go1'),''));}catch(e){rejected=String(e);}
    if(r){item.captureStatus=r.status;item.terminal=r.terminal;item.checks.detached=r.unloaded===true&&r.detached===true&&r.clientClosed===true&&exit===undefined;item.checks.moduleUnchanged=r.modulesUnchanged===true;}
    if(scenario==='normal'){
      if(!r?.payloads)throw Error('No capture: '+(r?.error??rejected));const es=r.payloads;
      await wait(()=>exists(path.join(dir,'first.json')));const native=JSON.parse(await fs.readFile(path.join(dir,'first.json'),'utf8'));
      const starts=es.filter(e=>e.kind==='reader_enter'),ends=es.filter(e=>e.kind==='reader_leave'),resolvers=es.filter(e=>e.kind==='resolver_leave');
      const bindings=native.records.map(n=>{
        const ns=native.records.filter(x=>x.thread===n.thread),at=ns.indexOf(n),end=ends.filter(e=>e.thread===n.thread)[at];
        const reader=starts.find(e=>e.id===end?.id),cb=es.find(e=>e.kind==='callback_leave'&&e.readerId===reader?.id);
        const resolver=resolvers.find(e=>e.id===cb?.resolverId),append=es.find(e=>e.kind==='append_leave'&&e.readerId===reader?.id);
        return {native:n,reader,cb,resolver,append};
      });
      const named=name=>bindings.filter(b=>b.native.name===name);
      const one=(name,predicate)=>named(name).length===1&&named(name).every(predicate);
      item.checks.complete=r.captureComplete===true;
      item.checks.scopePairs=starts.length===22&&ends.length===22&&resolvers.length===22&&resolvers.every(e=>es.some(s=>s.kind==='resolver_enter'&&s.id===e.id&&s.callbackId===e.callbackId&&s.thread===e.thread));
      item.checks.nativeTables=bindings.length===22&&bindings.every(b=>b.reader?.table===b.native.table&&b.append?.tableEntry===b.native.entity&&b.append?.appendObserved);
      item.checks.sameCallback=bindings.every(b=>b.resolver?.callbackId===b.cb?.id&&b.resolver?.thread===b.native.thread);
      item.checks.threeThreads=new Set(resolvers.map(e=>e.thread)).size===3;
      item.checks.nested=starts.filter(e=>e.parent!==null).length===1&&one('inner',b=>b.reader.parent!==null);
      for(const name of ['previous','dataset','inner','outer','missing_name_fill','changed_callback'])
        item.checks[name+'_candidate']=one(name,b=>b.resolver?.branch==='candidate'&&b.resolver?.pathObserved&&b.resolver?.descriptor!==null);
      item.checks.threadCandidates=named('thread').length===2&&named('thread').every(b=>b.resolver?.branch==='candidate'&&b.resolver?.pathObserved);
      item.checks.fallback=one('fallback',b=>b.resolver?.branch==='fallback'&&b.resolver?.pathObserved);
      for(const name of ['cache_candidate','cache_fallback','first_seen_cache','reused_cache_address'])
        item.checks[name]=one(name,b=>b.resolver?.branch==='cache'&&b.resolver?.pathObserved&&b.resolver?.cacheOrigin==='unknown');
      item.checks.sameAddressNoInheritedProvenance=one('first_seen_cache',a=>one('reused_cache_address',b=>a.native.resolver===b.native.resolver&&a.native.entity!==b.native.entity&&b.resolver.cacheOrigin==='unknown'));
      item.checks.unsupported=one('unsupported',b=>b.resolver?.branch==='unsupported'&&b.resolver?.pathObserved&&b.cb?.entity==='0x0'&&b.append?.relation==='empty_callback_then_append');
      for(const name of ['wrong_slot','changed_adopted','wrong_holder_output','wrong_resolver_output','wrong_cache_table','released_handle','wrong_match_caller'])
        item.checks[name+'_not_admitted']=one(name,b=>b.resolver?.pathObserved===false&&b.cb?.resolverPathPreserved===false&&b.append?.resolverPathPreserved===false);
      item.checks.changedCallbackNotPropagated=one('changed_callback',b=>!b.cb.resolverPathPreserved&&!b.append.resolverPathPreserved);
      item.checks.staleHandleObserved=one('released_handle',b=>es.filter(e=>e.kind==='resolver_release'&&e.resolverId===b.resolver.id).length===2&&
        es.filter(e=>e.kind==='resolver_release'&&e.resolverId===b.resolver.id).every(e=>e.entityBefore===e.entityAfter&&e.entityAfter!=='0x0'&&!e.livenessProven));
      item.checks.partialFill=one('missing_name_fill',b=>es.some(e=>e.kind==='resolver_cache_object_return'&&e.resolverId===b.resolver.id&&e.appendObserved)&&!es.some(e=>e.kind==='resolver_cache_name_return'&&e.resolverId===b.resolver.id));
      item.checks.missThenHit=one('previous',b=>{const matches=es.filter(e=>e.kind==='resolver_match'&&e.resolverId===b.resolver.id);return matches.length===2&&!matches[0].holderPathObserved&&matches[1].holderPathObserved;});
      item.checks.datasetRoute=one('dataset',b=>es.some(e=>e.kind==='resolver_match'&&e.resolverId===b.resolver.id&&e.route==='dataset'&&e.holderPathObserved));
      item.checks.noOverclaims=resolvers.every(e=>e.sourceProven===false&&e.lifetimeGeneration===null&&e.holdingContinuityProven===false&&e.cacheOrigin==='unknown')&&
        es.filter(e=>e.kind.startsWith('resolver_cache_')).every(e=>e.cachePublicationProven===false);
      item.checks.noUnexpectedRejectedHelpers=es.filter(e=>e.kind==='resolver_helper_rejected').length===2;
      item.checks.rejectedNestedDoesNotContaminate=one('nested_unadmitted',b=>b.resolver?.pathObserved===true&&b.resolver?.branch==='candidate'&&
        es.filter(e=>e.kind==='resolver_match'&&e.resolverId===b.resolver.id).length===2)&&es.filter(e=>e.kind==='resolver_rejected').length===1;
      item.events=es.length;item.branchCounts=Object.fromEntries([...new Set(resolvers.map(e=>e.branch))].map(k=>[k,resolvers.filter(e=>e.branch===k).length]));
    }else if(scenario==='event_cap')item.checks.expectedStop=r?.status==='STOPPED'&&r.terminal?.reason==='event_cap'&&r.terminal.pending.length>0;
    else if(scenario==='invalid_list')item.checks.expectedStop=r?.status==='STOPPED'&&r.terminal?.reason==='invalid_observation'&&/list_bound/.test(r.terminal.detail);
    else if(scenario!=='baseline')item.checks.rejected=typeof rejected==='string'&&/mismatch/.test(rejected);
  }catch(e){item.error=String(e.stack??e);}
  finally{
    await fs.writeFile(path.join(dir,'go1'),'');await fs.writeFile(path.join(dir,'go2'),'');
    try{await wait(()=>Promise.resolve(exit!==undefined));}catch(e){child.kill();item.error??=String(e);try{await wait(()=>Promise.resolve(exit!==undefined));}catch(e2){item.cleanupError=String(e2);}}
    item.exit=exit;item.checks.normalExit=exit?.code===0;
    try{const a=JSON.parse(await fs.readFile(path.join(dir,'first.json'),'utf8')),b=JSON.parse(await fs.readFile(path.join(dir,'after.json'),'utf8'));
      item.checks.nativeBehavior=a.ok===true&&b.ok===true;
      const stable=x=>x.records.map(r=>({name:r.name,mode:r.mode,entity:r.entity,cacheCount:r.cacheCount,nameCount:r.nameCount})).sort((a,b)=>a.name.localeCompare(b.name));item.nativeBehavior=stable(a);
      if(scenario!=='invalid_list')item.checks.afterMatches=JSON.stringify(stable(a))===JSON.stringify(stable(b));
      if(scenario!=='baseline'&&scenario!=='invalid_list')item.checks.baselineMatches=JSON.stringify(stable(a))===JSON.stringify(summary.cases[0].nativeBehavior);
    }catch(e){item.error??=String(e);}
    item.checks.binaryUnchanged=sha(await fs.readFile(fixture))===identity;item.status=!item.error&&Object.values(item.checks).every(x=>x===true)?'PASS':'FAIL';summary.cases.push(item);
    await fs.writeFile(path.join(dir,'qualification.json'),JSON.stringify(item,null,2)+'\n',{flag:'wx'});
    console.log(JSON.stringify({scenario,status:item.status,failed:Object.entries(item.checks).filter(([,v])=>!v).map(([k])=>k),error:item.error,events:item.events,branches:item.branchCounts}));
  }
}
summary.status=summary.cases.every(c=>c.status==='PASS')?'PASS':'FAIL';
summary.limits=['Self-owned x86 synthetic handles; no GE, COM reference-count implementation or real entity lifetime qualification.',
 'Only scoped branch/handle transfers; cache origin remains unknown even after a previously observed fill.',
 'No D3DX source join, entity/resource generation, name-content match, atomic publication, writer exclusion or pixel proof.'];
await fs.writeFile(path.join(output,'summary.json'),JSON.stringify(summary,null,2)+'\n',{flag:'wx'});if(summary.status!=='PASS')process.exitCode=1;
