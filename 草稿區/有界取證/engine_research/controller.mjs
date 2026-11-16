import fs from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { execFile } from 'node:child_process';
import { promisify } from 'node:util';
import { PassThrough } from 'node:stream';
import { absolute, canonical, fileSha, readJson, sha, sleep, writeNew } from './io.mjs';
import { inspectResponse, materialize, validateScope } from './model.mjs';
import { NATIVE, runReaQueries } from './runner.mjs';

const here=path.dirname(fileURLToPath(import.meta.url));
const execFileAsync=promisify(execFile);
async function survey() {
  // Execute the fixed, reviewed query as a command. No execution-policy setting
  // is changed, and neither scope nor tool output becomes PowerShell source.
  const command='[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new();\n'+await fs.readFile(path.join(here,'resource_survey.ps1'),'utf8');
  const r=await execFileAsync('powershell.exe',['-NoProfile','-NonInteractive','-Command',command],
    {windowsHide:true,timeout:15000,maxBuffer:1048576});
  const value=JSON.parse(r.stdout.replace(/^\uFEFF/,''));
  if(value.complete!==true||!Array.isArray(value.matchingProcesses))throw new Error('Resource survey incomplete');
  return value;
}
function compact(r){const {evidence,...rest}=r;return rest;}
function next(scope,results) {
  for(const step of scope.steps){
    if(results.has(step.id))continue;
    let value;try{value=materialize(step,results,scope);}catch(e){value={status:'INVALID',reason:String(e)};}
    if(value.status==='WAITING')continue;
    if(value.status==='READY')return {step,...value};
    results.set(step.id,value);
    return next(scope,results);
  }
  return null;
}
// Finite declared scopes only. No eval, guessed callees, tool-selected commands,
// automatic reconnect, or implicit reset of an exhausted activity's budget.
export async function research({mode,scopePath,output,runtimeRoot=path.join(NATIVE,'rea-toolchain/runtime'),
  python=path.resolve(runtimeRoot,'../../../../toolchain/ai-texture-runtime/Scripts/python.exe'),
  runnerOptions={},surveyFn=survey,onProgress=()=>{}}) {
  if(!['inspect','replay','run'].includes(mode))throw new Error('Expected inspect|replay|run');
  absolute(scopePath);absolute(output);absolute(runtimeRoot);
  const scopeBytes=await fs.readFile(scopePath);let scope=JSON.parse(scopeBytes);const checked=await validateScope(scope);
  const started=Date.now(),deadline=started+checked.limits.acquisitionMs;
  await fs.mkdir(output);await fs.writeFile(path.join(output,'scope.json'),scopeBytes,{flag:'wx'});
  const receipts={schemaVersion:1,mode,scopeSha256:sha(scopeBytes),question:scope.question,purpose:scope.purpose,
    started:new Date(started).toISOString(),controllerPid:process.pid,
    code:await Promise.all(['controller.mjs','model.mjs','runner.mjs','processes.mjs','process_inventory.py','io.mjs','resource_survey.ps1'].map(async f=>({file:f,sha256:await fileSha(path.join(here,f))}))),
    identities:checked.identities,binding:checked.binding,limits:checked.limits,
    interpretationReferences:scope.interpretationReferences??[],dispatches:[],products:[],revisions:[],
    resourceRelease:mode==='run'?'UNVERIFIED':'NOT_ACQUIRED',semanticVerdict:'NOT_AUTOMATICALLY_ASSESSED'};
  const results=new Map();let count=0,sessionProfile=null,lease=null,runnerResult=null;
  let input=null,finished=false,idleTimer=null,inFlight=null,controllerError=null;
  let activeScopeSha256=receipts.scopeSha256;
  const log=async data=>fs.appendFile(path.join(output,'controller.jsonl'),JSON.stringify({at:new Date().toISOString(),...data})+'\n');
  const stop=async reason=>{if(finished)return;finished=true;clearTimeout(idleTimer);receipts.stopReason=reason;
    await log({type:'finish_selected',reason});if(input&&!input.destroyed)input.end('finish\n');};
  const qualify=async(step,query,file)=>{
    const raw=await readJson(file);let r=inspectResponse(raw,query,step,scope);
    if(r.status==='AVAILABLE'&&sessionProfile&&r.profile!==sessionProfile)r={status:'INVALID',reason:'analysis profile changed within dependent activity'};
    if(r.status==='AVAILABLE'&&r.profile)sessionProfile??=r.profile;
    results.set(step.id,{...r,source:{path:file,sha256:await fileSha(file)},query});
    return r;
  };
  const waitRevision=async()=>{
    if(!scope.awaitRevision)return false;
    const idleEnd=Math.min(deadline,Date.now()+checked.limits.idleMs);
    const name=`revision-${String(receipts.revisions.length+1).padStart(3,'0')}.json`,p=path.join(output,name);
    await log({type:'awaiting_revision',path:p,deadline:new Date(idleEnd).toISOString()});
    while(!finished&&Date.now()<idleEnd){
      let bytes;try{bytes=await fs.readFile(p);}catch(e){if(e.code!=='ENOENT')throw e;}
      if(bytes){
        const revision=JSON.parse(bytes),updated=revision.scope;
        if(revision.previousScopeSha256!==activeScopeSha256)throw new Error('Revision base differs; no implicit rebase');
        const checkedRevision=await validateScope(updated);
        for(const field of ['question','purpose','target','binding','inputs','env','openArguments','openTimeoutMs','synthetic'])
          if(canonical(updated[field])!==canonical(scope[field]))throw new Error('Revision changes activity identity: '+field);
        if(canonical(checkedRevision.limits)!==canonical(checked.limits))throw new Error('Revision cannot reset activity budget');
        if(updated.steps.length<scope.steps.length||scope.steps.some((s,i)=>canonical(s)!==canonical(updated.steps[i])))throw new Error('Revision must preserve earlier steps exactly');
        if((scope.requiredProducts??[]).some(id=>!(updated.requiredProducts??[]).includes(id)))throw new Error('Revision cannot remove required products');
        if(!updated.interpretationReferences?.length)throw new Error('Assistant revision needs a pinned interpretation reference');
        scope=updated;activeScopeSha256=sha(Buffer.from(canonical(scope)));
        const accepted={path:p,sha256:sha(bytes),scopeSha256:activeScopeSha256,queryCount:count,acceptedAt:new Date().toISOString(),interpretationReferences:scope.interpretationReferences};
        receipts.revisions.push(accepted);await log({type:'revision_accepted',...accepted});return true;
      }
      await sleep(Math.min(100,Math.max(1,idleEnd-Date.now())));
    }
    await stop(Date.now()>=deadline?'acquisition dispatch deadline reached':'ready idle deadline');return false;
  };
  try {
    if(mode==='inspect'){
      receipts.steps=scope.steps.map(s=>({id:s.id,query:s.query,requires:s.requires??[],argumentsFrom:s.argumentsFrom??{},when:s.when??null}));
      receipts.status='INSPECTED';
    }else if(mode==='replay'){
      // Only explicitly pinned saved requests/responses are consumed. A saved
      // request without a response stays UNKNOWN; lifecycle success cannot fill it.
      let selected;while((selected=next(scope,results))){
        const {step,query,transfers}=selected;
        if(count>=checked.limits.maxQueries){await stop('query budget exhausted');break;}
        count++;const saved=scope.saved?.[step.id];
        if(!saved?.request){results.set(step.id,{status:'MISSING',reason:'No pinned saved request'});continue;}
        absolute(saved.request.path);if(await fileSha(saved.request.path)!==saved.request.sha256.toUpperCase())throw new Error('Saved request hash mismatch');
        const request=await readJson(saved.request.path);
        if(request.name!==query.name||canonical(request.arguments??{})!==canonical(query.arguments??{})){
          results.set(step.id,{status:'INVALID',reason:'Saved request differs from selected operation/parameters'});continue;}
        receipts.dispatches.push({id:step.id,query,transfers,savedRequest:saved.request});
        if(!saved.response){results.set(step.id,{status:'UNKNOWN',reason:'Request saved without qualified response'});continue;}
        absolute(saved.response.path);if(await fileSha(saved.response.path)!==saved.response.sha256.toUpperCase())throw new Error('Saved response hash mismatch');
        await qualify(step,query,saved.response.path);
      }
    }else {
      // This lease coordinates cooperating project controllers. The independent
      // process survey also checks existing backends; it cannot govern other apps.
      const leasePath=path.resolve(runtimeRoot,'../../../../toolchain/engine-research-resource-owner.json');
      lease=await fs.open(leasePath,'wx');receipts.leasePath=leasePath;
      await lease.writeFile(JSON.stringify({controllerPid:process.pid,started:receipts.started,scopeSha256:receipts.scopeSha256,output}));
      const snapshot=await surveyFn();await writeNew(output,'resource-survey.json',snapshot);
      if(snapshot.complete!==true||snapshot.matchingProcesses.length)throw new Error('Shared analysis resource is not available; see resource-survey.json');
      const remaining=()=>Math.max(1,deadline-Date.now());
      if(Date.now()>=deadline)throw new Error('Acquisition deadline reached before starting runner');
      const plan={target:scope.target.path,targetSha256:scope.target.sha256,env:scope.env??{},acquisitionDeadlineMs:deadline,
        timeoutMs:Math.min(180000,remaining()),lifecycleTimeoutMs:Math.min(60000,remaining()),
        cleanup:{budgetMs:checked.limits.cleanupMs,exportTimeoutMs:Math.min(120000,checked.limits.cleanupMs),closeTimeoutMs:Math.min(120000,checked.limits.cleanupMs)},
        calls:[{id:'lifecycle_open',name:'open_binary',arguments:{path:scope.target.path,...scope.openArguments},timeoutMs:Math.min(scope.openTimeoutMs??1200000,remaining())}],followup:true};
      // Never permit openArguments to substitute a different target.
      plan.calls[0].arguments.path=scope.target.path;
      await writeNew(output,'plan.json',plan);input=new PassThrough();
      const dispatch=async()=>{
        if(finished)return;clearTimeout(idleTimer);
        if(Date.now()>=deadline)return stop('acquisition dispatch deadline reached');
        if(count>=checked.limits.maxQueries)return stop('query budget exhausted');
        const selected=next(scope,results);if(!selected){if(await waitRevision())return dispatch();return stop('declared scope exhausted; semantic interpretation required');}
        const {step,transfers}=selected,query={...selected.query,timeoutMs:Math.min(selected.query.timeoutMs??180000,remaining())};
        count++;inFlight={step,query};
        const receipt={id:step.id,query,transfers,ordinal:count,consumedRuleSha256:activeScopeSha256};
        await writeNew(output,`dispatch-${String(count).padStart(3,'0')}.json`,receipt);
        receipts.dispatches.push(receipt);await log({type:'dispatch',...receipt});
        input.write(JSON.stringify(query)+'\n');
      };
      runnerResult=await runReaQueries({...runnerOptions,planPath:path.join(output,'plan.json'),output:path.join(output,'raw'),runtimeRoot,python,input,
        onEvent:async e=>{
          await onProgress(e);
          if(e.type==='response_saved'&&inFlight&&e.stepId===inFlight.step.id){
            const r=await qualify(inFlight.step,inFlight.query,path.join(output,'raw',e.file));
            await log({type:'product',id:inFlight.step.id,...compact(r),evidence:undefined});inFlight=null;
          }
          if(e.type==='request_unknown'&&inFlight){results.set(inFlight.step.id,{status:'UNKNOWN',reason:e.error});inFlight=null;finished=true;}
          if(e.type==='cleanup_started'){finished=true;clearTimeout(idleTimer);}
          if(e.type==='ready'&&!finished){
            await dispatch();
          }
        }});
      receipts.runner={path:path.join(output,'raw/result.json'),sha256:await fileSha(path.join(output,'raw/result.json')),status:runnerResult.status};
      receipts.resourceRelease=runnerResult.resourceRelease;
      if(runnerResult.status!=='COMPLETE')controllerError=runnerResult.error??'runner failed';
    }
  }catch(e){controllerError=String(e?.stack??e);}
  finally {
    clearTimeout(idleTimer);input?.destroy();
    if(lease){await lease.close();
      // If no runner was dispatched, no owned backend was acquired. Otherwise
      // leave the lease as a recovery marker until release has actually been checked.
      if(receipts.resourceRelease==='RELEASED'||(!runnerResult&&!await fs.stat(path.join(output,'raw')).catch(()=>null))){
        await fs.unlink(receipts.leasePath);receipts.leaseReleased=true;
      }else receipts.leaseReleased=false;
    }
    for(const step of scope.steps)if(!results.has(step.id)&&mode!=='inspect'){
      const request=runnerResult?.requests?.find(r=>r.id===step.id);
      if(request?.responseSaved)results.set(step.id,{status:'NEEDS_ANALYSIS',reason:'Response saved; controller qualification did not finish',
        source:{path:path.join(output,'raw',request.file),identityVerified:false},request});
      else if(request?.responseReceived)results.set(step.id,{status:'NEEDS_ANALYSIS',reason:'Response received but persistence failed; inspect runner receipt before recovery',request});
      else if(request?.dispatched)results.set(step.id,{status:'UNKNOWN',reason:'Request dispatched without a confirmed response',request});
      else results.set(step.id,{status:'NOT_RUN',reason:receipts.stopReason??'No dispatched request in completed runner receipt'});
    }
    receipts.products=[...results].map(([id,r])=>({id,...compact(r)}));
    receipts.requiredProducts=(scope.requiredProducts??[]).map(id=>({id,status:results.get(id)?.status??'NOT_RUN'}));
    receipts.queryCount=count;receipts.finished=new Date().toISOString();
    receipts.dataQualified=mode!=='inspect'&&receipts.requiredProducts.length>0&&receipts.requiredProducts.every(p=>p.status==='AVAILABLE');
    if(controllerError)receipts.error=controllerError;
    receipts.status??=controllerError?'FAILED':receipts.dataQualified?'QUALIFIED':'INCOMPLETE';
    await writeNew(output,'handoff.json',receipts);
  }
  return receipts;
}
