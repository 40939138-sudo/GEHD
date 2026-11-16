#define GEHD_SHA_LOADER_HOST_ONLY
#include "sha_loader_gpu.cpp"
#include "loading_runtime.hpp"
// Uses the actual candidate fixture DLL, not a substitute preparation host.
struct Framework {
 Loader loader;BOOL(WINAPI*start)(const wchar_t*,const char*,UINT,UINT);void(WINAPI*stop)();BOOL(WINAPI*stats)(loading::Counters*);BOOL(WINAPI*finish)(loading::Counters*);BOOL(WINAPI*ready)();BOOL(WINAPI*drain)();
 Framework(const wchar_t*dll,const wchar_t*root,const wchar_t*store,const char*key,UINT mode,UINT delay):loader(dll,root){
  start=(decltype(start))GetProcAddress(loader.module,"_PackFrameworkStart@16");stop=(decltype(stop))GetProcAddress(loader.module,"_PackFrameworkStop@0");stats=(decltype(stats))GetProcAddress(loader.module,"_PackFrameworkStats@4");ready=(decltype(ready))GetProcAddress(loader.module,"_PackFrameworkReady@0");drain=(decltype(drain))GetProcAddress(loader.module,"_PackFrameworkDrain@0");require(start&&stop&&stats&&ready&&drain&&start(store,key,mode,delay),"framework start");
  finish=(decltype(finish))GetProcAddress(loader.module,"_PackFrameworkFinish@4");require(finish!=nullptr,"finish export");
 }
 ~Framework(){stop();}
};
int wmain(int argc,wchar_t**argv){try{
 require(argc==9,"quality|timing|train dll corpus store binding mode ready delay");
 auto mode=UINT(std::stoul(argv[6])),delay=UINT(std::stoul(argv[8]));bool quality=std::wstring(argv[1])==L"quality";bool awaitReady=std::wstring(argv[7])==L"1";
 std::filesystem::path root=argv[3];std::string key;for(auto*p=argv[5];*p;++p)key+=char(*p);
 Context c;std::vector<std::string>ids;std::vector<DDS>sources,outputs;std::vector<Info>infos;
 std::ifstream f(root/L"mapping.tsv");std::string line;std::getline(f,line);require(line=="GEHD_TEXTURE_PACK_MIXED_V1","mixed manifest");
 while(std::getline(f,line)){std::istringstream row(line);std::string id,sha;UINT os,sz,w,h,m,fmt,scale;require(bool(row>>id>>sha>>os>>sz>>w>>h>>m>>fmt>>scale),"row");ids.push_back(id);sources.emplace_back(root/L"original"/(id+".dds"));if(quality)outputs.emplace_back(root/(scale==4?L"4x":L"2x")/(id+".dds"));Info expected{};Com<IDirect3DTexture9>t;hr(c.native(c.dev.p,sources.back().b.data(),UINT(sources.back().b.size()),0xfffffffeu,0xfffffffeu,0xfffffffdu,0,D3DFMT_UNKNOWN,D3DPOOL_MANAGED,1,0xffffffffu,0,&expected,nullptr,t.put()),"native SourceInfo");infos.push_back(expected);}
 auto totalBegin=now();Framework framework(argv[2],argv[3],argv[4],key.c_str(),mode,delay);if(mode&&awaitReady)require(framework.ready(),"ready");
 bool competing=std::wstring(argv[1])==L"competition",overlap=false;uint64_t quietWait=0;
 if(competing){
  for(size_t i=0;i<ids.size();++i){Info info{};auto t=framework.loader.create(c,sources[i],info);c.draw(t.p,0,0);c.fence();hr(c.dev->SetTexture(0,nullptr),"prime unbind");}
  auto quietBegin=now();if(mode==2){require(framework.drain(),"prime drain");auto deadline=GetTickCount64()+10000;loading::Counters s{};
   do{require(framework.stats(&s),"save stats");if(s.saving){overlap=true;break;}Sleep(1);}while(GetTickCount64()<deadline);require(overlap,"observe automatic background checkpoint");}
  else Sleep(5000); // Equal idle interval: do not confound GPU power state with writer cost.
  quietWait=now()-quietBegin;
 }
 auto begin=now();uint64_t first=0;unsigned checks=0;
 for(size_t i=0;i<ids.size();++i){Info info{};auto t=framework.loader.create(c,sources[i],info);require(memcmp(&info,&infos[i],sizeof(info))==0,"original SourceInfo");c.draw(t.p,0,0);c.fence();if(!i)first=now()-begin;
  if(quality){auto ref=c.create(outputs[i],false);auto result=compare(c,outputs[i],ref.p,t.p);require(result.raw&&result.rgba&&result.alpha&&result.render,"pixel/mip/alpha/render");++checks;}
  hr(c.dev->SetTexture(0,nullptr),"unbind");
 }
 auto end=now();if(mode)require(framework.drain(),"observation drain");loading::Counters s{};if(mode)require(framework.stats(&s),"stats");
 if(mode)require(framework.finish(&s),"final drain");else framework.stop();auto drained=now();LARGE_INTEGER q{};QueryPerformanceFrequency(&q);
 std::cout<<"{\"type\":\"checkpoint\",\"saves\":"<<s.saves<<",\"failures\":"<<s.saveFailures<<",\"ticks\":"<<s.saveTicks<<",\"max_ticks\":"<<s.maxSaveTicks<<",\"generation\":"<<s.generation<<"}\n";
 std::cout<<"{\"type\":\"save_phases\",\"deferred\":"<<s.deferred<<",\"lock_ticks\":"<<s.savePhases[0]<<",\"load_encode_ticks\":"<<s.savePhases[1]<<",\"write_ticks\":"<<s.savePhases[2]<<",\"flush_ticks\":"<<s.savePhases[3]<<",\"readback_ticks\":"<<s.savePhases[4]<<",\"publish_ticks\":"<<s.savePhases[5]<<"}\n";
 if(competing)std::cout<<"{\"type\":\"competition\",\"save_active_at_start\":"<<(overlap?"true":"false")<<",\"quiet_wait_ticks\":"<<quietWait<<"}\n";
 std::cout<<"{\"passed\":true,\"requests\":"<<ids.size()<<",\"checks\":"<<checks<<",\"frequency\":"<<q.QuadPart<<",\"first_ticks\":"<<first<<",\"batch_ticks\":"<<end-begin<<",\"with_init_ticks\":"<<end-totalBegin<<",\"with_save_ticks\":"<<drained-totalBegin<<",\"loaded\":"<<(s.loaded?"true":"false")<<",\"predictions\":"<<s.predictions<<",\"ready_hits\":"<<s.pool.readyHits<<",\"late_hits\":"<<s.pool.lateHits<<",\"reads\":"<<s.pool.reads<<",\"hashes\":"<<s.pool.hashes<<",\"peak_bytes\":"<<s.pool.peakBytes<<",\"peak_spec\":"<<s.pool.peakSpecBytes<<",\"dropped\":"<<s.dropped<<",\"processed\":"<<s.processed<<"}\n";return 0;
 }catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
