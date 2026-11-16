#pragma once
// CPU-only loading framework. D3D and caller-owned source memory never enter it.
#include "bounded_prefetch.hpp"
#include "persistent_profile.hpp"
#include <array>
#include <unordered_map>
#include <optional>
namespace loading {
constexpr size_t Budget=256u*1024*1024,SpecBudget=64u*1024*1024,EventCapacity=256;
struct Config {
 std::filesystem::path store;
 std::string binding;
 std::function<std::string()> bind; // background-only installed-variant identity
 bool learn=true;
 bool persist=true;
#ifdef GEHD_LOADING_TEST
 unsigned writerDelayMs=0;
 unsigned coordinatorDelayMs=0;
#endif
};
struct Counters {
 uint64_t submitted{},processed{},dropped{},breaks{},predictions{},saves{},saveFailures{},saveTicks{},maxSaveTicks{},generation{},deferred{},savePhases[7]{};
 bool ready{},loaded{},saving{},writerRunning{},backgroundPriority{};bounded::Stats pool;
};
class Runtime {
 struct Event {std::array<char,65> id{};uint64_t epoch{};bool hint=false;};
 Config config;std::unordered_map<std::string,cpuprep::Entry> entries;
 std::mutex queueGate,writeGate;std::condition_variable changed,writeChanged;
 std::array<Event,EventCapacity> queue{};size_t head=0,count=0;
 bool closing=false,writerClosing=false;std::optional<persist::Model> pending;uint64_t pendingActivity{};
 std::thread coordinator,writer;
 std::atomic<uint64_t> epoch{},submitted{},processed{},dropped{},breaks{},predictions{},saves{},saveFailures{},saveTicks{},maxSaveTicks{},generation{};
 std::atomic<bool> ready{},loaded{},saving{},backgroundPriority{};std::atomic<int> ack{};
 std::atomic<bool> writerRunning{},finalSave{};std::atomic<unsigned> activeDemands{};
 std::atomic<uint64_t> activity{},lastActivity{},deferred{},savePhases[7]{};
 void writeLoop() noexcept {
  // Windows background mode lowers CPU, IO and memory priority. Never hold
  // the observation/pool locks while doing low-priority filesystem work.
  backgroundPriority=SetThreadPriority(GetCurrentThread(),THREAD_MODE_BACKGROUND_BEGIN)!=FALSE;
  for(;;){std::optional<persist::Model> model;uint64_t stamp;
   {std::unique_lock lock(writeGate);writeChanged.wait(lock,[&]{return writerClosing||pending.has_value();});if(!pending&&writerClosing)return;model=std::move(pending);stamp=pendingActivity;pending.reset();}
   auto begin=cpuprep::ticks();std::string result;persist::SaveTrace trace;writerRunning=true;
   try{
#ifdef GEHD_LOADING_TEST
    if(config.writerDelayMs)Sleep(config.writerDelayMs);
#endif
    result=backgroundPriority?persist::save(config.store,*model,model->generation,"",[&](const char*){return finalSave.load()||(!activeDemands.load()&&activity.load()==stamp&&GetTickCount64()-lastActivity.load()>=5000);},&trace):"BACKGROUND_PRIORITY_UNAVAILABLE";
   }catch(...){result="exception";}
   auto elapsed=cpuprep::ticks()-begin;saveTicks+=elapsed;maxSaveTicks.store(std::max(maxSaveTicks.load(),elapsed));
   for(unsigned i=0;i<7;++i)savePhases[i]+=trace.ticks[i];writerRunning=false;
   if(result=="saved"){++saves;generation=model->generation+1;ack=1;}else if(result=="deferred"){++deferred;ack=2;}else{++saveFailures;ack=-1;}
   saving=false;changed.notify_one();
  }
 }
 void submitSave(const persist::Model&model){
  // Only coordinator submits, with at most one outstanding full snapshot.
  std::lock_guard lock(writeGate);pendingActivity=activity.load();pending=model;saving=true;writeChanged.notify_one();
 }
 void coordinate() noexcept {
  try{
   persist::Model model;model.binding=config.binding;bool writable=!config.store.empty()&&config.persist;
   try{if(config.bind)model.binding=config.bind();persist::check(persist::hex(model.binding),"BINDING");
    if(!config.store.empty()){auto s=persist::load(config.store,model.binding);model=std::move(s.model);loaded=s.valid;}
   }catch(...){writable=false;model={};model.binding=config.binding;}
   // Unknown keys can never produce a file path or authorize a replacement.
   model.edges.erase(std::remove_if(model.edges.begin(),model.edges.end(),[&](const persist::Edge&e){return !entries.count(e.next)||!entries.count(e.context.substr(0,64))||(e.context.size()==128&&!entries.count(e.context.substr(64)));}),model.edges.end());
   generation=model.generation;
   if(model.lastSession==UINT64_MAX)throw std::runtime_error("session overflow");
   persist::Learning learner(model,model.lastSession+1);std::string previous,context;
   uint64_t seenEpoch=epoch.load(),revision=0,sentRevision=0,acceptedRevision=0,lastSave=GetTickCount64(),lastEvent=lastSave;bool failed=false,sessionObserved=false;
   ready=true;changed.notify_all();
   for(;;){
#ifdef GEHD_LOADING_TEST
    if(config.coordinatorDelayMs)Sleep(config.coordinatorDelayMs);
#endif
    int result=ack.exchange(0);if(result==1){model.generation=generation.load();acceptedRevision=sentRevision;}else if(result==2)lastSave=0;else if(result<0)failed=true;
    std::array<Event,EventCapacity> batch;size_t n=0;bool stopping;
    {std::unique_lock lock(queueGate);changed.wait_for(lock,std::chrono::milliseconds(100),[&]{return closing||count||ack.load()!=0;});stopping=closing;
     while(count){batch[n++]=queue[head];head=(head+1)%EventCapacity;--count;}}
    for(size_t i=0;i<n;++i){const auto&e=batch[i];std::string id=e.id.data();
     if(e.epoch!=seenEpoch||id.empty()){learner.breakSequence();previous.clear();context.clear();pool.trim({});++breaks;seenEpoch=e.epoch;}
     if(!id.empty()&&entries.count(id)){
      auto next=model.predict(previous+id);
      // A validated current DDS may trigger a hint before its D3D creation,
      // overlapping next CPU preparation with caller-thread driver work.
      // Only a subsequent successful real resource reinforces the model.
      if(!e.hint){bool updated=learner.observe(id);context=previous+id;previous=id;if(updated||!sessionObserved)++revision;sessionObserved=true;}
      auto found=entries.find(next);if(found!=entries.end()){pool.trim({found->second});if(pool.predict(found->second))++predictions;}else pool.trim({});
     }++processed;lastEvent=GetTickCount64();
    }
    auto now=GetTickCount64();bool dirty=revision!=acceptedRevision;
    // Save only after five seconds without texture events. Subsequent saves
    // are at least thirty seconds apart; sustained loading takes precedence.
    if(writable&&!failed&&dirty&&!saving&&ack.load()==0&&(stopping||(!activeDemands.load()&&now-std::max(lastEvent,lastActivity.load())>=5000&&(!sentRevision||now-lastSave>=30000)))){finalSave=stopping;submitSave(model);sentRevision=revision;lastSave=now;}
    if(stopping){if(saving||ack.load()!=0){std::unique_lock lock(queueGate);changed.wait_for(lock,std::chrono::milliseconds(50),[&]{return ack.load()!=0;});continue;}break;}
   }
  }catch(...){++saveFailures;ready=true;changed.notify_all();}
 }
public:
 bounded::Pool pool;
 Runtime(std::unordered_map<std::string,cpuprep::Entry> catalog,Config settings):config(std::move(settings)),entries(std::move(catalog)),pool(Budget,SpecBudget,2,frequency()*2){
  try{if(config.learn){writer=std::thread([this]{writeLoop();});coordinator=std::thread([this]{coordinate();});}else ready=true;}
  catch(...){ {std::lock_guard lock(writeGate);writerClosing=true;}writeChanged.notify_all();if(writer.joinable())writer.join();throw;}
 }
 Runtime(const Runtime&)=delete;
 static uint64_t frequency(){LARGE_INTEGER q{};QueryPerformanceFrequency(&q);return q.QuadPart;}
 ~Runtime(){stop();}
 void demandBegin() noexcept {++activeDemands;++activity;lastActivity=GetTickCount64();}
 void demandEnd() noexcept {lastActivity=GetTickCount64();++activity;--activeDemands;}
 void observe(const char*id,bool hint=false) noexcept {
  if(!config.learn)return;
  // No allocation, profile IO, writer wait, or unbounded queue on the hook.
  std::unique_lock lock(queueGate,std::try_to_lock);
  if(!lock.owns_lock()||count==EventCapacity){++dropped;++epoch;return;}
  if(closing)return;auto&e=queue[(head+count)%EventCapacity];e.id.fill(0);if(id)std::memcpy(e.id.data(),id,64);e.epoch=epoch.load();e.hint=hint;++count;++submitted;changed.notify_one();
 }
 bool waitReady(unsigned ms=10000){std::unique_lock lock(queueGate);return changed.wait_for(lock,std::chrono::milliseconds(ms),[&]{return ready.load();});}
 bool waitProcessed(uint64_t target,unsigned ms=10000){auto end=GetTickCount64()+ms;while(processed<target&&GetTickCount64()<end)Sleep(1);return processed>=target;}
 void stop() noexcept {
  // Caller must first quiesce hook invocations and release all Pool Tickets.
  {std::lock_guard lock(queueGate);closing=true;}changed.notify_all();if(coordinator.joinable())coordinator.join();
  {std::lock_guard lock(writeGate);writerClosing=true;}writeChanged.notify_all();if(writer.joinable())writer.join();pool.stop();
 }
 Counters stats(){Counters s;s.submitted=submitted;s.processed=processed;s.dropped=dropped;s.breaks=breaks;s.predictions=predictions;s.saves=saves;s.saveFailures=saveFailures;s.saveTicks=saveTicks;s.maxSaveTicks=maxSaveTicks;s.generation=generation;s.ready=ready;s.loaded=loaded;s.saving=saving;s.writerRunning=writerRunning;s.deferred=deferred;for(unsigned i=0;i<7;++i)s.savePhases[i]=savePhases[i];s.backgroundPriority=backgroundPriority;s.pool=pool.snapshot();return s;}
};
// Keeps verified bytes alive through D3DX. Releasing a Ticket must not alter
// the native HRESULT/GetLastError contract on any return/fallback path.
struct Lease {
 std::optional<bounded::Pool::Ticket> ticket;
 Runtime* runtime{};const char*success{};
 explicit Lease(Runtime*r):runtime(r){auto saved=GetLastError();if(runtime)runtime->demandBegin();SetLastError(saved);}
 ~Lease(){auto saved=GetLastError();ticket.reset();if(runtime){runtime->observe(success);runtime->demandEnd();}SetLastError(saved);}
};
}
