// Self-owned DDS and real system D3DX9. Hidden HAL device; no game assets or draw.
#define NOMINMAX
#include <windows.h>
#include <d3d9.h>
#include <cstdio>
#include <cstring>
#include <cstddef>
#include <string>
#include <vector>
#include <sstream>
#include <stdexcept>
using Api = HRESULT (WINAPI*)(IDirect3DDevice9*,LPCVOID,UINT,UINT,UINT,UINT,DWORD,D3DFORMAT,D3DPOOL,DWORD,DWORD,D3DCOLOR,void*,PALETTEENTRY*,IDirect3DTexture9**);
static Api api;
static IDirect3DDevice9* device;
static HRESULT lastHr;
static void Check(HRESULT hr) {if(FAILED(hr)){char b[64];sprintf_s(b,"D3D operation HRESULT=0x%08x",static_cast<unsigned>(hr));throw std::runtime_error(b);}}
struct FileTexture {
    unsigned char prefix[0x44]{};
    IDirect3DTexture9* texture{};
    IDirect3DSurface9* surface{};
    __declspec(noinline) unsigned Load(const void* data,unsigned size);
};
static_assert(offsetof(FileTexture,texture)==0x44 && offsetof(FileTexture,surface)==0x48 && sizeof(void*)==4);
__declspec(noinline) unsigned FileTexture::Load(const void* data,unsigned size) {
    unsigned info[7]{};
    lastHr=api(device,data,size,0xfffffffeu,0xfffffffeu,0xfffffffdu,0,D3DFMT_UNKNOWN,D3DPOOL_MANAGED,1,0xffffffffu,0,info,nullptr,&texture);
    if(FAILED(lastHr))return 0xa5000000;
    if(!texture)return 0xa5000000;
    if(FAILED(texture->GetSurfaceLevel(0,&surface)))return 0xa5000000;
    return 0xa5000001;
}
static std::string Ptr(const void* p) {char b[32];sprintf_s(b,"0x%x",static_cast<unsigned>(reinterpret_cast<uintptr_t>(p)));return b;}
static void Write(const std::string& p,const void* bytes,size_t size) {
    HANDLE h=CreateFileA(p.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(h==INVALID_HANDLE_VALUE)throw std::runtime_error("create output");
    DWORD n=0;bool ok=WriteFile(h,bytes,static_cast<DWORD>(size),&n,nullptr)&&n==size;CloseHandle(h);
    if(!ok)throw std::runtime_error("write output");
}
static void Text(const std::string& p,const std::string& s) {Write(p,s.data(),s.size());}
static void Wait(const std::string& p) {
    auto end=GetTickCount64()+45000;
    while(GetTickCount64()<end){if(GetFileAttributesA(p.c_str())!=INVALID_FILE_ATTRIBUTES)return;Sleep(10);}
    throw std::runtime_error("gate timeout");
}
static std::vector<unsigned char> Dds(unsigned color) {
    std::vector<unsigned char> b(128+84,0);auto put=[&](size_t at,unsigned v){memcpy(b.data()+at,&v,4);};
    memcpy(b.data(),"DDS ",4);put(4,124);put(8,0x2100f);put(12,4);put(16,4);put(20,16);put(28,3);
    put(76,32);put(80,0x41);put(88,32);put(92,0xff0000);put(96,0xff00);put(100,0xff);put(104,0xff000000);put(108,0x401008);
    for(size_t at=128;at<b.size();at+=4)put(at,color);
    return b;
}
static bool Content(FileTexture& owner,unsigned color,const std::string& file) {
    bool ok=owner.texture && owner.surface && owner.texture->GetLevelCount()==3;
    IDirect3DTexture9* container=nullptr;
    Check(owner.surface->GetContainer(__uuidof(IDirect3DTexture9),reinterpret_cast<void**>(&container)));
    ok=container==owner.texture&&ok;container->Release();
    std::vector<unsigned char> bytes;
    for(unsigned level=0;level<3;++level) {
        unsigned side=4u>>level;D3DSURFACE_DESC desc{};Check(owner.texture->GetLevelDesc(level,&desc));
        ok=desc.Width==side&&desc.Height==side&&desc.Format==D3DFMT_A8R8G8B8&&desc.Pool==D3DPOOL_MANAGED&&ok;
        D3DLOCKED_RECT lock{};Check(owner.texture->LockRect(level,&lock,nullptr,D3DLOCK_READONLY));
        if(lock.Pitch<static_cast<INT>(side*4)){owner.texture->UnlockRect(level);throw std::runtime_error("pitch");}
        for(unsigned y=0;y<side;++y) {
            auto row=static_cast<unsigned char*>(lock.pBits)+y*lock.Pitch;
            bytes.insert(bytes.end(),row,row+side*4);
            for(unsigned x=0;x<side;++x){unsigned v;memcpy(&v,row+x*4,4);ok=(v==color)&&ok;}
        }
        Check(owner.texture->UnlockRect(level));
    }
    Write(file,bytes.data(),bytes.size());return ok;
}
static bool Release(FileTexture& owner) {
    if(owner.surface){owner.surface->Release();owner.surface=nullptr;}
    ULONG refs=owner.texture?owner.texture->Release():0;owner.texture=nullptr;return refs==0;
}
static void Exercise(const std::string& dir,const std::string& phase) {
    auto a=Dds(0xff123456),b=Dds(0xffa1b2c3),bad=a;bad[0]='X';
    FileTexture x{},y{};bool ok=true;std::ostringstream rows;rows<<"[";
    auto load=[&](FileTexture& owner,const std::vector<unsigned char>& data,const char* name,unsigned color,bool success) {
        unsigned result=owner.Load(data.data(),static_cast<unsigned>(data.size()));
        if(rows.str().size()>1)rows<<",";
        rows<<"{\"name\":\""<<name<<"\",\"owner\":\""<<Ptr(&owner)<<"\",\"source\":\""<<Ptr(data.data())
            <<"\",\"texture\":\""<<Ptr(owner.texture)<<"\",\"surface\":\""<<Ptr(owner.surface)<<"\",\"hr\":"<<lastHr<<",\"result\":"<<result<<"}";
        if(success)ok=result==0xa5000001&&SUCCEEDED(lastHr)&&Content(owner,color,dir+"/"+phase+"-"+name+".raw")&&ok;
        else ok=result==0xa5000000&&FAILED(lastHr)&&!owner.texture&&!owner.surface&&ok;
    };
    load(x,a,"a",0xff123456,true);load(y,a,"same",0xff123456,true);
    bool separate=x.texture!=y.texture&&x.surface!=y.surface;ok=separate&&ok;
    ok=Release(x)&&ok;load(x,b,"b",0xffa1b2c3,true);ok=Release(x)&&ok;
    load(x,bad,"invalid",0,false);ok=Release(x)&&Release(y)&&ok;
    rows<<"]";Text(dir+"/"+phase+".json",std::string("{\"ok\":")+(ok?"true":"false")+",\"separateLiveResources\":"+(separate?"true":"false")+",\"records\":"+rows.str()+"}");
    if(!ok)throw std::runtime_error("native control failed");
}
int main(int argc,char** argv) {
    if(argc!=2)return 2;std::string dir=argv[1];int result=1;
    HMODULE library=nullptr;IDirect3D9* d3d=nullptr;HWND window=nullptr;
    try {
        WNDCLASSW wc{};wc.hInstance=GetModuleHandleW(nullptr);wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"SourceParameterD3dxFixture";
        if(!RegisterClassW(&wc))throw std::runtime_error("window class");
        window=CreateWindowW(wc.lpszClassName,L"Source parameter qualification",WS_OVERLAPPEDWINDOW,0,0,64,64,nullptr,nullptr,wc.hInstance,nullptr);
        if(!window)throw std::runtime_error("hidden window");
        d3d=Direct3DCreate9(D3D_SDK_VERSION);if(!d3d)throw std::runtime_error("D3D9");
        D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=window;
        pp.BackBufferWidth=64;pp.BackBufferHeight=64;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
        Check(d3d->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&device));
        // Explicit physical PE32 path avoids WOW64 logical-path ambiguity in live binding.
        wchar_t sys[MAX_PATH]{};if(!GetSystemWow64DirectoryW(sys,MAX_PATH))throw std::runtime_error("WOW64 directory");
        std::wstring dll=std::wstring(sys)+L"\\d3dx9_43.dll";
        library=LoadLibraryExW(dll.c_str(),nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);if(!library)throw std::runtime_error("D3DX library");
        api=reinterpret_cast<Api>(GetProcAddress(library,"D3DXCreateTextureFromFileInMemoryEx"));if(!api)throw std::runtime_error("D3DX export");
        auto fn=&FileTexture::Load;unsigned loader=0;static_assert(sizeof(fn)==4);memcpy(&loader,&fn,4);
        // Fixed /Od x86 build: DUMPBIN shows API saved to [ebp-28h], then call and store HRESULT.
        auto code=reinterpret_cast<unsigned char*>(loader);unsigned hrSlot=reinterpret_cast<unsigned>(&lastHr);
        const unsigned char callBytes[]={0xff,0x55,0xd8,0xa3};
        if(memcmp(code+0x60,callBytes,4)!=0||memcmp(code+0x64,&hrSlot,4)!=0)throw std::runtime_error("fixed API call instruction");
        unsigned caller=loader+0x63;
        auto mainBase=reinterpret_cast<unsigned>(GetModuleHandleW(nullptr));
        char binding[512];sprintf_s(binding,"{\"loader\":%u,\"caller\":%u,\"api\":%u,\"device\":\"%s\"}",loader-mainBase,caller-mainBase,reinterpret_cast<unsigned>(api)-reinterpret_cast<unsigned>(library),Ptr(device).c_str());
        auto a=Dds(0xff123456),b=Dds(0xffa1b2c3);Write(dir+"/a.dds",a.data(),a.size());Write(dir+"/b.dds",b.data(),b.size());
        Text(dir+"/bindings.json",binding);Wait(dir+"/go1");Exercise(dir,"first");Wait(dir+"/go2");Exercise(dir,"after");result=0;
    }catch(const std::exception& e){try{Text(dir+"/error.txt",e.what());}catch(...){} }
    ULONG refs=device?device->Release():0;device=nullptr;if(refs)result=1;
    if(d3d)d3d->Release();if(library)FreeLibrary(library);if(window)DestroyWindow(window);
    UnregisterClassW(L"SourceParameterD3dxFixture",GetModuleHandleW(nullptr));
    try{Text(dir+"/exit.json",std::string("{\"code\":")+std::to_string(result)+",\"deviceRemainingRefs\":"+std::to_string(refs)+"}");}catch(...){result=1;}
    return result;
}
