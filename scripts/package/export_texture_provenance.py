"""Export selected production metadata for the frozen preview, without images/weights.

This reads retained local receipts, not the public source archive alone. It does
not run inference, install a mod, or prove that all production steps can rerun.
"""
from pathlib import Path
import hashlib, json, re

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'dependencies/texture-production'
OUT.mkdir(exist_ok=True)

def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest().upper()
def read(p): return json.loads(p.read_text(encoding='utf-8-sig'))
def safe(value):
    if isinstance(value, dict): return {k:safe(v) for k,v in value.items()}
    if isinstance(value, list): return [safe(v) for v in value]
    if isinstance(value, str):
        value=value.replace(str(ROOT)+'\\','').replace(str(ROOT).replace('\\','/')+'/', '')
        if re.match(r'^[A-Za-z]:[\\/]', value):
            value='external-model-reference/'+value.replace('\\','/').split('/tvt-models/')[-1] if '/tvt-models/' in value.replace('\\','/') else 'external-reference/'+value.replace('\\','/').split('/')[-1]
        return value.replace('\\','/')
    return value
def write(name, data):
    (OUT/name).write_text(json.dumps(safe(data),ensure_ascii=False,separators=(',',':'))+'\n',encoding='utf-8')

media=read(ROOT/'dist/github/v0.1.0-preview.1/media-manifest.json')
summary={'schema':'GEHD_TEXTURE_PROVENANCE_V1','release':'v0.1.0-preview.1','games':[],
 'limits':['Metadata export; not a complete image-generation pipeline',
 'Mapping source identity is the loader DDS hash, not proof of an original file SHA256',
 'Local evidence paths are provenance labels, not downloadable files',
 'Historical recipes are scoped references, not a single recipe applied to every shipped texture',
 'Game source images, model weights and all intermediate masters are not included']}
maps={}
for game,folder in [('GER','p5-ger-full-distribution'),('GE2RB','p5-ge2rb-current-distribution')]:
    p=ROOT/'toolchain/mod-installer-r1/shared-framework-r1'/folder/'build.json'
    build=read(p);g=next(x for x in media['games'] if x['game']==game)
    assert sha(p)==g['sourceBuildReceiptSha256'] and build['release']==g['release']
    data=Path(build['dataRoot']);mapping=data/'mapping.tsv';release=data/'release.files'
    assert sha(release)==g['release']
    shipped={Path(x['path']).name:x for x in build['files'] if x['path'].endswith('.dds')}
    rows=[]
    for line in mapping.read_text(encoding='utf-8-sig').splitlines()[1:]:
        c=line.split('\t')
        if not c or not c[0]:continue
        assert len(c)==9
        src,dst=c[:2];rec=shipped[src+'.dds']
        assert rec['sha256'].upper()==dst.upper() and rec['bytes']==int(c[3])
        rows.append({'sourceIdentity':src,'replacementSha256':dst,'sourceBytes':int(c[2]),'outputBytes':int(c[3]),
                     'width':int(c[4]),'height':int(c[5]),'mips':int(c[6]),'formatCode':int(c[7]),'scale':int(c[8])})
    assert len(rows)==len(shipped) and len({r['sourceIdentity'] for r in rows})==len(rows)
    write(game+'-mapping.json',{'columns':'sourceIdentity is the game loader identity; replacementSha256 is full shipped DDS SHA256','textures':rows})
    maps[game]={r['sourceIdentity']:r for r in rows}
    summary['games'].append({'game':game,'textures':len(rows),'dataRelease':g['release'],'mappingSha256':sha(mapping),
       'distributionBuildSha256':sha(p),'allRowsMatchFrozenDistributionFileTable':True})

# Final GER source/repair lineage, including decisions made after initial AI inference.
p=ROOT/'GER顏色高清預覽包_r1/source-manifest.json';items=read(p)
assert len(items)==len(maps['GER'])
for row in items:
    assert row['dds_sha256'].upper()==maps['GER'][row['source']]['replacementSha256'].upper()
write('GER-lineage.json',{'sourceManifestSha256':sha(p),'scope':'All rows match shipped GER DDS hashes; record/verification paths reference retained local originals','items':items})

