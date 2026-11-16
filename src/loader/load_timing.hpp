#pragma once
// Opt-in diagnostic only. No file writes, allocations or locks on a texture call.
// Fixed 11 MiB capture buffer; a process-lifetime worker writes committed records.
// This measures CPU wall time in the hook, not GPU completion or scene load time.
namespace loadtiming {
enum Stage : unsigned { None=8, HashLookup=0, Eligibility, ReadDDS, ValidateDDS,
 NativeReplacement, ValidateResource, NativeOriginal, EventLog };
enum Outcome : unsigned { Bypass=0, Replaced, Miss, Parameters, Dimensions,
 SourceInfo, DeviceCaps, FileValidation, PrepareException, NativeFailure,
 PrefilterParameters, PrefilterSize };
#ifdef GEHD_LOAD_TIMING
constexpr unsigned capacity=65536;
struct Header {
 char magic[8]; unsigned version,headerBytes,recordBytes,limit;
 unsigned long long frequency,startQpc,startFileTime;
 unsigned pid,written,seen,dropped,ioError,cleanStop;
 unsigned reserved[14];
};
struct Record {
 unsigned sequence,thread,sourceBytes,replacementBytes,outcome; LONG hr;
 char source[64]; unsigned long long start,total,ticks[8];
};
static_assert(sizeof(Header)==128 && sizeof(Record)==168,"Trace format changed");
struct Slot { volatile LONG committed; Record record; };
Slot* slots{}; HANDLE file=INVALID_HANDLE_VALUE,worker{},stopEvent{};
volatile LONG accepting{},seen{},dropped{};
Header header{};
unsigned long long stamp() noexcept {
 const DWORD saved=GetLastError(); LARGE_INTEGER q{};QueryPerformanceCounter(&q);
 SetLastError(saved);return static_cast<unsigned long long>(q.QuadPart);
}
bool writeBytes(const void* p,DWORD bytes) noexcept {
 DWORD count{};if(!WriteFile(file,p,bytes,&count,nullptr)||count!=bytes){
  header.ioError=GetLastError();if(!header.ioError)header.ioError=ERROR_WRITE_FAULT;
  InterlockedExchange(&accepting,0);return false;
 }return true;
}
DWORD WINAPI writeLoop(void*) noexcept {
 Record batch[256];unsigned cursor=0;bool stop=false;
 do {
  stop=WaitForSingleObject(stopEvent,250)==WAIT_OBJECT_0;
  while(cursor<capacity){
   unsigned count=0;
   while(count<256 && cursor+count<capacity &&
         InterlockedCompareExchange(&slots[cursor+count].committed,0,0)){
    batch[count]=slots[cursor+count].record;++count;
   }
   if(!count)break;
   LARGE_INTEGER offset{};offset.QuadPart=sizeof(Header)+static_cast<LONGLONG>(cursor)*sizeof(Record);
   if(!SetFilePointerEx(file,offset,nullptr,FILE_BEGIN)||!writeBytes(batch,count*sizeof(Record)))return 1;
   cursor+=count;
  }
  header.written=cursor;header.seen=static_cast<unsigned>(InterlockedCompareExchange(&seen,0,0));
  header.dropped=static_cast<unsigned>(InterlockedCompareExchange(&dropped,0,0));header.cleanStop=stop?1:0;
  LARGE_INTEGER zero{};
  if(!SetFilePointerEx(file,zero,nullptr,FILE_BEGIN)||!writeBytes(&header,sizeof(header)))return 1;
 }while(!stop);
 return FlushFileBuffers(file)?0:1;
}
// Call outside DllMain, before texture traffic. A failed trace never disables HD.
bool start(const std::filesystem::path& directory) noexcept {
 const DWORD saved=GetLastError();bool ok=false;
 try {
  if(worker) {SetLastError(saved);return false;}
  std::filesystem::create_directories(directory);
  auto path=directory/(L"timing-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64())+L".bin");
  file=CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
  LARGE_INTEGER freq{};FILETIME utc{};GetSystemTimeAsFileTime(&utc);
  if(file!=INVALID_HANDLE_VALUE && QueryPerformanceFrequency(&freq) && freq.QuadPart>0){
   slots=static_cast<Slot*>(VirtualAlloc(nullptr,sizeof(Slot)*capacity,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
   stopEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);
   header={};memcpy(header.magic,"GEHDTM1",8);header.version=1;header.headerBytes=sizeof(Header);
   header.recordBytes=sizeof(Record);header.limit=capacity;header.frequency=freq.QuadPart;
   header.startQpc=stamp();header.startFileTime=(static_cast<unsigned long long>(utc.dwHighDateTime)<<32)|utc.dwLowDateTime;
   header.pid=GetCurrentProcessId();seen=0;dropped=0;
   if(slots&&stopEvent&&writeBytes(&header,sizeof(header))){
    worker=CreateThread(nullptr,0,writeLoop,nullptr,0,nullptr);
    if(worker){InterlockedExchange(&accepting,1);ok=true;}
   }
  }
 }catch(...){}
 if(!ok){
  if(slots){VirtualFree(slots,0,MEM_RELEASE);slots=nullptr;}
  if(stopEvent){CloseHandle(stopEvent);stopEvent=nullptr;}
  if(file!=INVALID_HANDLE_VALUE){CloseHandle(file);file=INVALID_HANDLE_VALUE;}
 }
 SetLastError(saved);return ok;
}
// Fixtures only, after all producers have joined. Never wait inside DllMain.
bool stop() noexcept {
 const DWORD saved=GetLastError();InterlockedExchange(&accepting,0);bool ok=false;
 if(worker){SetEvent(stopEvent);WaitForSingleObject(worker,INFINITE);DWORD code=1;GetExitCodeThread(worker,&code);ok=code==0;
  CloseHandle(worker);worker=nullptr;CloseHandle(stopEvent);stopEvent=nullptr;
  CloseHandle(file);file=INVALID_HANDLE_VALUE;VirtualFree(slots,0,MEM_RELEASE);slots=nullptr;}
 SetLastError(saved);return ok;
}
class Request {
 Slot* slot{};Stage current=None;unsigned long long last{};
public:
 explicit Request(unsigned bytes) noexcept {
  const DWORD saved=GetLastError();
  if(InterlockedCompareExchange(&accepting,0,0)){
   LONG index=InterlockedIncrement(&seen)-1;
   if(static_cast<unsigned>(index)<capacity){slot=&slots[index];auto&r=slot->record;
    r.sequence=static_cast<unsigned>(index)+1;r.thread=GetCurrentThreadId();r.sourceBytes=bytes;
    r.start=last=stamp();
   }else InterlockedIncrement(&dropped);
  }SetLastError(saved);
 }
 Request(const Request&)=delete;
 void stage(Stage next) noexcept {if(slot){auto now=stamp();if(current!=None)slot->record.ticks[current]+=now-last;last=now;current=next;}}
 void identify(const std::string& id,unsigned bytes) noexcept {if(slot){if(id.size()==64)memcpy(slot->record.source,id.data(),64);slot->record.replacementBytes=bytes;}}
 void outcome(Outcome value) noexcept {if(slot)slot->record.outcome=value;}
 HRESULT result(HRESULT value) noexcept {if(slot)slot->record.hr=value;return value;}
 ~Request() noexcept {if(slot){stage(None);slot->record.total=last-slot->record.start;InterlockedExchange(&slot->committed,1);}}
};
#else
class Request {
public:
 explicit Request(unsigned) noexcept {}
 void stage(Stage) noexcept {}
 void identify(const std::string&,unsigned) noexcept {}
 void outcome(Outcome) noexcept {}
 HRESULT result(HRESULT value) noexcept {return value;}
};
#endif
}
