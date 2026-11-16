#define GEHD_PACK_FIXTURE
#include "pack_loader.cpp"
#include <iostream>
int wmain(int argc,wchar_t**argv){
 if(argc!=2||!PackFixtureStart(argv[1]))return 2;
 auto apiModule=LoadLibraryW(L"C:\\Windows\\SysWOW64\\d3d9.dll");auto create=(IDirect3D9*(WINAPI*)(UINT))GetProcAddress(apiModule,"Direct3DCreate9");auto api=create(D3D_SDK_VERSION);
 D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=GetDesktopWindow();pp.BackBufferWidth=64;pp.BackBufferHeight=64;IDirect3DDevice9*dev{};
 if(FAILED(api->CreateDevice(0,D3DDEVTYPE_HAL,pp.hDeviceWindow,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&dev)))return 3;
 auto it=entries.begin();auto id=it->first;auto original=it->second;auto path=root;auto b=read(root/L"original"/(id+".dds"),cap);
 unsigned failures=0,checks=0;
 auto expect=[&](const char*name,UINT expected,IDirect3DTexture9*tex,HRESULT hr){D3DSURFACE_DESC d{};bool ok=SUCCEEDED(hr)&&tex&&SUCCEEDED(tex->GetLevelDesc(0,&d))&&d.Width==expected;++checks;if(!ok)++failures;std::cout<<"{\"case\":\""<<name<<"\",\"pass\":"<<(ok?"true":"false")<<"}\n";if(tex)tex->Release();};
 IDirect3DTexture9*tex{};auto call=[&]{tex=nullptr;return PackFixtureLoad(dev,b.data(),UINT(b.size()),&tex);};
 HRESULT hr=call();expect("2x",original.w,tex,hr);
 it->second.replacement=std::string(64,'0');hr=call();expect("bad_hash_original",original.w/original.scale,tex,hr);it->second=original;
 root=path/L"missing-directory";hr=call();expect("missing_file_original",original.w/original.scale,tex,hr);root=path;
 auto altered=b;altered.back()^=1;tex=nullptr;hr=PackFixtureLoad(dev,altered.data(),UINT(altered.size()),&tex);expect("unknown_source_original",original.w/original.scale,tex,hr);
 tex=nullptr;hr=PackFixtureExplicit(dev,b.data(),UINT(b.size()),&tex);expect("explicit_dimensions_original",original.w/original.scale,tex,hr);
 // A retained texture survives disabling; subsequent loads use original data.
 IDirect3DTexture9*retained{};hr=PackFixtureLoad(dev,b.data(),UINT(b.size()),&retained);PackFixtureDisable();expect("disable_retains_old_resource",original.w,retained,hr);hr=call();expect("disable_new_load_original",original.w/original.scale,tex,hr);
 dev->Release();api->Release();std::cout<<"{\"checks\":"<<checks<<",\"failures\":"<<failures<<",\"hits\":"<<hit<<",\"fallbacks\":"<<fallback<<"}\n";
 return failures||checks!=7||hit!=2||fallback!=2?1:0;
}
