// GE2RB x86 trial: full-source SHA256 -> independent per-entry 2x/4x DDS, synchronous and lazy.
// Default build: no game package writes, texture vtable hooks, draw changes, or retained textures.
// GEHD_MIP0_COMPARE opts into reversible sampler-only diagnostic draw scopes.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d9.h>
#include <bcrypt.h>
#include <vector>
#include <string>
#include <sstream>
#include <fstream>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <atomic>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include "load_timing.hpp"
#include "loader_hash.hpp"
#include "game_profile.hpp"
#include "loading_options.hpp"
extern "C" __declspec(dllexport) const auto GEHD_GameDescriptor=gameprofile::descriptor(gameprofile::selected);
#ifdef GEHD_LOADING_FRAMEWORK
#include "loading_runtime.hpp"
#include "loading_identity.hpp"
#endif

namespace {
constexpr const char* exeHash=gameprofile::selected.executableHash;
constexpr const char* dxHash=gameprofile::selected.d3dxHash;
constexpr size_t cap=256u*1024*1024;
struct Info {UINT w,h,d,m;D3DFORMAT format;D3DRESOURCETYPE type;UINT fileFormat;};
using Fn=HRESULT(WINAPI*)(IDirect3DDevice9*,LPCVOID,UINT,UINT,UINT,UINT,DWORD,D3DFORMAT,D3DPOOL,DWORD,DWORD,D3DCOLOR,Info*,PALETTEENTRY*,IDirect3DTexture9**);
using InfoFn=HRESULT(WINAPI*)(LPCVOID,UINT,Info*);
struct Entry {std::string replacement;UINT sourceSize,size,w,h,m,format,scale;};
std::unordered_map<std::string,Entry> entries;
#ifdef GEHD_REQUEST_PREFILTER
std::unordered_set<UINT> sourceSizes;
std::atomic<unsigned> prefilterParameters{},prefilterSizes{};
#ifdef GEHD_OPTIMIZATION_TEST
bool usePrefilter=true;
#endif
#endif
std::filesystem::path root; UINT packScale=2;
HMODULE own,real9;Fn realLoad{};InfoFn sourceInfo{};
std::once_flag initOnce,realOnce;std::mutex logGate;
HANDLE logFile=INVALID_HANDLE_VALUE;
std::atomic<unsigned> sequence{},hit{},fallback{},miss{};
bool enabled=false;
#ifdef GEHD_LOADING_FRAMEWORK
// Intentionally process-lived in the pinned production DLL. No join/flush in
// DllMain or static destructors. Fixture shutdown is explicit and quiescent.
loading::Runtime* loadingRuntime=nullptr;
std::string manifestIdentity,releaseIdentity;
void startLoading(loading::Config config){
 std::unordered_map<std::string,cpuprep::Entry> catalog;
 for(const auto&[id,e]:entries)catalog.emplace(id,cpuprep::Entry{id,e.replacement,root/(e.scale==4?L"4x":L"2x")/(id+".dds"),e.sourceSize,e.size,e.w,e.h,e.m,e.format,e.scale,1});
 loadingRuntime=new loading::Runtime(std::move(catalog),std::move(config));
}
#endif
std::wstring modulePath(HMODULE m){wchar_t b[32768];DWORD n=GetModuleFileNameW(m,b,32768);return n&&n<32768?std::wstring(b,n):L"";}
std::wstring nativeFilePath(const std::filesystem::path&p){
 auto path=std::filesystem::absolute(p).lexically_normal().wstring();
 if(path.size()<3||path[1]!=L':'||path[2]!=L'\\')return L"";
 return L"\\\\?\\"+path;
}
std::vector<unsigned char> read(const std::filesystem::path& p,size_t limit){
 auto native=nativeFilePath(p);if(native.empty())return {};
 HANDLE f=CreateFileW(native.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
 if(f==INVALID_HANDLE_VALUE)return {};
 std::vector<unsigned char>b;LARGE_INTEGER n;DWORD got;
 try{if(GetFileSizeEx(f,&n)&&n.QuadPart>0&&uint64_t(n.QuadPart)<=limit){b.resize(size_t(n.QuadPart));if(!ReadFile(f,b.data(),DWORD(b.size()),&got,nullptr)||got!=b.size())b.clear();}}catch(...){CloseHandle(f);throw;}
 CloseHandle(f);return b;
}
std::string hash(const void* p,size_t n){
 unsigned char digest[32];
 auto result=loaderhash::digest(p,ULONG(n),digest);
 if(result<0)return {};static const char hex[]="0123456789ABCDEF";std::string s; s.reserve(64);
 for(auto c:digest){s+=hex[c>>4];s+=hex[c&15];}return s;
}
bool hexhash(const std::string&s){return s.size()==64&&s.find_first_not_of("0123456789ABCDEF")==std::string::npos;}
void log(const char* event,const std::string& source="",const char* reason="",HRESULT hr=0,UINT w=0,UINT h=0){
 const DWORD saved=GetLastError();try{
 auto seq=++sequence;if(seq<=12000){std::lock_guard lock(logGate);if(logFile!=INVALID_HANDLE_VALUE){
 std::ostringstream s;s<<"{\"event\":\""<<event<<"\",\"seq\":"<<seq<<",\"source\":\""<<source<<"\",\"reason\":\""<<reason<<"\",\"hr\":"<<int32_t(hr)<<",\"width\":"<<w<<",\"height\":"<<h<<",\"hits\":"<<hit<<",\"fallbacks\":"<<fallback<<",\"misses\":"<<miss;
#ifdef GEHD_REQUEST_PREFILTER
 s<<",\"prefilter_parameters\":"<<prefilterParameters<<",\"prefilter_sizes\":"<<prefilterSizes;
#endif
 s<<"}\n";
 auto t=s.str();DWORD written;WriteFile(logFile,t.data(),DWORD(t.size()),&written,nullptr);}}
 }catch(...){}SetLastError(saved);
}
UINT u32(const void* p,size_t o){UINT n;memcpy(&n,(const char*)p+o,4);return n;}
bool dds(const void*p,size_t n,UINT w,UINT h,UINT m,UINT fmt){
 if(n<128||u32(p,0)!=0x20534444||u32(p,4)!=124||u32(p,76)!=32||u32(p,16)!=w||u32(p,12)!=h||std::max(1u,u32(p,28))!=m||u32(p,112)||u32(p,24)>1)return false;
 if(fmt==D3DFMT_A8R8G8B8){
  if(u32(p,80)!=65||u32(p,84)||u32(p,88)!=32||u32(p,92)!=0xff0000||u32(p,96)!=0xff00||u32(p,100)!=0xff||u32(p,104)!=0xff000000)return false;
  size_t expected=128;for(UINT i=0;i<m;++i)expected+=size_t(std::max(1u,w>>i))*std::max(1u,h>>i)*4;return expected==n;
 }
 if(u32(p,84)!=fmt)return false;
 size_t expected=128;unsigned bs=fmt==D3DFMT_DXT1?8:16;
 if(fmt!=D3DFMT_DXT1&&fmt!=D3DFMT_DXT3&&fmt!=D3DFMT_DXT5)return false;
 for(UINT i=0;i<m;++i)expected+=size_t((std::max(1u,w>>i)+3)/4)*((std::max(1u,h>>i)+3)/4)*bs;
 return expected==n;
}
bool supportedParameters(UINT w,UINT h,UINT m,DWORD usage,D3DFORMAT fmt,D3DPOOL pool,DWORD filter,DWORD mf,D3DCOLOR color,PALETTEENTRY*palette){
 return w==0xfffffffeu&&h==0xfffffffeu&&m==0xfffffffdu&&!usage&&fmt==D3DFMT_UNKNOWN&&pool==D3DPOOL_MANAGED&&!palette&&!color&&filter==1&&mf==0xffffffffu;
}
HRESULT WINAPI replacement(IDirect3DDevice9*dev,LPCVOID src,UINT n,UINT w,UINT h,UINT m,DWORD usage,D3DFORMAT fmt,D3DPOOL pool,DWORD filter,DWORD mf,D3DCOLOR color,Info* info,PALETTEENTRY*palette,IDirect3DTexture9**out){
 DWORD incoming=GetLastError();
 loadtiming::Request timing(n);
 std::string id;
#ifdef GEHD_LOADING_FRAMEWORK
 loading::Lease lease(loadingRuntime);
#endif
 auto pass=[&]{timing.stage(loadtiming::NativeOriginal);SetLastError(incoming);auto result=realLoad(dev,src,n,w,h,m,usage,fmt,pool,filter,mf,color,info,palette,out);timing.stage(loadtiming::None);return timing.result(result);};
 auto event=[&](loadtiming::Outcome outcome,const char* name,const std::string& id,const char* reason="",HRESULT hr=0,UINT width=0,UINT height=0){timing.outcome(outcome);timing.stage(loadtiming::EventLog);log(name,id,reason,hr,width,height);timing.stage(loadtiming::None);};
 if(!enabled||!dev||!src||!out||n<128||n>cap)return pass();
 Entry e{};std::vector<unsigned char>b;Info originalInfo{};const unsigned char*prepared=nullptr;UINT preparedSize=0;
 try{
#ifdef GEHD_REQUEST_PREFILTER
#ifdef GEHD_OPTIMIZATION_TEST
 if(usePrefilter)
#endif
 {
 timing.stage(loadtiming::Eligibility);
 if(!supportedParameters(w,h,m,usage,fmt,pool,filter,mf,color,palette)){++prefilterParameters;event(loadtiming::PrefilterParameters,"prefilter",id,"PARAMETERS");return pass();}
 if(sourceSizes.find(n)==sourceSizes.end()){++prefilterSizes;event(loadtiming::PrefilterSize,"prefilter",id,"SOURCE_SIZE");return pass();}
 }
#endif
 timing.stage(loadtiming::HashLookup);id=hash(src,n);auto it=entries.find(id);timing.identify(id,0);
 if(it==entries.end()){++miss;event(loadtiming::Miss,"miss",id);return pass();}e=it->second;
 timing.stage(loadtiming::Eligibility);
 if(n!=e.sourceSize||!supportedParameters(w,h,m,usage,fmt,pool,filter,mf,color,palette)){event(loadtiming::Parameters,"skip",id,"PARAMETERS");return pass();}
 if(u32(src,0)==0x20534444 && (u32(src,16)*uint64_t(e.scale)!=e.w||u32(src,12)*uint64_t(e.scale)!=e.h)){event(loadtiming::Dimensions,"skip",id,"DIMENSIONS");return pass();}
 // GE stores pSrcInfo Width/Height in its logical image wrapper. Returning
 // replacement dimensions changes UI geometry/atlas coordinates. Preserve
 // the entire original source descriptor while returning a real scaled resource.
 if(info&&(!sourceInfo||FAILED(sourceInfo(src,n,&originalInfo))||uint64_t(originalInfo.w)*e.scale!=e.w||uint64_t(originalInfo.h)*e.scale!=e.h)){event(loadtiming::SourceInfo,"skip",id,"SOURCE_INFO");return pass();}
 D3DCAPS9 caps{};if(FAILED(dev->GetDeviceCaps(&caps))||e.w>caps.MaxTextureWidth||e.h>caps.MaxTextureHeight){event(loadtiming::DeviceCaps,"skip",id,"DEVICE_CAPS");return pass();}
 timing.stage(loadtiming::ReadDDS);
#ifdef GEHD_LOADING_FRAMEWORK
 if(loadingRuntime){
  cpuprep::Entry job{id,e.replacement,root/(e.scale==4?L"4x":L"2x")/(id+".dds"),e.sourceSize,e.size,e.w,e.h,e.m,e.format,e.scale,1};
  lease.ticket.emplace(loadingRuntime->pool.request(job,e.size<=256u*1024));const auto&s=lease.ticket->wait();
  timing.stage(loadtiming::ValidateDDS);timing.identify(id,e.size);
  if(!s.valid||!s.data){++fallback;event(loadtiming::FileValidation,"fallback",id,"FRAMEWORK_VALIDATION");return pass();}
  prepared=s.data.get();preparedSize=e.size;
 }else
#endif
 {
 b=read(root/(e.scale==4?L"4x":L"2x")/(std::wstring(id.begin(),id.end())+L".dds"),cap);
 timing.stage(loadtiming::ValidateDDS);timing.identify(id,UINT(b.size()));
 if(b.size()!=e.size||hash(b.data(),b.size())!=e.replacement||!dds(b.data(),b.size(),e.w,e.h,e.m,e.format)){++fallback;event(loadtiming::FileValidation,"fallback",id,"FILE_VALIDATION");return pass();}
 prepared=b.data();preparedSize=UINT(b.size());
 }
 }catch(...){++fallback;event(loadtiming::PrepareException,"fallback",id,"PREPARE_EXCEPTION");return pass();}
#ifdef GEHD_LOADING_FRAMEWORK
 if(loadingRuntime)loadingRuntime->observe(id.c_str(),true);
#endif
 timing.stage(loadtiming::NativeReplacement);
 Info local{};IDirect3DTexture9* tex{};SetLastError(incoming);
 HRESULT hr=realLoad(dev,prepared,preparedSize,w,h,m,usage,fmt,pool,filter,mf,color,&local,nullptr,&tex);
 timing.stage(loadtiming::ValidateResource);
 DWORD returned=GetLastError();D3DSURFACE_DESC desc{};
 bool ok=SUCCEEDED(hr)&&tex&&SUCCEEDED(tex->GetLevelDesc(0,&desc))&&desc.Width==e.w&&desc.Height==e.h&&desc.Format==e.format&&desc.Pool==pool&&desc.Usage==usage&&tex->GetLevelCount()==e.m;
 if(!ok){if(tex)tex->Release();++fallback;event(loadtiming::NativeFailure,"fallback",id,"NATIVE_LOAD",hr);return pass();}
#ifdef GEHD_LOADING_FRAMEWORK
 lease.success=id.c_str();
#endif
 if(info)*info=originalInfo;*out=tex;++hit;event(loadtiming::Replaced,"replaced",id,e.scale==4?"4X_ORIGINAL_SOURCE_INFO":"2X_ORIGINAL_SOURCE_INFO",hr,e.w,e.h);SetLastError(returned);return timing.result(hr);
}
bool parseManifest(const std::vector<unsigned char>& raw){
 if(raw.empty())return false;
 std::istringstream input(std::string(raw.begin(),raw.end()));std::string line;
 if(!std::getline(input,line))return false;
 if(!line.empty()&&line.back()=='\r')line.pop_back();
 bool mixed=line=="GEHD_TEXTURE_PACK_MIXED_V1";
 UINT scale=line=="GEHD_TEXTURE_PACK_2X_V1"?2:line=="GEHD_TEXTURE_PACK_4X_V1"?4:0; if(!mixed&&!scale)return false;
 std::unordered_map<std::string,Entry> parsed;
 while(std::getline(input,line)){if(!line.empty()&&line.back()=='\r')line.pop_back();if(line.empty())continue;std::istringstream row(line);std::string id,tail;Entry e{};e.scale=scale;
 if(!(row>>id>>e.replacement>>e.sourceSize>>e.size>>e.w>>e.h>>e.m>>e.format)||(mixed&&!(row>>e.scale))||(e.scale!=2&&e.scale!=4)||(row>>tail)||!hexhash(id)||!hexhash(e.replacement)||e.size>cap||e.sourceSize>cap||!e.w||!e.h||e.w>16384||e.h>16384||!e.m||e.m>15||!parsed.emplace(id,e).second||parsed.size()>200000)return false;}
 if(parsed.empty())return false;
#ifdef GEHD_REQUEST_PREFILTER
 // Build both structures before publishing either; a rejected manifest leaves
 // the previous complete index intact. This is bounded by manifest entries.
 std::unordered_set<UINT> sizes;for(const auto& item:parsed)sizes.insert(item.second.sourceSize);
 sourceSizes.swap(sizes);
#endif
 entries.swap(parsed);packScale=scale;return true;
}
bool resolveRoot(const std::filesystem::path& base){
 auto configPath=base/L"gehd-pack-path.txt";
 DWORD attributes=GetFileAttributesW(nativeFilePath(configPath).c_str());
 if(attributes==INVALID_FILE_ATTRIBUTES){
  DWORD error=GetLastError();
  if(error!=ERROR_FILE_NOT_FOUND&&error!=ERROR_PATH_NOT_FOUND)return false;
  root=base/L"GEHD"/L"data";return true;
 }
 if(attributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))return false;
 auto config=read(configPath,32768);
 if(config.empty())return false;
 std::string utf8(config.begin(),config.end());while(!utf8.empty()&&(utf8.back()=='\r'||utf8.back()=='\n'))utf8.pop_back();
 if(utf8.find('\0')!=utf8.npos)return false;
 int chars=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,utf8.data(),int(utf8.size()),nullptr,0);if(chars<=0)return false;
 std::wstring path(chars,0);MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,utf8.data(),int(utf8.size()),path.data(),chars);root=path;
 return root.is_absolute();
}
bool loadProductManifest(){
 auto raw=read(root/L"mapping.tsv",32*1024*1024);
 auto identity=read(root/L"pack.identity",1024);
 if(raw.empty()||identity.empty())return false;
 std::istringstream in(std::string(identity.begin(),identity.end()));std::vector<std::string>fields;std::string line;
 while(std::getline(in,line)){if(!line.empty()&&line.back()=='\r')line.pop_back();fields.push_back(line);}
 if(fields.size()!=5||fields[0]!="GEHD_PRODUCT_V1"||!gameprofile::accepts(gameprofile::selected,fields[1],fields[2])||fields[3]!=hash(raw.data(),raw.size())||!hexhash(fields[4]))return false;
#ifdef GEHD_LOADING_FRAMEWORK
 manifestIdentity=fields[3];releaseIdentity=fields[4];
#endif
 return parseManifest(raw);
}
void openProductLog(const std::filesystem::path& base){
 // Diagnostics are best-effort and never make a read-only external pack unusable.
 try{
  auto logs=base/L"GEHD"/L"logs";
  std::filesystem::create_directories(nativeFilePath(logs));
  auto path=logs/(L"game-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64())+L".jsonl");
  logFile=CreateFileW(nativeFilePath(path).c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
 }catch(...){}
}
bool loadManifest(){
 auto base=std::filesystem::path(modulePath(own)).parent_path();
 openProductLog(base);
 if(!resolveRoot(base)){log("disabled","","PACK_PATH_INVALID");return false;}
 return loadProductManifest();
}
#ifdef GEHD_LOADING_FRAMEWORK
loadingoptions::Options loadLoadingOptions(const std::filesystem::path&game){
 loadingoptions::Options options;
  auto optionPath=game/L"GEHD/state/loading-options.tsv";
  if(std::filesystem::exists(optionPath)){auto raw=read(optionPath,1024);std::istringstream in(std::string(raw.begin(),raw.end()));std::vector<std::string> rows;std::string line;while(std::getline(in,line)){if(!line.empty()&&line.back()=='\r')line.pop_back();rows.push_back(line);}options=loadingoptions::parse(rows,gameprofile::selected);}
 return options;
}
#endif
void init(){try{
 if(!loadManifest()){log("disabled","","MANIFEST");return;}
 auto exe=read(modulePath(nullptr),128*1024*1024);if(hash(exe.data(),exe.size())!=exeHash){log("disabled","","EXE_IDENTITY");return;}
 auto dx=GetModuleHandleW(L"d3dx9_43.dll");if(!dx){log("disabled","","D3DX_ABSENT");return;}
 auto dll=read(modulePath(dx),16*1024*1024);if(hash(dll.data(),dll.size())!=dxHash){log("disabled","","D3DX_IDENTITY");return;}
 sourceInfo=(InfoFn)GetProcAddress(dx,"D3DXGetImageInfoFromFileInMemory");if(!sourceInfo){log("disabled","","SOURCE_INFO_EXPORT");return;}
 auto base=(unsigned char*)GetModuleHandleW(nullptr);auto dos=(IMAGE_DOS_HEADER*)base;auto nt=(IMAGE_NT_HEADERS32*)(base+dos->e_lfanew);
 if(nt->FileHeader.Machine!=IMAGE_FILE_MACHINE_I386||nt->OptionalHeader.SizeOfImage<gameprofile::selected.textureIat+sizeof(void*)){log("disabled","","PE");return;}
 void** slot=(void**)(base+gameprofile::selected.textureIat);auto expected=(Fn)GetProcAddress(dx,"D3DXCreateTextureFromFileInMemoryEx");
 if(!expected||*slot!=(void*)expected){log("disabled","","IAT_CONFLICT");return;}
 realLoad=expected;HMODULE pinned;
 if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,(LPCWSTR)&replacement,&pinned))return;
#ifdef GEHD_LOADING_FRAMEWORK
 try{
  auto game=std::filesystem::path(modulePath(own)).parent_path();auto options=loadLoadingOptions(game);
  wchar_t mode[32]{};auto lengthMode=GetEnvironmentVariableW(L"GEHD_LOADING_MODE",mode,32);
  if(!lengthMode){auto value=std::wstring(options.mode.begin(),options.mode.end());wcscpy_s(mode,value.c_str());}
  else if(lengthMode>=32||(wcscmp(mode,L"sync")&&wcscmp(mode,L"workers")&&wcscmp(mode,L"learned")))throw std::runtime_error("LOADING_MODE");
  if(wcscmp(mode,L"sync")!=0){
   loading::Config settings;settings.learn=wcscmp(mode,L"workers")!=0;
   auto installed=game.wstring();auto installKey=hash(installed.data(),installed.size()*sizeof(wchar_t));
   settings.binding=loading::bindingKey(exeHash,manifestIdentity,releaseIdentity,"transient");
   wchar_t local[32768]{};auto length=GetEnvironmentVariableW(L"LOCALAPPDATA",local,32768);
   if(length&&length<32768&&std::filesystem::path(local).is_absolute())settings.store=std::filesystem::path(local)/L"GEHD"/L"prefetch"/gameprofile::selected.id/installKey;
   settings.bind=[game,epoch=options.epoch]{return loading::bindingKey(exeHash,manifestIdentity,releaseIdentity,loading::variantStamp(game)+"\n"+epoch);};
   startLoading(std::move(settings));log("loading_framework","","WORKERS_2_BUDGET_256_SPEC_64");
  }
 }catch(...){log("loading_framework","","START_FAILED_SYNCHRONOUS_SHA");}
#endif
 DWORD prior;if(!VirtualProtect(slot,sizeof(void*),PAGE_READWRITE,&prior))return;
 auto old=InterlockedCompareExchangePointer(slot,(void*)&replacement,(void*)expected);DWORD ignored;
 bool restored=VirtualProtect(slot,sizeof(void*),prior,&ignored)!=FALSE;
 enabled=old==(void*)expected&&restored;log(enabled?"armed":"disabled","",enabled?"MANIFEST_READY":"PATCH_FAILED");
#ifdef GEHD_LOAD_TIMING
 if(enabled){bool recording=false;try{recording=loadtiming::start(std::filesystem::path(modulePath(own)).parent_path()/L"GEHD"/L"logs");}catch(...){}log(recording?"timing_started":"timing_unavailable","","GEHDTM1");}
#endif
 }catch(...){enabled=false;log("disabled","","INIT_EXCEPTION");}}
