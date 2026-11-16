// Opt-in diagnostic: transient sampler-only state blocks, no texture mutations.
#pragma once
#include <array>
#include <memory>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <fstream>
#include <filesystem>

namespace mipzero {
inline const std::filesystem::path work = L"D:\\tools\\GE MOD\\toolchain\\mip0-comparison-r1";
inline std::atomic<bool> active{false}, fatal{false};
inline std::atomic<unsigned long long> draws{}, forced{}, skipped{}, failures{}, restored{}, presents{};
inline std::mutex gate, outputGate;
struct Api { IDirect3D9* object; std::array<void*,17> original; void** table; };
struct Device { IDirect3DDevice9* object; std::array<void*,119> original; void** table; bool recording=false; void* recordEnd=nullptr; };
inline std::unordered_map<void*, std::shared_ptr<Api>> apis;
inline std::unordered_map<void*, std::shared_ptr<Device>> devices;
inline std::unordered_map<void*, std::shared_ptr<Api>> apiTables;
inline std::unordered_map<void*, std::shared_ptr<Device>> deviceTables;
template<class T> inline std::shared_ptr<T> lookup(std::unordered_map<void*,std::shared_ptr<T>>& map,void* p) {
    std::lock_guard lock(gate); auto it=map.find(p);if(it!=map.end())return it->second;
    auto& tables=[]()->auto& {if constexpr(std::is_same_v<T,Api>)return apiTables;else return deviceTables;}();
    auto found=tables.find(*reinterpret_cast<void***>(p));return found==tables.end()?nullptr:found->second;
}
template<class F,class T> inline F fn(const std::shared_ptr<T>& c,size_t i) { return reinterpret_cast<F>(c->original[i]); }
inline void report(const char* event, DWORD flags=0) {
    const auto saved=GetLastError();
    try { std::lock_guard lock(outputGate);
        std::ofstream f(work / ("runtime-"+std::to_string(GetCurrentProcessId())+".jsonl"), std::ios::app);
        f<<"{\"event\":\""<<event<<"\",\"tick\":"<<GetTickCount64()<<",\"mode\":\""<<(active?"mip0":"original")
         <<"\",\"flags\":"<<flags<<",\"pure\":"<<((flags&D3DCREATE_PUREDEVICE)?"true":"false")
         <<",\"draws\":"<<draws<<",\"forced\":"<<forced<<",\"restored\":"<<restored<<",\"skipped\":"<<skipped
         <<",\"failures\":"<<failures<<",\"fatal\":"<<(fatal?"true":"false")<<"}\n";
    } catch(...) {} SetLastError(saved);
}
inline void poll() {
    std::ifstream f(work/L"mode.txt"); std::string mode, extra;
    const bool next=bool(std::getline(f,mode))&&mode=="mip0"&&!std::getline(f,extra);
    if(active.exchange(next)!=next) report("mode_changed");
}
inline bool patch(void** slot, void* expected, void* replacement) {
    if(*slot==replacement)return true;DWORD prior=0;
    if(!VirtualProtect(slot,sizeof(void*),PAGE_EXECUTE_READWRITE,&prior))return false;
    void* old=InterlockedCompareExchangePointer(slot,replacement,expected);DWORD ignored=0;
    bool protect=VirtualProtect(slot,sizeof(void*),prior,&ignored)!=FALSE;FlushInstructionCache(GetCurrentProcess(),slot,sizeof(void*));
    return old==expected&&protect;
}
inline HRESULT put(IDirect3DDevice9* d) {
    HRESULT result=D3D_OK;
    for(DWORD s=0;s<16;++s) {
        for(auto [type,value] : {std::pair{D3DSAMP_MIPFILTER,DWORD(D3DTEXF_NONE)},
                                 std::pair{D3DSAMP_MIPMAPLODBIAS,DWORD(0)},
                                 std::pair{D3DSAMP_MAXMIPLEVEL,DWORD(0)}}) {
            HRESULT hr=d->SetSamplerState(s,type,value); if(FAILED(hr)) result=hr;
        }
    }
    return result;
}
inline bool refreshDevice(IDirect3DDevice9*,const std::shared_ptr<Device>&);
// No GetSamplerState: Capture/Apply preserve precisely the 48 modified states.
template<class Draw> inline HRESULT drawing(IDirect3DDevice9* d, Draw call) {
    const DWORD incoming=GetLastError(); ++draws;
    auto c=lookup(devices,d);
    if(!c||c->object!=d||!active||fatal||c->recording) { ++skipped;SetLastError(incoming);return call(); }
    IDirect3DStateBlock9* state=nullptr;
    HRESULT hr=d->BeginStateBlock();
    if(FAILED(hr)) { ++skipped;SetLastError(incoming);return call(); }
    HRESULT record=put(d);
    // D3D9 switches to its recording dispatch table at BeginStateBlock.
    // Use that current table for EndStateBlock, never a cached normal-table slot.
    hr=d->EndStateBlock(&state);
    if(FAILED(record)||FAILED(hr)||!state||FAILED(state->Capture())) {
        if(state)state->Release();++failures;SetLastError(incoming);return call();
    }
    hr=put(d);
    if(FAILED(hr)) {
        if(FAILED(state->Apply()))fatal=true;
        state->Release();++failures;SetLastError(incoming);return call();
    }
    ++forced;SetLastError(incoming);const HRESULT ret=call();const DWORD returned=GetLastError();
    if(SUCCEEDED(state->Apply())){++restored;refreshDevice(d,c);}
    else { ++failures;fatal=true;report("restore_failed"); }
    state->Release();SetLastError(returned);return ret;
}
inline HRESULT WINAPI draw(IDirect3DDevice9*d,D3DPRIMITIVETYPE t,UINT a,UINT n) {
    auto c=lookup(devices,d);using F=HRESULT(WINAPI*)(IDirect3DDevice9*,D3DPRIMITIVETYPE,UINT,UINT);
    return drawing(d,[&]{return fn<F>(c,81)(d,t,a,n);});
}
inline HRESULT WINAPI indexed(IDirect3DDevice9*d,D3DPRIMITIVETYPE t,INT b,UINT m,UINT n,UINT a,UINT p) {
    auto c=lookup(devices,d);using F=HRESULT(WINAPI*)(IDirect3DDevice9*,D3DPRIMITIVETYPE,INT,UINT,UINT,UINT,UINT);
    return drawing(d,[&]{return fn<F>(c,82)(d,t,b,m,n,a,p);});
}
inline HRESULT WINAPI up(IDirect3DDevice9*d,D3DPRIMITIVETYPE t,UINT n,const void* p,UINT s) {
    auto c=lookup(devices,d);using F=HRESULT(WINAPI*)(IDirect3DDevice9*,D3DPRIMITIVETYPE,UINT,const void*,UINT);
    return drawing(d,[&]{return fn<F>(c,83)(d,t,n,p,s);});
}
inline HRESULT WINAPI indexedUp(IDirect3DDevice9*d,D3DPRIMITIVETYPE t,UINT m,UINT n,UINT p,const void* i,D3DFORMAT f,const void* v,UINT s) {
    auto c=lookup(devices,d);using F=HRESULT(WINAPI*)(IDirect3DDevice9*,D3DPRIMITIVETYPE,UINT,UINT,UINT,const void*,D3DFORMAT,const void*,UINT);
    return drawing(d,[&]{return fn<F>(c,84)(d,t,m,n,p,i,f,v,s);});
}
inline HRESULT WINAPI end(IDirect3DDevice9*,IDirect3DStateBlock9**);
inline HRESULT WINAPI begin(IDirect3DDevice9*d) {
    auto c=lookup(devices,d);using F=HRESULT(WINAPI*)(IDirect3DDevice9*);
    HRESULT hr=fn<F>(c,60)(d);
    if(SUCCEEDED(hr)) {
        c->recording=true;void** table=*reinterpret_cast<void***>(d);
        if(table[61]!=reinterpret_cast<void*>(&end))c->recordEnd=table[61];
        if(!c->recordEnd||!patch(table+61,c->recordEnd,reinterpret_cast<void*>(&end))){fatal=true;report("recording_end_patch_failed");}
    }return hr;
}
inline HRESULT WINAPI end(IDirect3DDevice9*d,IDirect3DStateBlock9**out) {
    auto c=lookup(devices,d);using F=HRESULT(WINAPI*)(IDirect3DDevice9*,IDirect3DStateBlock9**);
    if(!c||!c->recordEnd)return D3DERR_INVALIDCALL;
    HRESULT hr=reinterpret_cast<F>(c->recordEnd)(d,out);
    if(SUCCEEDED(hr)){c->recording=false;refreshDevice(d,c);}return hr;
}
inline HRESULT WINAPI present(IDirect3DDevice9*d,const RECT*s,const RECT*t,HWND w,const RGNDATA*r) {
    auto c=lookup(devices,d);using F=HRESULT(WINAPI*)(IDirect3DDevice9*,const RECT*,const RECT*,HWND,const RGNDATA*);
    poll();if((++presents)%120==0)report("progress");return fn<F>(c,17)(d,s,t,w,r);
}
inline HRESULT WINAPI reset(IDirect3DDevice9*d,D3DPRESENT_PARAMETERS*p) {
    auto c=lookup(devices,d);using F=HRESULT(WINAPI*)(IDirect3DDevice9*,D3DPRESENT_PARAMETERS*);
    HRESULT hr=fn<F>(c,16)(d,p);if(SUCCEEDED(hr))refreshDevice(d,c);report(SUCCEEDED(hr)?"reset_ok":"reset_failed");return hr;
}
inline ULONG WINAPI releaseDevice(IDirect3DDevice9*d) {
    auto c=lookup(devices,d);using F=ULONG(WINAPI*)(IDirect3DDevice9*);ULONG n=fn<F>(c,2)(d);
    if(!n) { report("device_released");std::lock_guard lock(gate);devices.erase(d); } return n;
}
inline bool refreshDevice(IDirect3DDevice9*d,const std::shared_ptr<Device>&c) {
    void** table=*reinterpret_cast<void***>(d);
    const std::pair<size_t,void*> hooks[]={{2,reinterpret_cast<void*>(&releaseDevice)},{16,reinterpret_cast<void*>(&reset)},
        {17,reinterpret_cast<void*>(&present)},{60,reinterpret_cast<void*>(&begin)},{81,reinterpret_cast<void*>(&draw)},
        {82,reinterpret_cast<void*>(&indexed)},{83,reinterpret_cast<void*>(&up)},{84,reinterpret_cast<void*>(&indexedUp)}};
    // Known runtime transitions replace normal/recording dispatch entries in place.
    // Refresh only at creation, EndStateBlock, our Apply and successful Reset.
    for(auto [slot,hook]:hooks)if(table[slot]!=hook)c->original[slot]=table[slot];
    c->table=table;{std::lock_guard lock(gate);deviceTables[table]=c;}
    for(auto [slot,hook]:hooks)if(!patch(table+slot,c->original[slot],hook)){fatal=true;report("slot_patch_failed");return false;}
    return true;
}
inline bool wrapDevice(IDirect3DDevice9*d,DWORD flags) {
    if(!d)return false;
    {std::lock_guard lock(gate);if(devices.count(d))return true;}
    auto c=std::make_shared<Device>();c->object=d;c->table=*reinterpret_cast<void***>(d);
    {std::lock_guard lock(gate);auto found=deviceTables.find(c->table);if(found!=deviceTables.end())c->original=found->second->original;
        else std::copy_n(c->table,119,c->original.begin());devices[d]=c;deviceTables[c->table]=c;}
    if(!refreshDevice(d,c))return false;
    poll();report("device_bound",flags);return true;
}
inline HRESULT WINAPI createDevice(IDirect3D9*a,UINT n,D3DDEVTYPE t,HWND w,DWORD flags,D3DPRESENT_PARAMETERS*p,IDirect3DDevice9**out) {
    auto c=lookup(apis,a);using F=HRESULT(WINAPI*)(IDirect3D9*,UINT,D3DDEVTYPE,HWND,DWORD,D3DPRESENT_PARAMETERS*,IDirect3DDevice9**);
    HRESULT hr=fn<F>(c,16)(a,n,t,w,flags,p,out);
    if(SUCCEEDED(hr)&&out&&*out&&!wrapDevice(*out,flags))report("device_bind_failed");return hr;
}
inline ULONG WINAPI releaseApi(IDirect3D9*a) {
    auto c=lookup(apis,a);using F=ULONG(WINAPI*)(IDirect3D9*);ULONG n=fn<F>(c,2)(a);
    if(!n){std::lock_guard lock(gate);apis.erase(a);}return n;
}
inline IDirect3D9* wrapApi(IDirect3D9*a) {
    if(!a)return a;
    {std::lock_guard lock(gate);if(apis.count(a))return a;}
    auto c=std::make_shared<Api>();c->object=a;c->table=*reinterpret_cast<void***>(a);
    {std::lock_guard lock(gate);auto found=apiTables.find(c->table);if(found!=apiTables.end())c->original=found->second->original;
        else std::copy_n(c->table,17,c->original.begin());apis[a]=c;apiTables[c->table]=c;}
    if(!patch(c->table+2,c->original[2],reinterpret_cast<void*>(&releaseApi))||!patch(c->table+16,c->original[16],reinterpret_cast<void*>(&createDevice))) {fatal=true;report("api_bind_failed");}return a;
}
}
