import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { research } from '../controller.mjs';
import { fileSha, readJson } from '../io.mjs';
import { inspectResponse, materialize, validateScope } from '../model.mjs';
const here=path.dirname(fileURLToPath(import.meta.url)),project=process.env.GE_TEST_PROJECT_ROOT;
assert(project,'GE_TEST_PROJECT_ROOT must name the existing project fixture source');
const runtimeRoot=path.join(project,'草稿區/有界取證/rea-toolchain/runtime');
const python=path.join(project,'toolchain/ai-texture-runtime/Scripts/python.exe');
const artifacts=path.join(project,'toolchain/engine-research-controller-r1/qualification');
await fs.mkdir(artifacts,{recursive:true});
async function base(){
  const dir=await fs.mkdtemp(path.join(artifacts,'controller-')),target=path.join(dir,'synthetic.bin');
  await fs.writeFile(target,'SYNTHETIC ONLY');
  const scope={schemaVersion:1,question:'Synthetic dependency qualification, not GE evidence',purpose:'qualification',synthetic:true,
    target:{path:target,sha256:await fileSha(target),ranges:[{start:'0x1000',endExclusive:'0x1010'}]},
    limits:{maxQueries:4,acquisitionMs:10000,idleMs:1000,cleanupMs:5000},openTimeoutMs:1000,
    steps:[{id:'analysis',query:{name:'analyze_function',arguments:{procedure:'0x1000'},timeoutMs:1000}},
      {id:'assembly',query:{name:'procedure_assembly',timeoutMs:1000},requires:['analysis'],
        argumentsFrom:{procedure:{step:'analysis',path:['normalized_result','procedure','address'],type:'address',space:'target-va'}}}],requiredProducts:['analysis','assembly']};
  return {dir,scope};
}
async function runFixture(options={}){
  const f=await base();options.edit?.(f.scope);const p=path.join(f.dir,'input.json');await fs.writeFile(p,JSON.stringify(f.scope));
  const result=await research({mode:'run',scopePath:p,output:path.join(f.dir,'run'),runtimeRoot,python,
    surveyFn:async()=>({complete:true,matchingProcesses:[],synthetic:true}),
    runnerOptions:{serverEntry:path.join(here,'mock_rea_server.mjs'),serverArgs:[],testEnv:{GE_MOCK_SCENARIO:options.scenario??'success',GE_MOCK_LOG:path.join(f.dir,'mock.jsonl')}},
    onProgress:e=>options.onProgress?.(e,f)});return {...f,result};
}
test('actual owned MCP process: persisted producer yields typed followup then verified cleanup',async()=>{
  const r=await runFixture();assert.equal(r.result.status,'QUALIFIED',JSON.stringify(r.result));
  assert.equal(r.result.queryCount,2);assert.equal(r.result.dataQualified,true);assert.equal(r.result.resourceRelease,'RELEASED');
  assert.equal(r.result.dispatches[1].query.arguments.procedure,'0x1000');assert.equal(r.result.dispatches[1].transfers[0].producer,'analysis');
  const request=await readJson(path.join(r.dir,'run/raw/002-request.json'));assert.equal(request.arguments.procedure,'0x1000');
  assert.equal(r.result.semanticVerdict,'NOT_AUTOMATICALLY_ASSESSED');
});
test('expected rejection blocks dependent but permits predeclared independent fallback',async()=>{
  const r=await runFixture({scenario:'tool_refusal',edit:s=>{
    s.steps[0].query.allowError=true;s.steps.push({id:'fallback',query:{name:'read_bytes',arguments:{address:'0x1000',length:1}},when:{step:'analysis',statuses:['REJECTED']}});
  }});
  const products=Object.fromEntries(r.result.products.map(x=>[x.id,x.status]));
  assert.equal(products.analysis,'REJECTED');assert.equal(products.assembly,'BLOCKED');
  assert.equal(r.result.queryCount,2);assert.equal(r.result.resourceRelease,'RELEASED');assert.equal(r.result.dataQualified,false);
});
test('query budget stops dependent dispatch without calling result complete',async()=>{
  const r=await runFixture({edit:s=>{s.limits.maxQueries=1;}});assert.equal(r.result.queryCount,1);assert.equal(r.result.status,'INCOMPLETE');
  assert.equal(r.result.products.find(p=>p.id==='assembly').status,'NOT_RUN');assert.equal(r.result.leaseReleased,true);
});
test('saved response survives a controller observer failure and is never mislabeled not run',async()=>{
  const r=await runFixture({onProgress:e=>{if(e.type==='response_saved'&&e.stepId==='analysis')throw new Error('synthetic controller observer failure');}});
  const product=r.result.products.find(p=>p.id==='analysis');
  assert.equal(product.status,'NEEDS_ANALYSIS');assert.equal(product.request.dispatched,true);assert.equal(product.request.responseSaved,true);
  assert.equal((await readJson(product.source.path)).structuredContent.operation,'analyze_function');
  assert.equal(r.result.resourceRelease,'RELEASED');assert.equal(r.result.dataQualified,false);
});
test('scope and transfer reject out-of-range VA, foreign address space, cycles and identity changes',async()=>{
  const {scope}=await base();await validateScope(scope);
  const results=new Map([['analysis',{status:'AVAILABLE',evidence:{normalized_result:{procedure:{address:'0x2000'}}}}]]);
  assert.throws(()=>materialize(scope.steps[1],results,scope),/outside/);
  scope.steps[1].argumentsFrom.procedure.space='file-offset';await assert.rejects(validateScope(scope),/target-va/);
  scope.steps[1].argumentsFrom.procedure.space='target-va';scope.steps[0].requires=['assembly'];await assert.rejects(validateScope(scope),/cycle/);
  scope.steps[0].requires=[];scope.target.sha256='0'.repeat(64);await assert.rejects(validateScope(scope),/identity/);
});
test('materialized lengths, exact branch states and response parameters cannot bypass qualification',async()=>{
  const {scope}=await base();
  const producer=new Map([['analysis',{status:'AVAILABLE',evidence:{length:-1}}]]);
  assert.throws(()=>materialize({id:'read',query:{name:'read_bytes',arguments:{address:'0x1000',length:1}},
    argumentsFrom:{length:{step:'analysis',path:['length'],type:'integer'}}},producer,scope),/read length/);
  scope.steps[1].when={step:'analysis',statuses:'NOT_AVAILABLE'};await assert.rejects(validateScope(scope),/statuses/);
  const q={name:'read_bytes',arguments:{address:'0x1000',length:1}};
  const raw={structuredContent:{evidence_id:'fixture',synthetic:true,subject:{digest:{sha256:scope.target.sha256}},operation:q.name,
    analysis_profile:{digest:'1'.repeat(64)},parameters:{address:'0x1001',length:2},normalized_result:{address:'0x1001'}}};
  assert.equal(inspectResponse(raw,q,{},scope).status,'INVALID');
  raw.structuredContent.parameters=q.arguments;raw.structuredContent.normalized_result.address='0x1000';delete raw.structuredContent.analysis_profile;
  assert.equal(inspectResponse(raw,q,{},scope).status,'NEEDS_ANALYSIS');
});
test('a slow resource survey exhausts acquisition before any runner is started',async()=>{
  const f=await base();f.scope.limits.acquisitionMs=10;
  const scopePath=path.join(f.dir,'input.json');await fs.writeFile(scopePath,JSON.stringify(f.scope));
  const r=await research({mode:'run',scopePath,output:path.join(f.dir,'run'),runtimeRoot,python,
    surveyFn:async()=>{await new Promise(r=>setTimeout(r,25));return {complete:true,matchingProcesses:[],synthetic:true};}});
  assert.match(r.error,/deadline/);assert.equal(r.queryCount,0);assert.equal(r.leaseReleased,true);
  await assert.rejects(fs.stat(path.join(f.dir,'run/raw')),e=>e.code==='ENOENT');
});
test('actual REA 6.7.0 saved assembly string is qualified without fabricating procedure metadata',async()=>{
  const base=path.join(project,'toolchain/engine-state-discovery-r1');
  const scope=await readJson(path.join(base,'n2-replay-scope.json'));
  const raw=await readJson(path.join(base,'n2-run-r2/raw/002-procedure_assembly.json'));
  const q={name:'procedure_assembly',arguments:{procedure:'0x207087c'}};
  const result=inspectResponse(raw,q,{},scope);assert.equal(result.status,'AVAILABLE');
  assert.equal(typeof result.evidence.normalized_result,'string');
  q.arguments.procedure='0x2070880';assert.equal(inspectResponse(raw,q,{},scope).status,'INVALID');
});
test('append-only assistant revision consumes the same query budget and pins its interpretation',async()=>{
  const r=await runFixture({edit:s=>{s.awaitRevision=true;s.limits.maxQueries=3;},onProgress:async(e,f)=>{
    if(e.type!=='ready'||e.calls!==3)return;
    const note=path.join(f.dir,'interpretation.txt');await fs.writeFile(note,'Synthetic review selects a different fixture address.');
    const updated=structuredClone(f.scope);updated.interpretationReferences=[{path:note,sha256:await fileSha(note)}];
    updated.steps.push({id:'selected',query:{name:'procedure_assembly',arguments:{procedure:'0x1001'}}});updated.requiredProducts.push('selected');
    await fs.writeFile(path.join(f.dir,'run/revision-001.json'),JSON.stringify({previousScopeSha256:await fileSha(path.join(f.dir,'input.json')),scope:updated}));
  }});
  assert.equal(r.result.queryCount,3);assert.equal(r.result.revisions.length,1);assert.equal(r.result.dataQualified,true);
  assert.equal(r.result.stopReason,'query budget exhausted');assert.equal(r.result.resourceRelease,'RELEASED');
});
test('idle expiry finishes before a late plan; the ended session never dispatches it',async()=>{
  let late;
  const r=await runFixture({edit:s=>{s.awaitRevision=true;s.limits.idleMs=70;},onProgress:(e,f)=>{
    if(e.type==='ready'&&e.calls===3)late=new Promise(resolve=>setTimeout(async()=>{
      await fs.writeFile(path.join(f.dir,'run/revision-001.json'),JSON.stringify({late:true}));resolve();
    },160));
  }});
  await late;assert.equal(r.result.queryCount,2);assert.equal(r.result.revisions.length,0);assert.equal(r.result.stopReason,'ready idle deadline');
  assert.equal(r.result.resourceRelease,'RELEASED');assert.equal((await readJson(path.join(r.dir,'run/revision-001.json'))).late,true);
});
test('saved real effect/device/upload replies replay with pinned request identity; no REA launched',async()=>{
  for(const role of ['effect','device','upload']){
    const dir=await fs.mkdtemp(path.join(artifacts,'replay-'+role+'-'));
    const savedDir=path.join(project,'toolchain/engine-command-prediction-r1',role),plan=await readJson(path.join(savedDir,'plan.json'));
    const bindingPath=path.join(path.dirname(plan.target),'binding.json'),binding=await readJson(bindingPath);
    const scope={schemaVersion:1,question:'Saved '+role+' receipt qualification',purpose:'qualification',target:{path:plan.target,sha256:plan.targetSha256,
      ranges:[{start:binding.va,endExclusive:'0x'+(Number(BigInt(binding.va))+binding.length).toString(16)}]},binding:{path:bindingPath,sha256:await fileSha(bindingPath)},steps:[],saved:{},requiredProducts:[]};
    for(let i=1;i<plan.calls.length;i++){
      const id='s'+i,prefix=String(i).padStart(3,'0'),q=plan.calls[i];scope.steps.push({id,query:q});scope.requiredProducts.push(id);
      const req=path.join(savedDir,prefix+'-request.json'),res=path.join(savedDir,prefix+'-'+q.name+'.json');
      scope.saved[id]={request:{path:req,sha256:await fileSha(req)},response:{path:res,sha256:await fileSha(res)}};
    }
    const sp=path.join(dir,'scope.json');await fs.writeFile(sp,JSON.stringify(scope));
    const result=await research({mode:'replay',scopePath:sp,output:path.join(dir,'replay'),runtimeRoot,python});
    assert.equal(result.resourceRelease,'NOT_ACQUIRED');assert.equal(result.error,undefined);
    for(const p of result.products)assert(['AVAILABLE','REJECTED'].includes(p.status),JSON.stringify(p));
    // Replay with an actually missing response is UNKNOWN, never inferred from a session's status.
    delete scope.saved.s1.response;await fs.writeFile(sp,JSON.stringify(scope));
    const partial=await research({mode:'replay',scopePath:sp,output:path.join(dir,'partial'),runtimeRoot,python});
    assert.equal(partial.products[0].status,'UNKNOWN');assert.equal(partial.dataQualified,false);
    if(role==='effect'){
      const original=await readJson(path.join(savedDir,'001-analyze_function.json')),step=scope.steps[0];
      const altered=structuredClone(original);altered.structuredContent.subject.digest.sha256='0'.repeat(64);
      assert.equal(inspectResponse(altered,step.query,step,scope).status,'NEEDS_ANALYSIS');
      altered.content=[];assert.equal(inspectResponse(altered,step.query,step,scope).status,'INVALID');
    }
  }
});
