"""Generate non-game DDS fixtures and run both adapter contracts and GUI flow."""
import argparse,hashlib,json,struct,subprocess
from pathlib import Path
def sha(b):return hashlib.sha256(b).hexdigest().upper()
def dds(w,h,seed):
 b=bytearray(128);b[:4]=b'DDS '
 for off,n in [(4,124),(8,0x100f),(12,h),(16,w),(20,w*4),(28,1),(76,32),(80,65),(88,32),(92,0xff0000),(96,0xff00),(100,0xff),(104,0xff000000),(108,0x1000)]:struct.pack_into('<I',b,off,n)
 return bytes(b)+bytes(((i*17+seed)%256 for i in range(w*h*4)))
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--build',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
 a.out.mkdir(parents=True,exist_ok=False);corpus=a.out/'synthetic';corpus.mkdir();(corpus/'original').mkdir();(corpus/'2x').mkdir()
 rows=['GEHD_TEXTURE_PACK_MIXED_V1']
 for i in range(8):
  old=dds(8,8,i);new=dds(16,16,i);key=sha(old)
  (corpus/'original'/f'{key}.dds').write_bytes(old);(corpus/'2x'/f'{key}.dds').write_bytes(new)
  rows.append('\t'.join(map(str,[key,sha(new),len(old),len(new),16,16,1,21,2])))
 (corpus/'mapping.tsv').write_text('\n'.join(rows)+'\n',encoding='ascii')
 checks=[]
 for game in ['GE2RB','GER']:
  for exe in ['game-profile-contract','loading-framework-contract','loading-save-contract']:
   name=game+'-'+exe
   r=subprocess.run([str(a.build/game/(exe+'.exe')),str(corpus.resolve()),str((a.out/name).resolve())],capture_output=True,timeout=180)
   (a.out/(name+'.stdout')).write_bytes(r.stdout);(a.out/(name+'.stderr')).write_bytes(r.stderr)
   if r.returncode:raise RuntimeError(name+': '+r.stderr.decode(errors='replace'))
   result=json.loads(r.stdout.decode().splitlines()[-1]);checks.append(dict(name=name,**result));print(name,result,flush=True)
 r=subprocess.run([str(a.build/'management/manager-flow-test.exe')],capture_output=True,timeout=30)
 (a.out/'manager-flow.txt').write_bytes(r.stdout+r.stderr)
 if r.returncode:raise RuntimeError('Manager flow failed')
 checks.append(dict(name='manager-flow',passed=True,output=r.stdout.decode().strip()))
 (a.out/'qualification.json').write_text(json.dumps(dict(passed=True,checks=checks,gameExecuted=False,scope='New clean build; synthetic CPU contract and manager flow, no GPU or live-game claims.'),indent=2)+'\n',encoding='utf-8')
if __name__=='__main__':main()
