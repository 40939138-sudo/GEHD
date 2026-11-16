"""Conservative 2x compatibility pack: cubic resampling, original channels/formats.
This is not AI detail synthesis. Preserve original lower mip bytes for BCn files.
"""
import sys,json,struct,subprocess,concurrent.futures,os,collections,time
from pathlib import Path
from dds import dds_info,sha

def build(root,texconv):
    inventory=json.loads((root/'inventory.json').read_text(encoding='utf-8'))
    dst=root/'2x';dst.mkdir(exist_ok=False)
    temp=root/'conversion';temp.mkdir(exist_ok=False)
    logs=root/'conversion-logs';logs.mkdir(exist_ok=False)
    def one(item):
        key=item['sha256'];src=root/'original'/f'{key}.dds';raw=src.read_bytes()
        if sha(raw)!=key:raise RuntimeError('SOURCE_CHANGED')
        info=dds_info(raw);w,h=info['width']*2,info['height']*2
        rgb=info['format']=='RGB32' and struct.unpack_from('<8I',raw,76)==(32,65,0,32,0xff0000,0xff00,0xff,0xff000000)
        if info['format'] not in ('DXT1','DXT3','DXT5') and not rgb:return dict(source=key,status='SKIPPED',reason='FORMAT_'+info['format'])
        if max(w,h)>8192:return dict(source=key,status='SKIPPED',reason='DIMENSION_CAP')
        # Generate only the new top level. The original mip chain becomes levels
        # 1..n, exactly preserving old small-distance samples and authored mips.
        cmd=[str(texconv),'-nologo','-w',str(w),'-h',str(h),'-m','1','-f','B8G8R8A8_UNORM' if rgb else info['format'],'-if','CUBIC','-sepalpha','-nowic','-dx9','-nogpu','--single-proc','-o',str(temp),str(src)]
        p=subprocess.run(cmd,capture_output=True,timeout=180,creationflags=0x08000000)
        (logs/f'{key}.txt').write_bytes(p.stdout+p.stderr)
        output=temp/f'{key}.dds'
        if p.returncode or not output.exists():return dict(source=key,status='FAILED',reason='TEXCONV',exitCode=p.returncode)
        top=output.read_bytes();ti=dds_info(top)
        if (ti['width'],ti['height'],ti['mips'],ti['format'])!=(w,h,1,info['format']):raise RuntimeError('TOP_LAYOUT')
        header=bytearray(top[:128]);levels=info['mips']+1
        if rgb and top[76:108]!=raw[76:108]:raise RuntimeError('RGB_MASK_MISMATCH')
        struct.pack_into('<I',header,8,struct.unpack_from('<I',header,8)[0]|0x20000)
        struct.pack_into('<I',header,28,levels)
        struct.pack_into('<I',header,108,0x401008)
        new=bytes(header)+top[128:]+raw[128:]
        ni=dds_info(new)
        target=dst/f'{key}.dds';target.write_bytes(new)
        if target.read_bytes()!=new:raise RuntimeError('READBACK')
        return dict(source=key,status='BUILT',replacement=sha(new),sourceSize=len(raw),size=len(new),width=w,height=h,mips=levels,format=info['format'],formatCode=21 if rgb else int.from_bytes(info['format'].encode('ascii'),'little'),originalLowerMipsExact=True)
    results=[]
    with (root/'conversion.jsonl').open('x',encoding='utf-8') as receipt:
        with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
            for r in pool.map(one,inventory['textures']):
                results.append(r);receipt.write(json.dumps(r)+'\n');receipt.flush()
                if len(results)%100==0:print(json.dumps(dict(processed=len(results),total=len(inventory['textures']),statuses=dict(collections.Counter(x['status'] for x in results)))),flush=True)
    good=[x for x in results if x['status']=='BUILT']
    with (root/'mapping.tsv').open('x',encoding='ascii',newline='\n') as f:
        f.write('GEHD_TEXTURE_PACK_2X_V1\n')
        for r in good:
            fmt=r['formatCode']
            f.write(f"{r['source']}\t{r['replacement']}\t{r['sourceSize']}\t{r['size']}\t{r['width']}\t{r['height']}\t{r['mips']}\t{fmt}\n")
    report=dict(status='BUILT_TRIAL_NOT_VISUAL_ACCEPTANCE',method='CUBIC_2X_NEW_TOP_ORIGINAL_MIPS_PRESERVED',aiUpscale=False,texconvSha256=sha(texconv.read_bytes()),inventorySha256=sha((root/'inventory.json').read_bytes()),counts=dict(collections.Counter(x['status'] for x in results)),sourceBytes=sum(x['sourceSize'] for x in good),replacementBytes=sum(x['size'] for x in good),mappingSha256=sha((root/'mapping.tsv').read_bytes()),results=results)
    (root/'build.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    print(json.dumps({k:v for k,v in report.items() if k!='results'}),flush=True)
if __name__=='__main__':build(Path(sys.argv[1]),Path(sys.argv[2]))
