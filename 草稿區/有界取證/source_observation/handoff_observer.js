// Fixed x86 reader/callback/list handoff. No entity dereference, lifetime or source inference.
'use strict';
(() => {
  const p=SOURCE_PROFILE,stacks=new Map(),materialStacks=new Map(),callbackStacks=new Map(),listeners=[];
  let resolverHooks;
  let serial=0,events=0,stopped=false,armed=false,materialAppendEnter,materialAppendLeave;
  const norm=s=>s.replace(/\\/g,'/').toLowerCase();
  const hex=b=>Array.from(new Uint8Array(b),x=>x.toString(16).padStart(2,'0')).join('');
  function stop(reason,detail){if(stopped)return;stopped=true;send({kind:'terminal',reason,detail,events,
    pending:[...[...stacks.values(),...materialStacks.values(),...callbackStacks.values()].flat().map(x=>x.id),...(resolverHooks?.pending()??[])]});for(const l of listeners)l.detach();}
  function emit(e){if(stopped||!armed)return;if(events>=p.maxEvents){stop('event_cap');return;}events++;send(e);}
  const top=thread=>stacks.get(thread)?.at(-1);
  function listAt(owner){const count=owner.add(12).readU32(),capacity=owner.add(16).readU32();
    if(count>capacity||capacity>p.maxListItems)throw Error('list_bound');
    return {owner,data:owner.add(8).readPointer(),count,capacity};}
  const list=wrapper=>listAt(wrapper.readPointer());
  try {
    if(Process.id!==p.pid||Process.arch!=='ia32'||Process.pointerSize!==4||norm(Process.mainModule.path)!==norm(p.modules.main.path))throw Error('target_identity');
    const m=Process.mainModule,expected=p.modules.main;if(m.size!==expected.imageSize)throw Error('module_identity');
    function bytes(point,rva){const raw=Uint8Array.from(point.bytes.match(/../g),x=>parseInt(x,16)),v=new DataView(raw.buffer);
      for(const offset of point.relocations)v.setUint32(offset,(v.getUint32(offset,true)+m.base.toUInt32()-expected.imageBase)>>>0,true);
      const at=m.base.add(rva);if(hex(at.readByteArray(raw.length))!==hex(raw.buffer))throw Error('entry_bytes');return at;}
    const reader=bytes(p.reader,p.reader.rva),callback=bytes(p.callback,p.callback.rva),append=bytes(p.append,p.append.rva);
    for(const c of [p.callbackCaller,p.appendCaller])bytes(c,c.returnRva-c.bytes.length/2);
    const callbackCaller=m.base.add(p.callbackCaller.returnRva),appendCaller=m.base.add(p.appendCaller.returnRva);
    listeners.push(Interceptor.attach(reader,{
      onEnter(args){if(stopped||!armed)return;try{const stack=stacks.get(this.threadId)??[];
        this.scope={id:++serial,parent:stack.at(-1)?.id??null,table:args[0],callback:args[3],pending:null};
        stack.push(this.scope);stacks.set(this.threadId,stack);
        emit({kind:'reader_enter',id:this.scope.id,parent:this.scope.parent,thread:this.threadId,table:args[0].toString(),callback:args[3].toString(),lifetimeGeneration:null});
      }catch(e){stop('invalid_observation',String(e));}},
      onLeave(retval){if(stopped||!armed||!this.scope)return;try{if(top(this.threadId)!==this.scope)throw Error('reader_pairing');
        stacks.get(this.threadId).pop();emit({kind:'reader_leave',id:this.scope.id,thread:this.threadId,al:retval.toUInt32()&255,
          unmatchedCallback:this.scope.pending?.id??null,allEntriesProven:false});
      }catch(e){stop('invalid_observation',String(e));}}
    }));
    listeners.push(Interceptor.attach(callback,{
      onEnter(args){if(stopped||!armed)return;try{const scope=top(this.threadId);
        if(!scope||!this.returnAddress.equals(callbackCaller)||!scope.callback.equals(callback)){
          emit({kind:'callback_rejected',thread:this.threadId,reason:!scope?'no_reader_scope':'caller_or_target'});return;}
        if(scope.pending)throw Error('unmatched_callback');
        this.call={id:++serial,scope,out:args[0],context:args[1],name:args[2]};
        const callbacks=callbackStacks.get(this.threadId)??[];callbacks.push(this.call);callbackStacks.set(this.threadId,callbacks);
        emit({kind:'callback_enter',id:this.call.id,readerId:scope.id,thread:this.threadId,outputHandle:args[0].toString(),contextHandle:args[1].toString(),nameObject:args[2].toString()});
      }catch(e){stop('invalid_observation',String(e));}},
      onLeave(retval){if(stopped||!armed||!this.call)return;try{const c=this.call;
        if(top(this.threadId)!==c.scope||callbackStacks.get(this.threadId)?.at(-1)!==c)throw Error('callback_pairing');
        callbackStacks.get(this.threadId).pop();
        const exact=retval.equals(c.out);c.entity=exact?c.out.readPointer():null;c.exact=exact;c.scope.pending=c;
        c.resolverPreserved=!!(exact&&c.resolved?.valid&&c.entity.equals(c.resolved.entity));
        emit({kind:'callback_leave',id:c.id,readerId:c.scope.id,thread:this.threadId,returnedHandle:retval.toString(),
          expectedOutputHandle:exact,entity:c.entity?.toString()??null,candidateProven:false,
          resolverId:c.resolved?.id??null,resolverBranch:c.resolved?.branch??null,resolverPathPreserved:c.resolverPreserved});
      }catch(e){stop('invalid_observation',String(e));}}
    }));
    listeners.push(Interceptor.attach(append,{
      onEnter(args){if(stopped||!armed)return;try{if(resolverHooks?.appendEnter(this,args)||materialAppendEnter?.(this,args,true))return;const scope=top(this.threadId);
        if(!scope||!this.returnAddress.equals(appendCaller)||!this.context.ecx.equals(scope.table)){
          emit({kind:'append_rejected',thread:this.threadId,reason:!scope?'no_reader_scope':'caller_or_table'});return;}
        const before=list(scope.table),entity=args[0].readPointer();
        this.call={id:++serial,scope,before,entity,callback:scope.pending};scope.pending=null;
        emit({kind:'append_enter',id:this.call.id,readerId:scope.id,thread:this.threadId,table:scope.table.toString(),list:before.owner.toString(),
          index:before.count,entity:entity.toString(),callbackId:this.call.callback?.id??null});
      }catch(e){stop('invalid_observation',String(e));}},
      onLeave(){if(stopped||!armed)return;if(this.resolverAppend){resolverHooks.appendLeave(this);return;}if(this.materialCall){materialAppendLeave(this);return;}if(!this.call)return;try{const c=this.call;if(top(this.threadId)!==c.scope)throw Error('append_pairing');
        const after=list(c.scope.table),layout=after.owner.equals(c.before.owner)&&after.count===c.before.count+1;
        const item=layout?after.data.add(c.before.count*4).readPointer():null;
        const same=layout&&item.equals(c.entity),cb=c.callback;
        const relation=!cb?(c.scope.callback.isNull()?'callback_absent':'callback_unobserved'):!cb.exact?'callback_output_mismatch':
          cb.entity.isNull()?'empty_callback_then_append':cb.entity.equals(c.entity)?'callback_entity_preserved':'entity_changed_after_callback';
        emit({kind:'append_leave',id:c.id,readerId:c.scope.id,thread:this.threadId,index:c.before.count,countAfter:after.count,
          tableEntry:item?.toString()??null,appendObserved:same,relation,sourceProven:false,lifetimeGeneration:null,
          resolverId:cb?.resolved?.id??null,resolverBranch:cb?.resolved?.branch??null,
          resolverPathPreserved:!!(same&&cb?.resolverPreserved&&cb.entity.equals(c.entity)),
          sharingProven:false,materialUseProven:false,atomicSnapshot:false});
      }catch(e){stop('invalid_observation',String(e));}}
    }));
    if(p.materials) {
      const spec=p.materials,points={},callers={};
      for(const k of ['fixed','user','readIndex','lookup','append'])points[k]=bytes(spec[k],spec[k].rva);
      for(const kind of ['fixed','user'])for(const role of ['Read','Lookup','Append']) {
        const k=kind+role+'Caller',c=spec[k];bytes(c,c.returnRva-c.bytes.length/2);callers[k]=m.base.add(c.returnRva);
      }
      const current=thread=>materialStacks.get(thread)?.at(-1);
      const selected=(inv,role)=>{const s=current(inv.threadId);return s&&inv.returnAddress.equals(callers[s.kind+role+'Caller'])?s:null;};
      const snapshot=pointer=>{const l=listAt(pointer),values=[];for(let i=0;i<l.count;i++)values.push(l.data.add(i*4).readPointer().toString());return {list:l.owner.toString(),values};};
      for(const kind of ['fixed','user'])listeners.push(Interceptor.attach(points[kind],{
        onEnter(args){if(stopped||!armed)return;try{const stack=materialStacks.get(this.threadId)??[];
          this.scope={id:++serial,kind,owner:this.context.ecx,table:args[1],read:null,choice:null,appended:[]};stack.push(this.scope);materialStacks.set(this.threadId,stack);
          emit({kind:'material_enter',id:this.scope.id,materialKind:kind,thread:this.threadId,owner:this.scope.owner.toString(),table:args[1].toString()});
        }catch(e){stop('invalid_observation',String(e));}},
        onLeave(){if(stopped||!armed||!this.scope)return;try{const s=this.scope;if(current(this.threadId)!==s)throw Error('material_pairing');
          const final=snapshot(s.owner.add(s.kind==='fixed'?0x84:0x64).readPointer());
          const matches=JSON.stringify(final.values)===JSON.stringify(s.appended.map(a=>a.entity));
          materialStacks.get(this.threadId).pop();emit({kind:'material_leave',id:s.id,thread:this.threadId,finalList:final.list,entities:final.values,
            appendSequenceMatches:matches,allEntriesProven:false,shaderUseProven:false,sharingProven:false,lifetimeGeneration:null,atomicSnapshot:false});
        }catch(e){stop('invalid_observation',String(e));}}
      }));
      listeners.push(Interceptor.attach(points.readIndex,{
        onEnter(args){if(stopped||!armed)return;const s=selected(this,'Read');if(s)this.call={id:++serial,scope:s,out:args[0]};},
        onLeave(retval){if(stopped||!armed||!this.call)return;try{const c=this.call;if(current(this.threadId)!==c.scope)throw Error('material_read_pairing');
          c.index=c.out.readU16();c.readBytes=retval.toUInt32();c.scope.read=c;c.scope.choice=null;
          emit({kind:'material_index',id:c.id,materialId:c.scope.id,thread:this.threadId,index:c.index,readBytes:c.readBytes,fullRead:c.readBytes===2});
        }catch(e){stop('invalid_observation',String(e));}}
      }));
      listeners.push(Interceptor.attach(points.lookup,{
        onEnter(args){if(stopped||!armed)return;try{const s=selected(this,'Lookup');if(!s)return;
          const index=args[0].toUInt32(),table=this.context.ecx,read=s.read;this.call={id:++serial,scope:s,index,table,valid:false};
          if(!table.equals(s.table)||!read||read.readBytes!==2||read.index!==index||index===0xffff)return;
          const l=list(table);if(index>=l.count)return;this.call.valid=true;this.call.list=l;this.call.slot=l.data.add(index*4);
        }catch(e){stop('invalid_observation',String(e));}},
        onLeave(retval){if(stopped||!armed||!this.call)return;try{const c=this.call;if(current(this.threadId)!==c.scope)throw Error('material_lookup_pairing');
          let valid=c.valid&&retval.equals(c.slot),entity=null;
          if(valid){const after=list(c.table);valid=after.owner.equals(c.list.owner)&&after.data.equals(c.list.data)&&after.count===c.list.count;if(valid)entity=retval.readPointer();}
          c.scope.choice={id:c.id,valid,entity};
          emit({kind:'material_lookup',id:c.id,materialId:c.scope.id,thread:this.threadId,index:c.index,table:c.table.toString(),returnedSlot:retval.toString(),
            boundedSlotObserved:valid,entity:entity?.toString()??null,atomicSnapshot:false});
        }catch(e){stop('invalid_observation',String(e));}}
      }));
      materialAppendEnter=(inv,args,wrapper)=>{
        const s=selected(inv,'Append');if(!s||s.kind!==(wrapper?'user':'fixed'))return false;
        inv.materialCall={id:++serial,scope:s,before:listAt(wrapper?inv.context.ecx.readPointer():inv.context.ecx),entity:args[0].readPointer(),read:s.read,choice:s.choice};s.read=null;s.choice=null;return true;
      };
      materialAppendLeave=inv=>{try{const c=inv.materialCall,s=c.scope;if(current(inv.threadId)!==s)throw Error('material_append_pairing');
          const after=listAt(c.before.owner),valid=after.count===c.before.count+1;
          const entity=valid?after.data.add(c.before.count*4).readPointer():null,appended=valid&&entity.equals(c.entity);
          const selection=c.read?.readBytes!==2?'index_incomplete_or_unobserved':c.read.index===0xffff?'sentinel_no_table_selection':
            !c.choice?.valid?'lookup_not_adopted':!c.choice.entity.equals(c.entity)?'changed_after_lookup':'table_value_preserved';
          if(appended)s.appended.push({entity:entity.toString()});
          emit({kind:'material_append',id:c.id,materialId:s.id,thread:inv.threadId,index:c.read?.index??null,readId:c.read?.id??null,lookupId:c.choice?.id??null,
            destinationList:c.before.owner.toString(),destinationIndex:c.before.count,entity:entity?.toString()??null,appendObserved:appended,selection,
            sourceProven:false,shaderUseProven:false,lifetimeGeneration:null,atomicSnapshot:false});
        }catch(e){stop('invalid_observation',String(e));}};
      listeners.push(Interceptor.attach(points.append,{
        onEnter(args){if(stopped||!armed)return;try{materialAppendEnter(this,args,false);}catch(e){stop('invalid_observation',String(e));}},
        onLeave(){if(stopped||!armed||!this.materialCall)return;materialAppendLeave(this);}
      }));
    }
    if(p.resolver)resolverHooks=installResolverObservation({p,m,bytes,list,listeners,emit,stop,
      active:()=>armed&&!stopped,nextId:()=>++serial,callback:thread=>callbackStacks.get(thread)?.at(-1)});
    armed=true;send({kind:'ready',pid:Process.id,mode:'reference_handoff_only',materials:!!p.materials,resolver:!!p.resolver});setTimeout(()=>stop('window_end'),p.durationMs);
  }catch(e){stop('preflight_rejected',String(e));}
})();