void loadReal(){std::call_once(realOnce,[]{wchar_t dir[MAX_PATH];UINT n=GetSystemDirectoryW(dir,MAX_PATH);if(n&&n<MAX_PATH)real9=LoadLibraryW((std::wstring(dir,n)+L"\\d3d9.dll").c_str());});}
}
#ifdef GEHD_LOADING_FRAMEWORK
namespace cpuprep {
std::string fullHash(const void*p,size_t n){return hash(p,n);}
bool validDDS(const Entry&e,const void*p,size_t n){return dds(p,n,e.w,e.h,e.m,e.format);}
uint64_t ticks(){LARGE_INTEGER q{};QueryPerformanceCounter(&q);return q.QuadPart;}
}
#endif
#ifdef GEHD_MIP0_COMPARE
#include "mip0_compare.hpp"
#endif
extern "C" IDirect3D9* WINAPI Direct3DCreate9(UINT v){loadReal();if(!real9)return nullptr;std::call_once(initOnce,init);auto f=(IDirect3D9*(WINAPI*)(UINT))GetProcAddress(real9,"Direct3DCreate9");auto a=f?f(v):nullptr;
#ifdef GEHD_MIP0_COMPARE
 if(enabled)return mipzero::wrapApi(a);
#endif
 return a;}
