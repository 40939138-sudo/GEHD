#pragma once
// Offline-only bounded CPU preparation. No D3D calls and no game hooks.
#include <windows.h>
#include <filesystem>
#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <stdexcept>
#include <algorithm>
#include <cstdint>
namespace cpuprep {
struct Entry {
 std::string id,digest;std::filesystem::path path;
 UINT sourceBytes{},bytes{},w{},h{},m{},format{},scale{};uint64_t generation=1;
};
// These adapters reuse the unchanged loader's full SHA and DDS validator.
std::string fullHash(const void*,size_t);
bool validDDS(const Entry&,const void*,size_t);
uint64_t ticks();
enum class State {Pending,Preparing,Ready,Borrowed,Released};
struct Slot {
 std::unique_ptr<unsigned char[]> data;State state=State::Pending;
 size_t reserved{};bool valid=false;std::string error;
 uint64_t start{},readEnd{},hashEnd{},end{};
};
class Queue {
 const std::vector<Entry>& jobs;const size_t budget;const unsigned workers;
 std::vector<Slot> slots;std::mutex gate;std::condition_variable changed;
 std::vector<std::thread> threads;size_t next=0,published=0,used=0,peak=0;
 unsigned active=0,peakActive=0;bool stopping=false;
 void prepare(size_t i) noexcept {
  auto& s=slots[i];const auto& e=jobs[i];s.start=ticks();
  HANDLE file=INVALID_HANDLE_VALUE;
  try {
   if(!s.reserved)throw std::runtime_error("BUDGET");
   auto path=L"\\\\?\\"+std::filesystem::absolute(e.path).lexically_normal().wstring();
   file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
   if(file==INVALID_HANDLE_VALUE)throw std::runtime_error("OPEN");
   LARGE_INTEGER n{};
   if(!GetFileSizeEx(file,&n)||n.QuadPart!=e.bytes)throw std::runtime_error("SIZE");
   s.data=std::make_unique<unsigned char[]>(e.bytes);DWORD got=0;
   if(!ReadFile(file,s.data.get(),e.bytes,&got,nullptr)||got!=e.bytes)throw std::runtime_error("READ");
   CloseHandle(file);file=INVALID_HANDLE_VALUE;s.readEnd=ticks();
   auto digest=fullHash(s.data.get(),e.bytes);s.hashEnd=ticks();
   if(digest.empty()||digest!=e.digest)throw std::runtime_error("SHA");
   if(!validDDS(e,s.data.get(),e.bytes))throw std::runtime_error("DDS");
   s.valid=true;
  }catch(const std::exception& e){s.error=e.what();s.data.reset();}
   catch(...){s.error="EXCEPTION";s.data.reset();}
  if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);
  s.end=ticks();
  {std::lock_guard lock(gate);s.state=State::Ready;--active;}
  changed.notify_all();
 }
 bool fits(size_t i)const{return jobs[i].bytes>budget||jobs[i].bytes<=budget-used;}
 void reserve(size_t i){auto& s=slots[i];s.state=State::Preparing;
  if(jobs[i].bytes<=budget){s.reserved=jobs[i].bytes;used+=s.reserved;peak=std::max(peak,used);}
  ++active;peakActive=std::max(peakActive,active);
 }
 void worker(){for(;;){size_t i;
  {std::unique_lock lock(gate);changed.wait(lock,[&]{return stopping||(next<published&&fits(next));});
   if(stopping)return;i=next++;reserve(i);}
  prepare(i);
 }}
public:
 Queue(const std::vector<Entry>&j,size_t byteLimit,unsigned count):jobs(j),budget(byteLimit),workers(count),slots(j.size()){
  try{for(unsigned i=0;i<count;++i)threads.emplace_back([this]{worker();});}
  catch(...){cancel();throw;}
 }
 Queue(const Queue&)=delete;Queue&operator=(const Queue&)=delete;
 ~Queue(){cancel();}
 void publish(size_t n){std::lock_guard lock(gate);if(n>jobs.size()||n<published)throw std::runtime_error("publish order");published=n;changed.notify_all();}
 const Slot& take(size_t i){
  if(!workers){
   {std::lock_guard lock(gate);if(stopping||i!=next||i>=published||!fits(i))throw std::runtime_error("sync admission");++next;reserve(i);}
   prepare(i);
  }
  std::unique_lock lock(gate);changed.wait(lock,[&]{return stopping||slots.at(i).state==State::Ready;});
  if(stopping)throw std::runtime_error("cancelled");slots[i].state=State::Borrowed;return slots[i];
 }
 void release(size_t i){std::lock_guard lock(gate);auto& s=slots.at(i);if(s.state!=State::Borrowed)throw std::runtime_error("release state");
  // Free memory BEFORE making its reservation available to another worker.
  s.data.reset();used-=s.reserved;s.reserved=0;s.state=State::Released;changed.notify_all();
 }
 void cancel() noexcept {
  {std::lock_guard lock(gate);stopping=true;}changed.notify_all();
  for(auto& t:threads)if(t.joinable())t.join();threads.clear();
  for(auto& s:slots){s.data.reset();s.reserved=0;}used=0;
 }
 size_t peakBytes(){std::lock_guard lock(gate);return peak;}
 size_t currentBytes(){std::lock_guard lock(gate);return used;}
 unsigned concurrent(){std::lock_guard lock(gate);return peakActive;}
 const std::vector<Slot>& completed()const{return slots;} // caller must drain or cancel first
};
}
