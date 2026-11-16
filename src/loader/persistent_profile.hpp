#pragma once
// Offline profile store. Prediction metadata never authorizes texture content.
#include "cpu_prepare.hpp"
#include <sstream>
#include <set>
#include <charconv>
#include <limits>
#include <functional>
namespace persist {
constexpr size_t MaxFile=1024*1024,MaxEdges=4096;
inline bool hex(const std::string&s,size_t n=64){return s.size()==n&&s.find_first_not_of("0123456789ABCDEF")==s.npos;}
inline void check(bool v,const char*m){if(!v)throw std::runtime_error(m);}
inline uint64_t number(const std::string&s){uint64_t n=0;auto r=std::from_chars(s.data(),s.data()+s.size(),n);check(!s.empty()&&r.ec==std::errc{}&&r.ptr==s.data()+s.size(),"NUMBER");return n;}
struct Handle{HANDLE h=INVALID_HANDLE_VALUE;~Handle(){if(h!=INVALID_HANDLE_VALUE)CloseHandle(h);}Handle()=default;Handle(const Handle&)=delete;};
inline std::wstring native(const std::filesystem::path&p){return L"\\\\?\\"+std::filesystem::absolute(p).lexically_normal().wstring();}
inline void ordinary(std::filesystem::path p){p=std::filesystem::absolute(p).lexically_normal();for(;;){DWORD a=GetFileAttributesW(native(p).c_str());if(a!=INVALID_FILE_ATTRIBUTES)check(!(a&FILE_ATTRIBUTE_REPARSE_POINT),"REPARSE");else check(GetLastError()==ERROR_FILE_NOT_FOUND||GetLastError()==ERROR_PATH_NOT_FOUND,"PATH_ACCESS");auto parent=p.parent_path();if(parent==p||parent.empty())break;p=parent;}}
struct Edge{std::string context,next;uint32_t support=0;};
struct Model{
 std::string binding;uint64_t generation=0,lastSession=0;std::vector<Edge> edges;
 std::string semantic()const{std::ostringstream o;for(auto&e:edges)o<<e.context<<' '<<e.next<<' '<<e.support<<'\n';return o.str();}
 std::string fingerprint()const{auto s=semantic();return cpuprep::fullHash(s.data(),s.size());}
 std::string predict(const std::string&context)const{const Edge*best=nullptr;for(auto&e:edges)if(e.context==context&&e.support>=2&&(!best||e.support>best->support||(e.support==best->support&&e.next<best->next)))best=&e;return best?best->next:"";}
};
// Only observed, fully verified actual requests call observe. Support increases
// once per transition per monotonically numbered session, never on speculation.
class Learning{
 Model&model;std::vector<unsigned char>seen;std::string previous,context;bool enabled;
public:
 Learning(Model&m,uint64_t session):model(m),seen(MaxEdges,0),enabled(session>m.lastSession){if(enabled)model.lastSession=session;}
 bool active()const{return enabled;}
 void breakSequence(){previous.clear();context.clear();}
 bool observe(const std::string&id){check(hex(id),"OBSERVED_ID");if(!enabled)return false;bool changed=false;
  if(!context.empty()){size_t i=0;for(;i<model.edges.size();++i)if(model.edges[i].context==context&&model.edges[i].next==id)break;
   if(i<MaxEdges){if(i==model.edges.size())model.edges.push_back({context,id,0});if(!seen[i]){if(model.edges[i].support<std::numeric_limits<uint32_t>::max()){++model.edges[i].support;changed=true;}seen[i]=1;}}
  }context=previous+id;previous=id;return changed;
 }
};
inline std::string encode(const Model&m){check(hex(m.binding)&&m.generation&&m.edges.size()<=MaxEdges,"MODEL");std::ostringstream o;o<<"GEHD_PREFETCH_V1\n"<<m.binding<<'\n'<<m.generation<<'\n'<<m.lastSession<<'\n'<<m.edges.size()<<'\n'<<m.semantic();auto body=o.str();auto out=body+cpuprep::fullHash(body.data(),body.size())+'\n';check(out.size()<=MaxFile,"PROFILE_LIMIT");return out;}
inline Model decode(const std::string&raw,const std::string&binding){check(raw.size()>=65&&raw.size()<=MaxFile&&raw.back()=='\n',"PROFILE_LENGTH");auto body=raw.substr(0,raw.size()-65);auto hash=raw.substr(raw.size()-65,64);check(hex(hash)&&cpuprep::fullHash(body.data(),body.size())==hash,"CHECKSUM");
 std::istringstream in(body);std::string line;auto get=[&]{check(bool(std::getline(in,line)),"TRUNCATED");return line;};check(get()=="GEHD_PREFETCH_V1","SCHEMA");Model m;m.binding=get();check(hex(m.binding)&&m.binding==binding,"VERSION");m.generation=number(get());m.lastSession=number(get());check(m.generation>0&&m.lastSession>0,"EPOCH");auto count=number(get());check(count<=MaxEdges,"EDGE_LIMIT");std::set<std::pair<std::string,std::string>>seen;
 for(size_t i=0;i<count;++i){std::istringstream row(get());Edge e;std::string n,tail;check(bool(row>>e.context>>e.next>>n)&&!(row>>tail),"ROW");check((hex(e.context)||hex(e.context,128))&&hex(e.next),"KEY");auto v=number(n);check(v>0&&v<=std::numeric_limits<uint32_t>::max(),"COUNT");e.support=uint32_t(v);check(seen.emplace(e.context,e.next).second,"DUPLICATE");m.edges.push_back(std::move(e));}check(!std::getline(in,line),"EXTRA_ROWS");return m;
}
struct Snapshot{Model model;bool valid=false;int slot=-1;std::string status="empty";std::string slotStatus[2];};
inline std::string read(const std::filesystem::path&p){ordinary(p);Handle f;f.h=CreateFileW(native(p).c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);check(f.h!=INVALID_HANDLE_VALUE,"MISSING_OR_UNREADABLE");LARGE_INTEGER n{};check(GetFileSizeEx(f.h,&n)&&n.QuadPart>0&&n.QuadPart<=MaxFile,"PROFILE_LENGTH");std::string s(size_t(n.QuadPart),0);DWORD got=0;check(ReadFile(f.h,s.data(),DWORD(s.size()),&got,nullptr)&&got==s.size(),"PROFILE_READ");return s;}
inline Snapshot load(const std::filesystem::path&root,const std::string&binding){check(hex(binding),"BINDING");Snapshot out;out.model.binding=binding;
 for(int i=0;i<2;++i){try{auto m=decode(read(root/(L"profile-"+std::to_wstring(i)+L".bin")),binding);out.slotStatus[i]="valid";if(!out.valid||m.generation>out.model.generation){out.model=std::move(m);out.valid=true;out.slot=i;}}
  catch(const std::exception&e){out.slotStatus[i]=e.what();}}
 out.status=out.valid?"loaded":"disabled";return out;
}
inline void checkpoint(const std::string&requested,const char*at){if(requested==at)ExitProcess(77);}
struct SaveTrace {uint64_t ticks[7]{};};
struct SavePhase {SaveTrace*trace;unsigned index;uint64_t begin=cpuprep::ticks();~SavePhase(){if(trace)trace->ticks[index]+=cpuprep::ticks()-begin;}};
inline std::string save(const std::filesystem::path&root,Model model,uint64_t expected,const std::string&fault="",const std::function<bool(const char*)>&proceed={},SaveTrace*trace=nullptr){
 auto boundary=[&](const char*name){if(proceed&&!proceed(name))throw std::runtime_error("deferred");};
 try{boundary("before_lock");SavePhase phase{trace,0};ordinary(root);std::filesystem::create_directories(native(root));ordinary(root/L"writer.lock");Handle lock;lock.h=CreateFileW(native(root/L"writer.lock").c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);if(lock.h==INVALID_HANDLE_VALUE)return "busy_or_unwritable";
  if(trace)trace->ticks[0]+=cpuprep::ticks()-phase.begin;phase.trace=nullptr;
  boundary("before_load");SavePhase preparation{trace,1};
  auto old=load(root,model.binding);if(old.model.generation!=expected)return "conflict";check(expected<std::numeric_limits<uint64_t>::max(),"GENERATION_OVERFLOW");model.generation=expected+1;auto bytes=encode(model);
  if(trace)trace->ticks[1]+=cpuprep::ticks()-preparation.begin;preparation.trace=nullptr;auto pending=root/L"profile.pending",target=root/(L"profile-"+std::to_wstring(old.slot==0?1:0)+L".bin");ordinary(pending);ordinary(target);checkpoint(fault,"before_write");
  boundary("before_write");
  {Handle f;{SavePhase t{trace,2};f.h=CreateFileW(native(pending).c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);check(f.h!=INVALID_HANDLE_VALUE,"CREATE_PENDING");DWORD wrote=0,half=DWORD(bytes.size()/2);check(WriteFile(f.h,bytes.data(),half,&wrote,nullptr)&&wrote==half,"WRITE_PREFIX");checkpoint(fault,"partial_write");DWORD rest=DWORD(bytes.size())-half;check(WriteFile(f.h,bytes.data()+half,rest,&wrote,nullptr)&&wrote==rest,"WRITE_SUFFIX");}
   boundary("before_flush");{SavePhase t{trace,3};check(FlushFileBuffers(f.h),"FLUSH");}}
  boundary("before_readback");{SavePhase t{trace,4};check(read(pending)==bytes,"READBACK");}checkpoint(fault,"after_flush");
  boundary("before_publish");{SavePhase t{trace,5};check(MoveFileExW(native(pending).c_str(),native(target).c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH),"PUBLISH");}checkpoint(fault,"after_publish");return "saved";
 }catch(const std::exception&e){return e.what();}
}
}