extern "C" void WINAPI D3DPERF_SetOptions(DWORD v){loadReal();if(real9){auto f=(void(WINAPI*)(DWORD))GetProcAddress(real9,"D3DPERF_SetOptions");if(f)f(v);}}
BOOL WINAPI DllMain(HINSTANCE h,DWORD reason,LPVOID){if(reason==DLL_PROCESS_ATTACH){own=h;DisableThreadLibraryCalls(h);}return TRUE;}
#ifdef GEHD_MIP0_FIXTURE
extern "C" __declspec(dllexport) IDirect3D9* WINAPI MipFixtureApi(IDirect3D9*a){return mipzero::wrapApi(a);}
extern "C" __declspec(dllexport) BOOL WINAPI MipFixtureBind(IDirect3DDevice9*d,DWORD flags){return mipzero::wrapDevice(d,flags);}
extern "C" __declspec(dllexport) void WINAPI MipFixtureMode(BOOL on){mipzero::active=on!=FALSE;}
extern "C" __declspec(dllexport) UINT WINAPI MipFixtureFailures(){return UINT(mipzero::failures);}
extern "C" __declspec(dllexport) UINT WINAPI MipFixtureForced(){return UINT(mipzero::forced);}
#endif
#ifdef GEHD_PACK_FIXTURE
#ifdef GEHD_LOADING_FRAMEWORK
// Test lifecycle calls require no concurrent fixture requests.
extern "C" __declspec(dllexport) void WINAPI PackFrameworkStop(){delete loadingRuntime;loadingRuntime=nullptr;}
extern "C" __declspec(dllexport) BOOL WINAPI PackFrameworkFinish(loading::Counters*out){if(!loadingRuntime||!out)return FALSE;loadingRuntime->stop();*out=loadingRuntime->stats();PackFrameworkStop();return TRUE;}
extern "C" __declspec(dllexport) BOOL WINAPI PackFrameworkStart(const wchar_t*store,const char*binding,UINT mode,UINT delay){try{
 PackFrameworkStop();if(!mode)return TRUE;loading::Config c;c.store=store;c.binding=binding;c.learn=mode>=2;c.persist=mode!=3;
#ifdef GEHD_LOADING_TEST
 c.writerDelayMs=delay;
#else
 (void)delay;
#endif
 startLoading(std::move(c));return TRUE;
 }catch(...){return FALSE;}}
