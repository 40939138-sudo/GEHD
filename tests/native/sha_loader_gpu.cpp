// Reuse the qualified hidden D3D9 host, full mip comparator and EVENT fence.
#define wmain residency_main
#include "d3dx_residency_compare.cpp"
#undef wmain
struct Loader {
 HMODULE module{};
 using Load=HRESULT(WINAPI*)(IDirect3DDevice9*,const void*,UINT,Info*,IDirect3DTexture9**);
 Load load{};UINT(WINAPI*hits)(){};
 Loader(const wchar_t*dll,const wchar_t*root){module=LoadLibraryW(dll);require(module!=nullptr,"fixture module");auto start=(BOOL(WINAPI*)(const wchar_t*))GetProcAddress(module,"_PackFixtureStart@4");load=(Load)GetProcAddress(module,"_PackFixtureLoadInfo@20");hits=(UINT(WINAPI*)())GetProcAddress(module,"_PackFixtureHits@0");require(start&&load&&hits&&start(root),"fixture start");}
 Com<IDirect3DTexture9> create(Context&c,const DDS&source,Info&info){Com<IDirect3DTexture9>t;auto before=hits();hr(load(c.dev.p,source.b.data(),UINT(source.b.size()),&info,t.put()),"hook load");require(t.p&&hits()==before+1,"HD hit required");return t;}
};
#ifndef GEHD_SHA_LOADER_HOST_ONLY
int wmain(int argc,wchar_t**argv){try{
 require(argc==5,"quality|benchmark baseline candidate root");bool quality=std::wstring(argv[1])==L"quality";require(quality||std::wstring(argv[1])==L"benchmark","mode");
 Loader base(argv[2],argv[4]),candidate(argv[3],argv[4]);Context c;std::filesystem::path root=argv[4];std::ifstream f(root/L"mapping.tsv");std::string line;std::getline(f,line);require(line=="GEHD_TEXTURE_PACK_MIXED_V1","mixed mapping");unsigned count=0;
 while(std::getline(f,line)){std::istringstream row(line);std::string id,digest;UINT os,sz,w,h,m,fmt,scale;require(bool(row>>id>>digest>>os>>sz>>w>>h>>m>>fmt>>scale),"mapping row");DDS source(root/L"original"/(id+".dds")),output(root/(scale==4?L"4x":L"2x")/(id+".dds"));
  Info expected{};{Com<IDirect3DTexture9>t;hr(c.native(c.dev.p,source.b.data(),UINT(source.b.size()),0xfffffffeu,0xfffffffeu,0xfffffffdu,0,D3DFMT_UNKNOWN,D3DPOOL_MANAGED,1,0xffffffffu,0,&expected,nullptr,t.put()),"source info reference");}
  if(quality){auto reference=c.create(output,false);for(unsigned config=0;config<2;++config){Info info{};auto t=(config?candidate:base).create(c,source,info);require(memcmp(&info,&expected,sizeof(info))==0,"original SourceInfo");auto r=compare(c,output,reference.p,t.p);require(r.raw&&r.rgba&&r.alpha&&r.render,"full mip/alpha/render comparison");std::cout<<"{\"type\":\"quality\",\"id\":\""<<id<<"\",\"config\":"<<config<<",\"mips\":"<<m<<",\"raw_equal\":true,\"rgba_equal\":true,\"alpha_equal\":true,\"render_equal\":true,\"source_info_equal\":true,\"passed\":true}\n"<<std::flush;++count;}}
  else{
   for(unsigned config=0;config<2;++config){Info info{};auto t=(config?candidate:base).create(c,source,info);c.draw(t.p,0,0);c.fence();hr(c.dev->SetTexture(0,nullptr),"warm unbind");}
   for(unsigned round=0;round<8;++round)for(unsigned order=0;order<2;++order){unsigned config=(round+order)%2;c.fence();Info info{};auto begin=now();auto t=(config?candidate:base).create(c,source,info);auto created=now();c.draw(t.p,0,0);c.fence();auto end=now();require(memcmp(&info,&expected,sizeof(info))==0,"timed SourceInfo");hr(c.dev->SetTexture(0,nullptr),"unbind");std::cout<<"{\"type\":\"timing\",\"id\":\""<<id<<"\",\"round\":"<<round<<",\"order\":"<<order<<",\"config\":"<<config<<",\"source_bytes\":"<<source.b.size()<<",\"replacement_bytes\":"<<output.b.size()<<",\"hook_ticks\":"<<created-begin<<",\"total_ticks\":"<<end-begin<<"}\n"<<std::flush;++count;}
  }
 }
 std::cout<<"{\"type\":\"complete\",\"rows\":"<<count<<",\"passed\":true}\n";return 0;
 }catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
#endif