# Initial conversion can only explain unchanged final outputs, not later repairs.
p=ROOT/'toolchain/texture-color-coverage-production-r1/conversion.json';conversion=read(p)
keys=['source','replacement','productionMode','scale','masterSha256','method','recipe','model','modelIdentity','sourceSize']
matched=[]
for row in conversion['textures']:
    r=maps['GE2RB'].get(row['source'])
    if r and row.get('replacement','').upper()==r['replacementSha256'].upper():
        matched.append({k:row[k] for k in keys if k in row})
write('GE2RB-initial-conversion-match.json',{'receiptSha256':sha(p),'matchingFinalOutputs':len(matched),
 'unresolvedOrChangedSinceThisReceipt':len(maps['GE2RB'])-len(matched),'items':matched,
 'scope':'Exact replacement hash matches only; unmatched rows require later production/repair records'})

binding=read(ROOT/'顏色高清完整預覽包_TVT特效852/binding.json')
assert binding['files']['release.files'].upper()==summary['games'][1]['dataRelease']
write('GE2RB-repair-summary.json',{'scope':'Retained final binding metadata; counts can overlap and do not contain complete per-pixel repair recipes','binding':binding})

general=ROOT/'toolchain/texture-color-coverage-production-r1/production-binding.json'
tvt=ROOT/'toolchain/ger-color-hd-r1/production/segments/001/manifest.json'
model=ROOT/'toolchain/texture-ai-models-r1/sources.json'
g=read(general);t=read(tvt)
write('model-and-parameter-references.json',{'generalSR':{'receiptSha256':sha(general),'record':g,
 'scope':'Coverage-production family only; unchanged/reused and repaired items may use other methods'},
 'TVT':{'receiptSha256':sha(tvt),'recipe':t['recipe'],'modelPins':t['model_pins'],
 'scope':'GER initial production segment 001; later selections/repairs can supersede it',
 'sourceJobs':[{'sourceIdentity':j['asset_id'],'rgbInputSha256':j.get('source_sha256'),'alphaInputSha256':j.get('alpha_sha256')} for j in t['jobs']]},
 'upstreamModelSources':read(model),'modelSourceReceiptSha256':sha(model)})
summary['GERFinalLineageRows']=len(items)
summary['GE2RBInitialConversionExactMatches']=len(matched)
repair_rows=[];evidence=[];ge2_covered={r['source'] for r in matched}
for p in sorted((ROOT/'toolchain/join-repair-delivery').glob('*/*/manifest.json')):
    game=p.parent.name.removesuffix('-delivery')
    if game not in maps: continue
    d=read(p);selected=[]
    for r in d.get('items',[]):
        identity=r.get('source');output=r.get('dds',{})
        if identity in maps[game] and output.get('sha256','').upper()==maps[game][identity]['replacementSha256'].upper():
            selected.append(r)
            repair_rows.append({'game':game,'sourceIdentity':identity,'receipt':str(p.relative_to(ROOT)),'record':r})
            if game=='GE2RB':ge2_covered.add(identity)
    evidence.append({'path':str(p.relative_to(ROOT)),'sha256':sha(p),'matchedFinalOutputs':len(selected)})
write('final-repair-records.json',{'scope':'Only repair records whose output DDS SHA256 equals the frozen release; before hashes are historical references, not full published images',
 'receipts':evidence,'items':repair_rows})
summary['finalRepairRecordsMatched']=len(repair_rows)
summary['GE2RBFinalOutputsWithMatchedInitialOrRepairRecord']=len(ge2_covered)
summary['GE2RBRemainingLineageGap']=len(maps['GE2RB'])-len(ge2_covered)
from trace_texture_lineage import export as export_lineage
summary.update(export_lineage(ROOT,OUT,safe))
summary['files']=[{'name':p.name,'bytes':p.stat().st_size,'sha256':sha(p)} for p in sorted(OUT.glob('*.json')) if p.name!='index.json']
write('index.json',summary)
print(json.dumps({k:v for k,v in summary.items() if k!='files'},ensure_ascii=True))
