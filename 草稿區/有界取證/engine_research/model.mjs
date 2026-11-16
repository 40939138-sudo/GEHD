import fs from 'node:fs/promises';
import { absolute, canonical, duration, field, fileSha, readJson, sha } from './io.mjs';
import { validateStep } from './runner.mjs';

const HASH=/^[A-Fa-f0-9]{64}$/;
export function address(value) {
  if(typeof value!=='string'||!/^0x[0-9a-f]+$/i.test(value))throw new Error('Expected explicit default-space hex VA');
  const n=Number(BigInt(value));if(!Number.isSafeInteger(n))throw new Error('Address outside safe range');return n;
}
export function inRange(value,target) {
  const n=address(value);
  if(!target.ranges?.some(r=>address(r.start)<=n&&n<address(r.endExclusive)))throw new Error('Address outside selected target VA ranges');
  return value.toLowerCase();
}
function peMap(b,va,len) {
  if(b.length<0x40||b.toString('ascii',0,2)!=='MZ')throw new Error('Invalid PE binding');
  const nt=b.readUInt32LE(0x3c),o=nt+24;
  if(o+224>b.length||b.toString('binary',nt,nt+4)!=='PE\0\0'||b.readUInt16LE(nt+4)!==0x14c||b.readUInt16LE(o)!==0x10b)throw new Error('Binding needs PE32 x86');
  const rva=va-b.readUInt32LE(o+28),table=o+b.readUInt16LE(nt+20),found=[];
  for(let i=0;i<b.readUInt16LE(nt+6);i++){
    const s=table+40*i;if(s+40>b.length)throw new Error('Truncated PE section table');
    const start=b.readUInt32LE(s+12),length=Math.min(b.readUInt32LE(s+8),b.readUInt32LE(s+16));
    if(rva>=start&&rva+len<=start+length){const offset=b.readUInt32LE(s+20)+rva-start;
      if(offset+len>b.length)throw new Error('Short initialized section');found.push(offset);}
  }
  if(found.length!==1)throw new Error('Ambiguous/unmapped VA');return found[0];
}
export async function validateScope(scope) {
  if(scope.schemaVersion!==1||typeof scope.question!=='string'||!scope.question.trim())throw new Error('Missing research question/schema');
  if(!['exploration','understanding','prediction','qualification'].includes(scope.purpose))throw new Error('Unknown purpose');
  const target=scope.target;absolute(target?.path,'target');if(!HASH.test(target.sha256??''))throw new Error('Pinned target hash required');
  if(!Array.isArray(target.ranges)||!target.ranges.length)throw new Error('Selected VA ranges required');
  for(const r of target.ranges)if(address(r.start)>=address(r.endExclusive))throw new Error('Empty VA range');
  const identities=[];
  for(const item of [target,...scope.inputs??[],...scope.interpretationReferences??[]]) {
    absolute(item.path);if(!HASH.test(item.sha256??''))throw new Error('Pinned input hash required');
    const actual=await fileSha(item.path);if(actual!==item.sha256.toUpperCase())throw new Error('Input identity mismatch: '+item.path);
    identities.push({path:item.path,sha256:actual});
  }
  let binding=null;
  if(scope.binding){
    absolute(scope.binding.path);if(await fileSha(scope.binding.path)!==scope.binding.sha256?.toUpperCase())throw new Error('Binding identity mismatch');
    const b=await readJson(scope.binding.path);absolute(b.source);
    if(b.executionAllowed!==false||!Number.isSafeInteger(b.length)||b.length<=0||b.length>1048576)throw new Error('Invalid analysis-container binding');
    const [source,wrapper]=await Promise.all([fs.readFile(b.source),fs.readFile(target.path)]);
    if(sha(source)!==b.sourceSha256?.toUpperCase()||sha(wrapper)!==b.wrapperSha256?.toUpperCase())throw new Error('Binding source/target mismatch');
    const start=address(b.va),a=peMap(source,start,b.length),w=peMap(wrapper,start,b.length);
    if(a!==b.sourceFileOffset||w!==b.wrapperPayloadOffset||!source.subarray(a,a+b.length).equals(wrapper.subarray(w,w+b.length))||sha(source.subarray(a,a+b.length))!==b.windowSha256?.toUpperCase())throw new Error('Binding payload mismatch');
    for(const r of target.ranges)if(address(r.start)<start||address(r.endExclusive)>start+b.length)throw new Error('Scope escapes bound original bytes');
    binding={path:scope.binding.path,sha256:scope.binding.sha256,source:b.source,sourceSha256:b.sourceSha256,limitations:b.limitations};
  }
  const limits={maxQueries:12,acquisitionMs:1800000,idleMs:300000,cleanupMs:300000,...scope.limits};
  for(const [k,v] of Object.entries(limits))duration(v,k);
  if(scope.openTimeoutMs!==undefined)duration(scope.openTimeoutMs,'openTimeoutMs');
  if(scope.awaitRevision!==undefined&&typeof scope.awaitRevision!=='boolean')throw new Error('awaitRevision must be boolean');
  if(scope.resume!==undefined)throw new Error('Resume requires explicit saved-evidence requalification; automatic reconnect is unsupported');
  const ids=new Set();
  if(!Array.isArray(scope.steps))throw new Error('steps must be an array');
  for(const s of scope.steps){
    if(typeof s.id!=='string'||!/^[a-zA-Z0-9_-]+$/.test(s.id)||ids.has(s.id)||s.id==='lifecycle_open')throw new Error('Unique non-lifecycle local step IDs required');ids.add(s.id);
    validateStep(s.query);if(['open_binary','close_binary'].includes(s.query.name))throw new Error('Lifecycle is controller-owned');
    if(s.requires!==undefined&&(!Array.isArray(s.requires)||s.requires.some(x=>typeof x!=='string')))throw new Error('requires must be step IDs');
    if(s.when&&(!Array.isArray(s.when.statuses)||!s.when.statuses.length||s.when.statuses.some(x=>!['AVAILABLE','REJECTED','INVALID','INSUFFICIENT','NEEDS_ANALYSIS','UNKNOWN','MISSING','BLOCKED','SKIPPED'].includes(x))))throw new Error('when.statuses must be explicit terminal states');
    if(s.query.name==='read_bytes'&&!s.argumentsFrom?.length&&(!Number.isSafeInteger(s.query.arguments?.length)||s.query.arguments.length<1||s.query.arguments.length>1048576))throw new Error('Bounded read length required');
  }
  for(const s of scope.steps){
    const deps=[...(s.requires??[]),...(s.when?[s.when.step]:[]),...Object.values(s.argumentsFrom??{}).map(x=>x.step)];
    for(const d of deps)if(!ids.has(d)||d===s.id)throw new Error('Unknown/self dependency: '+d);
    for(const [arg,src] of Object.entries(s.argumentsFrom??{})){
      if(['__proto__','constructor','prototype'].includes(arg))throw new Error('Unsafe argument key');
      if(!['address','integer','string','boolean'].includes(src.type))throw new Error('Explicit parameter type required');
      if(src.type==='address'&&src.space!=='target-va')throw new Error('Explicit target-va source required');
    }
  }
  const visiting=new Set(),visited=new Set(),byId=new Map(scope.steps.map(s=>[s.id,s]));
  function visit(id){if(visiting.has(id))throw new Error('Dependency cycle');if(visited.has(id))return;visiting.add(id);
    const s=byId.get(id);for(const d of [...s.requires??[],...Object.values(s.argumentsFrom??{}).map(x=>x.step),...(s.when?[s.when.step]:[])])visit(d);
    visiting.delete(id);visited.add(id);}
  for(const id of ids)visit(id);
  for(const id of scope.requiredProducts??[])if(!ids.has(id))throw new Error('Unknown required product');
  return {identities,binding,limits};
}
export function materialize(step,results,scope) {
  for(const id of step.requires??[]){if(!results.has(id))return {status:'WAITING'};if(results.get(id).status!=='AVAILABLE')return {status:'BLOCKED',reason:'required product '+id};}
  if(step.when){const r=results.get(step.when.step);if(!r)return {status:'WAITING'};
    if(!step.when.statuses.includes(r.status))return {status:'SKIPPED',reason:'branch condition not selected'};}
  const query=structuredClone(step.query);query.id=step.id;query.arguments??={};const transfers=[];
  for(const [arg,src] of Object.entries(step.argumentsFrom??{})){
    const r=results.get(src.step);if(!r)return {status:'WAITING'};if(r.status!=='AVAILABLE')return {status:'BLOCKED',reason:'parameter producer '+src.step};
    const v=field(r.evidence,src.path);
    if(src.type==='address'){query.arguments[arg]=inRange(v,scope.target);}
    else if(src.type==='integer'){if(!Number.isSafeInteger(v))throw new Error('Expected integer');query.arguments[arg]=v;}
    else {if(typeof v!==src.type)throw new Error('Parameter type mismatch');query.arguments[arg]=v;}
    transfers.push({argument:arg,producer:src.step,sourcePath:src.path,value:query.arguments[arg],space:src.space??null});
  }
  for(const key of ['procedure','address'])if(query.arguments[key]!==undefined)inRange(query.arguments[key],scope.target);
  if(query.name==='read_bytes'&&(!Number.isSafeInteger(query.arguments.length)||query.arguments.length<1||query.arguments.length>1048576))throw new Error('Materialized read length must be 1..1048576');
  if(query.name==='read_bytes'&&query.arguments.address!==undefined){const end=address(query.arguments.address)+query.arguments.length;
    if(!scope.target.ranges.some(r=>address(r.start)<=address(query.arguments.address)&&end<=address(r.endExclusive)))throw new Error('Read escapes selected interval');}
  return {status:'READY',query,transfers};
}
export function inspectResponse(raw,query,step,scope) {
  if(raw.isError===true)return {status:'REJECTED',reason:'tool error',toolError:raw.structuredContent??null};
  let evidence=raw.structuredContent;
  const text=raw.content?.filter(c=>c.type==='text').map(c=>c.text);
  if(text?.length===1){let parsed;try{parsed=JSON.parse(text[0]);}catch{}
    if(parsed&&evidence&&canonical(parsed)!==canonical(evidence))return {status:'NEEDS_ANALYSIS',reason:'text/structured content disagreement'};
    if(!evidence)evidence=parsed;}
  if(!evidence?.evidence_id||!evidence.subject?.digest?.sha256||!evidence.normalized_result)
    return {status:'NEEDS_ANALYSIS',reason:'unqualified Evidence shape'};
  if(evidence.subject.digest.sha256.toUpperCase()!==scope.target.sha256.toUpperCase())return {status:'INVALID',reason:'subject identity mismatch'};
  if(evidence.operation!==query.name)return {status:'INVALID',reason:'operation mismatch'};
  for(const [key,value] of Object.entries(query.arguments??{})){
    const returned=evidence.parameters?.[key];
    try{if(['address','procedure'].includes(key)?address(value)!==address(returned):canonical(value)!==canonical(returned))
      return {status:'INVALID',reason:'Evidence parameter mismatch: '+key};}
    catch{return {status:'INVALID',reason:'Unqualified Evidence parameter: '+key};}
  }
  if(Boolean(raw._meta?.synthetic||evidence.synthetic)!==Boolean(scope.synthetic))return {status:'INVALID',reason:'synthetic/real evidence kind mismatch'};
  if(!HASH.test(evidence.analysis_profile?.digest??''))return {status:'NEEDS_ANALYSIS',reason:'missing qualified analysis profile'};
  if(step.profileDigest&&evidence.analysis_profile?.digest!==step.profileDigest)return {status:'INVALID',reason:'profile mismatch'};
  // REA 6.7.0 procedure_assembly returns a plain assembly string. Its canonical
  // procedure binding is in parameters (already checked), not a fabricated object.
  const assemblyText=query.name==='procedure_assembly'&&typeof evidence.normalized_result==='string'&&evidence.normalized_result.trim().length>0;
  if(query.arguments?.procedure&&!assemblyText){try{if(address(query.arguments.procedure)!==address(evidence.normalized_result.procedure?.address))return {status:'INVALID',reason:'returned procedure mismatch'};}
    catch{return {status:'NEEDS_ANALYSIS',reason:'missing canonical returned procedure'};}}
  if(query.arguments?.address&&evidence.normalized_result.address!==undefined){try{if(address(query.arguments.address)!==address(evidence.normalized_result.address))return {status:'INVALID',reason:'returned address mismatch'};}
    catch{return {status:'INVALID',reason:'invalid returned address'};}}
  try{for(const c of step.checks??[]){const v=field(evidence,c.path);
    if(c.equals!==undefined&&canonical(v)!==canonical(c.equals))throw new Error('expected value differs');
    if(c.nonEmpty&&(!v||!v.length))throw new Error('required facet empty');}}
  catch(e){return {status:'INSUFFICIENT',reason:String(e)};}
  return {status:'AVAILABLE',evidence,evidenceId:evidence.evidence_id,profile:evidence.analysis_profile?.digest??null,
    limitations:evidence.limitations??[],relationshipCoverage:evidence.relationship_coverage??null,
    meaning:'Mechanical input qualification only; engine meaning needs interpretation'};
}
