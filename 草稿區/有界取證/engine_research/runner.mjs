import fs from 'node:fs/promises';
import { createWriteStream } from 'node:fs';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { createInterface } from 'node:readline';
import { once } from 'node:events';
import { absolute, duration, fileSha, sha, writeNew } from './io.mjs';
import { createTracker } from './processes.mjs';
const here=path.dirname(fileURLToPath(import.meta.url));
export const NATIVE=path.dirname(here);
export const ALLOWED=new Set(['binary_session','open_binary','close_binary','binary_overview',
  'analyze_function','procedure_assembly','search_strings','search_procedures','search_bytes','xrefs',
  'read_bytes','address_to_file_offset','file_offset_to_address','get_evidence_bundle','inspect_analysis_view']);
const ENV_ALLOWED=new Set(['GHIDRA_INSTALL_DIR','JAVA_HOME','REA_ANALYSIS_PROVIDER',
  'REA_GHIDRA_STARTUP_TIMEOUT_MS','REA_GHIDRA_DISABLED_ANALYZERS']);
export function validateStep(step) {
  if(!step||!ALLOWED.has(step.name))throw new Error('Unapproved operation: '+step?.name);
  if(step.timeoutMs!==undefined)duration(step.timeoutMs,'step.timeoutMs');
  if(step.arguments!==undefined&&(!step.arguments||typeof step.arguments!=='object'||Array.isArray(step.arguments)))throw new Error('arguments must be an object');
  if(step.allowError!==undefined&&typeof step.allowError!=='boolean')throw new Error('allowError must be boolean');
}
// Runtime/server injection is a trusted-code test seam, never a plan field.
export async function runReaQueries({planPath,output,runtimeRoot=path.join(NATIVE,'rea-toolchain/runtime'),
  serverEntry,serverArgs=['mcp'],testEnv={},input=process.stdin,onEvent=e=>console.log(JSON.stringify(e)),
  python=path.resolve(NATIVE,'../../toolchain/ai-texture-runtime/Scripts/python.exe'),trackerFactory=createTracker}={}) {
  absolute(planPath,'plan');absolute(output,'output');absolute(runtimeRoot,'runtimeRoot');
  const planBytes=await fs.readFile(planPath),plan=JSON.parse(planBytes);
  if(plan.acquisitionDeadlineMs!==undefined&&(!Number.isSafeInteger(plan.acquisitionDeadlineMs)||plan.acquisitionDeadlineMs<1))throw new Error('Invalid acquisition deadline');
  const acquisitionTimeout=ms=>{
    if(plan.acquisitionDeadlineMs===undefined)return ms;
    const remaining=plan.acquisitionDeadlineMs-Date.now();
    if(remaining<=0)throw new Error('Acquisition deadline reached; no new request sent');
    return Math.min(ms,remaining);
  };
  for(const step of plan.calls??[])validateStep(step);
  const defaultTimeout=duration(plan.timeoutMs??1200000,'plan.timeoutMs');
  const lifecycleTimeout=duration(plan.lifecycleTimeoutMs??60000,'plan.lifecycleTimeoutMs');
  const exportTimeout=duration(plan.cleanup?.exportTimeoutMs??120000,'export timeout');
  const closeTimeout=duration(plan.cleanup?.closeTimeoutMs??120000,'close timeout');
  const cleanupBudget=duration(plan.cleanup?.budgetMs??300000,'cleanup budget');
  const selectedEnv={};
  for(const [k,v] of Object.entries(plan.env??{})) {
    if(!ENV_ALLOWED.has(k)||typeof v!=='string')throw new Error('Unsupported environment setting: '+k);
    selectedEnv[k]=v;
  }
  const entry=serverEntry??path.join(runtimeRoot,'node_modules/rea-agents/scripts/rea.mjs');absolute(entry,'server entry');
  const [{Client},{StdioClientTransport}]=await Promise.all([
    import(pathToFileURL(path.join(runtimeRoot,'node_modules/@modelcontextprotocol/client/dist/index.mjs'))),
    import(pathToFileURL(path.join(runtimeRoot,'node_modules/@modelcontextprotocol/client/dist/stdio.mjs')))]);
  await fs.mkdir(output);await fs.writeFile(path.join(output,'plan.json'),planBytes,{flag:'wx'});
  const temp=path.resolve(NATIVE,'../../toolchain/rea-temp');await fs.mkdir(temp,{recursive:true});
  const summary={schemaVersion:2,status:'RUNNING',started:new Date().toISOString(),
    runnerSha256:await fileSha(fileURLToPath(import.meta.url)),reaEntrySha256:await fileSha(entry),planSha256:sha(planBytes),
    node:process.version,env:selectedEnv,runnerPid:process.pid,serverEntry:entry,lifecycleTimeoutMs:lifecycleTimeout,
    calls:[],requests:[],unknownRequests:[],eventErrors:[],cleanupErrors:[]};
  let seq=0;
  // A failed log/callback must stop acquisition, but must not poison later cleanup.
  const event=async data=>{const e={sequence:seq++,at:new Date().toISOString(),...data};
    try{await fs.appendFile(path.join(output,'lifecycle.jsonl'),JSON.stringify(e)+'\n');}
    catch(error){summary.eventErrors.push({type:e.type,stage:'journal',error:String(error)});throw error;}
    try{await onEvent(e);}
    catch(error){summary.eventErrors.push({type:e.type,stage:'callback',error:String(error)});throw error;}
  };
  const waitBounded=async(promise,ms,label)=>{
    let timer;
    try{return await Promise.race([promise,new Promise((_,reject)=>{timer=setTimeout(()=>reject(new Error(label+' deadline exceeded')),ms);})]);}
    finally{clearTimeout(timer);}
  };
  const transport=new StdioClientTransport({command:process.execPath,args:[entry,...serverArgs],
    env:{...process.env,...selectedEnv,...testEnv,TEMP:temp,TMP:temp},cwd:NATIVE,stderr:'pipe',maxBufferSize:64*1024*1024});
  const client=new Client({name:'ge-project-native-research',version:'2.0.0'}),tracker=trackerFactory(python);
  const stderr=createWriteStream(path.join(output,'server.stderr.log'),{flags:'wx'});
  let stderrError;stderr.on('error',e=>{stderrError=e;});transport.stderr?.pipe(stderr);
  let attemptedOpen=false,opened=false,connected=false,transportClosed=false,failure=null,ordinal=0,uncertain=false,tracking=false;
  transport.onclose=()=>{transportClosed=true;};
  const fail=(error,stage)=>{failure??=error;if(stage)summary.cleanupErrors.push({stage,error:String(error)});};
  const pathIdentity=p=>process.platform==='win32'?path.resolve(p).toLowerCase():path.resolve(p);
  try {
    if(plan.target){absolute(plan.target,'target');summary.target={path:plan.target,sha256:await fileSha(plan.target)};
      if(plan.targetSha256&&plan.targetSha256.toUpperCase()!==summary.target.sha256)throw new Error('Target identity mismatch before opening');}
    await event({type:'connecting',entry});
    // Attach the rejection handler immediately: tracker.start can outlast a failed handshake.
    const connectTimeout=acquisitionTimeout(lifecycleTimeout);
    const connection=waitBounded(client.connect(transport,{timeout:connectTimeout}),connectTimeout,'connect')
      .then(()=>({ok:true}),error=>({ok:false,error}));
    if(transport.pid){summary.serverPid=transport.pid;tracking=true;await tracker.start(transport.pid);}
    const connectionResult=await connection;if(!connectionResult.ok)throw connectionResult.error;
    connected=true;summary.serverPid=transport.pid;
    if(!summary.serverPid)throw new Error('Missing owned server PID');
    if(!tracking){tracking=true;await tracker.start(summary.serverPid);}
    const catalogTimeout=acquisitionTimeout(lifecycleTimeout);
    const catalog=await waitBounded(client.listTools(undefined,{timeout:catalogTimeout}),catalogTimeout,'listTools');
    await writeNew(output,'catalog.json',catalog);const names=new Set(catalog.tools.map(t=>t.name));
    await event({type:'connected',connected:true,serverPid:summary.serverPid,tools:names.size,output});
    const query=async step=>{
      if(stderrError)throw stderrError;
      validateStep(step);if(!names.has(step.name))throw new Error('Tool not advertised: '+step.name);
      if(step.name==='open_binary'){if(!plan.target||step.arguments?.path!==plan.target)throw new Error('open_binary must select the plan-bound target');}
      const i=ordinal++,prefix=String(i).padStart(3,'0'),file=`${prefix}-${step.name}.json`;
      let effectiveTimeout=acquisitionTimeout(step.timeoutMs??defaultTimeout);
      const receipt={ordinal:i,id:step.id??null,name:step.name,file,dispatched:false,responseReceived:false,responseSaved:false};
      summary.requests.push(receipt);
      await writeNew(output,`${prefix}-request.json`,step);
      await event({type:'request_sent',dispatchPhase:'prepared',starting:step.name,ordinal:i,stepId:step.id??null,effectiveTimeoutMs:effectiveTimeout,timeoutSource:step.timeoutMs===undefined?'plan/default':'step'});
      let response;
      effectiveTimeout=acquisitionTimeout(effectiveTimeout);receipt.effectiveTimeoutMs=effectiveTimeout;
      receipt.dispatched=true;if(step.name==='open_binary')attemptedOpen=true;
      try{response=await client.callTool({name:step.name,arguments:step.arguments??{}},{timeout:effectiveTimeout});receipt.responseReceived=true;}
      catch(e){uncertain=true;summary.unknownRequests.push({ordinal:i,stepId:step.id??null,name:step.name,error:String(e)});
        await event({type:'request_unknown',ordinal:i,stepId:step.id??null,error:String(e)});throw e;}
      if(step.name==='open_binary'&&!response.isError)opened=true;
      try{await writeNew(output,file,response);receipt.responseSaved=true;}
      catch(e){receipt.storageError=String(e);throw e;}
      summary.calls.push({ordinal:i,id:step.id??null,name:step.name,file,isError:response.isError===true,effectiveTimeoutMs:effectiveTimeout});
      if(step.name==='open_binary'&&!response.isError){
        const actual=response.structuredContent?.result;summary.openedTarget=actual??null;
        if(!actual||typeof actual.path!=='string'||typeof actual.sha256!=='string'||
          pathIdentity(actual.path)!==pathIdentity(plan.target)||actual.sha256.toUpperCase()!==summary.target.sha256)
          throw new Error('Opened target identity missing or mismatched');
        summary.openedTargetVerified=true;
      }
      await event({type:'response_saved',ordinal:i,stepId:step.id??null,name:step.name,file,isError:response.isError===true});
      if(response.isError&&!step.allowError)throw new Error('REA tool failed; see '+file);
    };
    for(const step of plan.calls??[])await query(step);
    if(plan.followup===true){const lines=createInterface({input,crlfDelay:Infinity});
      const queuedLines=lines[Symbol.asyncIterator]();
      try{await event({type:'ready',readyForFollowup:true,serverPid:transport.pid,calls:summary.calls.length});
        for await(const line of queuedLines){if(!line.trim())continue;if(line.trim()==='finish')break;
          await query(JSON.parse(line));await event({type:'ready',readyForFollowup:true,calls:summary.calls.length});}
      }finally{lines.close();}}
  }catch(e){failure=e;summary.error=String(e?.stack??e);}
  finally {
    const deadline=Date.now()+cleanupBudget;
    const bounded=n=>duration(Math.max(1,Math.min(n,deadline-Date.now())),'remaining cleanup');
    try{await waitBounded(event({type:'cleanup_started',uncertain}),bounded(cleanupBudget),'cleanup event');}
      catch(e){fail(e,'cleanup event');}
    if(connected&&attemptedOpen&&!transportClosed){
      if(opened&&!uncertain&&Date.now()<deadline)try {
        const exported=await client.callTool({name:'export_evidence_bundle',arguments:{path:path.join(output,'evidence.json'),overwrite:false}},{timeout:bounded(exportTimeout)});
        await writeNew(output,'export.json',exported);if(exported.isError)throw new Error('Evidence export returned an error');summary.evidenceExported=true;
      }catch(e){summary.exportError=String(e);failure??=e;}
      else if(uncertain)summary.exportSkipped='request outcome unknown; persisted original responses retained';
      if(Date.now()<deadline)try {
        const closed=await client.callTool({name:'close_binary',arguments:{}},{timeout:bounded(closeTimeout)});
        await writeNew(output,'close.json',closed);summary.closeReturned=true;if(closed.isError)throw new Error('REA close returned an error');summary.closeSucceeded=true;
      }catch(e){summary.closeError=String(e);failure??=e;}
      else summary.closeError='cleanup budget exhausted';
    }
    try{await waitBounded(client.close(),bounded(5000),'client close');summary.clientClosed=true;}
      catch(e){summary.clientCloseError=String(e);fail(e,'client close');}
    try{summary.processRelease=tracking?await waitBounded(tracker.finish(bounded(4000)),bounded(4000),'process release'):{status:'UNVERIFIED',reason:'server identity not acquired'};}
      catch(e){summary.processRelease={status:'UNVERIFIED',reason:String(e)};fail(e,'process release');}
    summary.resourceRelease=summary.processRelease.status==='RELEASED'&&(!attemptedOpen||summary.closeSucceeded)?'RELEASED':'UNVERIFIED';
    if(summary.resourceRelease!=='RELEASED')failure??=new Error('Owned resource release is not verified');
    try{stderr.end();if(!stderr.writableFinished&&!stderr.destroyed)await waitBounded(once(stderr,'finish'),bounded(4000),'stderr finish');}
      catch(e){stderrError=e;stderr.destroy();}
    if(stderrError){summary.stderrError=String(stderrError);failure??=stderrError;}
    if(summary.target)try{summary.targetAfterSha256=await fileSha(plan.target);if(summary.targetAfterSha256!==summary.target.sha256)throw new Error('Target changed');}
      catch(e){summary.targetError=String(e);failure??=e;}
    summary.finished=new Date().toISOString();summary.status=failure?'FAILED':'COMPLETE';
    // This notification precedes final persistence; result.json is the completion receipt.
    try{await waitBounded(event({type:'runner_finished',status:summary.status,finalReceiptPending:true,resourceRelease:summary.resourceRelease}),bounded(cleanupBudget),'finished event');}
      catch(e){fail(e,'finished event');summary.status='FAILED';}
    try{await writeNew(output,'result.json',summary);}
      catch(e){fail(e,'result persistence');summary.resultError=String(e);summary.status='FAILED';}
  }
  return summary;
}
