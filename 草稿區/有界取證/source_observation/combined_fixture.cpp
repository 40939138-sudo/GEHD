// Combine the qualified ABI fixtures and real D3DX fixture in one owned process.
#define NOMINMAX
#include <windows.h>
#include <d3d9.h>
#include <intrin.h>
#include <cstdio>
#include <cstring>
#include <cstddef>
#include <string>
#include <vector>
#include <sstream>
#include <stdexcept>
#include <map>
#include <new>
namespace D3dx {
#define main D3dxFixtureMain
#include "d3dx_fixture.cpp"
#undef main
}
#define main HandoffFixtureMain
#include "handoff_fixture.cpp"
#undef main
#define GEHD_HANDOFF_TYPES_INCLUDED
#define main MaterialFixtureMain
#include "material_fixture.cpp"
#undef main
#define main ResolverFixtureMain
#include "resolver_fixture.cpp"
#undef main
class __declspec(dllexport) OwnedTexture:public D3dx::FileTexture {
public:
    __declspec(noinline) OwnedTexture();
    __declspec(noinline) ~OwnedTexture();
    __declspec(noinline) void Clear();
};
__declspec(noinline) OwnedTexture::OwnedTexture(){texture=nullptr;surface=nullptr;}
__declspec(noinline) OwnedTexture::~OwnedTexture(){Clear();}
__declspec(noinline) void OwnedTexture::Clear(){if(!D3dx::Release(*this))InterlockedIncrement(&errors);}
static unsigned ownedEpoch;
static std::vector<std::string> combinedRows;
static void Consume(OwnedTexture* owner,const char* label,unsigned epoch){
    FixtureState state;state.held[0].entity=owner;state.descriptors[1].hit=true;state.owner.mode=0;
    Handle items[32]{};List l{};l.data=items;Table table{&l};Request request{{&state.owner},&state.owner,label};
    if(ResolverReader(&table,&request,nullptr,ResolverCallback,nullptr,nullptr)!=0xa5000001||l.count!=1||l.data[0].entity!=owner)InterlockedIncrement(&errors);
    Handle fixedItems[32]{},userItems[32]{};List fixedList{},userList{};fixedList.data=fixedItems;userList.data=userItems;
    Material material{};material.fixed=&fixedList;material.user=&userList;Stream a{{0},0,0},b{{0},0,0};
    material.Fixed(&a,&table);material.User(&b,&table);
    if(fixedList.count!=1||userList.count!=1||fixedItems[0].entity!=owner||userItems[0].entity!=owner)InterlockedIncrement(&errors);
    std::ostringstream row;row<<"{\"name\":\""<<label<<"\",\"epoch\":"<<epoch<<",\"owner\":\""<<::Ptr(owner)<<"\",\"texture\":\""<<Ptr(owner->texture)
      <<"\",\"surface\":\""<<Ptr(owner->surface)<<"\",\"table\":\""<<Ptr(&table)<<"\",\"fixed\":\""<<Ptr(&material)<<"\",\"user\":\""<<Ptr(&material)<<"\"}";
    combinedRows.push_back(row.str());
}
static void CombinedExercise(const std::string& dir,const std::string& phase,OwnedTexture* preexisting){
    errors=0;combinedRows.clear();auto a=D3dx::Dds(0xff123456),b=D3dx::Dds(0xffa1b2c3),bad=a;bad[0]='X';
    Consume(preexisting,"preexisting",0);
    alignas(OwnedTexture) unsigned char storage[sizeof(OwnedTexture)];
    auto x=new(storage) OwnedTexture();unsigned epoch=++ownedEpoch;
    if(x->Load(a.data(),static_cast<unsigned>(a.size()))!=0xa5000001)InterlockedIncrement(&errors);
    if(!D3dx::Content(*x,0xff123456,dir+"/"+phase+"-a.raw"))InterlockedIncrement(&errors);Consume(x,"a",epoch);
    OwnedTexture y;unsigned otherEpoch=++ownedEpoch;
    if(y.Load(a.data(),static_cast<unsigned>(a.size()))!=0xa5000001)InterlockedIncrement(&errors);
    if(y.texture==x->texture||y.surface==x->surface)InterlockedIncrement(&errors);
    if(!D3dx::Content(y,0xff123456,dir+"/"+phase+"-same.raw"))InterlockedIncrement(&errors);Consume(&y,"same",otherEpoch);
    x->Clear();Consume(x,"cleared",epoch);
    if(x->Load(bad.data(),static_cast<unsigned>(bad.size()))!=0xa5000000)InterlockedIncrement(&errors);Consume(x,"failed_load",epoch);
    if(x->Load(b.data(),static_cast<unsigned>(b.size()))!=0xa5000001)InterlockedIncrement(&errors);
    if(!D3dx::Content(*x,0xffa1b2c3,dir+"/"+phase+"-b.raw"))InterlockedIncrement(&errors);Consume(x,"reloaded",epoch);
    // Temporary field substitution is legal host code, then restore the owned references before cleanup.
    auto savedTexture=x->texture;auto savedSurface=x->surface;x->texture=y.texture;x->surface=y.surface;
    Consume(x,"fields_changed",epoch);x->texture=savedTexture;x->surface=savedSurface;
    x->~OwnedTexture();x=new(storage) OwnedTexture();epoch=++ownedEpoch;
    Consume(x,"reused_unloaded",epoch);
    if(x->Load(a.data(),static_cast<unsigned>(a.size()))!=0xa5000001)InterlockedIncrement(&errors);
    if(!D3dx::Content(*x,0xff123456,dir+"/"+phase+"-reused.raw"))InterlockedIncrement(&errors);Consume(x,"reused_loaded",epoch);x->~OwnedTexture();
    std::ostringstream out;out<<"{\"ok\":"<<(errors==0?"true":"false")<<",\"errors\":"<<errors<<",\"records\":[";
    for(size_t i=0;i<combinedRows.size();i++){if(i)out<<",";out<<combinedRows[i];}out<<"]}";D3dx::Text(dir+"/"+phase+".json",out.str());
    if(errors)throw std::runtime_error("combined native controls");
}
int main(int argc,char** argv){
    if(argc<2||argc>3)return 2;std::string dir=argv[1];InitializeCriticalSection(&guard);int result=1;HMODULE library=nullptr;IDirect3D9* d3d=nullptr;HWND window=nullptr;
    try{
        WNDCLASSW wc{};wc.hInstance=GetModuleHandleW(nullptr);wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"CombinedObservationFixture";
        if(!RegisterClassW(&wc))throw std::runtime_error("window class");
        window=CreateWindowW(wc.lpszClassName,L"Combined qualification",WS_OVERLAPPEDWINDOW,0,0,64,64,nullptr,nullptr,wc.hInstance,nullptr);
        if(!window)throw std::runtime_error("hidden window");d3d=Direct3DCreate9(D3D_SDK_VERSION);if(!d3d)throw std::runtime_error("D3D9");
        D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=window;pp.BackBufferWidth=64;pp.BackBufferHeight=64;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
        D3dx::Check(d3d->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&D3dx::device));
        wchar_t sys[MAX_PATH]{};if(!GetSystemWow64DirectoryW(sys,MAX_PATH))throw std::runtime_error("WOW64 directory");
        std::wstring dll=std::wstring(sys)+L"\\d3dx9_43.dll";library=LoadLibraryExW(dll.c_str(),nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);if(!library)throw std::runtime_error("D3DX library");
        D3dx::api=reinterpret_cast<D3dx::Api>(GetProcAddress(library,"D3DXCreateTextureFromFileInMemoryEx"));if(!D3dx::api)throw std::runtime_error("D3DX export");
        {FixtureState s;ResolverCase(s,"calibrate_previous",0,Entity(1));ResolverCase(s,"calibrate_cache",3,Entity(1));}
        {FixtureState s;ResolverCase(s,"calibrate_dataset",1,Entity(1));}
        {FixtureState s;ResolverCase(s,"calibrate_fallback",2,Entity(9));ResolverCase(s,"calibrate_unsupported",4,Entity(9));}calibrating=false;
        unsigned base=reinterpret_cast<unsigned>(GetModuleHandleW(nullptr));auto rva=[&](const void* p){return reinterpret_cast<unsigned>(p)-base;};
        auto method=[&](auto fn){unsigned p=0;static_assert(sizeof(fn)==4);memcpy(&p,&fn,4);return p-base;};
        Handle values[2]{{Entity(1)},{Entity(2)}};List l{};l.data=values;l.count=2;Table table{&l};
        MaterialCase("fixed",false,&table,{0},0);unsigned fr=rva(readReturn),fl=rva(lookupReturn),fa=rva(appendReturn);
        MaterialCase("user",true,&table,{0},0);unsigned ur=rva(readReturn),ul=rva(lookupReturn),ua=rva(appendReturn);
        unsigned loader=method(&D3dx::FileTexture::Load),caller=loader+0x63;auto code=reinterpret_cast<unsigned char*>(base+loader);
        unsigned hr=reinterpret_cast<unsigned>(&D3dx::lastHr);const unsigned char signature[]={0xff,0x55,0xd8,0xa3};
        if(memcmp(code+0x60,signature,4)||memcmp(code+0x64,&hr,4))throw std::runtime_error("fixed API call instruction");
        auto create=GetProcAddress(GetModuleHandleW(nullptr),"??0OwnedTexture@@QAE@XZ"),destroy=GetProcAddress(GetModuleHandleW(nullptr),"??1OwnedTexture@@QAE@XZ");
        if(!create||!destroy)throw std::runtime_error("exported lifetime symbols");
        OwnedTexture preexisting;auto dds=D3dx::Dds(0xff123456);if(preexisting.Load(dds.data(),static_cast<unsigned>(dds.size()))!=0xa5000001)throw std::runtime_error("preexisting load");
        std::ostringstream binding;binding<<"{\"loader\":"<<loader<<",\"caller\":"<<caller<<",\"api\":"<<reinterpret_cast<unsigned>(D3dx::api)-reinterpret_cast<unsigned>(library)
          <<",\"create\":"<<rva(create)<<",\"clear\":"<<method(&OwnedTexture::Clear)<<",\"destroy\":"<<rva(destroy)
          <<",\"reader\":"<<rva(reinterpret_cast<void*>(ResolverReader))<<",\"callback\":"<<rva(reinterpret_cast<void*>(ResolverCallback))<<",\"append\":"<<method(&Table::Append)
          <<",\"callbackCaller\":"<<rva(resolverCallbackReturn)<<",\"appendCaller\":"<<rva(resolverReaderAppendReturn)<<",\"resolver\":{\"resolver\":"<<method(&Resolver::Resolve)
          <<",\"match\":"<<rva(reinterpret_cast<void*>(Match))<<",\"holder\":"<<rva(reinterpret_cast<void*>(Holder))<<",\"copy\":"<<rva(reinterpret_cast<void*>(CopyHandle))
          <<",\"assign\":"<<rva(reinterpret_cast<void*>(AssignHandle))<<",\"slot\":"<<rva(reinterpret_cast<void*>(GetSlot))<<",\"fallback\":"<<rva(reinterpret_cast<void*>(Fallback))
          <<",\"release\":"<<rva(reinterpret_cast<void*>(ReleaseHandle))<<",\"nameAppend\":"<<rva(reinterpret_cast<void*>(AppendName));
        for(const auto& entry:callSites)binding<<",\""<<entry.first<<"\":"<<entry.second-base;
        binding<<"},\"materials\":{\"fixed\":"<<method(&Material::Fixed)<<",\"user\":"<<method(&Material::User)<<",\"readIndex\":"<<rva(reinterpret_cast<void*>(ReadIndex))
          <<",\"lookup\":"<<rva(reinterpret_cast<void*>(Lookup))<<",\"append\":"<<rva(reinterpret_cast<void*>(AppendList))<<",\"fixedReadCaller\":"<<fr<<",\"fixedLookupCaller\":"<<fl
          <<",\"fixedAppendCaller\":"<<fa<<",\"userReadCaller\":"<<ur<<",\"userLookupCaller\":"<<ul<<",\"userAppendCaller\":"<<ua<<"}}";
        auto readerByte=reinterpret_cast<unsigned char*>(ResolverReader);unsigned char original=*readerByte;
        auto patchReader=[&](unsigned char value){DWORD previous=0,ignored=0;if(!VirtualProtect(readerByte,1,PAGE_EXECUTE_READWRITE,&previous))throw std::runtime_error("fixture code protection");
            *readerByte=value;FlushInstructionCache(GetCurrentProcess(),readerByte,1);if(!VirtualProtect(readerByte,1,previous,&ignored))throw std::runtime_error("fixture code protection restore");};
        bool mismatch=argc==3&&std::string(argv[2])=="live_mismatch";if(mismatch)patchReader(0xcc);
        D3dx::Text(dir+"/bindings.json",binding.str());D3dx::Wait(dir+"/go1");if(mismatch)patchReader(original);
        CombinedExercise(dir,"first",&preexisting);D3dx::Wait(dir+"/go2");CombinedExercise(dir,"after",&preexisting);result=0;
    }catch(const std::exception& e){try{D3dx::Text(dir+"/error.txt",e.what());}catch(...){}}
    ULONG refs=D3dx::device?D3dx::device->Release():0;D3dx::device=nullptr;if(refs)result=1;if(d3d)d3d->Release();if(library)FreeLibrary(library);if(window)DestroyWindow(window);
    UnregisterClassW(L"CombinedObservationFixture",GetModuleHandleW(nullptr));DeleteCriticalSection(&guard);
    try{D3dx::Text(dir+"/exit.json",std::string("{\"code\":")+std::to_string(result)+",\"deviceRemainingRefs\":"+std::to_string(refs)+"}");}catch(...){result=1;}return result;
}
