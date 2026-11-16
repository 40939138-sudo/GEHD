#define GEHD_SHA_INTEGRATION_TEST
#define wmain negative_qualification_main
#include "negative.cpp"
#undef wmain
#include <thread>
#include <array>
#include <barrier>
unsigned checks=0;
void check(bool ok,const char*name){++checks;if(!ok)throw std::runtime_error(name);}
struct Args {
 IDirect3DDevice9* dev=(IDirect3DDevice9*)1;const void* data{};UINT bytes{},w=0xfffffffeu,h=0xfffffffeu,m=0xfffffffdu;
 DWORD usage=0;D3DFORMAT format=D3DFMT_UNKNOWN;D3DPOOL pool=D3DPOOL_MANAGED;DWORD filter=1,mf=0xffffffffu;D3DCOLOR color=0;
 Info* info{};PALETTEENTRY* palette{};IDirect3DTexture9** out{};
};
Args observed;DWORD stubIncoming{};unsigned stubCalls{};
HRESULT WINAPI stub(IDirect3DDevice9*d,LPCVOID p,UINT n,UINT w,UINT h,UINT m,DWORD u,D3DFORMAT f,D3DPOOL pool,DWORD filter,DWORD mf,D3DCOLOR color,Info*i,PALETTEENTRY*pal,IDirect3DTexture9**out){
 observed={d,p,n,w,h,m,u,f,pool,filter,mf,color,i,pal,out};stubIncoming=GetLastError();++stubCalls;
 if(i)*i={17,19,1,3,D3DFMT_A8R8G8B8,D3DRTYPE_TEXTURE,4};if(out)*out=(IDirect3DTexture9*)0x1234;
 SetLastError(0x12345678);return E_ABORT;
}
HRESULT call(const Args&a){return replacement(a.dev,a.data,a.bytes,a.w,a.h,a.m,a.usage,a.format,a.pool,a.filter,a.mf,a.color,a.info,a.palette,a.out);}
bool same(const Args&a,const Args&b){return a.dev==b.dev&&a.data==b.data&&a.bytes==b.bytes&&a.w==b.w&&a.h==b.h&&a.m==b.m&&a.usage==b.usage&&a.format==b.format&&a.pool==b.pool&&a.filter==b.filter&&a.mf==b.mf&&a.color==b.color&&a.info==b.info&&a.palette==b.palette&&a.out==b.out;}

