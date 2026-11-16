"""Wrap qualified local media; store only, no DDS/installer changes or publishing."""
from pathlib import Path
import argparse,hashlib,json,shutil,subprocess
from split_zip import create,restore
def sha(p):
 h=hashlib.sha256()
 with p.open('rb') as f:
  while chunk:=f.read(16*1024*1024):h.update(chunk)
 return h.hexdigest().upper()
def pin(p):return dict(name=p.name,bytes=p.stat().st_size,sha256=sha(p))
def save(p,v):p.write_text(json.dumps(v,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
def main():
 ap=argparse.ArgumentParser(description=__doc__)
 for name in ['receipt','manager','out','verification']:ap.add_argument('--'+name,type=Path,required=True)
 ap.add_argument('--version',required=True);a=ap.parse_args()
 if not a.version or any(c not in 'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-' for c in a.version):ap.error('Unsafe version')
 rec=json.loads(a.receipt.read_text(encoding='utf-8-sig'))
 if sha(a.manager)!=rec['commonZipSha256']:raise ValueError('Manager identity changed')
 a.out.mkdir(parents=True,exist_ok=False);a.verification.mkdir(parents=True,exist_ok=False)
 manager=a.out/f'GEHD-Manager-{a.version}.zip';shutil.copyfile(a.manager,manager)
 result=dict(schema='GEHD_MEDIA_V1',version=a.version,manager=pin(manager),games=[],container='ZIP_STORED byte-split ZIP64; 7-Zip compatible',ddsRecompressed=False)
 for pkg in rec['packages']:
  game=pkg['game']
  if game not in ('GER','GE2RB'):raise ValueError('Unknown game')
  src=Path(pkg['path']);expected={f['name']:f['sha256'] for f in pkg['archives']}
  expected.update({'game-pack.tsv':pkg['manifestSha256'],'GEHD_Setup.exe':pkg['setupSha256']})
  inner=[]
  for p in sorted(src.iterdir()):
   if not p.is_file():continue
   record=pin(p)
   if p.name in expected and record['sha256']!=expected[p.name]:raise ValueError('Changed inner media: '+p.name)
   inner.append(record);print('Pinned',game,p.name,flush=True)
  if not set(expected)<={p['name'] for p in inner}:raise ValueError('Missing expected inner media')
  if len(inner)!=len(list(src.iterdir())):raise ValueError('Unexpected inner directories')
  dest=a.out/f'GEHD-{game}-{a.version}.zip'
  volumes=[pin(p) for p in create(src,dest)]
  if not volumes or any(p['bytes']>=2*1024**3 for p in volumes):raise ValueError('Volume bounds')
  item=dict(game=game,directory=src.name,release=pkg['release'],sourceBuildReceiptSha256=pkg['sourceBuildSha256'],inner=inner,volumes=volumes)
  result['games'].append(item);save(a.out/'media-manifest.json',result)
  restored=a.verification/game
  actual=restore([a.out/v['name'] for v in volumes],restored,src.name,inner)
  if actual!=inner:raise ValueError('Restored media differs')
  if [pin(p) for p in sorted((restored/src.name).iterdir())]!=inner:raise ValueError('Restored file readback differs')
  print('Restored and SHA verified',game,len(inner),'files',len(volumes),'volumes',flush=True)
 save(a.verification/'qualification.json',dict(passed=True,restoredFileReadback=True,mediaManifestSha256=sha(a.out/'media-manifest.json'),games=[dict(game=p['game'],innerFiles=len(p['inner']),volumes=len(p['volumes'])) for p in result['games']],managerUnchanged=True,productionDeployed=False))
if __name__=='__main__':main()
