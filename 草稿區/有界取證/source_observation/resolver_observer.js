// Fixed resolver ABI. Branch/data-transfer observations, never persistent provenance or lifetime proof.
'use strict';
function installResolverObservation(env) {
  const {p,m,bytes,list,listeners,emit,stop,active,nextId,callback}=env,spec=p.resolver,points={},callers={},stacks=new Map();
  for(const k of ['resolver','match','holder','copy','assign','slot','fallback','release','nameAppend'])points[k]=bytes(spec[k],spec[k].rva);
  for(const k of ['bridgeToResolver','previousRecordMatch','datasetMatch','matchToHolder','holderSlot','holderCopy','previousRecordAdopt','datasetAdopt',
    'fallbackCreate','fallbackAdopt','cacheObjectAppend','cacheNameAppend','freshReturnCopy','freshLocalRelease','cacheSlot','cacheReturnCopy','unsupportedReturnCopy']) {
    const c=spec[k];bytes(c,c.returnRva-c.bytes.length/2);callers[k]=m.base.add(c.returnRva);
  }
  const rawTop=t=>stacks.get(t)?.at(-1),top=t=>{const s=rawTop(t);return s?.rejected?undefined:s;},is=(inv,k)=>inv.returnAddress.equals(callers[k]);
  const serial=(s,kind,extra={})=>emit({kind,id:nextId(),resolverId:s.id,thread:s.thread,...extra});
  const same=(a,b)=>!!a&&!!b&&a.equals(b);
  const safe=fn=>function(...args){if(!active())return;try{return fn.apply(this,args);}catch(e){stop('invalid_observation',String(e));}};
  const hook=(key,onEnter,onLeave)=>listeners.push(Interceptor.attach(points[key],{onEnter:safe(onEnter),onLeave:safe(onLeave??function(){})}));
  const check=s=>{if(top(s.thread)!==s)throw Error('resolver_pairing');};
  hook('resolver',function(args){
    const cb=callback(this.threadId);
    if(!cb||!is(this,'bridgeToResolver')||!same(args[0],cb.out)||!same(args[1],cb.name)||!same(this.context.ecx,cb.context.readPointer())) {
      // A rejected nested invocation must mask the outer scope until it returns.
      this.fence={id:nextId(),thread:this.threadId,rejected:true};const stack=stacks.get(this.threadId)??[];
      stack.push(this.fence);stacks.set(this.threadId,stack);
      emit({kind:'resolver_rejected',id:this.fence.id,thread:this.threadId,reason:'callback_caller_or_arguments'});return;
    }
    if(cb.resolved)throw Error('multiple_resolver_results');
    const s={id:nextId(),thread:this.threadId,cb,owner:this.context.ecx,out:args[0],name:args[1],match:null,holder:null,local:null,result:null};
    const stack=stacks.get(this.threadId)??[];stack.push(s);stacks.set(this.threadId,stack);this.scope=s;
    emit({kind:'resolver_enter',id:s.id,callbackId:cb.id,thread:s.thread,owner:s.owner.toString(),outputHandle:s.out.toString(),nameObject:s.name.toString()});
  },function(retval){if(this.fence){if(rawTop(this.threadId)!==this.fence)throw Error('rejected_resolver_pairing');stacks.get(this.threadId).pop();return;}
    if(!this.scope)return;const s=this.scope;check(s);
    const exact=retval.equals(s.out),entity=exact?s.out.readPointer():null,r=s.result;
    const valid=!!(exact&&r?.valid&&same(entity,r.entity));
    s.cb.resolved={id:s.id,branch:r?.branch??'unobserved',entity,valid};stacks.get(s.thread).pop();
    emit({kind:'resolver_leave',id:s.id,callbackId:s.cb.id,thread:s.thread,expectedOutputHandle:exact,entity:entity?.toString()??null,
      branch:r?.branch??'unobserved',pathObserved:valid,descriptor:valid?r?.descriptor?.toString()??null:null,
      cacheOrigin:'unknown',sourceProven:false,lifetimeGeneration:null,holdingContinuityProven:false});
  });
  hook('match',function(args){const s=top(this.threadId);if(!s)return;
    const route=is(this,'previousRecordMatch')?'previous':is(this,'datasetMatch')?'dataset':null;
    if(!route){serial(s,'resolver_helper_rejected',{helper:'match'});return;}
    if(s.match)throw Error('nested_match_without_resolver');
    this.call={s,id:nextId(),route,out:args[0],descriptor:args[1],holder:null};s.match=this.call;
  },function(retval){if(!this.call)return;const c=this.call,s=c.s;check(s);if(s.match!==c)throw Error('match_pairing');s.match=null;
    c.entity=retval.equals(c.out)?c.out.readPointer():null;
    c.valid=!!(c.holder?.valid&&same(c.entity,c.holder.entity)&&!c.entity.isNull());s.lastMatch=c;
    emit({kind:'resolver_match',id:c.id,resolverId:s.id,thread:s.thread,route:c.route,descriptor:c.descriptor.toString(),
      expectedOutputHandle:retval.equals(c.out),entity:c.entity?.toString()??null,holderPathObserved:c.valid});
  });
  hook('holder',function(args){const s=top(this.threadId),match=s?.match;if(!match)return;
    if(!is(this,'matchToHolder')||!same(args[0],match.out)||!same(args[1],match.descriptor)||args[2].toUInt32()!==0){serial(s,'resolver_helper_rejected',{helper:'holder'});return;}
    this.call={s,match,out:args[0],descriptor:args[1],table:args[1].add(16).readPointer(),slot:null,copy:null};s.holder=this.call;
  },function(retval){if(!this.call)return;const c=this.call,s=c.s;check(s);if(s.holder!==c)throw Error('holder_pairing');s.holder=null;
    c.entity=retval.equals(c.out)?c.out.readPointer():null;c.valid=!!(c.copy?.valid&&same(c.entity,c.copy.entity));c.match.holder=c;
    serial(s,'resolver_holder',{descriptor:c.descriptor.toString(),holder:c.table.toString(),entity:c.entity?.toString()??null,expectedOutputHandle:retval.equals(c.out),slotCopyObserved:c.valid});
  });
  hook('slot',function(args){const s=top(this.threadId);if(!s)return;
    const role=s.holder&&is(this,'holderSlot')?'holder':is(this,'cacheSlot')?'cache':null;if(!role)return;
    const expected=role==='holder'?s.holder.table:s.owner.add(80),index=args[0].toUInt32(),table=this.context.ecx;
    const before=list(table),valid=table.equals(expected)&&index<before.count;
    this.call={s,role,index,table,before,slot:valid?before.data.add(index*4):null,valid};
  },function(retval){if(!this.call)return;const c=this.call,s=c.s;check(s);const after=list(c.table);
    c.valid=c.valid&&same(retval,c.slot)&&same(after.owner,c.before.owner)&&same(after.data,c.before.data)&&after.count===c.before.count;
    c.entity=c.valid?retval.readPointer():null;
    if(c.role==='holder')s.holder.slot=c;else s.cacheSlot=c;
    serial(s,'resolver_slot',{role:c.role,index:c.index,table:c.table.toString(),returnedSlot:retval.toString(),boundedSlotObserved:c.valid,
      entity:c.entity?.toString()??null,atomicSnapshot:false});
  });
  hook('fallback',function(args){const s=top(this.threadId);if(!s||!is(this,'fallbackCreate'))return;this.call={s,out:args[0]};
  },function(retval){if(!this.call)return;const c=this.call,s=c.s;check(s);c.valid=retval.equals(c.out);c.entity=c.valid?c.out.readPointer():null;s.fallback=c;
    serial(s,'resolver_fallback',{expectedOutputHandle:c.valid,entity:c.entity?.toString()??null});
  });
  hook('assign',function(args){const s=top(this.threadId);if(!s)return;
    const route=is(this,'previousRecordAdopt')?'previous':is(this,'datasetAdopt')?'dataset':is(this,'fallbackAdopt')?'fallback':null;if(!route)return;
    const origin=route==='fallback'?s.fallback:s.lastMatch,source=args[0],entity=source.readPointer();
    this.call={s,out:this.context.ecx,source,entity,origin,branch:route==='fallback'?'fallback':'candidate',
      valid:!!(origin?.valid&&same(source,origin.out)&&same(entity,origin.entity)&&(route==='fallback'||route===origin.route))};
  },function(retval){if(!this.call)return;const c=this.call,s=c.s;check(s);c.valid=c.valid&&retval.equals(c.out)&&same(c.out.readPointer(),c.entity);
    c.descriptor=c.branch==='candidate'?c.origin?.descriptor:null;s.local=c;
    serial(s,'resolver_adopt',{branch:c.branch,entity:c.entity.toString(),destination:c.out.toString(),pathObserved:c.valid});
  });
  hook('copy',function(args){const s=top(this.threadId);if(!s)return;
    const role=s.holder&&is(this,'holderCopy')?'holder':is(this,'freshReturnCopy')?'fresh':is(this,'cacheReturnCopy')?'cache':is(this,'unsupportedReturnCopy')?'unsupported':null;
    if(!role)return;const source=args[0],out=this.context.ecx,entity=source.readPointer();
    const origin=role==='holder'?s.holder.slot:role==='fresh'?s.local:role==='cache'?s.cacheSlot:null;
    const expectedSource=role==='fresh'?origin?.out:origin?.slot,expectedOutput=role==='holder'?s.holder.out:s.out;
    this.call={s,role,source,out,entity,origin,valid:out.equals(expectedOutput)&&(role==='unsupported'||!!(origin?.valid&&same(source,expectedSource)&&same(entity,origin.entity)))};
  },function(retval){if(!this.call)return;const c=this.call,s=c.s;check(s);c.valid=c.valid&&retval.equals(c.out)&&same(c.out.readPointer(),c.entity);
    if(c.role==='holder')s.holder.copy=c;
    else s.result={branch:c.role==='fresh'?c.origin?.branch??'fresh_unknown':c.role,valid:c.valid,entity:c.entity,descriptor:c.role==='fresh'?c.origin?.descriptor:null};
    serial(s,'resolver_copy',{role:c.role,sourceHandle:c.source.toString(),outputHandle:c.out.toString(),entity:c.entity.toString(),pathObserved:c.valid,sourceProven:false});
  });
  hook('release',function(){const s=top(this.threadId);if(!s)return;const handle=this.context.ecx;
    this.call={s,handle,entity:handle.readPointer()};
    // Invalidate the observed handle use, even when its pointer bytes remain unchanged.
    for(const c of [s.local,s.lastMatch,s.fallback])if(c&&same(c.out,handle))c.valid=false;
    if(s.result&&same(s.out,handle))s.result.valid=false;
  },function(){if(!this.call)return;const c=this.call;check(c.s);serial(c.s,'resolver_release',{handle:c.handle.toString(),entityBefore:c.entity.toString(),
    entityAfter:c.handle.readPointer().toString(),livenessProven:false});
  });
  hook('nameAppend',function(args){const s=top(this.threadId);if(!s||!is(this,'cacheNameAppend'))return;
    this.scope=s;this.matched=this.context.ecx.equals(s.owner.add(84));this.name=args[0];
  },function(){if(!this.scope)return;check(this.scope);serial(this.scope,'resolver_cache_name_return',{receiverMatched:this.matched,nameObject:this.name.toString(),cachePublicationProven:false});});
  return {pending:()=>[...stacks.values()].flat().map(s=>s.id),
    appendEnter(inv,args){const s=top(inv.threadId);if(!s||!is(inv,'cacheObjectAppend'))return false;
      const table=inv.context.ecx;inv.resolverAppend={s,table,before:list(table),entity:args[0].readPointer(),receiverMatched:table.equals(s.owner.add(80))};return true;},
    appendLeave:safe(function(inv){const c=inv.resolverAppend;check(c.s);const after=list(c.table),valid=c.receiverMatched&&same(after.owner,c.before.owner)&&after.count===c.before.count+1;
      const entity=valid?after.data.add(c.before.count*4).readPointer():null;
      serial(c.s,'resolver_cache_object_return',{index:c.before.count,entity:entity?.toString()??null,appendObserved:valid&&same(entity,c.entity),
        cacheOriginProven:false,cachePublicationProven:false,atomicSnapshot:false});})};
}