Fn actualNative{};IDirect3DTexture9*failedTexture{};unsigned nativeAttempts=0;
Fn contractNative{};
HRESULT WINAPI stampNative(IDirect3DDevice9*d,LPCVOID p,UINT n,UINT w,UINT h,UINT m,DWORD u,D3DFORMAT f,D3DPOOL pool,DWORD filter,DWORD mf,D3DCOLOR color,Info*i,PALETTEENTRY*pal,IDirect3DTexture9**out){
 auto result=contractNative(d,p,n,w,h,m,u,f,pool,filter,mf,color,i,pal,out);SetLastError(0x78341256);return result;
}
HRESULT WINAPI failHd(IDirect3DDevice9*d,LPCVOID p,UINT n,UINT w,UINT h,UINT m,DWORD u,D3DFORMAT f,D3DPOOL pool,DWORD filter,DWORD mf,D3DCOLOR color,Info*i,PALETTEENTRY*pal,IDirect3DTexture9**out){
 ++nativeAttempts;auto result=actualNative(d,p,n,w,h,m,u,f,pool,filter,mf,color,i,pal,out);
 if(nativeAttempts==1&&SUCCEEDED(result)&&out&&*out){failedTexture=*out;failedTexture->AddRef();return E_FAIL;}return result;
}
int wmain(int argc,wchar_t**argv){try{
 if(argc!=3)return 2;unsigned mode=std::stoul(argv[2]);opensslsha::policy=mode==1?0:mode==2?2:-1;opensslsha::failSelfTest=mode==3;
 #ifdef GEHD_REQUEST_PREFILTER
 {std::array<unsigned char,128>data{};IDirect3DTexture9*t{};Args a;a.data=data.data();a.bytes=128;a.out=&t;a.w=1;enabled=true;realLoad=stub;sourceSizes.clear();check(call(a)==E_ABORT&&opensslsha::calls==0&&opensslsha::initializations==0,"parameter rejection before SHA initialization");a.w=0xfffffffeu;check(call(a)==E_ABORT&&opensslsha::calls==0&&opensslsha::initializations==0,"size rejection before SHA initialization");}
 #endif
 // Start all producers before the very first optimized call.
 std::array<std::string,8>buffers;std::array<opensslsha::Digest,8>answers;
 for(unsigned t=0;t<8;++t){buffers[t]=std::string(8192+t*13,char(t+1));check(BCryptHash(BCRYPT_SHA256_ALG_HANDLE,nullptr,0,(PUCHAR)buffers[t].data(),ULONG(buffers[t].size()),answers[t].data(),32)>=0,"oracle");}
 std::barrier gate(8);std::atomic<bool>ok{true};std::vector<std::thread>workers;
 for(unsigned t=0;t<8;++t)workers.emplace_back([&,t]{gate.arrive_and_wait();for(unsigned j=0;j<256;++j){opensslsha::Digest d{};if(loaderhash::digest(buffers[t].data(),ULONG(buffers[t].size()),d.data())<0||d!=answers[t])ok=false;}});
 for(auto&t:workers)t.join();check(ok&&opensslsha::initializations==1&&opensslsha::calls==2048,"first-call concurrency");
 unsigned caps[10];memcpy(caps,OPENSSL_ia32cap_P,sizeof(caps));
 bool accelerated=mode==0||mode==2;check(opensslsha::ready==accelerated,"dispatch selftest gate");check(accelerated?opensslsha::optimizedCalls==2048:opensslsha::cngCalls==2048,"dispatch backend counters");
 for(int ss=0;ss<2;++ss)for(int av=0;av<2;++av)for(int bmi=0;bmi<2;++bmi)for(int intel=0;intel<2;++intel){auto selected=opensslsha::choose(ss,av,bmi,intel,-1);check(selected==(ss?(av&&bmi&&intel?3u:2u):0u),"CPU selection truth table");check(opensslsha::choose(ss,av,bmi,intel,0)==0,"unsupported override");}
 for(size_t n=0;n<1026;++n)for(size_t offset: {0u,1u,7u,15u}){std::vector<unsigned char>b(n+16);for(size_t k=0;k<b.size();++k)b[k]=static_cast<unsigned char>(k*13+n);opensslsha::Digest a{},d{};check(BCryptHash(BCRYPT_SHA256_ALG_HANDLE,nullptr,0,b.data()+offset,ULONG(n),a.data(),32)>=0&&loaderhash::digest(b.data()+offset,ULONG(n),d.data())>=0&&a==d,"length alignment full hash");if(accelerated){opensslsha::Stream s;for(size_t k=0;k<n;k+=17)s.add(b.data()+offset+k,std::min(size_t(17),n-k));check(s.finish()==a,"incremental padding");}}
 check(hash("",0)=="E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855","empty known answer");
 std::string million(1000000,'a');check(hash(million.data(),million.size())=="CDC76E5C9914FB9281A1C7E284D73E67F1809A48A497200E046D39CCC7112CD0","million known answer");
 check(memcmp(caps,OPENSSL_ia32cap_P,sizeof(caps))==0,"immutable capabilities");
 // Argument/HRESULT/LastError forwarding on every existing direct path.
 #ifdef GEHD_REQUEST_PREFILTER
 auto manifest=[](UINT n){return std::string("GEHD_TEXTURE_PACK_MIXED_V1\n")+std::string(64,'A')+'\t'+std::string(64,'B')+'\t'+std::to_string(n)+"\t128\t2\t2\t1\t21\t2\n";};
 auto parse=[](const std::string&s){return parseManifest(std::vector<unsigned char>(s.begin(),s.end()));};
 check(parse(manifest(128))&&sourceSizes==std::unordered_set<UINT>{128},"first manifest size index");auto previous=entries;
 check(!parse(manifest(256)+"invalid\n")&&sourceSizes==std::unordered_set<UINT>{128}&&entries.size()==previous.size()&&entries.begin()->second.sourceSize==128,"invalid manifest preserves index and entries");
 check(parse(manifest(256))&&sourceSizes==std::unordered_set<UINT>{256},"manifest generation replaces index");
 #endif
 std::vector<unsigned char>b(256,0x5a);auto id=hash(b.data(),b.size());entries.clear();Entry e{};e.sourceSize=256;entries[id]=e;enabled=true;realLoad=stub;
 #ifdef GEHD_REQUEST_PREFILTER
 sourceSizes={256};
 #endif
#ifdef GEHD_LOADING_FRAMEWORK
 check(PackFrameworkStart(L"",std::string(64,'A').c_str(),1,0),"forwarding framework start");
#endif
 for(unsigned scenario=0;scenario<15;++scenario){Info info{};IDirect3DTexture9*texture{};Args a;a.data=b.data();a.bytes=UINT(b.size());a.info=&info;a.out=&texture;auto altered=b;altered.back()^=1;
 switch(scenario){case 0:a.w=1;break;case 1:a.h=1;break;case 2:a.m=1;break;case 3:a.usage=1;break;case 4:a.format=D3DFMT_A8R8G8B8;break;case 5:a.pool=D3DPOOL_DEFAULT;break;case 6:a.filter=2;break;case 7:a.mf=1;break;case 8:a.color=1;break;case 9:a.palette=(PALETTEENTRY*)1;break;case 10:a.data=altered.data();break;case 11:a.bytes=255;break;case 12:a.dev=nullptr;break;case 13:a.bytes=UINT(cap+1);break;case 14:a.data=nullptr;break;}
 stubCalls=0;auto beforeHash=opensslsha::calls.load();SetLastError(0xabcdef);auto result=call(a);DWORD error=GetLastError();check(result==E_ABORT&&error==0x12345678&&stubIncoming==0xabcdef&&stubCalls==1&&same(a,observed)&&texture==(IDirect3DTexture9*)0x1234&&info.w==17&&info.h==19,"forwarded contract");
 unsigned expected=scenario>=12?0:1;
 #ifdef GEHD_REQUEST_PREFILTER
 if(scenario<10||scenario==11)expected=0;
 #endif
 check(opensslsha::calls-beforeHash==expected,"parameter size bypass or full hash count");}
#ifdef GEHD_LOADING_FRAMEWORK
 PackFrameworkStop();
#endif
 hit=0;fallback=0;miss=0;check(negative_qualification_main(2,argv)==0,"seven native negative cases");
 check(PackFixtureStart(argv[1])!=FALSE,"fixture reset");auto it=entries.begin();id=it->first;auto entry=it->second;auto originalBytes=read(root/L"original"/(id+".dds"),cap);auto outputPath=root/(entry.scale==4?L"4x":L"2x")/(id+".dds");auto replacementBytes=read(outputPath,cap);
#ifdef GEHD_LOADING_FRAMEWORK
 check(PackFrameworkStart(L"",std::string(64,'A').c_str(),1,0),"native framework start");
#endif
 auto d9=LoadLibraryW(L"C:\\Windows\\SysWOW64\\d3d9.dll");auto create=(IDirect3D9*(WINAPI*)(UINT))GetProcAddress(d9,"Direct3DCreate9");auto api=create(D3D_SDK_VERSION);D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=GetDesktopWindow();pp.BackBufferWidth=64;pp.BackBufferHeight=64;IDirect3DDevice9*dev{};check(SUCCEEDED(api->CreateDevice(0,D3DDEVTYPE_HAL,pp.hDeviceWindow,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&dev)),"negative device");
 contractNative=realLoad;realLoad=stampNative;
 auto test=[&](const char*name,bool hd,const std::vector<unsigned char>&bytes){Info info{};IDirect3DTexture9*t{};auto before=hit.load();auto result=PackFixtureLoadInfo(dev,bytes.data(),UINT(bytes.size()),&info,&t);check(GetLastError()==0x78341256,"native LastError after lease release");D3DSURFACE_DESC d{};bool good=SUCCEEDED(result)&&t&&SUCCEEDED(t->GetLevelDesc(0,&d))&&d.Width==(hd?entry.w:entry.w/entry.scale)&&info.w==entry.w/entry.scale&&hit==before+(hd?1:0);if(t)t->Release();check(good,name);std::cout<<"{\"case\":\""<<name<<"\",\"passed\":true}\n";};
 auto hashBefore=opensslsha::calls.load();test("eligible_hit_two_full_hashes",true,originalBytes);check(opensslsha::calls-hashBefore==2,"hit keeps both complete hashes");
 // A valid DDS with its final mip removed has an unsupported source length.
 auto differentSize=originalBytes;UINT originalMips=u32(differentSize.data(),28);check(originalMips>1,"size fixture mips");UINT shorterMips=originalMips-1;memcpy(differentSize.data()+28,&shorterMips,4);differentSize.resize(differentSize.size()-(entry.format==D3DFMT_A8R8G8B8?4:entry.format==D3DFMT_DXT1?8:16));
 hashBefore=opensslsha::calls;test("valid_DDS_unknown_size_original",false,differentSize);
 unsigned skippedHashes=1;
 #ifdef GEHD_REQUEST_PREFILTER
 check(sourceSizes.count(UINT(differentSize.size()))==0,"length absent from index");skippedHashes=0;
 #endif
 check(opensslsha::calls-hashBefore==skippedHashes,"valid size miss hash count");
 {IDirect3DTexture9*t{};hashBefore=opensslsha::calls;auto result=PackFixtureExplicit(dev,originalBytes.data(),UINT(originalBytes.size()),&t);D3DSURFACE_DESC d{};check(SUCCEEDED(result)&&t&&SUCCEEDED(t->GetLevelDesc(0,&d))&&d.Width==entry.w/entry.scale,"explicit original native resource");if(t)t->Release();check(opensslsha::calls-hashBefore==skippedHashes,"explicit native hash count");}
 for(unsigned stage: {1u,2u}){opensslsha::failBackendAt=opensslsha::calls+stage;auto before=opensslsha::cngCalls.load();test(stage==1?"source_backend_to_cng":"replacement_backend_to_cng",true,originalBytes);check(opensslsha::cngCalls>before,"CNG fallback used");opensslsha::failBackendAt=0;
 opensslsha::failBackendAt=opensslsha::calls+stage;opensslsha::failCngAt=opensslsha::failBackendAt;test(stage==1?"both_source_hashes_fail_original":"both_replacement_hashes_fail_original",false,originalBytes);opensslsha::failBackendAt=0;opensslsha::failCngAt=0;}
 // This root must be a runner-created private copy, never the evidence corpus.
 auto write=[&](const std::vector<unsigned char>&v){std::ofstream f(outputPath,std::ios::binary|std::ios::trunc);f.write((const char*)v.data(),v.size());check(bool(f),"private corrupt fixture write");};
 for(size_t pos: {size_t(128),replacementBytes.size()/2,replacementBytes.size()-1}){auto damaged=replacementBytes;damaged[pos]^=1;write(damaged);test("same_length_replacement_corruption",false,originalBytes);write(replacementBytes);}
 for(size_t pos: {size_t(128),originalBytes.size()/2,originalBytes.size()-1}){auto damaged=originalBytes;damaged[pos]^=1;hashBefore=opensslsha::calls;test("unknown_source_full_hash",false,damaged);check(opensslsha::calls-hashBefore==1,"same length miss still hashes fully");}
 actualNative=realLoad;realLoad=failHd;nativeAttempts=0;test("D3DX_failure_original",false,originalBytes);realLoad=actualNative;check(nativeAttempts==2&&failedTexture,"D3DX fallback called");auto refs=failedTexture->Release();check(refs==0,"partial HD resource released");
 dev->Release();api->Release();check(memcmp(caps,OPENSSL_ia32cap_P,sizeof(caps))==0,"capabilities unchanged after integration");
#ifdef GEHD_LOADING_FRAMEWORK
 PackFrameworkStop();
#endif
 std::cout<<"{\"passed\":true,\"checks\":"<<checks<<",\"mode\":"<<mode<<",\"selected\":"<<opensslsha::selected<<",\"ready\":"<<(opensslsha::ready?"true":"false")<<",\"initializations\":"<<opensslsha::initializations<<",\"concurrent_hashes\":2048,\"native_negative_cases\":21,\"forwarded_calls\":15}\n";return 0;
 }catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
