"""Join every GE2RB shipped DDS to its selected production/repair record.

Final-output matches deliberately exclude before/legacy/master/observation hashes.
The retained inputs are metadata; this does not execute pixel synthesis.
"""
from pathlib import Path
import csv, hashlib, json, re

def output_hash(row):
    values=[]
    for key in ('dds_sha256','replacement_sha256','replacement','replacementSha256'):
        if isinstance(row.get(key),str):values.append(row[key].upper())
    dds=row.get('dds')
    if isinstance(dds,dict):
        for key in ('sha256','dds_sha256'):
            if isinstance(dds.get(key),str):values.append(dds[key].upper())
    line=row.get('mapping_line')
    if isinstance(line,str) and len(line.split('\t'))==9:values.append(line.split('\t')[1].upper())
    values={x for x in values if re.fullmatch('[0-9A-F]{64}',x)}
    if len(values)>1:raise ValueError('Conflicting selected output identities')
    return next(iter(values),None)

def row_lists(data):
    if isinstance(data,list):return data
    result=[]
    for key in ('items','inherited_items','rows','records'):
        if isinstance(data.get(key),list):result.extend(data[key])
    return result

def export(root,out,safe):
    def read(p):return json.loads(p.read_text(encoding='utf-8-sig'))
    def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest().upper()
    def write(name,value):(out/name).write_text(json.dumps(safe(value),ensure_ascii=False,separators=(',',':'))+'\n',encoding='utf-8')
    production=root/'toolchain/texture-detailed-classification-r1/whole-redraw-cross-type/tvt-batch/production'
    current=production/'current-color-preview'
    stages=[current/'selection.json']
    stages += [current/folder/'selection.json' for folder in ('completed-revisions-update','skin-color-restored-update','builwall-glass-update','recorded-text-update','sky-seam-update')]
    stages += [production/'inherited-fx852/file-verification.json']
    stages += sorted((production/'inherited-fx852').glob('*/selection.json'))
    stages += [root/'toolchain/chinese-hd-compatibility-r1/localized-hd-delivery/selection.json']
    # The final cumulative repair selection, pinned by the shipping pack binding.
    pack=read(root/'顏色高清完整預覽包_TVT特效852/binding.json')
    repair=Path(pack['joinRepairSelection']['path'])
    assert digest(repair)==pack['joinRepairSelection']['sha256'].upper()
    stages += [repair]
    target={r['sourceIdentity']:r for r in read(out/'GE2RB-mapping.json')['textures']}
    selected={};receipts=[]
    for p in stages:
        data=read(p);label=p.relative_to(root).as_posix();count=0
        for row in row_lists(data):
            src=row.get('source')
            if src in target and output_hash(row)==target[src]['replacementSha256'].upper():
                selected[src]={'sourceIdentity':src,'replacementSha256':output_hash(row),
                  'selectedStage':label,'selectedRecord':row};count+=1
        receipts.append({'path':label,'sha256':digest(p),'matchingFinalOutputs':count})
        print('Joined',label,count,'coverage',len(selected),flush=True)
    missing=sorted(set(target)-set(selected))
    assert not missing, 'Missing selected production records: '+str(missing)
    # Earlier per-source processing routes are context; superseded ones are labelled.
    csvpath=current/'processing-provenance/all8131.csv'
    baseline={r['source']:r for r in csv.DictReader(csvpath.open(encoding='utf-8-sig',newline=''))}
    recipe_paths=sorted({r['recipe'] for r in baseline.values() if r.get('recipe')})
    recipes=[]
    for name in recipe_paths:
        p=Path(name);entry={'path':name,'existsLocally':p.is_file()}
        if p.is_file():
            entry['sha256']=digest(p)
            if p.suffix.lower()=='.json':
                data=read(p)
                if isinstance(data,dict):
                    # Keep family settings/model pins without duplicating huge asset tables.
                    excluded={'items','textures','jobs','records','results','rows','inputs','files','sources','source_rows','segments','mipChecks'}
                    entry['settings']={k:v for k,v in data.items() if k not in excluded}
                else:entry['settingsStatus']='List receipt; refer to per-source production record'
            else:entry['settingsStatus']='Producer source reference, not an executable public pipeline'
        recipes.append(entry)
    for src,r in selected.items():
        prior=baseline.get(src)
        if prior:
            r['baselineProcessing']={k:prior[k] for k in ('name','category','processing','alpha','recipe','evidence','prior_tvt_receipt','selected_rgb_uses_tvt')}
            r['baselineProcessing']['sameOutputAsFinal']=prior['dds_sha256'].upper()==r['replacementSha256']
            r['baselineProcessing']['scope']='Earlier processing route; if superseded, final method is in selectedRecord'
        else:r['baselineProcessing']={'scope':'New localized source; use selectedRecord original/master/record bindings'}
    # Compact sharding keeps individual research files readable on GitHub.
    ordered=[selected[k] for k in sorted(selected)];shards=[]
    for offset in range(0,len(ordered),400):
        name=f'GE2RB-lineage-{offset//400+1:02d}.json';rows=ordered[offset:offset+400]
        write(name,{'schema':'GEHD_SELECTED_TEXTURE_LINEAGE_V1','textures':rows})
        shards.append({'file':name,'rows':len(rows)})
    write('GE2RB-lineage-index.json',{'schema':'GEHD_SELECTED_LINEAGE_INDEX_V1','release':'v0.1.0-preview.1',
      'total':len(target),'matchedFinalSelections':len(selected),'missing':missing,'shards':shards,'stageReceipts':receipts,
      'baselineProcessingReceipt':{'path':str(csvpath.relative_to(root)),'sha256':digest(csvpath)},
      'matchRule':'Same source identity AND selected DDS output SHA256; excludes old/before/master/observational matches',
      'scope':'Every final output has a production/selection/repair record. This is not a claim that all historical reference files/images/models are publicly present or that every pixel generation is deterministic.'})
    write('production-parameter-catalog.json',{'references':recipes,
      'scope':'Earlier per-source recipe references are linked from baselineProcessing; use sameOutputAsFinal to distinguish final and superseded processing. Family settings are not inferred for unmatched stages.'})
    return {'GE2RBSelectedProductionRecordsMatched':len(selected),'GE2RBRemainingLineageGap':len(missing),
            'GE2RBLineageShards':len(shards),'GE2RBRecipeReferences':len(recipes),
            'GE2RBRecipeReferencesMissingLocally':sum(not x['existsLocally'] for x in recipes)}

if __name__=='__main__':
    # Small failure controls: old observations and other image hashes cannot qualify.
    h='A'*64
    assert output_hash({'before':{'GE2RB':h},'master_sha256':h}) is None
    assert output_hash({'source':'B'*64,'dds':{'sha256':h}})==h
    try:output_hash({'replacement':h,'dds_sha256':'B'*64})
    except ValueError:pass
    else:raise AssertionError('Conflicting hashes accepted')
    print('PASS: final-output matching excludes prior/master hashes and rejects conflicts')
