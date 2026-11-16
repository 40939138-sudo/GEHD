#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d9.h>
#include <psapi.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>
#include <array>
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <utility>
template<class T>struct Com{
 T*p{};~Com(){if(p)p->Release();}Com()=default;Com(const Com&)=delete;Com&operator=(const Com&)=delete;
 Com(Com&&o)noexcept:p(std::exchange(o.p,nullptr)){}Com&operator=(Com&&o)noexcept{if(p)p->Release();p=std::exchange(o.p,nullptr);return *this;}
 T**put(){if(p)throw std::runtime_error("nonempty COM output");return &p;}T*operator->()const{return p;}
};
void require(bool v,const char*why){if(!v)throw std::runtime_error(why);}
void hr(HRESULT v,const char*why){if(FAILED(v)){std::ostringstream s;s<<why<<" HRESULT="<<std::hex<<unsigned(v);throw std::runtime_error(s.str());}}
uint64_t now(){LARGE_INTEGER q{};QueryPerformanceCounter(&q);return q.QuadPart;}
uint64_t frequency(){LARGE_INTEGER q{};QueryPerformanceFrequency(&q);return q.QuadPart;}
struct Info{UINT w,h,d,m;D3DFORMAT fmt;D3DRESOURCETYPE type;UINT file;};
struct Buffer: IUnknown {virtual void*WINAPI GetBufferPointer()=0;virtual DWORD WINAPI GetBufferSize()=0;};
using Native=HRESULT(WINAPI*)(IDirect3DDevice9*,const void*,UINT,UINT,UINT,UINT,DWORD,D3DFORMAT,D3DPOOL,DWORD,DWORD,D3DCOLOR,Info*,PALETTEENTRY*,IDirect3DTexture9**);
using SurfaceMemory=HRESULT(WINAPI*)(IDirect3DSurface9*,const PALETTEENTRY*,const RECT*,const void*,D3DFORMAT,UINT,const PALETTEENTRY*,const RECT*,DWORD,D3DCOLOR);
using SurfaceCopy=HRESULT(WINAPI*)(IDirect3DSurface9*,const PALETTEENTRY*,const RECT*,IDirect3DSurface9*,const PALETTEENTRY*,const RECT*,DWORD,D3DCOLOR);
using Compile=HRESULT(WINAPI*)(const char*,UINT,const void*,void*,const char*,const char*,DWORD,Buffer**,Buffer**,void**);
struct DDS{
 std::vector<unsigned char>b;UINT w{},h{},m{};D3DFORMAT fmt{};unsigned block{};std::vector<size_t>offset;
 static UINT u(const std::vector<unsigned char>&b,size_t p){UINT v;memcpy(&v,b.data()+p,4);return v;}
 explicit DDS(const std::filesystem::path&p){
  std::ifstream f(p,std::ios::binary|std::ios::ate);require(bool(f),"DDS open");auto n=f.tellg();require(n>=128&&n<=256*1024*1024,"DDS bounds");b.resize(size_t(n));f.seekg(0);f.read(reinterpret_cast<char*>(b.data()),n);require(bool(f),"DDS read");
  require(u(b,0)==0x20534444&&u(b,4)==124&&u(b,76)==32&&u(b,112)==0,"DDS header/cube");w=u(b,16);h=u(b,12);m=u(b,28);
  require(w&&h&&w<=16384&&h<=16384&&m&&m<=15,"DDS dimensions");
  if(u(b,80)&4)fmt=D3DFORMAT(u(b,84));else{require(u(b,88)==32&&u(b,92)==0xff0000&&u(b,96)==0xff00&&u(b,100)==0xff&&u(b,104)==0xff000000,"RGBA layout");fmt=D3DFMT_A8R8G8B8;}
  require(fmt==D3DFMT_DXT1||fmt==D3DFMT_DXT3||fmt==D3DFMT_DXT5||fmt==D3DFMT_A8R8G8B8,"DDS format");block=fmt==D3DFMT_DXT1?8:16;
  size_t pos=128;for(UINT l=0;l<m;++l){offset.push_back(pos);pos+=size_t(row(l))*rows(l);}require(pos==b.size(),"DDS exact length");
 }
 UINT width(UINT l)const{return std::max(1u,w>>l);}UINT height(UINT l)const{return std::max(1u,h>>l);}
 UINT row(UINT l)const{return fmt==D3DFMT_A8R8G8B8?width(l)*4:((width(l)+3)/4)*block;}
 UINT rows(UINT l)const{return fmt==D3DFMT_A8R8G8B8?height(l):(height(l)+3)/4;}
};
struct Context{
 HWND window{};HMODULE d9{},dx{};Com<IDirect3D9>api;Com<IDirect3DDevice9>dev;
 Com<IDirect3DSurface9>rt,readback,decodeA,decodeB;Com<IDirect3DPixelShader9>shader;Com<IDirect3DQuery9>event;
 Native native{};SurfaceMemory memory{};SurfaceCopy surface{};D3DCAPS9 caps{};
 Context(){
  WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"GEHDOfflineD3DComparison";require(RegisterClassW(&wc)!=0,"window class");
  window=CreateWindowExW(0,wc.lpszClassName,L"GEHD offline comparison",WS_OVERLAPPEDWINDOW,0,0,64,64,nullptr,nullptr,wc.hInstance,nullptr);require(window!=nullptr,"hidden window");
  d9=LoadLibraryW(L"C:\\Windows\\SysWOW64\\d3d9.dll");dx=LoadLibraryW(L"C:\\Windows\\SysWOW64\\d3dx9_43.dll");require(d9&&dx,"system D3D modules");
  auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(GetProcAddress(d9,"Direct3DCreate9"));api.p=create(D3D_SDK_VERSION);require(api.p!=nullptr,"D3D9");
  D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.BackBufferFormat=D3DFMT_UNKNOWN;pp.hDeviceWindow=window;pp.BackBufferWidth=64;pp.BackBufferHeight=64;
  hr(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,dev.put()),"CreateDevice");hr(dev->GetDeviceCaps(&caps),"caps");require(caps.PixelShaderVersion>=D3DPS_VERSION(3,0),"PS3 required");
  native=reinterpret_cast<Native>(GetProcAddress(dx,"D3DXCreateTextureFromFileInMemoryEx"));memory=reinterpret_cast<SurfaceMemory>(GetProcAddress(dx,"D3DXLoadSurfaceFromMemory"));surface=reinterpret_cast<SurfaceCopy>(GetProcAddress(dx,"D3DXLoadSurfaceFromSurface"));auto compile=reinterpret_cast<Compile>(GetProcAddress(dx,"D3DXCompileShader"));require(native&&memory&&surface&&compile,"D3DX exports");
  const char*code="sampler2D image:register(s0); float4 level:register(c0); float4 main(float2 uv:TEXCOORD0):COLOR0{return tex2Dlod(image,float4(uv,0,level.x));}";
  Com<Buffer>compiled,errors;auto result=compile(code,UINT(strlen(code)),nullptr,nullptr,"main","ps_3_0",0,compiled.put(),errors.put(),nullptr);hr(result,"pixel shader compile");hr(dev->CreatePixelShader(static_cast<DWORD*>(compiled->GetBufferPointer()),shader.put()),"pixel shader");
  hr(dev->CreateRenderTarget(64,64,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,rt.put(),nullptr),"RT");hr(dev->CreateOffscreenPlainSurface(64,64,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,readback.put(),nullptr),"readback");
  hr(dev->CreateOffscreenPlainSurface(256,256,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,decodeA.put(),nullptr),"decode A");hr(dev->CreateOffscreenPlainSurface(256,256,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,decodeB.put(),nullptr),"decode B");
  hr(dev->CreateQuery(D3DQUERYTYPE_EVENT,event.put()),"event query");hr(dev->SetRenderTarget(0,rt.p),"set RT");hr(dev->SetDepthStencilSurface(nullptr),"no depth surface");
  D3DVIEWPORT9 vp{0,0,64,64,0,1};hr(dev->SetViewport(&vp),"viewport");hr(dev->SetPixelShader(shader.p),"set shader");hr(dev->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1),"FVF");
  for(auto pair:std::initializer_list<std::pair<D3DRENDERSTATETYPE,DWORD>>{{D3DRS_ZENABLE,FALSE},{D3DRS_ZWRITEENABLE,FALSE},{D3DRS_CULLMODE,D3DCULL_NONE},{D3DRS_LIGHTING,FALSE},{D3DRS_ALPHATESTENABLE,FALSE},{D3DRS_SRGBWRITEENABLE,FALSE},{D3DRS_DITHERENABLE,FALSE},{D3DRS_COLORWRITEENABLE,15},{D3DRS_SRCBLEND,D3DBLEND_SRCALPHA},{D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA},{D3DRS_BLENDOP,D3DBLENDOP_ADD}})hr(dev->SetRenderState(pair.first,pair.second),"render state");
  for(auto pair:std::initializer_list<std::pair<D3DSAMPLERSTATETYPE,DWORD>>{{D3DSAMP_MINFILTER,D3DTEXF_POINT},{D3DSAMP_MAGFILTER,D3DTEXF_POINT},{D3DSAMP_MIPFILTER,D3DTEXF_POINT},{D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP},{D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP},{D3DSAMP_SRGBTEXTURE,FALSE},{D3DSAMP_MAXMIPLEVEL,0}})hr(dev->SetSamplerState(0,pair.first,pair.second),"sampler state");
  D3DADAPTER_IDENTIFIER9 id{};hr(api->GetAdapterIdentifier(0,0,&id),"adapter");std::cout<<"{\"type\":\"host\",\"adapter\":\""<<id.Description<<"\",\"vendor\":"<<id.VendorId<<",\"device\":"<<id.DeviceId<<",\"driver_hi\":"<<id.DriverVersion.HighPart<<",\"driver_lo\":"<<id.DriverVersion.LowPart<<",\"max_width\":"<<caps.MaxTextureWidth<<",\"max_height\":"<<caps.MaxTextureHeight<<",\"qpc_frequency\":"<<frequency()<<",\"bits\":"<<sizeof(void*)*8<<",\"timestamp_supported\":"<<(SUCCEEDED(dev->CreateQuery(D3DQUERYTYPE_TIMESTAMP,nullptr))?"true":"false")<<"}\n"<<std::flush;
 }
 ~Context(){if(dev.p)dev->SetTexture(0,nullptr);if(window)DestroyWindow(window);/* modules stay loaded until process exit */}
 void fence(){hr(event->Issue(D3DISSUE_END),"event issue");auto start=now();for(;;){HRESULT r=event->GetData(nullptr,0,D3DGETDATA_FLUSH);if(r==S_OK)break;hr(r,"event wait");require(now()-start<frequency()*10,"GPU event timeout");SwitchToThread();}}
 Com<IDirect3DTexture9> create(const DDS&d,bool direct,bool repair=true,int failLevel=-1){
  require(d.w<=caps.MaxTextureWidth&&d.h<=caps.MaxTextureHeight,"device dimensions");Com<IDirect3DTexture9> t;
  if(!direct){Info info{};hr(native(dev.p,d.b.data(),UINT(d.b.size()),0xfffffffeu,0xfffffffeu,0xfffffffdu,0,D3DFMT_UNKNOWN,D3DPOOL_MANAGED,1,0xffffffffu,0,&info,nullptr,t.put()),"D3DX create");require(info.w==d.w&&info.h==d.h&&info.m==d.m&&info.fmt==d.fmt,"D3DX info");}
  else{
   hr(dev->CreateTexture(d.w,d.h,d.m,0,d.fmt,D3DPOOL_MANAGED,t.put(),nullptr),"direct create");
   for(UINT l=0;l<d.m;++l){if(int(l)==failLevel)throw std::runtime_error("injected fill failure");
    if(repair&&d.fmt!=D3DFMT_A8R8G8B8&&(d.width(l)%4||d.height(l)%4)){
     Com<IDirect3DSurface9>s;hr(t->GetSurfaceLevel(l,s.put()),"tail surface");RECT rect{0,0,LONG(d.width(l)),LONG(d.height(l))};hr(memory(s.p,nullptr,nullptr,d.b.data()+d.offset[l],d.fmt,d.row(l),nullptr,&rect,1,0),"tail normalization");
    }else{
     D3DLOCKED_RECT lock{};hr(t->LockRect(l,&lock,nullptr,0),"fill lock");bool valid=lock.Pitch>=int(d.row(l));if(valid)for(UINT y=0;y<d.rows(l);++y)memcpy(static_cast<unsigned char*>(lock.pBits)+size_t(y)*lock.Pitch,d.b.data()+d.offset[l]+size_t(y)*d.row(l),d.row(l));auto result=t->UnlockRect(l);require(valid,"fill pitch");hr(result,"fill unlock");
    }
   }
   hr(t->AddDirtyRect(nullptr),"dirty whole texture");
  }
  require(t->GetLevelCount()==d.m,"mip count");for(UINT l=0;l<d.m;++l){D3DSURFACE_DESC desc{};hr(t->GetLevelDesc(l,&desc),"level desc");require(desc.Width==d.width(l)&&desc.Height==d.height(l)&&desc.Format==d.fmt&&desc.Pool==D3DPOOL_MANAGED&&desc.Usage==0,"resource description");}return t;
 }
 void draw(IDirect3DTexture9*t,UINT level,unsigned mode){
  hr(dev->SetTexture(0,t),"bind");float c[4]={float(level),0,0,0};hr(dev->SetPixelShaderConstantF(0,c,1),"LOD constant");hr(dev->SetRenderState(D3DRS_ALPHABLENDENABLE,mode?TRUE:FALSE),"blend");hr(dev->Clear(0,nullptr,D3DCLEAR_TARGET,mode==2?0xffffffff:0xff000000,1,0),"clear");
  struct V{float x,y,z,rhw,u,v;};const V v[]={{-.5f,-.5f,0,1,0,0},{63.5f,-.5f,0,1,1,0},{-.5f,63.5f,0,1,0,1},{63.5f,63.5f,0,1,1,1}};
  hr(dev->BeginScene(),"begin");auto result=dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,v,sizeof(V));auto end=dev->EndScene();hr(result,"draw");hr(end,"end");
 }
 std::vector<unsigned char> rendered(IDirect3DTexture9*t,UINT level,unsigned mode){draw(t,level,mode);fence();hr(dev->GetRenderTargetData(rt.p,readback.p),"RT readback");D3DLOCKED_RECT lock{};hr(readback->LockRect(&lock,nullptr,D3DLOCK_READONLY),"RT lock");std::vector<unsigned char>b(64*64*4);for(unsigned y=0;y<64;++y)memcpy(b.data()+y*64*4,static_cast<unsigned char*>(lock.pBits)+size_t(y)*lock.Pitch,64*4);hr(readback->UnlockRect(),"RT unlock");return b;}
};
struct Comparison{bool raw=true,rgba=true,alpha=true,render=true;int firstRaw=-1,firstPixel=-1;uint64_t pixels=0,alphaZero=0,alphaPartial=0,alphaOpaque=0;};
Comparison compare(Context&c,const DDS&d,IDirect3DTexture9*a,IDirect3DTexture9*b,bool draws=true){
 Comparison out;
 for(UINT l=0;l<d.m;++l){
  D3DLOCKED_RECT la{},lb{};hr(a->LockRect(l,&la,nullptr,D3DLOCK_READONLY),"compare lock A");auto hb=b->LockRect(l,&lb,nullptr,D3DLOCK_READONLY);if(FAILED(hb)){a->UnlockRect(l);hr(hb,"compare lock B");}
  bool equal=true;for(UINT y=0;y<d.rows(l);++y)if(memcmp(static_cast<unsigned char*>(la.pBits)+size_t(y)*la.Pitch,static_cast<unsigned char*>(lb.pBits)+size_t(y)*lb.Pitch,d.row(l)))equal=false;
  hr(a->UnlockRect(l),"compare unlock A");hr(b->UnlockRect(l),"compare unlock B");if(!equal){out.raw=false;if(out.firstRaw<0)out.firstRaw=int(l);}
  // Exact format/block equality already proves all encoded color/alpha equal.
  // Decode any differing mip fully, in bounded tiles, to separate padding from visible differences.
  if(!equal){Com<IDirect3DSurface9>sa,sb;hr(a->GetSurfaceLevel(l,sa.put()),"decode source A");hr(b->GetSurfaceLevel(l,sb.put()),"decode source B");
   for(UINT y=0;y<d.height(l);y+=256)for(UINT x=0;x<d.width(l);x+=256){UINT w=std::min(256u,d.width(l)-x),h=std::min(256u,d.height(l)-y);RECT src{LONG(x),LONG(y),LONG(x+w),LONG(y+h)},dst{0,0,LONG(w),LONG(h)};
    hr(c.surface(c.decodeA.p,nullptr,&dst,sa.p,nullptr,&src,1,0),"decode A");hr(c.surface(c.decodeB.p,nullptr,&dst,sb.p,nullptr,&src,1,0),"decode B");D3DLOCKED_RECT ra{},rb{};hr(c.decodeA->LockRect(&ra,&dst,D3DLOCK_READONLY),"decode lock A");auto r=c.decodeB->LockRect(&rb,&dst,D3DLOCK_READONLY);if(FAILED(r)){c.decodeA->UnlockRect();hr(r,"decode lock B");}
    for(UINT row=0;row<h;++row)for(UINT col=0;col<w;++col){auto pa=static_cast<unsigned char*>(ra.pBits)+size_t(row)*ra.Pitch+4*col;auto pb=static_cast<unsigned char*>(rb.pBits)+size_t(row)*rb.Pitch+4*col;if(memcmp(pa,pb,4)){out.rgba=false;if(out.firstPixel<0)out.firstPixel=int(l);}if(pa[3]!=pb[3])out.alpha=false;}
    hr(c.decodeA->UnlockRect(),"decode unlock A");hr(c.decodeB->UnlockRect(),"decode unlock B");
   }
  }
  out.pixels+=uint64_t(d.width(l))*d.height(l);
  if(draws)for(unsigned mode=0;mode<3;++mode){auto pa=c.rendered(a,l,mode),pb=c.rendered(b,l,mode);if(pa!=pb)out.render=false;if(!mode)for(size_t k=3;k<pa.size();k+=4){if(pa[k]!=pb[k])out.alpha=false;if(pa[k]==0)++out.alphaZero;else if(pa[k]==255)++out.alphaOpaque;else ++out.alphaPartial;}}
 }
 hr(c.dev->SetTexture(0,nullptr),"unbind comparison");c.fence();return out;
}
struct Asset{std::string id;std::filesystem::path path;bool bench;};
std::vector<Asset> manifest(const std::filesystem::path&p){std::ifstream f(p);require(bool(f),"manifest");std::vector<Asset>v;std::string line;while(std::getline(f,line)){std::istringstream s(line);std::string id,path,flag;require(bool(std::getline(s,id,'\t'))&&bool(std::getline(s,path,'\t'))&&bool(std::getline(s,flag)),"manifest row");v.push_back({id,std::filesystem::path(path),flag=="1"});}require(!v.empty(),"manifest empty");return v;}
void qualification(Context&c,const std::vector<Asset>&assets){unsigned tested=0,failed=0,rawDifferent=0;uint64_t zero=0,partial=0;bool negativeDone=false;
 for(const auto&asset:assets){DDS d(asset.path);auto baseline=c.create(d,false);
  // Retain raw-copy counterexample independently from the qualified edge-mip path.
  auto raw=c.create(d,true,false);auto rawResult=compare(c,d,baseline.p,raw.p,false);if(!rawResult.rgba||!rawResult.alpha)++rawDifferent;raw=Com<IDirect3DTexture9>{};
  for(unsigned config=0;config<4;++config){auto candidate=c.create(d,(config&1)!=0);if(config&2)candidate->PreLoad();auto r=compare(c,d,baseline.p,candidate.p);bool pass=r.rgba&&r.alpha&&r.render;zero+=r.alphaZero;partial+=r.alphaPartial;++tested;if(!pass)++failed;
   std::cout<<"{\"type\":\"quality\",\"id\":\""<<asset.id<<"\",\"config\":"<<config<<",\"pass\":"<<(pass?"true":"false")<<",\"mips\":"<<d.m<<",\"pixels\":"<<r.pixels<<",\"raw_equal\":"<<(r.raw?"true":"false")<<",\"rgba_equal\":"<<(r.rgba?"true":"false")<<",\"alpha_equal\":"<<(r.alpha?"true":"false")<<",\"render_equal\":"<<(r.render?"true":"false")<<",\"first_raw_mip\":"<<r.firstRaw<<",\"first_pixel_mip\":"<<r.firstPixel<<",\"raw_copy_pixel_equal\":"<<(rawResult.rgba&&rawResult.alpha?"true":"false")<<",\"sample_alpha_zero\":"<<r.alphaZero<<",\"sample_alpha_partial\":"<<r.alphaPartial<<"}\n"<<std::flush;
  }
  if(!negativeDone&&d.fmt==D3DFMT_A8R8G8B8&&d.w<=64&&d.h<=64){
   auto corrupt=c.create(d,true);D3DLOCKED_RECT lock{};hr(corrupt->LockRect(0,&lock,nullptr,0),"negative lock");static_cast<unsigned char*>(lock.pBits)[3]^=255;hr(corrupt->UnlockRect(0),"negative unlock");auto neg=compare(c,d,baseline.p,corrupt.p);require(!neg.alpha&&!neg.rgba&&!neg.render,"alpha negative not detected");
   bool rejected=false;try{c.create(d,true,true,1);}catch(...){rejected=true;}require(rejected,"fill failure not rejected");negativeDone=true;
  }
 }
 require(negativeDone&&zero&&partial,"alpha control coverage");std::cout<<"{\"type\":\"qualification\",\"tested\":"<<tested<<",\"failed\":"<<failed<<",\"raw_copy_pixel_mismatch_assets\":"<<rawDifferent<<",\"alpha_negative_detected\":true,\"fill_failure_rejected\":true,\"passed\":"<<(failed?"false":"true")<<"}\n";require(!failed,"quality mismatch");
}
void benchmark(Context&c,const std::vector<Asset>&assets){unsigned rows=0;
 for(const auto&a:assets)if(a.bench){DDS d(a.path);
  // Warm shader/driver paths outside timing. Each measurement creates a new texture.
  {auto t=c.create(d,false);c.draw(t.p,0,0);c.fence();hr(c.dev->SetTexture(0,nullptr),"warm unbind");}c.fence();
  for(unsigned round=0;round<8;++round)for(unsigned order=0;order<4;++order){unsigned config=(round+(round%2?3-order:order))%4;c.fence();auto start=now();auto texture=c.create(d,(config&1)!=0);auto created=now();if(config&2)texture->PreLoad();auto preloaded=now();c.draw(texture.p,0,0);auto submitted=now();c.fence();auto completed=now();c.draw(texture.p,0,0);c.fence();auto repeated=now();hr(c.dev->SetTexture(0,nullptr),"benchmark unbind");
   PROCESS_MEMORY_COUNTERS_EX mem{};mem.cb=sizeof(mem);require(GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&mem),sizeof(mem))!=0,"process memory");
   std::cout<<"{\"type\":\"timing\",\"id\":\""<<a.id<<"\",\"round\":"<<round<<",\"order\":"<<order<<",\"config\":"<<config<<",\"bytes\":"<<d.b.size()<<",\"create_ticks\":"<<created-start<<",\"preload_ticks\":"<<preloaded-created<<",\"first_submit_ticks\":"<<submitted-preloaded<<",\"first_wait_ticks\":"<<completed-submitted<<",\"total_ticks\":"<<completed-start<<",\"repeat_ticks\":"<<repeated-completed<<",\"private_bytes\":"<<mem.PrivateUsage<<",\"peak_working_set_bytes\":"<<mem.PeakWorkingSetSize<<"}\n"<<std::flush;++rows;
  }
 }
 std::cout<<"{\"type\":\"benchmark_complete\",\"rows\":"<<rows<<",\"passed\":true}\n";
}
int wmain(int argc,wchar_t**argv){try{require(argc==3,"arguments: qualify|benchmark manifest");Context c;auto assets=manifest(argv[2]);if(std::wstring(argv[1])==L"qualify")qualification(c,assets);else if(std::wstring(argv[1])==L"benchmark")benchmark(c,assets);else throw std::runtime_error("mode");return 0;}catch(const std::exception&e){std::cerr<<e.what()<<"\n";return 1;}}
