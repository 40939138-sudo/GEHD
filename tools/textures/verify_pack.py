"""Independent readback: 2x dimensions, complete DDS length, source mip bytes."""
import sys,json,collections
from pathlib import Path
from dds import sha,dds_info

if not __debug__:
    raise RuntimeError('Verification requires Python without -O/-OO')
def verify(root):
    lines=(root/'mapping.tsv').read_text(encoding='ascii').splitlines()
    assert lines[0]=='GEHD_TEXTURE_PACK_2X_V1'
    seen=set();formats=collections.Counter();source_bytes=output_bytes=0
    for line in lines[1:]:
        oldhash,newhash,oldsize,newsize,w,h,m,fmt=line.split();w,h,m,fmt=map(int,(w,h,m,fmt))
        assert oldhash not in seen;seen.add(oldhash)
        old=(root/'original'/f'{oldhash}.dds').read_bytes();new=(root/'2x'/f'{oldhash}.dds').read_bytes()
        assert sha(old)==oldhash and sha(new)==newhash
        oi,ni=dds_info(old),dds_info(new)
        assert len(old)==int(oldsize) and len(new)==int(newsize)
        assert (ni['width'],ni['height'],ni['mips'])==(w,h,m)==(oi['width']*2,oi['height']*2,oi['mips']+1)
        assert ni['format']==oi['format']
        assert (int.from_bytes(new[84:88],'little')==fmt if ni['format']!='RGB32' else fmt==21 and new[76:108]==old[76:108])
        assert new[128+ni['levelBytes'][0]:]==old[128:]
        formats[oi['format']]+=1;source_bytes+=len(old);output_bytes+=len(new)
    result=dict(status='PASS_FILES_ONLY',count=len(seen),formats=formats,sourceBytes=source_bytes,outputBytes=output_bytes,allLowerMipsByteExact=True,gameVisibleImprovementProven=False)
    (root/'verification.json').write_text(json.dumps(result,indent=2),encoding='utf-8');print(json.dumps(result))
if __name__=='__main__':verify(Path(sys.argv[1]))
