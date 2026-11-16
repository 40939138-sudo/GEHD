#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d9.h>
#include <filesystem>
#include <fstream>
#include <vector>
#include <string>
#include <iostream>
#include <sstream>
#include <cstring>
using Start=BOOL(WINAPI*)(const wchar_t*);
struct Info {UINT w,h,d,m;D3DFORMAT format;D3DRESOURCETYPE type;UINT fileFormat;};
using Load=HRESULT(WINAPI*)(IDirect3DDevice9*,const void*,UINT,Info*,IDirect3DTexture9**);
using Hits=UINT(WINAPI*)();
using Native=HRESULT(WINAPI*)(IDirect3DDevice9*,const void*,UINT,UINT,UINT,UINT,DWORD,D3DFORMAT,D3DPOOL,DWORD,DWORD,D3DCOLOR,void*,PALETTEENTRY*,IDirect3DTexture9**);
int wmain(int argc,wchar_t**argv){
 if(argc!=3&&argc!=4)return 2;bool rebuiltMips=argc==4&&std::wstring(argv[3])==L"--rebuilt-mips";if(argc==4&&!rebuiltMips)return 2;
 auto dll=LoadLibraryW(argv[1]);if(!dll)return 3;
 auto start=(Start)GetProcAddress(dll,"_PackFixtureStart@4");auto load=(Load)GetProcAddress(dll,"_PackFixtureLoadInfo@20");auto hits=(Hits)GetProcAddress(dll,"_PackFixtureHits@0");
 if(!start||!load||!hits||!start(argv[2]))return 4;
 auto real=LoadLibraryW(L"C:\\Windows\\SysWOW64\\d3d9.dll");auto create=(IDirect3D9*(WINAPI*)(UINT))GetProcAddress(real,"Direct3DCreate9");
 auto api=create(D3D_SDK_VERSION);if(!api)return 5;
 D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.BackBufferFormat=D3DFMT_UNKNOWN;pp.hDeviceWindow=GetDesktopWindow();pp.BackBufferWidth=64;pp.BackBufferHeight=64;
 IDirect3DDevice9* dev{};HRESULT hr=api->CreateDevice(0,D3DDEVTYPE_HAL,pp.hDeviceWindow,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&dev);
 if(FAILED(hr)){std::cout<<"device "<<std::hex<<hr;api->Release();return 6;}
 std::filesystem::path root=argv[2];std::ifstream list(root/L"mapping.tsv");std::string line;std::getline(list,line);unsigned count=0,failed=0;bool mixed=line=="GEHD_TEXTURE_PACK_MIXED_V1";UINT scale=line=="GEHD_TEXTURE_PACK_4X_V1"?4:2,originalLevel=scale==4?2:1; D3DCAPS9 caps{};dev->GetDeviceCaps(&caps);std::cout<<"{\"maxTextureWidth\":"<<caps.MaxTextureWidth<<",\"maxTextureHeight\":"<<caps.MaxTextureHeight<<"}\n";
 while(std::getline(list,line)){
  std::istringstream row(line);std::string id,repl;UINT origSize,size,w,h,m,fmt;row>>id>>repl>>origSize>>size>>w>>h>>m>>fmt;if(mixed)row>>scale;originalLevel=scale==4?2:1;
  std::ifstream file(root/L"original"/(id+".dds"),std::ios::binary);std::vector<char>b((std::istreambuf_iterator<char>(file)),{});
  Info returnedInfo{},referenceInfo{};
  IDirect3DTexture9*t{};hr=load(dev,b.data(),UINT(b.size()),&returnedInfo,&t);D3DSURFACE_DESC d{};
  bool good=SUCCEEDED(hr)&&t&&SUCCEEDED(t->GetLevelDesc(0,&d))&&d.Width==w&&d.Height==h&&d.Format==fmt&&t->GetLevelCount()==m;
  // Compare with direct D3DX loading of the same output. D3DX may normalize
  // compressed small mip padding; file-byte equality is reported separately.
  std::ifstream rf(root/(scale==4?L"4x":L"2x")/(id+".dds"),std::ios::binary);std::vector<char>expected((std::istreambuf_iterator<char>(rf)),{});size_t pos=128;
  auto dx=GetModuleHandleW(L"d3dx9_43.dll");auto native=(Native)GetProcAddress(dx,"D3DXCreateTextureFromFileInMemoryEx");IDirect3DTexture9* control{};
  IDirect3DTexture9* sourceControl{};
  bool infoPreserved=SUCCEEDED(native(dev,b.data(),UINT(b.size()),0xfffffffeu,0xfffffffeu,0xfffffffdu,0,D3DFMT_UNKNOWN,D3DPOOL_MANAGED,1,0xffffffffu,0,&referenceInfo,nullptr,&sourceControl))&&sourceControl&&memcmp(&returnedInfo,&referenceInfo,sizeof(Info))==0&&returnedInfo.w*scale==w&&returnedInfo.h*scale==h;
  if(sourceControl)sourceControl->Release();good=good&&infoPreserved;
  good=good&&SUCCEEDED(native(dev,expected.data(),UINT(expected.size()),0xfffffffeu,0xfffffffeu,0xfffffffdu,0,D3DFMT_UNKNOWN,D3DPOOL_MANAGED,1,0xffffffffu,0,nullptr,nullptr,&control))&&control;
  bool fileEqual=true;int firstFileMismatch=-1;
  for(UINT level=0;good&&level<m;++level){D3DLOCKED_RECT lock{},reference{};if(FAILED(t->LockRect(level,&lock,nullptr,D3DLOCK_READONLY))){good=false;break;}
   if(FAILED(control->LockRect(level,&reference,nullptr,D3DLOCK_READONLY))){t->UnlockRect(level);good=false;break;}
   UINT lw=std::max(1u,w>>level),lh=std::max(1u,h>>level),rows=fmt==D3DFMT_A8R8G8B8?lh:(lh+3)/4,bytes=fmt==D3DFMT_A8R8G8B8?lw*4:((lw+3)/4)*(fmt==D3DFMT_DXT1?8:16);
   for(UINT y=0;y<rows;++y){if(memcmp((char*)lock.pBits+y*lock.Pitch,(char*)reference.pBits+y*reference.Pitch,bytes))good=false;if(pos+bytes>expected.size()||memcmp((char*)lock.pBits+y*lock.Pitch,expected.data()+pos,bytes)){fileEqual=false;if(firstFileMismatch<0)firstFileMismatch=int(level);}pos+=bytes;}
   t->UnlockRect(level);control->UnlockRect(level);
  }
  good=good&&pos==expected.size();bool nativeSourceLevelPreserved=true;
  if(good&&fmt==D3DFMT_A8R8G8B8){
   IDirect3DTexture9*original{};auto nativeHr=native(dev,b.data(),UINT(b.size()),0xfffffffeu,0xfffffffeu,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,1,0xffffffffu,0,nullptr,nullptr,&original);
   D3DLOCKED_RECT lo{},ln{};bool lockedOld=SUCCEEDED(nativeHr)&&original&&SUCCEEDED(original->LockRect(0,&lo,nullptr,D3DLOCK_READONLY));bool lockedNew=lockedOld&&SUCCEEDED(t->LockRect(originalLevel,&ln,nullptr,D3DLOCK_READONLY));
   nativeSourceLevelPreserved=lockedNew;
   if(lockedNew){for(UINT y=0;y<h/scale;++y)if(memcmp((char*)lo.pBits+y*lo.Pitch,(char*)ln.pBits+y*ln.Pitch,(w/scale)*4))nativeSourceLevelPreserved=false;t->UnlockRect(originalLevel);}
   if(lockedOld)original->UnlockRect(0);if(original)original->Release();if(!rebuiltMips)good=good&&nativeSourceLevelPreserved;
  }
  if(t)t->Release();if(control)control->Release();++count;if(!good)++failed;
  std::cout<<"{\"source\":\""<<id<<"\",\"pass\":"<<(good?"true":"false")<<",\"hr\":"<<(int)hr<<",\"fileBytesEqual\":"<<(fileEqual?"true":"false")<<",\"firstFileMismatchLevel\":"<<firstFileMismatch<<",\"nativeSourceLevelPreserved\":"<<(nativeSourceLevelPreserved?"true":"false")<<",\"sourceInfoPreserved\":"<<(infoPreserved?"true":"false")<<"}\n"<<std::flush;
 }
 dev->Release();api->Release();std::cout<<"{\"tested\":"<<count<<",\"failed\":"<<failed<<",\"hits\":"<<hits()<<"}\n";
 return failed||!count||hits()!=count?1:0;
}
