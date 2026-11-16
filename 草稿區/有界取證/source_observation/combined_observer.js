// One window for unchanged source/reference payloads and three explicit owner boundaries.
// All generations and joins are conditional on the observed boundaries, never global lifetime proof.
'use strict';
function installCombinedObservation(p,profiles,sourcePayload,referencePayload) {
  const domains=['source','reference'],ready={},terminal={},closers={},listeners=[],owners=new Map(),loads=new Map(),apis=new Map(),boundaries=new Map();
  let armed=false,stopping=false,finished=false,setupTraffic=false,sequence=0,events=0,generation=0,boundarySerial=0,reason=null,detail;
  const key=(d,id)=>d+':'+id;
  function finish(){if(finished||domains.some(d=>!terminal[d]))return;finished=true;
    for(const l of listeners)l.detach();
    send({kind:'terminal',reason,detail,events,sequence,hashedBytes:terminal.source.hashedBytes??0,
      pending:[...domains.flatMap(d=>terminal[d].pending.map(id=>key(d,id))),...[...boundaries.keys()].map(id=>key('lifetime',id))],domains:terminal});
  }
  function stop(why,more){if(finished)return;if(!reason){reason=why;detail=more;}if(stopping){finish();return;}stopping=true;armed=false;
    for(const d of domains)if(!terminal[d]){
      if(closers[d])try{closers[d]();}catch(e){terminal[d]={reason:'close_error',detail:String(e),pending:['unknown']};}
      else terminal[d]={reason:'not_ready',pending:[]};
    }finish();
  }
  function emit(domain,event){if(finished)return;if(!armed){setupTraffic=true;return;}
    if(events>=p.maxEvents){stop('event_cap');return;}events++;send({...event,domain,sequence:++sequence});
  }
  function arm(){if(armed||stopping||domains.some(d=>!ready[d]||!closers[d]))return;
    if(setupTraffic){stop('setup_traffic');return;}armed=true;
    send({kind:'ready',pid:Process.id,mode:'combined_observation',domains:ready,observedBoundaryGenerationsOnly:true});
    setTimeout(()=>stop('window_end'),p.durationMs);
  }
  function sample(e) {
    const roles={resolver_leave:['entity','pathObserved'],callback_leave:['entity','resolverPathPreserved'],
      append_leave:['tableEntry','resolverPathPreserved'],material_append:['entity','appendObserved']},role=roles[e.kind];
    if(!role)return;const owner=e[role[0]];if(!owner||owner==='0x0')return;
    const state=owners.get(owner),link=state?.source;
    let why=!e[role[1]]?'reference_path_unproven':!state?.active?'owner_window_unknown':!link?'no_current_source':null;
    if(e.kind==='material_append'&&e.selection!=='table_value_preserved')why='reference_path_unproven';
    let texture=null,surface=null;
    if(!why){texture=ptr(owner).add(68).readPointer().toString();surface=ptr(owner).add(72).readPointer().toString();
      if(texture!==link.texture||surface!==link.surface||texture==='0x0'||surface==='0x0')why='resource_fields_changed';}
    emit('join',{kind:'source_reference_sample',referenceKind:e.kind,referenceId:e.id,owner,
      observedGeneration:state?.active?state.generation:null,resourceRevision:state?.revision??null,
      sourceLoaderId:why?null:link.loaderId,sourceApiId:why?null:link.apiId,inputSha256:why?null:link.inputSha256,
      texture,surface,parameterPathObserved:!why,reason:why??'observed_parameter_path',
      completeLifetimeCoverage:false,unobservedWritersExcluded:false,resourceContentProven:false,sharingProven:false});
  }
  function consume(domain,e){
    if(e.kind==='ready'){if(ready[domain]){stop('duplicate_ready');return;}ready[domain]=e;arm();return;}
    if(e.kind==='terminal'){terminal[domain]=e;if(!stopping)stop(e.reason,e.detail);else finish();return;}
    emit(domain,e);if(!armed)return;
    try{
      if(domain==='reference'){sample(e);return;}
      if(e.kind==='loader_enter'){
        const state=owners.get(e.owner);if(state){state.source=null;state.revision++;}
        loads.set(e.id,{owner:e.owner,generation:state?.active?state.generation:null,revision:state?.revision,api:[]});
      }else if(e.kind==='api_enter'){apis.set(e.id,e);loads.get(e.loaderId)?.api.push(e.id);}
      else if(e.kind==='api_leave'){const a=apis.get(e.id);if(a)apis.set(e.id,{...a,...e});}
      else if(e.kind==='loader_leave'){
        const load=loads.get(e.id),state=load&&owners.get(load.owner),a=load?.api.length===1?apis.get(load.api[0]):null;
        if(state?.active&&load.generation===state.generation&&load.revision===state.revision&&e.al!==0&&a?.parameterLink&&e.texture===a.returnedTexture)
          state.source={loaderId:e.id,apiId:a.id,inputSha256:a.inputSha256,texture:e.texture,surface:e.surface};
        if(load)for(const id of load.api)apis.delete(id);loads.delete(e.id);
      }
    }catch(error){stop('invalid_observation',String(error));}
  }
  try{
    const m=Process.mainModule,expected=p.modules.main;
    if(Process.id!==p.pid||Process.arch!=='ia32'||Process.pointerSize!==4||m.size!==expected.imageSize)throw Error('lifetime_target');
    for(const name of ['create','clear','destroy']){
      const point=p.lifetime[name],raw=Uint8Array.from(point.bytes.match(/../g),x=>parseInt(x,16)),view=new DataView(raw.buffer);
      for(const offset of point.relocations)view.setUint32(offset,(view.getUint32(offset,true)+m.base.toUInt32()-expected.imageBase)>>>0,true);
      const address=m.base.add(point.rva),actual=new Uint8Array(address.readByteArray(raw.length));
      if(raw.some((v,i)=>v!==actual[i]))throw Error('lifetime_bytes');
      listeners.push(Interceptor.attach(address,{
        onEnter(){if(stopping)return;if(!armed){setupTraffic=true;return;}try{
          const owner=this.context.ecx.toString();let state=owners.get(owner);
          if(name==='create'){state={generation:++generation,active:false,revision:0,source:null};owners.set(owner,state);}
          else if(state){state.source=null;state.revision++;if(name==='destroy')state.active=false;}
          this.boundary={id:++boundarySerial,owner,state};boundaries.set(this.boundary.id,this.boundary);
          emit('lifetime',{kind:'owner_boundary',action:name+'_enter',owner,generation:state?.generation??null,observedPathsOnly:true});
        }catch(e){stop('invalid_observation',String(e));}},
        onLeave(){if(!armed||!this.boundary)return;try{const {owner,state}=this.boundary;
          if(name==='create'&&state===owners.get(owner))state.active=true;
          boundaries.delete(this.boundary.id);
          emit('lifetime',{kind:'owner_boundary',action:name+'_leave',owner,generation:state?.generation??null,observedPathsOnly:true});
        }catch(e){stop('invalid_observation',String(e));}}
      }));
    }
    for(const [domain,payload] of [['source',sourcePayload],['reference',referencePayload]]){
      if(stopping)break;
      payload(profiles[domain],e=>consume(domain,e),(closer,ms)=>{
        if(ms!==p.durationMs||closers[domain])throw Error('window_registration');closers[domain]=closer;arm();return 0;});
    }
    arm();
  }catch(error){stop('preflight_rejected',String(error));}
}
