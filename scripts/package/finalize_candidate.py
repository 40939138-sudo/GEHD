"""Seal the verified local candidate manifest and checksums; no remote operations."""
from pathlib import Path
import argparse,hashlib,json,shutil,datetime
def sha(p):
 h=hashlib.sha256()
 with p.open('rb') as f:
  while chunk:=f.read(16*1024*1024):h.update(chunk)
 return h.hexdigest().upper()
def read(p):return json.loads(p.read_text(encoding='utf-8-sig'))
def write(p,v):p.write_text(json.dumps(v,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
def pin(p):return dict(name=p.name,bytes=p.stat().st_size,sha256=sha(p))
def require(ok,message):
 if not ok:raise ValueError(message)
def main():
 ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--out',type=Path,required=True);ap.add_argument('--evidence',type=Path,required=True);a=ap.parse_args()
 root=Path(__file__).resolve().parents[2];version='v'+(root/'VERSION').read_text().strip();e=a.evidence
 require(not (a.out/'release-manifest.json').exists(),'Candidate already sealed')
 export=read(e/'export.json');native=read(e/'native-qualification/qualification.json');offline=read(e/'offline-tools/verification.json');textures=read(e/'texture-tools/qualification.json');pe=read(e/'binary-comparison.json');media=read(a.out/'media-manifest.json');roundtrip=read(e/'media-verification-r4/qualification.json')
 require(export['passed'] and export['nativeSourceMatchesCleanBuild'],'Source export not verified')
 require(native['passed'] and offline['status']=='PASS' and textures['passed'],'Tool/native checks failed')
 require(len(pe['results'])==4 and all(p['pathAndRttiNormalizedExact'] and p['textSectionExact'] for p in pe['results']),'Binary comparison failed')
 require(roundtrip['passed'] and roundtrip.get('restoredFileReadback') and roundtrip['mediaManifestSha256']==sha(a.out/'media-manifest.json'),'Outer media qualification mismatch or missing file readback')
 require(media['version']==version and {p['game'] for p in media['games']}=={'GER','GE2RB'},'Media version or games mismatch')
 handoff=read(e/'handoff-qualification.json');require(handoff['passed'],'Handoff tests failed')
 source=read(root/'source-map.json');require(all(sha(root/x['destination'])==x.get('destinationSha256',x['sha256']) for x in source['files']),'Migrated source identity changed')
 for name,src in [('RELEASE_NOTES.md',root/'scripts/package/release-notes.md'),('verify_release.py',root/'scripts/package/verify_release.py'),('split_zip.py',root/'scripts/package/split_zip.py'),('LICENSE.md',root/'LICENSE.md')]:shutil.copyfile(src,a.out/name)
 notices=(root/'THIRD_PARTY_NOTICES.md').read_text(encoding='utf-8')
 import re
 notices=re.sub(r'\[([^\]]+)\]\((?!https?://)([^)]+)\)',lambda m:m[1]+'（來源ZIP內：`'+m[2]+'`）',notices)
 (a.out/'THIRD_PARTY_NOTICES.md').write_text(notices,encoding='utf-8')
 verification=dict(schema='GEHD_LOCAL_VERIFICATION_V1',passed=True,sourceCommit=export['sourceCommit'],native=native,offline=dict(passed=True,checks=len(offline['checks']),receiptSha256=sha(e/'offline-tools/verification.json')),textures=textures,handoff=handoff,media=roundtrip,
  binaryComparison=[{k:p[k] for k in ('component','byteExact','normalizedExact','pathAndRttiNormalizedExact','textSectionExact')} for p in pe['results']],
  buildReceiptSha256=sha(e/'clean-build/build.json'),binaryComparisonReceiptSha256=sha(e/'binary-comparison.json'),sourceArchiveVerified=export['nativeSourceMatchesCleanBuild'],syntheticInnerMedia=read(e/'inner-media-qualification.json'),
  reusedQualification=dict(scope='Unchanged shipping binary/data; prior two-game title/load/hub and return to pre-test deployment (not a clean vanilla baseline); prior manager GUI and distribution controls.',newGameRun=False,newDeployment=False,fullMissionOrAllTextureQuality=False),
  limits=['Not byte-for-byte rebuild','No public-release license clearance','Evidence roots and complete game inputs are not in source/tool archives'])
 require(verification['syntheticInnerMedia']['passed'],'Inner media smoke failed');write(a.out/'VERIFICATION.json',verification)
 # Large volumes were already SHA checked after writing and fully restored.
 # Reuse those recorded attachment identities here; the final verifier rereads.
 known={x['name']:x for game in media['games'] for x in game['volumes']};known[media['manager']['name']]=media['manager']
 for item in export['artifacts']:known[item['name']]=item
 attachments=[]
 for p in sorted(a.out.iterdir()):
  require(p.is_file(),'Unexpected directory in download root')
  item=known.get(p.name) or pin(p);require(p.stat().st_size==item['bytes'],'Attachment size changed');attachments.append(item)
 components=[]
 for p in pe['results']:
  components.append(dict(name=p['component'],shippingSha256=p['baseline']['sha256'],rebuiltSha256=p['rebuilt']['sha256'],sourcePaths=['src/management'] if p['component'].startswith('management/') else ['src/loader','src/games'],qualifiedBuild=p['qualified']))
 manifest=dict(schema='GEHD_RELEASE_MANIFEST_V1',version=version,createdUtc=datetime.datetime.now(datetime.timezone.utc).isoformat(),sourceCommit=export['sourceCommit'],tag=None,proposedTag=version,published=False,localCandidateReady=True,publicReleaseEligible=False,publicBlockers=['Self-authored source license not selected','Game-derived asset redistribution scope not confirmed'],
  source=dict(archive=f'GEHD-Source-{version}.zip',migrationMap='source-map.json',buildEntry='scripts/build/build.py',dependencyManifest='dependencies/toolchain.json',vendorSource='dependencies/openssl-sha/source.json',buildReceiptSha256=verification['buildReceiptSha256']),
  components=components,games=[dict(game=g['game'],dataRelease=g['release'],directory=g['directory'],gamePackManifestSha256=next(x['sha256'] for x in g['inner'] if x['name']=='game-pack.tsv'),sourceBuildReceiptSha256=g['sourceBuildReceiptSha256'],volumeNames=[v['name'] for v in g['volumes']]) for g in media['games']],
  compatibility=dict(commonManager='GEHD_Common_Manager',selection='Restore every volume of the selected game; select its complete GamePack directory in the manager',doNotMixGames=True,innerNamesUnchanged=True,payloadUnchanged=True),qualification='VERIFICATION.json',releaseNotes='RELEASE_NOTES.md',attachments=attachments)
 write(a.out/'release-manifest.json',manifest)
 sums=attachments+[pin(a.out/'release-manifest.json')]
 (a.out/'SHA256SUMS.txt').write_text(''.join(p['sha256']+'  '+p['name']+'\n' for p in sorted(sums,key=lambda p:p['name'])),encoding='utf-8')
 print('Sealed local candidate:',version,'attachments:',len(attachments),'source:',export['sourceCommit'])
if __name__=='__main__':main()