extern "C" __declspec(dllexport) BOOL WINAPI PackFrameworkStats(loading::Counters*out){if(!loadingRuntime||!out)return FALSE;*out=loadingRuntime->stats();return TRUE;}
extern "C" __declspec(dllexport) BOOL WINAPI PackFrameworkReady(){return loadingRuntime&&loadingRuntime->waitReady();}
extern "C" __declspec(dllexport) BOOL WINAPI PackFrameworkDrain(){return loadingRuntime&&loadingRuntime->waitProcessed(loadingRuntime->stats().submitted);}
#endif
#ifdef GEHD_OPTIMIZATION_TEST
// Fixture only: change options only while no requests are active.
extern "C" __declspec(dllexport) void WINAPI PackOptimizationMode(UINT mode){loaderhash::useReuse=(mode&1)!=0;usePrefilter=(mode&2)!=0;}
#endif
#ifdef GEHD_LOAD_TIMING
extern "C" __declspec(dllexport) BOOL WINAPI PackTimingStart(const wchar_t* path){return loadtiming::start(path);}
extern "C" __declspec(dllexport) BOOL WINAPI PackTimingStop(){return loadtiming::stop();}
#endif
extern "C" __declspec(dllexport) BOOL WINAPI PackFixtureProduct(const wchar_t* base){try{entries.clear();return resolveRoot(base)&&loadProductManifest();}catch(...){return FALSE;}}
extern "C" __declspec(dllexport) UINT WINAPI PackFixtureLogLimit(){for(unsigned i=0;i<12010;++i)log("fixture");return sequence;}
extern "C" __declspec(dllexport) BOOL WINAPI PackFixtureStart(const wchar_t* path){root=path;enabled=false;if(!parseManifest(read(root/L"mapping.tsv",32*1024*1024)))return FALSE;auto dx=LoadLibraryW(L"d3dx9_43.dll");realLoad=dx?(Fn)GetProcAddress(dx,"D3DXCreateTextureFromFileInMemoryEx"):nullptr;sourceInfo=dx?(InfoFn)GetProcAddress(dx,"D3DXGetImageInfoFromFileInMemory"):nullptr;enabled=realLoad&&sourceInfo&&!entries.empty();return enabled;}
extern "C" __declspec(dllexport) UINT WINAPI PackFixtureEntries(){return UINT(entries.size());}
extern "C" __declspec(dllexport) HRESULT WINAPI PackFixtureLoad(IDirect3DDevice9*d,const void*p,UINT n,IDirect3DTexture9**t){return replacement(d,p,n,0xfffffffeu,0xfffffffeu,0xfffffffdu,0,D3DFMT_UNKNOWN,D3DPOOL_MANAGED,1,0xffffffffu,0,nullptr,nullptr,t);}
extern "C" __declspec(dllexport) HRESULT WINAPI PackFixtureLoadInfo(IDirect3DDevice9*d,const void*p,UINT n,Info*i,IDirect3DTexture9**t){return replacement(d,p,n,0xfffffffeu,0xfffffffeu,0xfffffffdu,0,D3DFMT_UNKNOWN,D3DPOOL_MANAGED,1,0xffffffffu,0,i,nullptr,t);}
extern "C" __declspec(dllexport) UINT WINAPI PackFixtureHits(){return hit;}
extern "C" __declspec(dllexport) UINT WINAPI PackFixtureFallbacks(){return fallback;}
extern "C" __declspec(dllexport) void WINAPI PackFixtureDisable(){enabled=false;}
extern "C" __declspec(dllexport) HRESULT WINAPI PackFixtureExplicit(IDirect3DDevice9*d,const void*p,UINT n,IDirect3DTexture9**t){return replacement(d,p,n,u32(p,16),u32(p,12),0xfffffffdu,0,D3DFMT_UNKNOWN,D3DPOOL_MANAGED,1,0xffffffffu,0,nullptr,nullptr,t);}
#endif
