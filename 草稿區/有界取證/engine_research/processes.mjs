import { spawn } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import path from 'node:path';
import { sleep } from './io.mjs';

const here = path.dirname(fileURLToPath(import.meta.url));
export function inventory(python, pids) {
  return new Promise((resolve, reject) => {
    const p = spawn(python, ['-B', '-X', 'utf8', path.join(here, 'process_inventory.py')], { windowsHide: true, stdio: ['pipe','pipe','pipe'] });
    let out='',err='';const timer=setTimeout(()=>{p.kill();reject(new Error('Process inventory deadline'));},10000);
    p.stdout.on('data', b=>{out+=b;});p.stderr.on('data',b=>{err+=b;});
    p.on('error', e=>{clearTimeout(timer);reject(e);});
    p.on('close', code=>{clearTimeout(timer);try {if(code!==0)throw new Error(err||`inventory exit ${code}`);resolve(JSON.parse(out));}catch(e){reject(e);}});
    p.stdin.end(JSON.stringify({pids}));
  });
}
// The inventory returns PID-based candidates, not an ownership verdict.
// Defaults match the runner's direct Node child; injection is only for tests.
export function createTracker(python, { inventory: readInventory=inventory, pollIntervalMs=500 }={}) {
  const known=new Map(),errors=[],reported=new Set(),pendingCandidates=new Map();
  let busy=null,last=null,root=null,expected=null,initial=true,timer;
  let owned=[],unresolved=[],lastQueryPids=[],sampleNumber=0;
  const image = value=>path.win32.normalize(value).toLowerCase();
  const readable = r=>Number.isInteger(r.pid) && r.pid>0 && Number.isInteger(r.parentPid) && r.parentPid>=0 &&
    typeof r.createdFiletime==='string' && /^[1-9][0-9]*$/.test(r.createdFiletime) &&
    typeof r.image==='string' && r.image.length>0 && !r.identityError && !r.imageError;
  const key = r=>JSON.stringify([r.pid,r.parentPid,r.createdFiletime,image(r.image)]);
  const creation = r=>typeof r?.createdFiletime==='string' && /^[1-9][0-9]*$/.test(r.createdFiletime) && !r.identityError ? r.createdFiletime : null;
  function error(pid,reason) {
    const id=JSON.stringify([pid,reason]);
    if(!reported.has(id)){reported.add(id);errors.push({at:new Date().toISOString(),pid,error:reason});}
  }
  function candidate(r,reason,snapshot,parent,expectedIdentity) {
    const born=expectedIdentity?.createdFiletime??creation(r);
    const id=JSON.stringify([r.pid,r.parentPid,born,reason]);
    if(!pendingCandidates.has(id)) {
      const entry={at:new Date().toISOString(),pid:r.pid,error:reason,candidate:true,
        partialIdentity:{...r},createdFiletime:born,
        observedSnapshot:sampleNumber,observedBeforeFiletime:snapshot.snapshotBeforeFiletime??null,
        ...(parent?{observedParentIdentity:{...parent}}:{}),
        ...(expectedIdentity?{expectedIdentity:{...expectedIdentity}}:{})};
      errors.push(entry);pendingCandidates.set(id,entry);
    }
  }
  function acceptSnapshot(snapshot) {
    owned=[];unresolved=[];
    const rows=snapshot.processes,current=new Map(),candidateRoots=new Set();
    // A historical read failure is not permanent proof of a live process. Only
    // an explicitly covered, complete later snapshot may settle the candidate.
    if(snapshot.complete===true)for(const [id,c] of pendingCandidates) {
      if(!lastQueryPids.includes(c.pid))continue;
      const r=rows.find(x=>x.pid===c.pid);
      const replaced=r && c.createdFiletime && readable(r) && r.createdFiletime!==c.createdFiletime;
      if(!r || replaced) {
        c.resolvedAt=new Date().toISOString();
        c.resolution={kind:replaced?'creation_replaced':'absent_from_complete_snapshot',
          snapshot:sampleNumber,snapshotBeforeFiletime:snapshot.snapshotBeforeFiletime??null,
          complete:true,queriedPid:c.pid,...(replaced?{replacementIdentity:{...r}}:{})};
        pendingCandidates.delete(id);
      }
    }
    if(initial) {
      initial=false;
      const r=rows.find(x=>x.pid===root);
      if(!r)error(root,'initial_identity_missing');
      else if(!readable(r)){error(root,'initial_identity_unreadable');unresolved.push(r);}
      else if(r.parentPid!==expected.parentPid || image(r.image)!==image(expected.image) ||
        (expected.createdFiletime!==undefined && r.createdFiletime!==expected.createdFiletime))error(root,'initial_identity_mismatch');
      else known.set(key(r),{...r});
    }
    // Pin all identity fields. A reused PID cannot replace an owned identity.
    for(const r of rows) {
      const previous=[...known.values()].filter(x=>x.pid===r.pid);
      if(readable(r) && known.has(key(r)))current.set(r.pid,r);
      else if(previous.some(x=>!r.createdFiletime || x.createdFiletime===r.createdFiletime)) {
        if(readable(r))error(r.pid,'identity_mismatch');
        else {
          const matched=previous.find(x=>x.createdFiletime===r.createdFiletime)??(previous.length===1?previous[0]:undefined);
          candidate(r,'identity_unreadable',snapshot,null,matched);candidateRoots.add(r.pid);
        }
        unresolved.push(r);
      }
    }
    // Only a parent present with a proved identity can establish a new child.
    // A known child remains owned after its parent exits; an unseen orphan does not.
    let changed;
    do {
      changed=false;
      for(const r of rows) {
        if(current.has(r.pid))continue;
        const parent=current.get(r.parentPid);
        if(!parent)continue;
        if(!readable(r)){candidate(r,'child_identity_unreadable',snapshot,parent);candidateRoots.add(r.pid);if(!unresolved.includes(r))unresolved.push(r);continue;}
        if(BigInt(r.createdFiletime)<BigInt(parent.createdFiletime))continue;
        // If an old owned identity conflicts at this PID, do not silently rebind it.
        if([...known.values()].some(x=>x.pid===r.pid && x.createdFiletime===r.createdFiletime))continue;
        known.set(key(r),{...r});current.set(r.pid,r);changed=true;
      }
    }while(changed);
    // Retain descendants actually seen through an unreadable candidate in this
    // snapshot as candidates, not owned identities. Do not discover new children
    // through a historical candidate whose parent is no longer present/proved.
    do {
      changed=false;
      for(const r of rows) {
        if(current.has(r.pid)||candidateRoots.has(r.pid)||!candidateRoots.has(r.parentPid))continue;
        const parent=rows.find(x=>x.pid===r.parentPid);
        if(creation(r)&&creation(parent)&&BigInt(creation(r))<BigInt(creation(parent)))continue;
        candidate(r,'candidate_descendant_unverified',snapshot,parent);
        candidateRoots.add(r.pid);unresolved.push(r);changed=true;
      }
    }while(changed);
    owned=[...current.values()];
  }
  async function sample() {
    if(busy)return busy;
    if(!root)return null;
    busy=(async()=>{
      try {
        lastQueryPids=[...new Set([root,...[...known.values()].map(x=>x.pid),...[...pendingCandidates.values()].map(x=>x.pid)])];
        sampleNumber++;
        const r=await readInventory(python,lastQueryPids);
        if(!r || !Array.isArray(r.processes))throw new Error('Invalid process inventory');
        last=r;
        acceptSnapshot(r);
        return r;
      }catch(e){initial=false;error(root,String(e));last=null;owned=[];unresolved=[];return null;}
      finally{busy=null;}
    })();return busy;
  }
  return {
    async start(pid,{parentPid=process.pid,image:expectedImage=process.execPath,createdFiletime}={}) {
      if(root!==null)throw new Error('Process tracker already started');
      if(!Number.isInteger(pid)||pid<=0)throw new Error('A positive owned server PID is required');
      root=pid;expected={parentPid,image:expectedImage,createdFiletime};await sample();
      if(pollIntervalMs>0){timer=setInterval(()=>void sample(),pollIntervalMs);timer.unref();}
    },
    sample,
    async finish(waitMs=4000) {
      clearInterval(timer);if(busy)await busy;
      const end=Date.now()+waitMs;
      do {
        await sample();
        if(last?.complete && !owned.length && !unresolved.length && !pendingCandidates.size)break;
        if(Date.now()<end)await sleep(100);
      }while(Date.now()<end);
      const remaining=last?[...owned,...unresolved]:null;
      return {status:errors.some(e=>!e.resolution) || !last?.complete || !known.size ? 'UNVERIFIED' : remaining.length ? 'STILL_RUNNING' : 'RELEASED',
        rootPid:root,known:[...known.values()],remaining,errors,
        pendingCandidates:[...pendingCandidates.values()],
        finalSnapshot:last?{...last,queryPids:[...lastQueryPids],sampleNumber}:null,
        scope:'Observed owned process identities and descendants, not an operating-system-wide absence claim'};
    }
  };
}
