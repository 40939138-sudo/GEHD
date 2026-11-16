#pragma once
// Offline-only bounded tracing. All serialization happens after worker drain.
#include <windows.h>
#include <atomic>
#include <vector>
#include <ostream>
#include <cstring>
#include <streambuf>
namespace preftrace {
// Fixed memory output prevents one trial's log writes overlapping the next IO.
class OutputBuffer:public std::streambuf {
 std::vector<char> data;
public:
 explicit OutputBuffer(size_t bytes):data(bytes){setp(data.data(),data.data()+data.size());}
 size_t size()const{return size_t(pptr()-pbase());}
 size_t capacity()const{return data.size();}
 const char*begin()const{return data.data();}
};
struct Stamp {uint64_t qpc=0,cpu=0,cycles=0;DWORD thread=0;bool cpuOK=false,cyclesOK=false;};
inline Stamp stamp() noexcept {
 Stamp s;FILETIME born{},exit{},kernel{},user{};LARGE_INTEGER q{};
 s.thread=GetCurrentThreadId();s.cpuOK=GetThreadTimes(GetCurrentThread(),&born,&exit,&kernel,&user)!=0;
 s.cpu=(uint64_t(kernel.dwHighDateTime)<<32)+kernel.dwLowDateTime+(uint64_t(user.dwHighDateTime)<<32)+user.dwLowDateTime;
 s.cyclesOK=QueryThreadCycleTime(GetCurrentThread(),&s.cycles)!=0;
 QueryPerformanceCounter(&q);s.qpc=q.QuadPart;return s;
}
struct Event {const char*phase=nullptr;uint64_t job=0,request=0;Stamp begin,end;};
struct Identity {uint64_t job=0,generation=0,bytes=0;char source[65]{};bool predicted=false;};
class Trace {
 std::vector<Event> events;std::vector<Identity> identities;
 std::atomic<size_t> next{0},nextIdentity{0};
public:
 bool enabled=false;
 explicit Trace(size_t capacity=4096,size_t jobs=128):events(capacity),identities(jobs){}
 // Reset only before constructing a pool, dump only after its workers join.
 void reset(bool on){next=0;nextIdentity=0;enabled=on;}
 void add(const Event&e)noexcept{if(!enabled)return;auto i=next.fetch_add(1,std::memory_order_relaxed);if(i<events.size())events[i]=e;}
 void mark(const char*phase,uint64_t job=0,uint64_t request=0)noexcept{if(enabled){auto s=stamp();add({phase,job,request,s,s});}}
 template<class E>void identify(uint64_t id,const E&e,bool predicted)noexcept{
  if(!enabled)return;auto i=nextIdentity.fetch_add(1,std::memory_order_relaxed);if(i>=identities.size())return;
  auto&v=identities[i];v.job=id;v.generation=e.generation;v.bytes=e.bytes;v.predicted=predicted;
  auto n=(std::min)(size_t(64),e.id.size());std::memcpy(v.source,e.id.data(),n);v.source[n]=0;
 }
 bool overflow()const{return next.load()>events.size()||nextIdentity.load()>identities.size();}
 size_t count()const{return next.load();}
 size_t storageBytes()const{return events.size()*sizeof(Event)+identities.size()*sizeof(Identity);}
 void dump(std::ostream&o,const std::string&prefix)const{
  for(size_t i=0;i<(std::min)(nextIdentity.load(),identities.size());++i){const auto&v=identities[i];o<<prefix<<",\"type\":\"job\",\"job\":"<<v.job<<",\"generation\":"<<v.generation<<",\"bytes\":"<<v.bytes<<",\"source\":\""<<v.source<<"\",\"predicted\":"<<(v.predicted?"true":"false")<<"}\n";}
  for(size_t i=0;i<(std::min)(next.load(),events.size());++i){const auto&e=events[i];o<<prefix<<",\"type\":\"span\",\"phase\":\""<<e.phase<<"\",\"job\":"<<e.job<<",\"request\":"<<e.request<<",\"thread\":"<<e.begin.thread<<",\"begin\":"<<e.begin.qpc<<",\"end\":"<<e.end.qpc<<",\"cpu_100ns\":"<<e.end.cpu-e.begin.cpu<<",\"cycles\":"<<e.end.cycles-e.begin.cycles<<",\"cpu_ok\":"<<(e.begin.cpuOK&&e.end.cpuOK?"true":"false")<<",\"cycles_ok\":"<<(e.begin.cyclesOK&&e.end.cyclesOK?"true":"false")<<"}\n";}
 }
};
inline Trace* defaultTrace=nullptr; // Set only on host thread before constructing pools.
struct Span {
 Trace*trace;Event event;
 Span(Trace*t,const char*phase,uint64_t job=0,uint64_t request=0)noexcept:trace(t&&t->enabled?t:nullptr){if(trace){event.phase=phase;event.job=job;event.request=request;event.begin=stamp();}}
 void finish()noexcept{if(trace){event.end=stamp();trace->add(event);trace=nullptr;}}
 ~Span(){finish();}
 Span(const Span&)=delete;Span&operator=(const Span&)=delete;
};
}
