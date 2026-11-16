"""Clean Windows x86 build. Requires Python 3.11+, MSVC/Windows SDK, Perl.
No prebuilt object or old workspace is used. Output must be new.
"""
from pathlib import Path
import argparse,hashlib,json,os,shutil,subprocess

def digest(p):
 with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest().upper()
def run(args,**kw):return subprocess.run(args,check=True,**kw)
def finish_receipts(out):
 # Preserve the existing inner-media builder's component contract.
 for game in ['GE2RB','GER']:
  shutil.copyfile(out/'management/pack-audit.exe',out/game/'pack-audit.exe')
 for component in ['GE2RB','GER','management']:
  work=out/component
  receipt=dict(schema='GEHD_LOADING_FRAMEWORK_BUILD_V1' if component!='management' else 'GEHD_PRODUCT_CONTROL_BUILD_V1',gameId=component,architecture='x86',productionDeployed=False,
   files=[dict(name=f.relative_to(work).as_posix(),bytes=f.stat().st_size,sha256=digest(f)) for f in sorted(work.rglob('*')) if f.is_file() and f.name!='build.json'])
  (work/'build.json').write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf-8')
def main():
 p=argparse.ArgumentParser(description=__doc__)
 p.add_argument('--out',type=Path,required=True)
 p.add_argument('--vcvars',type=Path,required=True)
 p.add_argument('--perl',type=Path,required=True)
 a=p.parse_args();root=Path(__file__).resolve().parents[2];out=a.out.resolve()
 if not a.vcvars.is_file() or not a.perl.is_file():p.error('Explicit existing vcvars32.bat and Perl required')
 out.mkdir(parents=True,exist_ok=False)
 vendor=root/'dependencies/openssl-sha';pins=json.loads((vendor/'source.json').read_text(encoding='utf-8-sig'))
 for f in pins['files']:
  if digest(vendor/f['path'])!=f['sha256']:raise ValueError('Vendor mismatch: '+f['path'])
 source=[x for base in [root/'src',root/'tests/native'] for x in sorted(base.rglob('*')) if x.is_file()]
 records=[]
 for game in ['GE2RB','GER','management']:
  work=out/game;work.mkdir()
  for s in source:
   target=work/s.name
   if target.exists():raise ValueError('Duplicate staged filename '+s.name)
   shutil.copyfile(s,target)
  shutil.copytree(vendor,work/'vendor')
  cmds=['@echo off','cd /d "%~dp0"',f'call "{a.vcvars.resolve()}" >nul','if errorlevel 1 exit /b %errorlevel%',
        f'"{a.perl.resolve()}" vendor/crypto/sha/asm/sha256-586.pl win32 -DOPENSSL_IA32_SSE2 sha256.asm',
        'if errorlevel 1 exit /b %errorlevel%','ml /nologo /c /coff sha256.asm','if errorlevel 1 exit /b %errorlevel%']
  base='cl /nologo /std:c++20 /EHsc /O2 /W4 /MT '
  def cl(options):cmds.extend([base+options,'if errorlevel 1 exit /b %errorlevel%'])
  if game!='management':
   flags=('/DGEHD_GAME_GER ' if game=='GER' else '')+'/DGEHD_HASH_OPENSSL /DGEHD_REQUEST_PREFILTER /DGEHD_LOADING_FRAMEWORK '
   cl(flags+'/LD pack_loader.cpp sha256.obj /Fe:d3d9.dll /Fo:loader.obj /link /DEF:d3d9.def bcrypt.lib')
   cl(flags+'/DGEHD_LOADING_TEST /DGEHD_PACK_FIXTURE /LD pack_loader.cpp sha256.obj /Fe:fixture.dll /Fo:fixture.obj /link bcrypt.lib')
   for src,exe,extra in [('game_profile_contract','game-profile-contract',''),('loading_framework_contract','loading-framework-contract','/DGEHD_LOADING_TEST '),('loading_save_contract','loading-save-contract','/DGEHD_LOADING_TEST '),('sha_loader_contract','sha-loader-contract','')]:
    cl(flags+extra+f'{src}.cpp sha256.obj /Fe:{exe}.exe /link bcrypt.lib user32.lib')
   cl('loading_framework_gpu.cpp /Fe:loading-framework-gpu.exe /link user32.lib psapi.lib')
   cl('qualify.cpp /Fe:qualify.exe /link user32.lib')
  else:
   flags='/utf-8 /DGEHD_PRODUCT_SHA_OPENSSL '
   manifest='/MANIFEST:EMBED /MANIFESTUAC:"level=\'asInvoker\' uiAccess=\'false\'" '
   for src,exe,extra,link in [('deploy','pack-control','',''),('deploy','control-test','/DGEHD_PRODUCT_CONTROL_TEST ',''),('product_manager','GEHD_Manager','','/SUBSYSTEM:WINDOWS user32.lib gdi32.lib ole32.lib shell32.lib uuid.lib '),('product_audit','pack-audit','',''),('product_copy','product-copy','',''),('process_guard_fixture','process-guard-fixture','','')]:
    cl(flags+extra+f'{src}.cpp /Fe:{exe}.exe /Fo:{exe}.obj sha256.obj /link '+manifest+link+'bcrypt.lib')
   cl('/utf-8 shared_manager.cpp /Fe:GEHD_Common_Manager.exe /Fo:manager.obj /link '+manifest+'/SUBSYSTEM:WINDOWS user32.lib gdi32.lib ole32.lib shell32.lib uuid.lib bcrypt.lib')
   cl('/utf-8 test_shared_manager_flow.cpp /Fe:manager-flow-test.exe /link bcrypt.lib')
  cmds+=['cl 2> compiler-version.txt','ver > windows-version.txt','exit /b 0']
  (work/'build.cmd').write_text('\r\n'.join(cmds)+'\r\n',encoding='utf-8')
  with (work/'build.log').open('wb') as log:run(['cmd.exe','/d','/c',str(work/'build.cmd')],stdout=log,stderr=subprocess.STDOUT)
  records.append(dict(component=game,commands=cmds,files=[dict(name=f.name,bytes=f.stat().st_size,sha256=digest(f)) for f in sorted(work.iterdir()) if f.suffix in ('.exe','.dll','.obj')]))
  print('Built',game,flush=True)
 finish_receipts(out)
 receipt=dict(schema='GEHD_CLEAN_BUILD_V1',architecture='x86',source=[dict(path=s.relative_to(root).as_posix(),sha256=digest(s)) for s in source],vendorCommit=pins['commit'],tools=dict(vcvars=str(a.vcvars),perl=str(a.perl),perlSha256=digest(a.perl)),components=records,componentReceipts=[dict(component=g,sha256=digest(out/g/'build.json')) for g in ['GE2RB','GER','management']],productionDeployed=False)
 (out/'build.json').write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf-8')
if __name__=='__main__':main()
