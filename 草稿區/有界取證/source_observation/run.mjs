import fs from 'node:fs/promises';
import path from 'node:path';
import {fileURLToPath, pathToFileURL} from 'node:url';
import {createHash} from 'node:crypto';
const here = path.dirname(fileURLToPath(import.meta.url));
export const resolverHookKeys=['resolver','match','holder','copy','assign','slot','fallback','release','nameAppend'];
export const resolverCallerKeys=['bridgeToResolver','previousRecordMatch','datasetMatch','matchToHolder','holderSlot','holderCopy',
  'previousRecordAdopt','datasetAdopt','fallbackCreate','fallbackAdopt','cacheObjectAppend','cacheNameAppend','freshReturnCopy','freshLocalRelease','cacheSlot','cacheReturnCopy','unsupportedReturnCopy'];
export function combinedProfiles(p) {
  const shared={pid:p.pid,durationMs:p.durationMs,maxEvents:p.maxEvents,maxBufferBytes:p.maxBufferBytes,maxHashBytes:p.maxHashBytes,maxListItems:p.maxListItems};
  return {source:{...p.source,...shared,schema:'GEHD_SOURCE_PARAMETER_OBSERVER_V1',modules:p.modules},
    reference:{...p.reference,...shared,schema:'GEHD_OBJECT_HANDOFF_V1',modules:{main:p.modules?.main}}};
}
export const sha = b => createHash('sha256').update(b).digest('hex').toUpperCase();
const samePath=(a,b)=>path.resolve(a).toLowerCase()===path.resolve(b).toLowerCase();
const within=(a,b)=>{const r=path.relative(b,a);return r===''||(!r.startsWith('..'+path.sep)&&r!=='..'&&!path.isAbsolute(r));};
export function peInfo(b) {
  const nt=b.readUInt32LE(0x3c), opt=nt+24;
  if (b.toString('ascii',0,2)!=='MZ' || b.readUInt32LE(nt)!==0x4550 ||
      b.readUInt16LE(nt+4)!==0x14c || b.readUInt16LE(opt)!==0x10b) throw Error('x86 PE32 required');
  return {nt,opt,imageSize:b.readUInt32LE(opt+56),imageBase:b.readUInt32LE(opt+28)};
}
export function bytesAtRva(b,rva,size) {
  const {nt,opt}=peInfo(b), start=opt+b.readUInt16LE(nt+20), matches=[];
  if (!Number.isSafeInteger(rva)||!Number.isSafeInteger(size)||rva<0||size<1) throw Error('RVA bounds');
  for(let i=0;i<b.readUInt16LE(nt+6);i++) {
    const s=start+i*40, va=b.readUInt32LE(s+12), length=Math.min(b.readUInt32LE(s+8),b.readUInt32LE(s+16));
    if(rva>=va&&rva+size<=va+length) matches.push(b.readUInt32LE(s+20)+rva-va);
  }
  if(matches.length!==1||matches[0]+size>b.length) throw Error('RVA mapping');
  return b.subarray(matches[0],matches[0]+size);
}
export function relocationOffsets(b,rva,size) {
  const {opt}=peInfo(b), tableRva=b.readUInt32LE(opt+136), length=b.readUInt32LE(opt+140), out=[];
  if(!tableRva&&!length)return out;
  const table=bytesAtRva(b,tableRva,length);
  for(let at=0;at<table.length;) {
    if(at+8>table.length)throw Error('Relocation header');
    const page=table.readUInt32LE(at), block=table.readUInt32LE(at+4);
    if(block<8||block%2||at+block>table.length)throw Error('Relocation block');
    for(let i=at+8;i<at+block;i+=2) {
      const entry=table.readUInt16LE(i),type=entry>>>12,address=page+(entry&4095);
      if(!type)continue;
      if(address<rva+size&&address+4>rva) {
        if(type!==3||address<rva||address+4>rva+size)throw Error('Unsupported or partial relocation');
        if(out.includes(address-rva))throw Error('Duplicate relocation');
        out.push(address-rva);
      }
    }
    at+=block;
  }
  return out.sort((a,b)=>a-b);
}
export async function validate(p) {
  if(p.schema==='GEHD_COMBINED_OBSERVER_V1') {
    if(!p.source||!p.reference||!p.lifetime)throw Error('Combined profile sections');
    const branches=combinedProfiles(p);await validate(branches.source);await validate(branches.reference);
    await validate({...branches.reference,reader:p.lifetime.create,callback:p.lifetime.clear,append:p.lifetime.destroy});
    const r=branches.reference,hooks=[p.source.loader,p.source.api,r.reader,r.callback,r.append,...Object.values(p.lifetime)];
    if(r.resolver)hooks.push(...resolverHookKeys.map(k=>r.resolver[k]));
    if(r.materials)hooks.push(...['fixed','user','readIndex','lookup','append'].map(k=>r.materials[k]));
    if(Object.keys(p.lifetime).sort().join(',')!=='clear,create,destroy'||Object.values(p.lifetime).some(h=>h.module!=='main'))throw Error('Lifetime binding schema');
    for(let i=0;i<hooks.length;i++)for(let j=i+1;j<hooks.length;j++)if(hooks[i].module===hooks[j].module&&Math.abs(hooks[i].rva-hooks[j].rva)<32)throw Error('Overlapping combined hooks');
    return;
  }
  const handoff=p.schema==='GEHD_OBJECT_HANDOFF_V1';
  if((!handoff&&p.schema!=='GEHD_SOURCE_PARAMETER_OBSERVER_V1')||!Number.isSafeInteger(p.pid)||p.pid<1) throw Error('Profile schema/PID');
  for(const [key,max] of Object.entries({durationMs:30000,maxEvents:2000,maxBufferBytes:2097152,maxHashBytes:67108864}))
    if(!Number.isSafeInteger(p[key])||p[key]<1||p[key]>max) throw Error('Bound '+key);
  if(!p.modules?.main||Object.keys(p.modules).length>2) throw Error('Module set');
  if(handoff&&(Object.keys(p.modules).length!==1||!Number.isInteger(p.maxListItems)||p.maxListItems<1||p.maxListItems>4096))throw Error('Handoff bounds/modules');
  const images={};
  for(const [key,m] of Object.entries(p.modules)) {
    if(!path.isAbsolute(m.path)||! /^[A-F0-9]{64}$/.test(m.sha256)) throw Error('Module identity');
    if(!samePath(await fs.realpath(m.path),m.path))throw Error('Module alias/reparse path');
    if(m.livePath!==undefined) {
      // Only the observed x86 system D3DX redirection is admitted; never arbitrary path aliases.
      const windows=process.env.SystemRoot;
      if(process.platform!=='win32'||!windows||key!=='api'||
        !samePath(m.path,path.join(windows,'SysWOW64','d3dx9_43.dll'))||
        !samePath(m.livePath,path.join(windows,'System32','d3dx9_43.dll')))throw Error('Unqualified live module alias');
    }
    const b=await fs.readFile(m.path);
    if(sha(b)!==m.sha256||peInfo(b).imageSize!==m.imageSize||peInfo(b).imageBase!==m.imageBase) throw Error('Module identity mismatch');
    images[key]=b;
  }
  const hooks=handoff?[p.reader,p.callback,p.append]:[p.loader,p.api];
  if(p.materials!==undefined&&!handoff)throw Error('Material scope schema');
  if(p.resolver!==undefined&&!handoff)throw Error('Resolver scope schema');
  if(p.resolver)hooks.push(...resolverHookKeys.map(k=>p.resolver[k]));
  if(p.materials)hooks.push(p.materials.fixed,p.materials.user,p.materials.readIndex,p.materials.lookup,p.materials.append);
  for(const h of hooks) {
    if(!h||!images[h.module]||! /^(?:[a-f0-9]{2}){8,32}$/i.test(h.bytes)) throw Error('Hook binding');
    if(bytesAtRva(images[h.module],h.rva,h.bytes.length/2).toString('hex')!==h.bytes.toLowerCase()) throw Error('Hook bytes mismatch');
    if(JSON.stringify(h.relocations)!==JSON.stringify(relocationOffsets(images[h.module],h.rva,h.bytes.length/2))) throw Error('Hook relocations mismatch');
  }
  const callers=handoff?[p.callbackCaller,p.appendCaller]:[p.caller];
  if(p.resolver)callers.push(...resolverCallerKeys.map(k=>p.resolver[k]));
  if(p.materials)for(const kind of ['fixed','user'])callers.push(p.materials[kind+'ReadCaller'],p.materials[kind+'LookupCaller'],p.materials[kind+'AppendCaller']);
  for(const c of callers) {
  if(!c||!images[c.module]||!Number.isSafeInteger(c.returnRva)||! /^(?:[a-f0-9]{2}){5,32}$/i.test(c.bytes)) throw Error('Caller binding');
  if(bytesAtRva(images[c.module],c.returnRva-c.bytes.length/2,c.bytes.length/2).toString('hex')!==c.bytes.toLowerCase()) throw Error('Caller bytes mismatch');
  if(JSON.stringify(c.relocations)!==JSON.stringify(relocationOffsets(images[c.module],c.returnRva-c.bytes.length/2,c.bytes.length/2))) throw Error('Caller relocations mismatch');
  }
  for(let i=0;i<hooks.length;i++)for(let j=i+1;j<hooks.length;j++)
    if(hooks[i].module===hooks[j].module&&Math.abs(hooks[i].rva-hooks[j].rva)<32) throw Error('Overlapping hooks');
}
export async function run(p, output, runtime, onReady) {
  if(!path.isAbsolute(output)||!path.isAbsolute(runtime)) throw Error('Absolute output/runtime required');
  await validate(p);
  if(!samePath(await fs.realpath(path.dirname(output)),path.dirname(output)))throw Error('Output parent alias/reparse path');
  for(const inputRoot of [here,runtime,...Object.values(p.modules).map(m=>path.dirname(m.path))])
    if(within(output,inputRoot)||within(inputRoot,output))throw Error('Output overlaps input/code/runtime');
  // mkdir without recursive/reuse: never overwrite an earlier activity.
  await fs.mkdir(output);
  const save=(name,data)=>fs.writeFile(path.join(output,name),JSON.stringify(data,null,2)+'\n',{flag:'wx'});
  let template;
  if(p.schema==='GEHD_COMBINED_OBSERVER_V1') {
    const source=await fs.readFile(path.join(here,'observer.js'),'utf8');
    const reference=(p.reference.resolver?await fs.readFile(path.join(here,'resolver_observer.js'),'utf8')+'\n':'')+await fs.readFile(path.join(here,'handoff_observer.js'),'utf8');
    template=await fs.readFile(path.join(here,'combined_observer.js'),'utf8')+'\ninstallCombinedObservation(SOURCE_PROFILE,'+JSON.stringify(combinedProfiles(p))+',\n'+
      '(SOURCE_PROFILE,send,setTimeout)=>{\n'+source+'\n},\n(SOURCE_PROFILE,send,setTimeout)=>{\n'+reference+'\n});\n';
  }else template=(p.resolver?await fs.readFile(path.join(here,'resolver_observer.js'),'utf8')+'\n':'')+
    await fs.readFile(path.join(here,p.schema==='GEHD_OBJECT_HANDOFF_V1'?'handoff_observer.js':'observer.js'),'utf8');
  const source='const SOURCE_PROFILE = '+JSON.stringify(p)+';\n'+template;
  const entry=path.join(runtime,'node_modules/rea-agents/scripts/rea.mjs');
  const clientFile=path.join(runtime,'node_modules/@modelcontextprotocol/client/dist/index.mjs');
  const transportFile=path.join(runtime,'node_modules/@modelcontextprotocol/client/dist/stdio.mjs');
  const result={schema:p.schema,started:new Date().toISOString(),mode:'observe_only',calls:[],
    inputs:{profile:sha(Buffer.from(JSON.stringify(p))),observer:sha(Buffer.from(template)),
      runner:sha(await fs.readFile(fileURLToPath(import.meta.url))),rea:sha(await fs.readFile(entry)),
      client:sha(await fs.readFile(clientFile)),transport:sha(await fs.readFile(transportFile)),
      node:sha(await fs.readFile(process.execPath))}};
  await save('profile.json',p); await fs.writeFile(path.join(output,'observer.js'),source,{flag:'wx'});
  const {Client}=await import(pathToFileURL(clientFile));
  const {StdioClientTransport}=await import(pathToFileURL(transportFile));
  const transport=new StdioClientTransport({command:process.execPath,args:[entry,'mcp'],cwd:runtime,
    env:{...process.env,TEMP:output,TMP:output},stderr:'pipe'});
  let stderr='',session,script,connected=false,failure;
  transport.stderr?.on('data',b=>stderr+=b);
  const client=new Client({name:'ge-source-parameter-observation',version:'1.0.0'});
  async function call(name,args) {
    const n=String(result.calls.length).padStart(3,'0');
    await save(n+'-request.json',{name,arguments:args});
    const response=await client.callTool({name,arguments:args},{timeout:30000});
    await save(n+'-'+name+'.json',response);result.calls.push({name,isError:response.isError===true});
    if(response.isError) throw Error('REA rejected '+name);
    return JSON.parse(response.content.find(c=>c.type==='text').text);
  }
  try {
    await client.connect(transport); connected=true;
    session=(await call('start_frida_session',{mode:'attach',device_id:'local',pid:p.pid})).session_id;
    const loaded=await call('load_frida_script',{session_id:session,source_kind:'file',path:path.join(output,'observer.js')});
    script=loaded.normalized_result?.script_id;
    result.initial=loaded.normalized_result?.messages??[];
    if(!result.initial.some(m=>m.type==='send'&&m.payload?.kind==='ready')) {
      result.terminal=result.initial.find(m=>m.type==='send'&&m.payload?.kind==='terminal')?.payload;
      throw Error('Observer not ready: '+JSON.stringify(result.terminal));
    }
    if(onReady) await onReady();
    await new Promise(resolve=>setTimeout(resolve,p.durationMs+300));
    const status=await call('frida_session_status',{session_id:session});
    result.observation=status;
    const v=status.normalized_result;
    result.payloads=(v?.messages??[]).filter(m=>m.type==='send').map(m=>m.payload);
    result.terminal=result.payloads.find(m=>m.kind==='terminal');
    result.captureComplete=v?.messages_truncated===false&&!(v?.messages??[]).some(m=>m.type==='error')&&
      result.terminal?.reason==='window_end'&&result.terminal.pending.length===0;
    if(!result.captureComplete) failure='Capture incomplete';
  } catch(error) {failure=String(error.stack??error);}
  finally {
    if(connected&&session) {
      if(script) try {await call('unload_frida_script',{session_id:session,script_id:script});result.unloaded=true;}
        catch(error){failure??=String(error);}
      try {await call('close_frida_session',{session_id:session});result.detached=true;}catch(error){failure??=String(error);}
    }
    try{await client.close();result.clientClosed=true;}catch(error){failure??=String(error);}
    result.modulesUnchanged=true;
    for(const m of Object.values(p.modules)) if(sha(await fs.readFile(m.path))!==m.sha256) result.modulesUnchanged=false;
    if(!result.modulesUnchanged) failure??='Module changed';
    result.status=failure?'STOPPED':'COMPLETE';result.error=failure;result.finished=new Date().toISOString();
    await fs.writeFile(path.join(output,'server.stderr.log'),stderr,{flag:'wx'});await save('result.json',result);
  }
  return result;
}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url)) {
  const [profile,output,runtime]=process.argv.slice(2);
  const r=await run(JSON.parse(await fs.readFile(profile,'utf8')),output,runtime);
  console.log(JSON.stringify({status:r.status,error:r.error,output}));if(r.status!=='COMPLETE')process.exitCode=1;
}
