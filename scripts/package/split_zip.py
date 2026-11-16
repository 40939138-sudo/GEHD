"""Standard ZIP64 stored archive, byte-split as .zip.001 (7-Zip compatible)."""
from pathlib import Path
import io,zipfile,hashlib,bisect
BLOCK=16*1024*1024
class Writer(io.RawIOBase):
 def __init__(self,base,limit):
  if limit<1:raise ValueError('Positive volume size required')
  self.base=base;self.limit=limit;self.position=0;self.stream=None;self.number=0
 def writable(self):return True
 def seekable(self):return False
 def tell(self):return self.position
 def write(self,data):
  view=memoryview(data);length=len(view)
  while view:
   if self.stream is None or self.position%self.limit==0:
    if self.stream:self.stream.close()
    self.number+=1;self.stream=Path(str(self.base)+f'.{self.number:03d}').open('xb')
   count=min(len(view),self.limit-self.position%self.limit)
   self.stream.write(view[:count]);self.position+=count;view=view[count:]
  return length
 def flush(self):
  if self.stream and not self.stream.closed:self.stream.flush()
 def close(self):
  if self.stream:self.stream.close()
  super().close()
class Reader(io.RawIOBase):
 def __init__(self,volumes):
  self.volumes=[Path(p) for p in volumes];self.ends=[];self.position=0;self.stream=None;self.index=-1
  total=0
  for p in self.volumes:total+=p.stat().st_size;self.ends.append(total)
  if not total:raise ValueError('Empty archive')
 def readable(self):return True
 def seekable(self):return True
 def tell(self):return self.position
 def seek(self,offset,whence=0):
  if whence not in (0,1,2):raise ValueError('Invalid whence')
  target=offset+(0 if whence==0 else self.position if whence==1 else self.ends[-1])
  if target<0:raise ValueError('Negative seek')
  self.position=target;return target
 def read(self,size=-1):
  remaining=self.ends[-1]-self.position
  size=max(0,remaining if size<0 else min(size,remaining));parts=[]
  while size:
   index=bisect.bisect_right(self.ends,self.position)
   if self.index!=index:
    if self.stream:self.stream.close()
    self.stream=self.volumes[index].open('rb');self.index=index
   start=0 if index==0 else self.ends[index-1];self.stream.seek(self.position-start)
   count=min(size,self.ends[index]-self.position);chunk=self.stream.read(count)
   if len(chunk)!=count:raise ValueError('Truncated volume')
   parts.append(chunk);size-=count;self.position+=count
  return b''.join(parts)
 def close(self):
  if self.stream:self.stream.close()
  super().close()
def create(source,base,limit=1900*1024*1024):
 with Writer(base,limit) as stream,zipfile.ZipFile(stream,'w',compression=zipfile.ZIP_STORED,allowZip64=True) as archive:
  for p in sorted(source.iterdir()):
   if not p.is_file() or p.is_symlink():raise ValueError('Ordinary flat GamePack required')
   info=zipfile.ZipInfo(source.name+'/'+p.name,date_time=(2026,1,1,0,0,0));info.file_size=p.stat().st_size
   with p.open('rb') as inp,archive.open(info,'w',force_zip64=True) as out:
    while chunk:=inp.read(BLOCK):out.write(chunk)
   print('Wrapped',source.name,p.name,flush=True)
 return [Path(str(base)+f'.{i:03d}') for i in range(1,stream.number+1)]
def restore(volumes,destination,directory,expected):
 if directory not in ('GER_GamePack','GE2RB_GamePack'):raise ValueError('Unsupported GamePack directory')
 if destination.exists():raise ValueError('Fresh extraction directory required')
 records={directory+'/'+x['name']:x for x in expected}
 with Reader(volumes) as stream,zipfile.ZipFile(stream) as archive:
  names=archive.namelist()
  if len(names)!=len(set(names)) or set(names)!=set(records):raise ValueError('Unexpected, missing or duplicate archive member')
  for name in names:
   info=archive.getinfo(name);record=records[name]
   if info.file_size!=record['bytes'] or info.compress_type!=zipfile.ZIP_STORED:raise ValueError('Unexpected member layout')
   if any(c in record['name'] for c in ('/','\\',':')) or record['name'] in ('.','..'):raise ValueError('Unsafe member')
  destination.mkdir(parents=True,exist_ok=False);folder=destination/directory;folder.mkdir()
  result=[]
  for name in names:
   record=records[name];h=hashlib.sha256()
   with archive.open(name) as inp,(folder/record['name']).open('xb') as out:
    while chunk:=inp.read(BLOCK):h.update(chunk);out.write(chunk)
   if h.hexdigest().upper()!=record['sha256']:raise ValueError('Restored hash mismatch: '+name)
   result.append(record)
 return result
