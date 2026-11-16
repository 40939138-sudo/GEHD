"""Verify a downloaded local candidate, optionally restore one game's outer volumes."""
from pathlib import Path
import argparse,hashlib,json,subprocess
from split_zip import restore
def sha(p):
 h=hashlib.sha256()
 with p.open('rb') as f:
  while chunk:=f.read(16*1024*1024):h.update(chunk)
 return h.hexdigest().upper()
def safe(root,name):
 if not isinstance(name,str) or not name or '/' in name or '\\' in name or ':' in name or name in ('.','..'):raise ValueError('Unsafe attachment name')
 return root/name
def check(root,records):
 seen=set()
 for item in records:
  name=item['name'];p=safe(root,name)
  if name in seen:raise ValueError('Duplicate attachment')
  seen.add(name)
  if not p.is_file() or p.stat().st_size!=item['bytes'] or sha(p)!=item['sha256']:raise ValueError('Missing or changed attachment: '+name)
def selected(media,game):
 matches=[p for p in media['games'] if p['game']==game]
 if len(matches)!=1:raise ValueError('Missing or ambiguous game')
 item=matches[0]
 if item['directory']!=game+'_GamePack':raise ValueError('Wrong game directory')
 prefix=f"GEHD-{game}-{media['version']}.zip."
 for i,v in enumerate(item['volumes'],1):
  if v['name']!=prefix+f'{i:03d}':raise ValueError('Wrong game, missing or unordered volume')
 if not item['volumes']:raise ValueError('No volumes')
 return item
def main():
 ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('directory',type=Path);ap.add_argument('--game',choices=['GER','GE2RB']);ap.add_argument('--out',type=Path);a=ap.parse_args()
 manifest=json.loads((a.directory/'release-manifest.json').read_text(encoding='utf-8'))
 attachments={p['name']:p for p in manifest['attachments']}
 if len(attachments)!=len(manifest['attachments']):raise ValueError('Duplicate release attachment')
 check(a.directory,[attachments['media-manifest.json']] if a.game else manifest['attachments'])
 media=json.loads((a.directory/'media-manifest.json').read_text(encoding='utf-8'))
 if media['version']!=manifest['version']:raise ValueError('Media/release version mismatch')
 for record in [media['manager']]+[v for p in media['games'] for v in p['volumes']]:
  if attachments.get(record['name'])!=record:raise ValueError('Media missing from release attachment identities')
 for game in ('GER','GE2RB'):selected(media,game)
 if a.game:
  if not a.out or a.out.exists():ap.error('--game requires a new --out directory')
  pkg=selected(media,a.game);check(a.directory,[media['manager'],*pkg['volumes']])
  restore([a.directory/v['name'] for v in pkg['volumes']],a.out,pkg['directory'],pkg['inner'])
  actual={p.name for p in (a.out/pkg['directory']).iterdir()}
  if actual!={p['name'] for p in pkg['inner']}:raise ValueError('Unexpected extracted files')
 print('PASS: selected game and manager verified; selected game restored' if a.game else 'PASS: all attachments verified')
if __name__=='__main__':main()
