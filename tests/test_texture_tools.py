"""Exercise DDS conversion/readback and corrupt-output rejection with generated pixels."""
from pathlib import Path
import argparse,importlib.util,json,struct,sys
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'tools/textures'))
from dds import sha,dds_info
from build_pack import build
from verify_pack import verify
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--texconv',type=Path,required=True);ap.add_argument('--out',type=Path,required=True);a=ap.parse_args()
 a.out.mkdir(parents=True,exist_ok=False);(a.out/'original').mkdir();items=[]
 for fmt in ['DXT1','DXT3','DXT5','RGB32']:
  b=bytearray(128);b[:4]=b'DDS '
  for off,n in [(4,124),(8,0x2100f),(12,8),(16,8),(20,32),(28,2),(76,32),(80,65 if fmt=='RGB32' else 4),(108,0x401008)]:struct.pack_into('<I',b,off,n)
  if fmt=='RGB32':
   for off,n in [(88,32),(92,0xff0000),(96,0xff00),(100,0xff),(104,0xff000000)]:struct.pack_into('<I',b,off,n)
   data=bytes((i%256 for i in range(320)))
  else:
   b[84:88]=fmt.encode();data=bytes(5*(8 if fmt=='DXT1' else 16))
  raw=bytes(b)+data;key=sha(raw);info=dds_info(raw)
  (a.out/'original'/f'{key}.dds').write_bytes(raw);items.append(dict(sha256=key,**info))
 (a.out/'inventory.json').write_text(json.dumps(dict(textures=items)),encoding='utf-8')
 build(a.out,a.texconv.resolve());verify(a.out)
 result=json.loads((a.out/'verification.json').read_text());assert result['count']==4
 target=next((a.out/'2x').glob('*.dds'));original=target.read_bytes();target.write_bytes(original[:-1]+bytes([original[-1]^255]))
 rejected=False
 try:verify(a.out)
 except AssertionError:rejected=True
 finally:target.write_bytes(original)
 if not rejected:raise RuntimeError('Changed DDS accepted')
 for malformed in [b'',b'DDS '+bytes(124),original[:-1]]:
  try:dds_info(malformed)
  except ValueError:continue
  raise RuntimeError('Malformed DDS accepted')
 (a.out/'qualification.json').write_text(json.dumps(dict(passed=True,formats=4,lowerMipsExact=True,changedPayloadRejected=True,malformedLayoutsRejected=3,gameExecuted=False),indent=2)+'\n',encoding='utf-8')
if __name__=='__main__':main()
