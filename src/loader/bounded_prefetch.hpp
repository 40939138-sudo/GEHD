#pragma once
#include "cpu_prepare.hpp"
#include "prefetch_trace.hpp"
#include <map>
#include <functional>
#include <chrono>
#include <cassert>
namespace bounded {
using Entry=cpuprep::Entry;
enum class State {Queued,Reading,Ready,Failed,Cancelled};
struct Job {
 Entry entry;cpuprep::Slot buffer;State state=State::Queued;
 std::wstring key;uint64_t order{},expires{};size_t reserved{};unsigned refs=0;
 bool demand=false,predicted=false,discard=false,chargeSpec=false,activeSpec=false;
 unsigned hashes=0;
};
struct Stats {
 uint64_t requests=0,predictions=0,readyHits=0,lateHits=0,misses=0,merges=0;
 uint64_t reads=0,hashes=0,preparedBytes=0,wasteBytes=0,rejectedPredictions=0;
 uint64_t readTicks=0,hashTicks=0;
 size_t peakBytes=0,peakSpecBytes=0;unsigned peakActive=0,peakSpecActive=0;
};
class Pool {
 size_t budget,specLimit,maxJobs;unsigned workerCount;uint64_t ttl,generation=1,sequence=0;
 size_t used=0,specUsed=0;unsigned active=0,activeSpec=0;bool stopping=false;
 std::mutex mutex;std::condition_variable cv;std::vector<std::thread> threads;
 std::map<std::wstring,std::shared_ptr<Job>> jobs;Stats stats;
 preftrace::Trace*trace=preftrace::defaultTrace;
 void mark(const char*phase,uint64_t job){if(trace)trace->mark(phase,job);}
 std::function<void(const Entry&)> beforeRead; // offline deterministic gate, empty during timing
 static std::wstring key(const Entry&e){
  std::wstring s=e.path.wstring()+L"|"+std::to_wstring(e.generation);
  for(const auto&x:{e.id,e.digest})s+=L"|"+std::wstring(x.begin(),x.end());
  for(UINT x:{e.sourceBytes,e.bytes,e.w,e.h,e.m,e.format,e.scale})s+=L"|"+std::to_wstring(x);return s;
 }
 void freeBuffer(Job&j){
  if(j.reserved){if(j.predicted&&!j.demand&&j.hashes)stats.wasteBytes+=j.entry.bytes;
   {preftrace::Span timer(trace,"free_buffer",j.order);j.buffer.data.reset();}used-=j.reserved;if(j.chargeSpec)specUsed-=j.reserved;j.reserved=0;j.chargeSpec=false;}
 }
 void prune(){
  auto now=cpuprep::ticks();
  for(auto it=jobs.begin();it!=jobs.end();){auto&j=*it->second;
   if(!j.demand&&(j.expires<=now||j.discard))j.discard=true;
   // A demand Ticket can still reference an invalidated queued job. Wake it
   // even though no worker will ever select this discarded job.
   if(j.discard&&j.state==State::Queued)j.state=State::Cancelled;
   if(j.discard&&j.state!=State::Reading&&!j.refs){freeBuffer(j);j.state=State::Cancelled;it=jobs.erase(it);}else ++it;
  }
 }
 std::shared_ptr<Job> pick(){
  // Inline preparation occupies a worker slot too. Two worker threads must
  // not start a third preparation while a caller owns the inline slot.
  if(active>=std::max(1u,workerCount))return {};
  std::shared_ptr<Job> demand,pred;
  for(const auto&item:jobs){auto j=item.second;if(j->state!=State::Queued||j->discard)continue;
   if(j->entry.bytes>budget-used)continue;
   if(j->demand){if(!demand||j->order<demand->order)demand=j;}
   else if(!activeSpec&&j->entry.bytes<=specLimit-specUsed&&(!pred||j->order<pred->order))pred=j;
  }
  return demand?demand:pred;
 }
 void reserve(Job&j){mark("dequeue",j.order);j.state=State::Reading;j.reserved=j.entry.bytes;used+=j.reserved;
  if(!j.demand){j.chargeSpec=j.activeSpec=true;specUsed+=j.reserved;++activeSpec;}
  ++active;stats.peakBytes=std::max(stats.peakBytes,used);stats.peakSpecBytes=std::max(stats.peakSpecBytes,specUsed);stats.peakActive=std::max(stats.peakActive,active);stats.peakSpecActive=std::max(stats.peakSpecActive,activeSpec);
  assert(used<=budget&&specUsed<=specLimit&&active<=std::max(1u,workerCount)&&activeSpec<=1);
 }
 void prepare(const std::shared_ptr<Job>&j) noexcept {
  preftrace::Span preparation(trace,"prepare_total",j->order);
  auto&s=j->buffer;const auto&e=j->entry;HANDLE file=INVALID_HANDLE_VALUE;s.start=cpuprep::ticks();
  try{
   if(beforeRead)beforeRead(e);
   preftrace::Span opening(trace,"open_size",j->order);
   auto path=L"\\\\?\\"+std::filesystem::absolute(e.path).lexically_normal().wstring();
   file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
   if(file==INVALID_HANDLE_VALUE)throw std::runtime_error("OPEN");LARGE_INTEGER n{};
   if(!GetFileSizeEx(file,&n)||n.QuadPart!=e.bytes)throw std::runtime_error("SIZE");
   opening.finish();
   {preftrace::Span timer(trace,"allocate_zero",j->order);s.data=std::make_unique<unsigned char[]>(e.bytes);}
   DWORD got=0;preftrace::Span reading(trace,"read_file",j->order);
   if(!ReadFile(file,s.data.get(),e.bytes,&got,nullptr)||got!=e.bytes)throw std::runtime_error("READ");
   reading.finish();
   {preftrace::Span timer(trace,"close_file",j->order);CloseHandle(file);file=INVALID_HANDLE_VALUE;}s.readEnd=cpuprep::ticks();
   ++j->hashes;std::string digest;{preftrace::Span timer(trace,"replacement_sha",j->order);digest=cpuprep::fullHash(s.data.get(),e.bytes);}s.hashEnd=cpuprep::ticks();
   if(digest.empty()||digest!=e.digest)throw std::runtime_error("SHA");
   {preftrace::Span timer(trace,"dds_validate",j->order);if(!cpuprep::validDDS(e,s.data.get(),e.bytes))throw std::runtime_error("DDS");}s.valid=true;
  }catch(const std::exception&e){s.error=e.what();s.data.reset();}catch(...){s.error="EXCEPTION";s.data.reset();}
  if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);s.end=cpuprep::ticks();
  {preftrace::Span acquiring(trace,"publish_lock",j->order);std::lock_guard lock(mutex);acquiring.finish();preftrace::Span held(trace,"publish_locked",j->order);--active;if(j->activeSpec){--activeSpec;j->activeSpec=false;}
   ++stats.reads;stats.hashes+=j->hashes;if(s.readEnd){stats.preparedBytes+=e.bytes;stats.readTicks+=s.readEnd-s.start;}if(s.hashEnd)stats.hashTicks+=s.hashEnd-s.readEnd;
   if(j->discard||e.generation!=generation||stopping){s.valid=false;j->discard=true;j->state=State::Cancelled;}
   else j->state=s.valid?State::Ready:State::Failed;
   if(j->state==State::Cancelled||j->state==State::Failed)freeBuffer(*j);
   prune();mark("ready_publish",j->order);}
  cv.notify_all();mark("ready_notify",j->order);
 }
 void worker(){for(;;){std::shared_ptr<Job> j;
  {preftrace::Span acquiring(trace,"worker_lock");std::unique_lock lock(mutex);acquiring.finish();prune();while(!stopping&&!(j=pick())){{preftrace::Span parked(trace,"worker_park");cv.wait_for(lock,std::chrono::milliseconds(50));}prune();}if(stopping)return;reserve(*j);}
  prepare(j);
 }}
 void promote(Job&j){
  j.demand=true;j.discard=false;
  if(j.chargeSpec){specUsed-=j.reserved;j.chargeSpec=false;}
  if(j.activeSpec){--activeSpec;j.activeSpec=false;}
 }
 void release(const std::shared_ptr<Job>&j){preftrace::Span acquiring(trace,"release_lock",j->order);std::lock_guard lock(mutex);acquiring.finish();preftrace::Span held(trace,"release_locked",j->order);assert(j->refs);--j->refs;
  if(!j->refs&&j->demand){j->discard=true;if(j->state!=State::Reading){freeBuffer(*j);auto it=jobs.find(j->key);if(it!=jobs.end()&&it->second==j)jobs.erase(it);}}
  cv.notify_all();
 }
public:
 class Ticket {
  Pool*pool{};std::shared_ptr<Job> job;cpuprep::Slot rejected;friend class Pool;
  Ticket(Pool*p,std::shared_ptr<Job>j):pool(p),job(std::move(j)){}
 public:
  Ticket(const Ticket&)=delete;Ticket&operator=(const Ticket&)=delete;
  Ticket(Ticket&&o)noexcept:pool(std::exchange(o.pool,nullptr)),job(std::move(o.job)){}
  ~Ticket(){if(pool)pool->release(job);}
  uint64_t id()const{return job->order;}
  const cpuprep::Slot& wait(){
   preftrace::Span acquiring(pool->trace,"wait_lock",job->order);std::unique_lock lock(pool->mutex);acquiring.finish();
   if(!pool->workerCount&&job->state==State::Queued){if(job->entry.bytes>pool->budget-pool->used)throw std::runtime_error("sync quota");pool->reserve(*job);lock.unlock();pool->prepare(job);lock.lock();}
   {preftrace::Span blocked(pool->trace,"condition_wait",job->order);pool->cv.wait(lock,[&]{return job->state==State::Ready||job->state==State::Failed||job->state==State::Cancelled;});}pool->mark("wait_return",job->order);
   // Do not mutate a shared buffer that another lease may already be reading.
   if(job->entry.generation!=pool->generation){rejected.error="GENERATION";return rejected;}return job->buffer;
  }
 };
 // Pool must outlive every Ticket. It owns no device or caller source pointer.
 Pool(size_t bytes,size_t speculative,unsigned count,uint64_t ttlTicks,size_t slots=64,std::function<void(const Entry&)> gate={}):budget(bytes),specLimit(speculative),maxJobs(slots),workerCount(count),ttl(ttlTicks),beforeRead(std::move(gate)){
  if(speculative>bytes||count>2||!slots)throw std::runtime_error("pool limits");
  try{for(unsigned i=0;i<count;++i)threads.emplace_back([this]{worker();});}catch(...){stop();throw;}
 }
 ~Pool(){stop();}Pool(const Pool&)=delete;Pool&operator=(const Pool&)=delete;
 Ticket request(const Entry&e,bool inlineIfIdle=false){preftrace::Span acquiring(trace,"request_lock");std::unique_lock lock(mutex);acquiring.finish();preftrace::Span held(trace,"request_locked");prune();if(stopping)throw std::runtime_error("stopped");++stats.requests;auto k=key(e);
  // Validate epoch before an existing Ready job can be promoted or leased.
  if(e.generation!=generation){++stats.misses;auto j=std::make_shared<Job>();j->entry=e;j->key=k;j->demand=true;j->refs=1;j->state=State::Failed;j->buffer.error="GENERATION";return Ticket(this,j);}
  auto it=jobs.find(k);
  if(it!=jobs.end()&&it->second->state!=State::Cancelled){auto j=it->second;
   if(j->predicted&&!j->demand){if(j->state==State::Ready)++stats.readyHits;else ++stats.lateHits;}else ++stats.merges;
   promote(*j);++j->refs;cv.notify_all();return Ticket(this,j);
  }
  ++stats.misses;
  // Cancel low-priority reservations when a new real request needs room.
  if(e.bytes>budget-used||jobs.size()>=maxJobs){for(auto&item:jobs)if(!item.second->demand)item.second->discard=true;prune();}
  auto j=std::make_shared<Job>();j->entry=e;j->key=k;j->order=++sequence;j->demand=true;j->refs=1;
  if(e.generation!=generation||e.bytes>budget){j->state=State::Failed;j->buffer.error="GENERATION_OR_BUDGET";return Ticket(this,j);}
  if(jobs.size()>=maxJobs)throw std::runtime_error("demand metadata capacity");
  jobs[k]=j;if(trace)trace->identify(j->order,e,false);mark("enqueue",j->order);
  // Small demand can avoid a worker handoff when the pool is idle. It uses
  // the same reservation, full-SHA preparation, publication and lease path.
  // Older runnable demands keep priority; active preparation remains <=2.
  if(inlineIfIdle&&!active&&e.bytes<=budget-used&&pick()==j){reserve(*j);held.finish();lock.unlock();prepare(j);}else cv.notify_all();
  return Ticket(this,j);
 }
 bool predict(const Entry&e){preftrace::Span acquiring(trace,"predict_lock");std::lock_guard lock(mutex);acquiring.finish();preftrace::Span held(trace,"predict_locked");prune();++stats.predictions;auto k=key(e);
  if(stopping||e.generation!=generation||!workerCount||e.bytes>specLimit||jobs.size()>=maxJobs){++stats.rejectedPredictions;return false;}
  if(jobs.count(k))return true;
  auto j=std::make_shared<Job>();j->entry=e;j->key=k;j->order=++sequence;j->predicted=true;j->expires=cpuprep::ticks()+ttl;jobs[k]=j;if(trace)trace->identify(j->order,e,true);mark("enqueue",j->order);cv.notify_all();return true;
 }
 void trim(const std::vector<Entry>&keep){preftrace::Span acquiring(trace,"trim_lock");std::lock_guard lock(mutex);acquiring.finish();preftrace::Span held(trace,"trim_locked");std::vector<std::wstring>keys;for(const auto&e:keep)keys.push_back(key(e));
  for(auto&item:jobs)if(!item.second->demand&&std::find(keys.begin(),keys.end(),item.first)==keys.end())item.second->discard=true;prune();cv.notify_all();
 }
 void setGeneration(uint64_t value){std::lock_guard lock(mutex);generation=value;for(auto&item:jobs)if(item.second->entry.generation!=value)item.second->discard=true;prune();cv.notify_all();}
 void awaitPrediction(const Entry&e){std::unique_lock lock(mutex);auto k=key(e);if(!cv.wait_for(lock,std::chrono::seconds(10),[&]{auto it=jobs.find(k);return it!=jobs.end()&&it->second->state!=State::Queued&&it->second->state!=State::Reading;}))throw std::runtime_error("prediction timeout");}
 void stop() noexcept {
  {std::lock_guard lock(mutex);stopping=true;for(auto&item:jobs){item.second->discard=true;if(item.second->state==State::Queued){item.second->state=State::Cancelled;item.second->buffer.valid=false;}}}cv.notify_all();
  for(auto&t:threads)if(t.joinable())t.join();threads.clear();
  {std::lock_guard lock(mutex);for(auto&item:jobs){assert(!item.second->refs);freeBuffer(*item.second);}jobs.clear();}
 }
 Stats snapshot(){std::lock_guard lock(mutex);return stats;}
 size_t currentBytes(){std::lock_guard lock(mutex);return used;}
};
}
