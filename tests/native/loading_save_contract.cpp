#define GEHD_PACK_FIXTURE
#include "pack_loader.cpp"
#include <iostream>
int wmain(int argc,wchar_t**argv){try{
 if(argc!=3)throw std::runtime_error("corpus output");unsigned checks=0;
 auto check=[&](bool b,const char*m){if(!b)throw std::runtime_error(m);++checks;};
 std::filesystem::path corpus=argv[1],out=argv[2];check(!std::filesystem::exists(out),"fresh output");std::filesystem::create_directories(out);
 check(PackFixtureStart(corpus.c_str()),"fixture");std::vector<cpuprep::Entry> jobs;
 for(auto&[id,e]:entries)jobs.push_back({id,e.replacement,corpus/(e.scale==4?L"4x":L"2x")/(id+".dds"),e.sourceSize,e.size,e.w,e.h,e.m,e.format,e.scale,1});check(jobs.size()>2,"three jobs");
 std::atomic<unsigned> entered{};std::atomic<bool> release{};
 bounded::Pool pool(loading::Budget,loading::SpecBudget,2,loading::Runtime::frequency()*2,64,[&](const auto&){++entered;while(!release.load())Sleep(1);});
 std::atomic<bool> good{true};std::thread inlineCaller([&]{auto t=pool.request(jobs[0],true);good=t.wait().valid;});
 auto until=GetTickCount64()+5000;while(!entered&&GetTickCount64()<until)Sleep(1);
 {auto b=pool.request(jobs[1]);auto c=pool.request(jobs[2]);while(entered<2&&GetTickCount64()<until)Sleep(1);Sleep(100);auto active=entered.load();release=true;inlineCaller.join();check(active==2,"inline occupies one of two preparation slots");check(b.wait().valid&&c.wait().valid&&good,"all queued work eventually completes");}
 check(pool.snapshot().peakActive<=2,"bounded peak after inline overlap");pool.stop();
 auto key=hash("save contract",13);persist::Model model;model.binding=key;persist::Learning learn(model,1);
 for(unsigned i=0;i<3;++i)for(auto&e:jobs)learn.observe(e.id);
 auto before=model.fingerprint();bool changed=false;for(auto&e:jobs)changed|=learn.observe(e.id);
 check(!changed&&model.fingerprint()==before,"repeated session transitions are not dirty");
 for(auto boundary:{"before_lock","before_load","before_write","before_flush","before_readback","before_publish"}){
  auto store=out/boundary;check(persist::save(store,model,0)=="saved","initial snapshot");auto old=persist::load(store,key);auto next=old.model;next.lastSession++;
  persist::SaveTrace trace;check(persist::save(store,next,old.model.generation,"",[&](const char*at){return strcmp(at,boundary)!=0;},&trace)=="deferred","deferred at safe boundary");
  auto still=persist::load(store,key);check(still.valid&&still.model.generation==old.model.generation&&still.model.lastSession==1,"defer preserves committed snapshot");
  check(persist::save(store,next,old.model.generation)=="saved","defer retry commits");check(persist::load(store,key).model.lastSession==2,"retry preserves session epoch");
 }
 // Real runtime: demand begins while a queued writer is delayed, dirty data
 // must survive deferral and flush on explicit offline shutdown.
 loading::Config config;config.binding=key;config.store=out/L"runtime";config.writerDelayMs=400;
 startLoading(config);check(PackFrameworkReady(),"runtime ready");for(auto&e:jobs)loadingRuntime->observe(e.id.c_str());check(PackFrameworkDrain(),"runtime drained");
 until=GetTickCount64()+10000;while(!loadingRuntime->stats().writerRunning&&GetTickCount64()<until)Sleep(1);check(loadingRuntime->stats().writerRunning,"writer running distinguished from queued");
 loadingRuntime->demandBegin();Sleep(600);auto state=loadingRuntime->stats();check(state.deferred==1&&state.saveFailures==0,"demand deferral is not persistence failure");loadingRuntime->demandEnd();PackFrameworkStop();
 check(persist::load(config.store,key).valid,"dirty survives deferred shutdown");
 config.persist=false;config.writerDelayMs=0;startLoading(config);check(PackFrameworkReady()&&loadingRuntime->stats().loaded,"writer-disabled arm loads same learned model");for(auto&e:jobs)loadingRuntime->observe(e.id.c_str());PackFrameworkDrain();loadingRuntime->stop();check(loadingRuntime->stats().saves==0,"writer-disabled arm does not save");PackFrameworkStop();
 std::cout<<"{\"passed\":true,\"checks\":"<<checks<<"}\n";return 0;
 }catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
