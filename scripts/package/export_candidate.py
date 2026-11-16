"""Export committed source and generated reader/tool bundles; never publish."""
from pathlib import Path
import argparse,hashlib,json,re,subprocess,zipfile,io,posixpath
from urllib.parse import unquote,urlsplit
def sha(p):
 h=hashlib.sha256()
 with p.open('rb') as f:
  while chunk:=f.read(16*1024*1024):h.update(chunk)
 return h.hexdigest().upper()
def pin(p):return dict(name=p.name,bytes=p.stat().st_size,sha256=sha(p))
def main():
 ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--out',type=Path,required=True);ap.add_argument('--commit',required=True);ap.add_argument('--evidence',type=Path,required=True);a=ap.parse_args()
 root=Path(__file__).resolve().parents[2]
 commit=subprocess.check_output(['git','rev-parse',a.commit+'^{commit}'],cwd=root,text=True).strip()
 version='v'+(root/'VERSION').read_text().strip()
 source=a.out/f'GEHD-Source-{version}.zip'
 if source.exists():raise ValueError('Source archive already exists')
 subprocess.run(['git','archive','--format=zip','--prefix=GEHD-source/','--output='+str(source.resolve()),commit],cwd=root,check=True)
 with zipfile.ZipFile(source) as z:files={n.removeprefix('GEHD-source/'):z.read(n) for n in z.namelist() if not n.endswith('/')}
 if files['VERSION'].decode().strip()!=version[1:]:raise ValueError('Version differs from selected commit')
 # Verify the exact native input bytes already rebuilt from an isolated copy.
 build=json.loads((a.evidence/'clean-build/build.json').read_text())
 for item in build['source']:
  if hashlib.sha256(files[item['path']]).hexdigest().upper()!=item['sha256']:raise ValueError('Unverified source in export: '+item['path'])
 portable={n:b for n,b in files.items() if n.startswith(('tools/research/offline/','tools/textures/')) or n in ('tools/README.md','tools/catalog.json','tests/test_texture_tools.py','LICENSE.md','THIRD_PARTY_NOTICES.md','dependencies/toolchain.json') or n.startswith('dependencies/licenses/')}
 docs={n:b for n,b in files.items() if n.endswith('.md') and (n.startswith(('草稿區/研究與開發計畫/','previous-research/')) or n in ('README.md','BUILDING.md','CHANGELOG.md','LICENSE.md','THIRD_PARTY_NOTICES.md','tools/README.md','草稿區/有界取證/README.md'))}
 # Include small provenance and licenses needed by the reader bundle.
 docs.update({n:b for n,b in files.items() if n in ('source-map.json','tools/catalog.json','dependencies/toolchain.json','dependencies/openssl-sha/source.json','dependencies/openssl-sha/LICENSE.txt') or n.startswith('dependencies/licenses/')})
 omitted=[]
 def reader_view(name,data,selected,bundle):
  if not name.endswith('.md'):return data
  text=data.decode('utf-8-sig')
  def link(m):
   label,target=m.group(1),m.group(2).strip('<>');u=urlsplit(target)
   if u.scheme in ('https','http','mailto') or target.startswith('#'):return m.group()
   rel=posixpath.normpath(posixpath.join(posixpath.dirname(name),unquote(u.path)))
   if rel in selected or any(n.startswith(rel.rstrip('/')+'/') for n in selected):return m.group()
   omitted.append(dict(bundle=bundle,document=name,target=target))
   return label+'（本機參照，未隨包附送：`'+target+'`）'
  return re.sub(r'\[([^\]\n]+)\]\(([^)\n]+)\)',link,text).encode('utf-8')
 for label,selection in [('Tools',portable),('Docs',docs)]:
  path=a.out/f'GEHD-{label}-{version}.zip'
  if path.exists():raise ValueError('Bundle already exists')
  with zipfile.ZipFile(path,'x',zipfile.ZIP_DEFLATED) as z:
   for name,data in sorted(selection.items()):z.writestr(name,reader_view(name,data,selection,label))
   z.writestr('BUNDLE.json',json.dumps(dict(version=version,sourceCommit=commit,kind=label,generatedView=True,unavailableReferences='UNAVAILABLE_REFERENCES.json',limits='Original game data, private evidence and runtimes are not bundled.'),ensure_ascii=False,indent=2))
   z.writestr('UNAVAILABLE_REFERENCES.json',json.dumps([i for i in omitted if i['bundle']==label],ensure_ascii=False,indent=2))
  with zipfile.ZipFile(path) as z:
   if z.testzip():raise ValueError('ZIP integrity failed')
 (a.evidence/'export.json').write_text(json.dumps(dict(passed=True,sourceCommit=commit,sourceFiles=len(files),docsFiles=len(docs),toolsFiles=len(portable),unavailableReferenceCount=len(omitted),nativeSourceMatchesCleanBuild=True,artifacts=[pin(a.out/f'GEHD-{label}-{version}.zip') for label in ('Source','Tools','Docs')]),indent=2)+'\n')
 print('Exported committed source, tools and generated docs:',commit)
if __name__=='__main__':main()
