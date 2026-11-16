"""Negative handoff checks use tiny fixtures, never alter release files."""
import importlib.util,json,tempfile,hashlib,unittest,sys,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts/package'))
spec=importlib.util.spec_from_file_location('verify',ROOT/'scripts/package/verify_release.py');v=importlib.util.module_from_spec(spec);spec.loader.exec_module(v)
class Handoff(unittest.TestCase):
 def test_selected_download_does_not_require_other_game(self):
  from split_zip import create
  with tempfile.TemporaryDirectory() as t:
   root=Path(t);src=root/'GER_GamePack';src.mkdir();(src/'fixture.txt').write_text('synthetic')
   pin=lambda p:dict(name=p.name,bytes=p.stat().st_size,sha256=v.sha(p))
   volumes=create(src,root/'GEHD-GER-v0.1.0-preview.1.zip')
   manager=root/'manager.zip';manager.write_bytes(b'synthetic manager fixture')
   media=dict(version='v0.1.0-preview.1',manager=pin(manager),games=[dict(game='GER',directory='GER_GamePack',inner=[pin(src/'fixture.txt')],volumes=[pin(p) for p in volumes]),dict(game='GE2RB',directory='GE2RB_GamePack',volumes=[dict(name='GEHD-GE2RB-v0.1.0-preview.1.zip.001',bytes=1,sha256='A'*64)])])
   m=root/'media-manifest.json';m.write_text(json.dumps(media))
   manifest=dict(version=media['version'],attachments=[pin(m),media['manager'],*media['games'][0]['volumes'],*media['games'][1]['volumes']])
   (root/'release-manifest.json').write_text(json.dumps(manifest))
   cmd=[sys.executable,'-B',str(ROOT/'scripts/package/verify_release.py'),str(root)]
   selected_run=subprocess.run(cmd+['--game','GER','--out',str(root/'restored')],capture_output=True)
   self.assertEqual(selected_run.returncode,0,selected_run.stderr.decode())
   self.assertNotEqual(subprocess.run(cmd,capture_output=True).returncode,0)
 def test_missing_changed_and_traversal(self):
  with tempfile.TemporaryDirectory() as tmp:
   root=Path(tmp);p=root/'part.001';p.write_bytes(b'original')
   rows=[dict(name=p.name,bytes=8,sha256=v.sha(p))];v.check(root,rows)
   p.write_bytes(b'modified')
   with self.assertRaises(ValueError):v.check(root,rows)
   p.unlink()
   with self.assertRaises(ValueError):v.check(root,rows)
   for name in ['../escape','C:escape','a/b','a\\b']:
    with self.assertRaises(ValueError):v.safe(root,name)
 def test_game_and_volume_selection(self):
  def media(game='GER',directory='GER_GamePack',volume='GEHD-GER-v0.1.0-preview.1.zip.001'):
   return dict(version='v0.1.0-preview.1',games=[dict(game=game,directory=directory,volumes=[dict(name=volume)])])
  self.assertEqual(v.selected(media(),'GER')['game'],'GER')
  for m in [media(directory='GE2RB_GamePack'),media(volume='GEHD-GE2RB-v0.1.0-preview.1.zip.001'),media(volume='GEHD-GER-v0.1.0-preview.1.zip.002')]:
   with self.assertRaises(ValueError):v.selected(m,'GER')
if __name__=='__main__':unittest.main()
