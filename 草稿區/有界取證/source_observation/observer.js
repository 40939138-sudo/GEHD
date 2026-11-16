// REA Frida payload. Observes calls only; never replaces arguments, memory or returns.
// Interceptor patches code while attached. This is not an atomic content witness.
'use strict';
(() => {
  const p = SOURCE_PROFILE, stacks = new Map(), listeners = [];
  let serial = 0, events = 0, hashed = 0, stopped = false;
  const norm = s => s.replace(/\\/g, '/').toLowerCase();
  const hex = b => Array.from(new Uint8Array(b), x => x.toString(16).padStart(2, '0')).join('');
  function stop(reason, detail) {
    if (stopped) return;
    stopped = true;
    send({kind:'terminal', reason, detail, events, hashedBytes:hashed,
      pending:[...stacks.values()].flat().map(x => x.id)});
    for (const listener of listeners) listener.detach();
  }
  function emit(event) {
    if (stopped) return;
    if (events >= p.maxEvents) { stop('event_cap'); return; }
    events++; send(event);
  }
  function digest(pointer, size) {
    if (size < 1 || size > p.maxBufferBytes || !Number.isSafeInteger(size)) throw Error('buffer_bound');
    if (hashed + size > p.maxHashBytes) throw Error('hash_bound');
    const bytes = pointer.readByteArray(size); hashed += size;
    return Checksum.compute('sha256', bytes).toUpperCase();
  }
  try {
    if (Process.id !== p.pid || Process.arch !== 'ia32' || Process.pointerSize !== 4 ||
        norm(Process.mainModule.path) !== norm(p.modules.main.path)) throw Error('target_identity');
    const modules = {};
    for (const [key, expected] of Object.entries(p.modules)) {
      const matches = Process.enumerateModules().filter(m => norm(m.path) === norm(expected.livePath??expected.path));
      if (matches.length !== 1 || matches[0].size !== expected.imageSize) {
        const named=Process.enumerateModules().filter(m=>norm(m.path).split('/').at(-1)===norm(expected.path).split('/').at(-1));
        throw Error('module_identity:' + key + ':' + JSON.stringify(named.map(m=>({path:m.path,size:m.size}))));
      }
      if(expected.livePath && Checksum.compute('sha256',File.readAllBytes(matches[0].path)).toUpperCase()!==expected.sha256)
        throw Error('redirected_module_file_hash:'+key);
      modules[key] = matches[0];
    }
    function relocated(point) {
      const bytes=Uint8Array.from(point.bytes.match(/../g), x=>parseInt(x,16));
      const view=new DataView(bytes.buffer),delta=modules[point.module].base.toUInt32()-p.modules[point.module].imageBase;
      for(const offset of point.relocations)view.setUint32(offset,(view.getUint32(offset,true)+delta)>>>0,true);
      return hex(bytes.buffer);
    }
    function bind(point) {
      const module = modules[point.module], address = module.base.add(point.rva);
      if (point.rva < 0 || point.rva + point.bytes.length / 2 > module.size ||
          hex(address.readByteArray(point.bytes.length / 2)) !== relocated(point)) throw Error('entry_bytes:'+point.module+':'+point.rva);
      return address;
    }
    const loader = bind(p.loader), api = bind(p.api);
    const caller = modules[p.caller.module].base.add(p.caller.returnRva);
    if (hex(caller.sub(p.caller.bytes.length / 2).readByteArray(p.caller.bytes.length / 2)) !== relocated(p.caller))
      throw Error('caller_bytes');
    listeners.push(Interceptor.attach(loader, {
      onEnter() {
        if (stopped) return;
        try {
          const stack = stacks.get(this.threadId) ?? [];
          this.scope = {id:++serial, parent:stack.at(-1)?.id ?? null, owner:this.context.ecx,
            thread:this.threadId, apiIds:[]};
          stack.push(this.scope); stacks.set(this.threadId, stack);
          emit({kind:'loader_enter', id:this.scope.id, parent:this.scope.parent, thread:this.threadId,
            owner:this.scope.owner.toString(), lifetimeGeneration:null});
        } catch (error) { stop('invalid_observation', String(error)); }
      },
      onLeave(retval) {
        if (stopped || !this.scope) return;
        try {
          const stack = stacks.get(this.threadId);
          if (stack?.at(-1) !== this.scope) throw Error('loader_pairing');
          const al = retval.toUInt32() & 255;
          const out = {kind:'loader_leave', id:this.scope.id, thread:this.threadId, al,
            apiIds:this.scope.apiIds, owner:this.scope.owner.toString()};
          // Failure returns may leave borrowed or stale non-null fields. Do not adopt them.
          if (al !== 0) {
            out.texture = this.scope.owner.add(0x44).readPointer().toString();
            out.surface = this.scope.owner.add(0x48).readPointer().toString();
          }
          stack.pop(); emit(out);
        } catch (error) { stop('invalid_observation', String(error)); }
      }
    }));
    listeners.push(Interceptor.attach(api, {
      onEnter(args) {
        if (stopped) return;
        try {
          const scope = stacks.get(this.threadId)?.at(-1);
          const id = ++serial;
          if (!scope || !this.returnAddress.equals(caller)) {
            emit({kind:'api_rejected', id, thread:this.threadId, reason:!scope?'no_loader_scope':'caller_mismatch'});
            return;
          }
          const slot = args[14];
          if (!slot.equals(scope.owner.add(0x44))) {
            emit({kind:'api_rejected', id, loaderId:scope.id, thread:this.threadId, reason:'owner_slot_mismatch'});
            return;
          }
          this.call = {id, loaderId:scope.id, thread:this.threadId, slot, source:args[1], size:args[2].toUInt32()};
          this.call.inputSha256 = digest(this.call.source, this.call.size);
          scope.apiIds.push(id);
          emit({kind:'api_enter', id, loaderId:scope.id, thread:this.threadId, owner:scope.owner.toString(),
            device:args[0].toString(), source:args[1].toString(), size:this.call.size,
            outputSlot:slot.toString(), inputSha256:this.call.inputSha256,
            parameters:Array.from({length:9}, (_,i) => args[i+3].toUInt32()),
            sourceSelection:'caller_and_owner_slot', replacementAttempted:false});
        } catch (error) { stop('invalid_observation', String(error)); }
      },
      onLeave(retval) {
        if (stopped || !this.call) return;
        try {
          const c = this.call, scope = stacks.get(this.threadId)?.at(-1);
          if (scope?.id !== c.loaderId) throw Error('api_pairing');
          const hr = retval.toInt32();
          const out = {kind:'api_leave', id:c.id, loaderId:c.loaderId, thread:this.threadId, hresult:hr,
            outputSlot:c.slot.toString(), returnedTexture:null, parameterLink:false,
            inputBytesConsumedProven:false, resourceContentProven:false, sharingProven:false};
          if (hr >= 0) {
            const returned = c.slot.readPointer();
            out.returnedTexture = returned.toString();
            out.inputSha256After = digest(c.source, c.size);
            out.inputEqualAtEndpoints = out.inputSha256After === c.inputSha256;
            out.parameterLink = !returned.isNull() && out.inputEqualAtEndpoints;
            out.reason = returned.isNull()?'success_null_output':out.inputEqualAtEndpoints?'parameter_return_witness':'input_changed';
          } else out.reason = 'failed_hresult';
          emit(out);
        } catch (error) { stop('invalid_observation', String(error)); }
      }
    }));
    send({kind:'ready', pid:Process.id, arch:Process.arch, loader:loader.toString(), api:api.toString(),
      caller:caller.toString(), mode:'observe_only', noLifetimeInference:true,
      moduleBindings:Object.entries(modules).map(([key,m])=>({key,path:m.path,size:m.size,redirectedFileHashChecked:!!p.modules[key].livePath}))});
    setTimeout(() => stop('window_end'), p.durationMs);
  } catch (error) { stop('preflight_rejected', String(error)); }
})();
