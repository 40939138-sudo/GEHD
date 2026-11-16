#define GEHD_PACK_FIXTURE
#include "pack_loader.cpp"
#include <iostream>
void need(bool b,const char*m){if(!b)throw std::runtime_error(m);}
int wmain(int argc,wchar_t**argv){try{
 need(argc==3,"corpus output");std::filesystem::path corpus=argv[1],out=argv[2];std::filesystem::create_directories(out);
 need(PackFixtureStart(corpus.c_str()),"fixture setup");auto key=hash("framework contract",18);unsigned checks=0;
 auto check=[&](bool b,const char*m){need(b,m);++checks;std::cout<<"PASS "<<m<<'\n'<<std::flush;};
 check(loading::bindingKey(exeHash,key,key,key)!=loading::bindingKey(exeHash,key,key,hash("other",5)),"language binding changes");
 auto variant=out/L"variant";std::filesystem::create_directories(variant);auto stamp=loading::variantStamp(variant,gameprofile::ge2rb);std::filesystem::create_directories(variant/L"CE_DATA/GE2");{std::ofstream f(variant/L"CE_DATA/GE2/GECV_STR.bin");f<<"changed";}check(stamp!=loading::variantStamp(variant,gameprofile::ge2rb),"installed variant detection");
 auto store=out/L"profile";check(PackFrameworkStart(store.c_str(),key.c_str(),2,400),"runtime start");check(PackFrameworkReady(),"background ready");
 std::vector<cpuprep::Entry> jobs;for(auto&[id,e]:entries)jobs.push_back({id,e.replacement,corpus/(e.scale==4?L"4x":L"2x")/(id+".dds"),e.sourceSize,e.size,e.w,e.h,e.m,e.format,e.scale,1});need(jobs.size()>2,"corpus entries");
 for(unsigned run=0;run<3;++run){for(auto&e:jobs){{auto t=loadingRuntime->pool.request(e);auto&s=t.wait();check(s.valid,"full SHA preparation");}loadingRuntime->observe(e.id.c_str());}check(PackFrameworkDrain(),"events drained");}
 // Hold a deliberately slow checkpoint while real preparation and observations run.
 auto until=GetTickCount64()+10000;while(!loadingRuntime->stats().saving&&GetTickCount64()<until)Sleep(10);check(loadingRuntime->stats().saving,"background save active");
 check(loadingRuntime->stats().backgroundPriority,"writer IO and CPU background priority enabled");
 auto begin=GetTickCount64();for(unsigned i=0;i<512;++i)loadingRuntime->observe(jobs[i%jobs.size()].id.c_str());check(GetTickCount64()-begin<150,"submission does not wait 400ms writer");
 {auto t=loadingRuntime->pool.request(jobs[0]);check(t.wait().valid,"demand works during save");}
 PackFrameworkStop();auto p=persist::load(store,key);check(p.valid&&p.model.lastSession==1,"shutdown drains final checkpoint");
 check(PackFrameworkStart(store.c_str(),key.c_str(),2,0)&&PackFrameworkReady(),"reload start");check(loadingRuntime->stats().loaded,"snapshot reused");PackFrameworkStop();
 check(PackFrameworkStart(store.c_str(),hash("new binding",11).c_str(),2,0)&&PackFrameworkReady(),"new version start");check(!loadingRuntime->stats().loaded,"stale profile ignored");PackFrameworkStop();
 // Repeat lifecycle with outstanding CPU work drained before deletion.
 for(unsigned i=0;i<8;++i){check(PackFrameworkStart(L"",key.c_str(),1,0),"workers-only restart");{auto t=loadingRuntime->pool.request(jobs[0]);check(t.wait().valid,"restart verified bytes");}auto s=loadingRuntime->stats();check(s.pool.peakBytes<=loading::Budget&&s.pool.peakSpecBytes<=loading::SpecBudget,"budget bounds");PackFrameworkStop();}
 check(PackFrameworkStart(L"",key.c_str(),0,0)&&!loadingRuntime,"synchronous switch");
 loading::Config delayed;delayed.binding=key;delayed.coordinatorDelayMs=300;startLoading(delayed);check(PackFrameworkReady(),"overflow runtime ready");
 for(unsigned i=0;i<512;++i)loadingRuntime->observe(jobs[0].id.c_str());check(PackFrameworkDrain(),"overflow drain");auto drops=loadingRuntime->stats().dropped;check(drops>0,"bounded queue drops under overload");
 loadingRuntime->observe(jobs[1].id.c_str());check(PackFrameworkDrain(),"post-gap drain");check(loadingRuntime->stats().breaks>0,"drop resets learned adjacency");PackFrameworkStop();
 check(PackFrameworkStart(L"",key.c_str(),1,0),"concurrent start");std::atomic<bool> good{true};std::vector<std::thread> callers;
 for(unsigned t=0;t<4;++t)callers.emplace_back([&,t]{try{for(unsigned i=0;i<16;++i){auto lease=loadingRuntime->pool.request(jobs[(t+i)%jobs.size()],true);if(!lease.wait().valid)good=false;}}catch(...){good=false;}});
 for(auto&t:callers)t.join();auto stats=loadingRuntime->stats();check(good&&stats.pool.peakActive<=2&&stats.pool.peakBytes<=loading::Budget,"four callers share two workers and budget");PackFrameworkStop();
 auto hintStore=out/L"hints-only";check(PackFrameworkStart(hintStore.c_str(),key.c_str(),2,0)&&PackFrameworkReady(),"hints-only start");
 for(auto&e:jobs)loadingRuntime->observe(e.id.c_str(),true);check(PackFrameworkDrain(),"hints-only drained");PackFrameworkStop();check(!persist::load(hintStore,key).valid,"hints never train or save success");
 std::cout<<"{\"passed\":true,\"checks\":"<<checks<<"}\n";return 0;
 }catch(const std::exception&e){std::cerr<<e.what()<<'\n';PackFrameworkStop();return 1;}}
