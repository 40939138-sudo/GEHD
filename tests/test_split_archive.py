"""Boundary, corruption and path checks for the external ZIP volume handoff."""
from pathlib import Path
import sys,tempfile,unittest,hashlib,zipfile
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts/package'))
from split_zip import create,restore,Reader
def pin(p):return dict(name=p.name,bytes=p.stat().st_size,sha256=hashlib.sha256(p.read_bytes()).hexdigest().upper())
class SplitArchive(unittest.TestCase):
 def fixture(self,root):
  src=root/'GER_GamePack';src.mkdir();(src/'data.bin').write_bytes(bytes(range(256))*13);(src/'使用說明.txt').write_text('保留內部名稱',encoding='utf-8')
  records=[pin(p) for p in sorted(src.iterdir())];volumes=create(src,root/'fixture.zip',limit=511)
  return src,records,volumes
 def test_boundaries_crc_unicode(self):
  with tempfile.TemporaryDirectory() as t:
   root=Path(t);src,records,volumes=self.fixture(root)
   self.assertGreater(len(volumes),2);self.assertTrue(all(p.stat().st_size<=511 for p in volumes))
   restore(volumes,root/'out',src.name,records)
   self.assertEqual([pin(p) for p in sorted((root/'out'/src.name).iterdir())],records)
   with Reader(volumes) as f,zipfile.ZipFile(f) as z:self.assertIsNone(z.testzip())
 def test_missing_changed_and_wrong_members(self):
  with tempfile.TemporaryDirectory() as t:
   root=Path(t);src,records,volumes=self.fixture(root)
   with self.assertRaises((zipfile.BadZipFile,ValueError)):restore(volumes[:-1],root/'missing',src.name,records)
   original=volumes[1].read_bytes();volumes[1].write_bytes(bytes([original[0]^255])+original[1:])
   with self.assertRaises((zipfile.BadZipFile,ValueError)):restore(volumes,root/'changed',src.name,records)
   volumes[1].write_bytes(original)
   with self.assertRaises(ValueError):restore(volumes,root/'wrong','GE2RB_GamePack',records)
   self.assertFalse((root/'wrong').exists())
if __name__=='__main__':unittest.main()
