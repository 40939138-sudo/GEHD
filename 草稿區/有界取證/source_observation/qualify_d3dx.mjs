import fs from 'node:fs/promises';
import path from 'node:path';
import {spawn} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {run,bytesAtRva,peInfo,relocationOffsets,sha} from './run.mjs';
const [fixture,output,runtime,dll]=process.argv.slice(2);
if(![fixture,output,runtime,dll].every(x=>x&&path.isAbsolute(x))||/[^\x20-\x7e]/.test(output))throw Error('Absolute inputs and new ASCII output required');
await fs.mkdir(output);
const binary=await fs.readFile(fixture),apiBinary=await fs.readFile(dll);
const summary={fixture,fixtureSha256:sha(binary),api:dll,apiSha256:sha(apiBinary),cases:[],sources:{}};
const here=path.dirname(fileURLToPath(import.meta.url));
await fs.mkdir(path.join(output,'source-snapshot'));
for(const name of ['observer.js','run.mjs','d3dx_fixture.cpp','qualify_d3dx.mjs']) {
  const b=await fs.readFile(path.join(here,name));summary.sources[name]=sha(b);
  await fs.writeFile(path.join(output,'source-snapshot',name),b,{flag:'wx'});
}
const exists=p=>fs.access(p).then(()=>true,()=>false);
const wait=async test=>{const end=Date.now()+30000;while(Date.now()<end){if(await test())return;await new Promise(r=>setTimeout(r,25));}throw Error('Fixture timeout');};
const module=(p,b)=>({path:p,sha256:sha(b),...(({imageSize,imageBase})=>({imageSize,imageBase}))(peInfo(b))});
const point=(module,b,rva)=>({module,rva,bytes:bytesAtRva(b,rva,16).toString('hex'),relocations:relocationOffsets(b,rva,16)});
const normalize=p=>'0x'+BigInt(p).toString(16);
for(const scenario of ['baseline','observed']) {
  const dir=path.join(output,scenario);await fs.mkdir(dir);
  const child=spawn(fixture,[dir],{windowsHide:true,stdio:'ignore'});let exit;
  child.on('exit',(code,signal)=>exit={code,signal});child.on('error',error=>exit={error:String(error)});
  const item={scenario,pid:child.pid,checks:{}};
  try {
    await wait(async()=>await exists(path.join(dir,'bindings.json'))||exit!==undefined);
    if(exit!==undefined)throw Error('Fixture exited before binding: '+JSON.stringify(exit));
    const b=JSON.parse(await fs.readFile(path.join(dir,'bindings.json'),'utf8'));
    if(scenario==='baseline')await fs.writeFile(path.join(dir,'go1'),'');
    else {
      const p={schema:'GEHD_SOURCE_PARAMETER_OBSERVER_V1',pid:child.pid,durationMs:3000,maxEvents:100,maxBufferBytes:1024,maxHashBytes:16384,
        modules:{main:module(fixture,binary),api:{...module(dll,apiBinary),livePath:path.join(process.env.SystemRoot,'System32','d3dx9_43.dll')}},loader:point('main',binary,b.loader),api:point('api',apiBinary,b.api),
        caller:{module:'main',returnRva:b.caller,bytes:bytesAtRva(binary,b.caller-8,8).toString('hex'),relocations:relocationOffsets(binary,b.caller-8,8)}};
      const r=await run(p,path.join(dir,'observation'),runtime,()=>fs.writeFile(path.join(dir,'go1'),''));
      item.captureStatus=r.status;item.terminal=r.terminal;
      item.checks.complete=r.captureComplete===true;
      item.checks.detached=r.unloaded===true&&r.detached===true&&r.clientClosed===true&&exit===undefined;
      item.checks.modulesUnchanged=r.modulesUnchanged===true;
      item.checks.redirectedApiFileVerified=r.initial.some(m=>m.type==='send'&&m.payload.kind==='ready'&&m.payload.moduleBindings.some(b=>b.key==='api'&&b.redirectedFileHashChecked===true));
      if(!r.payloads)throw Error('Capture failed: '+r.error);
      await wait(()=>exists(path.join(dir,'first.json')));
      const native=JSON.parse(await fs.readFile(path.join(dir,'first.json'),'utf8'));
      const enters=r.payloads.filter(x=>x.kind==='api_enter'),leaves=r.payloads.filter(x=>x.kind==='api_leave');
      const loaders=r.payloads.filter(x=>x.kind==='loader_enter'),ends=r.payloads.filter(x=>x.kind==='loader_leave');
      item.checks.fourPaired=enters.length===4&&leaves.length===4&&loaders.length===4&&ends.length===4;
      const a=await fs.readFile(path.join(dir,'a.dds')),bb=await fs.readFile(path.join(dir,'b.dds')),bad=Buffer.from(a);bad[0]=88;
      const hashes=[sha(a),sha(a),sha(bb),sha(bad)];
      item.checks.nativeWitness=native.records.every((n,i)=>{
        const e=enters[i],l=leaves.find(x=>x.id===e?.id),end=ends.find(x=>x.id===e?.loaderId);
        return e&&l&&end&&e.owner===normalize(n.owner)&&e.source===normalize(n.source)&&e.device===normalize(b.device)&&e.size===212&&
          e.inputSha256===hashes[i]&&BigInt(e.outputSlot)===BigInt(n.owner)+68n&&l.hresult===n.hr&&end.al===(n.result&255)&&
          (n.hr<0?l.returnedTexture===null&&!l.parameterLink&&end.texture===undefined:
            l.parameterLink&&l.returnedTexture===normalize(n.texture)&&end.texture===normalize(n.texture)&&end.surface===normalize(n.surface));
      });
      item.checks.parameters=enters.every(e=>JSON.stringify(e.parameters)===JSON.stringify([0xfffffffe,0xfffffffe,0xfffffffd,0,0,1,1,0xffffffff,0]));
      item.checks.noClaims=leaves.every(l=>!l.inputBytesConsumedProven&&!l.resourceContentProven&&!l.sharingProven)&&loaders.every(l=>l.lifetimeGeneration===null);
      item.checks.sameInputDifferentResources=enters[0]?.inputSha256===enters[1]?.inputSha256&&leaves[0]?.returnedTexture!==leaves[1]?.returnedTexture;
    }
    await wait(()=>exists(path.join(dir,'first.json')));
  }catch(error){item.error=String(error.stack??error);}
  finally {
    await fs.writeFile(path.join(dir,'go1'),'');await fs.writeFile(path.join(dir,'go2'),'');
    try{await wait(()=>Promise.resolve(exit!==undefined));}catch(error){child.kill();item.error??=String(error);try{await wait(()=>Promise.resolve(exit!==undefined));}catch(e){item.cleanupError=String(e);}}
    item.exit=exit;item.checks.normalExit=exit?.code===0;
    try {
      const first=JSON.parse(await fs.readFile(path.join(dir,'first.json'),'utf8')),after=JSON.parse(await fs.readFile(path.join(dir,'after.json'),'utf8'));
      item.checks.nativeContentAndCom=first.ok===true&&after.ok===true&&first.separateLiveResources&&after.separateLiveResources;
      const closed=JSON.parse(await fs.readFile(path.join(dir,'exit.json'),'utf8'));item.checks.deviceReleased=closed.code===0&&closed.deviceRemainingRefs===0;
      item.rawHashes={};
      for(const phase of ['first','after'])for(const name of ['a','same','b']) {
        const raw=await fs.readFile(path.join(dir,phase+'-'+name+'.raw'));const expected=Buffer.alloc(84);
        for(let i=0;i<84;i+=4)expected.writeUInt32LE(name==='b'?0xffa1b2c3:0xff123456,i);
        item.checks[phase+'_'+name+'_bytes']=raw.equals(expected);item.rawHashes[phase+'_'+name]=sha(raw);
      }
      if(scenario==='observed')item.checks.matchesBaseline=JSON.stringify(item.rawHashes)===JSON.stringify(summary.cases[0].rawHashes);
    }catch(error){item.error??=String(error);}
    item.checks.inputBinariesUnchanged=sha(await fs.readFile(fixture))===summary.fixtureSha256&&sha(await fs.readFile(dll))===summary.apiSha256;
    item.status=!item.error&&Object.values(item.checks).every(x=>x===true)?'PASS':'FAIL';summary.cases.push(item);
    await fs.writeFile(path.join(dir,'qualification.json'),JSON.stringify(item,null,2)+'\n',{flag:'wx'});
    console.log(JSON.stringify(item));
  }
  if(item.status!=='PASS')break;
}
summary.status=summary.cases.length===2&&summary.cases.every(x=>x.status==='PASS')?'PASS':'FAIL';
summary.limits=['Real D3DX on self-owned 4x4 A8R8G8B8 DDS; no GE, BC3, draw or pixel qualification.',
  'Native host verifies its own Surface container, contents and release; observer does not establish resource generations or COM lifetime.',
  'No resolver/cache/table/material witness; endpoint hash equality does not exclude intermediate writers.'];
await fs.writeFile(path.join(output,'summary.json'),JSON.stringify(summary,null,2)+'\n',{flag:'wx'});
if(summary.status!=='PASS')process.exitCode=1;
