#ifndef GEHD_HANDOFF_TYPES_INCLUDED
#define main HandoffFixtureMain
#include "handoff_fixture.cpp"
#undef main
#endif
#include <map>
// Real x86 ABI calls with synthetic entities. No engine code or COM objects execute.
struct Name {unsigned value{1};};
struct Names {unsigned count{};};
struct Descriptor {unsigned char prefix[16]{};Table* holder{};bool hit{};};
struct Resolver;
struct Request {Handle context;Resolver* owner;const char* name;};
static bool calibrating=true;
static std::map<std::string,unsigned> callSites;
static thread_local const char* stage;
static thread_local int resolverMode;
static PVOID volatile resolverCallbackReturn,resolverReaderAppendReturn;
static std::vector<std::string> resolverRows;
static void Mark(void* caller){if(calibrating)callSites[stage]=reinterpret_cast<unsigned>(caller);}
extern "C" __declspec(noinline) Handle* __fastcall CopyHandle(Handle* out,void*,Handle* source){Mark(_ReturnAddress());*out=*source;return out;}
extern "C" __declspec(noinline) Handle* __fastcall AssignHandle(Handle* out,void*,Handle* source){Mark(_ReturnAddress());*out=*source;return out;}
extern "C" __declspec(noinline) void __fastcall ReleaseHandle(Handle* out,void*){Mark(_ReturnAddress());volatile void* unchanged=out->entity;(void)unchanged;}
extern "C" __declspec(noinline) Handle* __fastcall GetSlot(Table* table,void*,unsigned index){
    Mark(_ReturnAddress());return table->list->data+(resolverMode==5?1:index);
}
extern "C" __declspec(noinline) Handle* __cdecl Holder(Handle* out,Descriptor* descriptor,unsigned index){
    Mark(_ReturnAddress());stage="holderSlot";Handle* slot=GetSlot(descriptor->holder,nullptr,index);
    stage="holderCopy";CopyHandle(out,nullptr,slot);static Handle other{Entity(1)};
    return resolverMode==7?&other:out;
}
extern "C" __declspec(noinline) Handle* __fastcall Match(Resolver*,void*,Handle* out,Descriptor* descriptor,const char*){
    Mark(_ReturnAddress());if(!descriptor->hit){out->entity=nullptr;return out;}
    stage="matchToHolder";Holder(out,descriptor,0);return out;
}
extern "C" __declspec(noinline) Handle* __fastcall AlternateMatch(Resolver* owner,void*,Handle* out,Descriptor* descriptor,const char* name){return Match(owner,nullptr,out,descriptor,name);}
extern "C" __declspec(noinline) Handle* __fastcall Fallback(void*,void*,Handle* out){Mark(_ReturnAddress());out->entity=Entity(9);return out;}
extern "C" __declspec(noinline) void __fastcall AppendName(Names** names,void*,Name*){Mark(_ReturnAddress());(*names)->count++;}
struct Resolver {
    unsigned char prefix[80]{};Table cache{};Names* names{};
    int mode{};Descriptor* descriptors{};Table* other{};
    __declspec(noinline) Handle* Resolve(Handle*,Name*);
};
static_assert(offsetof(Resolver,cache)==80&&offsetof(Resolver,names)==84);
static void Nested();
__declspec(noinline) Handle* Resolver::Resolve(Handle* out,Name* name){
    Mark(_ReturnAddress());resolverMode=mode;Handle local{},temp{};static Handle empty{},otherResult{Entity(1)};
    if(mode==10){Nested();resolverMode=mode;}
    if(mode==16){mode=0;Handle unadmitted{};Resolve(&unadmitted,name);mode=16;resolverMode=mode;}
    if(mode==4){stage="unsupportedReturnCopy";CopyHandle(out,nullptr,&empty);return out;}
    if(mode==3||mode==5||mode==9||mode==15){
        stage="cacheSlot";Handle* slot=GetSlot(mode==9?other:&cache,nullptr,0);
        stage="cacheReturnCopy";CopyHandle(out,nullptr,slot);return out;
    }
    for(unsigned i=0;i<2&&!local.entity;i++){
        stage="previousRecordMatch";
        Handle* found=mode==14?AlternateMatch(this,nullptr,&temp,&descriptors[i],"item"):Match(this,nullptr,&temp,&descriptors[i],"item");
        stage="previousRecordAdopt";AssignHandle(&local,nullptr,found);
    }
    if(!local.entity&&mode==1){
        stage="datasetMatch";Handle* found=Match(this,nullptr,&temp,&descriptors[2],"item");
        stage="datasetAdopt";AssignHandle(&local,nullptr,found);
    }
    if(!local.entity){stage="fallbackCreate";Handle* created=Fallback(this,nullptr,&temp);stage="fallbackAdopt";AssignHandle(&local,nullptr,created);}
    if(mode==6)local.entity=Entity(2);
    cache.Append(&local);if(calibrating)callSites["cacheObjectAppend"]=reinterpret_cast<unsigned>(appendReturn);
    if(mode!=12){stage="cacheNameAppend";AppendName(&names,nullptr,name);}
    if(mode==11){stage="earlyRelease";ReleaseHandle(&local,nullptr);}
    stage="freshReturnCopy";CopyHandle(out,nullptr,&local);
    stage="freshLocalRelease";ReleaseHandle(&local,nullptr);
    return mode==8?&otherResult:out;
}
extern "C" __declspec(noinline) Handle* __fastcall Bridge(Resolver* owner,void*,Handle* out,Name* name){stage="bridgeToResolver";owner->Resolve(out,name);return out;}
extern "C" __declspec(noinline) Handle* __cdecl ResolverCallback(Handle* out,Handle* context,Name* name){
    InterlockedExchangePointer(&resolverCallbackReturn,_ReturnAddress());auto owner=static_cast<Resolver*>(context->entity);
    Bridge(owner,nullptr,out,name);if(owner->mode==13)out->entity=Entity(2);SetLastError(0x12345678);return out;
}
using ResolverCallbackType=Handle* (__cdecl*)(Handle*,Handle*,Name*);
extern "C" __declspec(noinline) unsigned __cdecl ResolverReader(Table* table,Request* request,void*,ResolverCallbackType callback,void*,void*){
    Handle out{};Name name{};Handle* result=callback(&out,&request->context,&name);
    if(GetLastError()!=0x12345678)InterlockedIncrement(&errors);
    Handle value=*result;if(!value.entity)value.entity=Entity(9);table->Append(&value);
    InterlockedExchangePointer(&resolverReaderAppendReturn,appendReturn);
    if(GetLastError()!=0x87654321)InterlockedIncrement(&errors);return 0xa5000001;
}
struct FixtureState {
    Handle cached[32]{},held[2]{{Entity(1)},{Entity(2)}},otherValues[1]{{Entity(2)}};
    List cacheList{},holderList{},otherList{};Table holder{},other{};Names names{};Descriptor descriptors[3]{};Resolver owner{};
    FixtureState(){cacheList.data=cached;holderList.data=held;holderList.count=2;holderList.capacity=2;holder.list=&holderList;
        otherList.data=otherValues;otherList.count=1;otherList.capacity=1;other.list=&otherList;
        for(auto& d:descriptors)d.holder=&holder;owner.cache.list=&cacheList;owner.names=&names;owner.descriptors=descriptors;owner.other=&other;}
};
static void ResolverCase(FixtureState& state,const char* label,int mode,void* expected){
    auto& r=state.owner;r.mode=mode;for(auto& d:state.descriptors)d.hit=false;
    if(mode==1)state.descriptors[2].hit=true;else if(mode!=2)state.descriptors[1].hit=true;
    if(mode==15)state.cacheList.capacity=33;
    Handle values[32]{};List l{};l.data=values;Table table{&l};Request request{{&r},&r,label};
    unsigned result=ResolverReader(&table,&request,nullptr,ResolverCallback,nullptr,nullptr);
    if(result!=0xa5000001||l.count!=1||l.data[0].entity!=expected)InterlockedIncrement(&errors);
    std::ostringstream row;row<<"{\"name\":\""<<label<<"\",\"mode\":"<<mode<<",\"thread\":"<<GetCurrentThreadId()<<",\"table\":\""<<Ptr(&table)
      <<"\",\"resolver\":\""<<Ptr(&r)<<"\",\"entity\":\""<<Ptr(l.data[0].entity)<<"\",\"cacheCount\":"<<state.cacheList.count<<",\"nameCount\":"<<state.names.count<<"}";
    EnterCriticalSection(&guard);resolverRows.push_back(row.str());LeaveCriticalSection(&guard);
}
static void Nested(){FixtureState nested;ResolverCase(nested,"inner",0,Entity(1));}
static DWORD WINAPI ResolverWorker(void*){FixtureState state;ResolverCase(state,"thread",1,Entity(1));return 0;}
static std::string ResolverExercise(bool invalid){
    errors=0;resolverRows.clear();
    if(invalid){FixtureState s;s.cached[0].entity=Entity(1);s.cacheList.count=1;ResolverCase(s,"invalid_list",15,Entity(1));}
    else {
        {FixtureState s;ResolverCase(s,"previous",0,Entity(1));ResolverCase(s,"cache_candidate",3,Entity(1));}
        {FixtureState s;ResolverCase(s,"dataset",1,Entity(1));}
        {FixtureState s;ResolverCase(s,"fallback",2,Entity(9));ResolverCase(s,"cache_fallback",3,Entity(9));}
        {FixtureState s;s.cached[0].entity=Entity(1);s.cacheList.count=1;ResolverCase(s,"first_seen_cache",3,Entity(1));
         s.cached[0].entity=Entity(9);ResolverCase(s,"reused_cache_address",3,Entity(9));}
        {FixtureState s;ResolverCase(s,"unsupported",4,Entity(9));}
        {FixtureState s;s.cached[0].entity=Entity(1);s.cached[1].entity=Entity(2);s.cacheList.count=2;ResolverCase(s,"wrong_slot",5,Entity(2));}
        {FixtureState s;ResolverCase(s,"changed_adopted",6,Entity(2));}
        {FixtureState s;ResolverCase(s,"wrong_holder_output",7,Entity(1));}
        {FixtureState s;ResolverCase(s,"wrong_resolver_output",8,Entity(1));}
        {FixtureState s;ResolverCase(s,"wrong_cache_table",9,Entity(2));}
        {FixtureState s;ResolverCase(s,"outer",10,Entity(1));}
        {FixtureState s;ResolverCase(s,"released_handle",11,Entity(1));}
        {FixtureState s;ResolverCase(s,"missing_name_fill",12,Entity(1));}
        {FixtureState s;ResolverCase(s,"changed_callback",13,Entity(2));}
        {FixtureState s;ResolverCase(s,"wrong_match_caller",14,Entity(1));}
        {FixtureState s;ResolverCase(s,"nested_unadmitted",16,Entity(1));}
        HANDLE threads[2]={CreateThread(nullptr,0,ResolverWorker,nullptr,0,nullptr),CreateThread(nullptr,0,ResolverWorker,nullptr,0,nullptr)};
        if(!threads[0]||!threads[1])InterlockedIncrement(&errors);else WaitForMultipleObjects(2,threads,TRUE,INFINITE);
        for(HANDLE thread:threads)if(thread)CloseHandle(thread);
    }
    std::ostringstream out;out<<"{\"ok\":"<<(errors==0?"true":"false")<<",\"errors\":"<<errors<<",\"records\":[";
    for(size_t i=0;i<resolverRows.size();i++){if(i)out<<",";out<<resolverRows[i];}out<<"]}";return out.str();
}
int main(int argc,char** argv){
    if(argc!=3)return 2;std::string dir=argv[1];InitializeCriticalSection(&guard);
    {FixtureState s;ResolverCase(s,"calibration_previous",0,Entity(1));ResolverCase(s,"calibration_cache",3,Entity(1));}
    {FixtureState s;ResolverCase(s,"calibration_dataset",1,Entity(1));}
    {FixtureState s;ResolverCase(s,"calibration_fallback",2,Entity(9));ResolverCase(s,"calibration_unsupported",4,Entity(9));}
    calibrating=false;
    unsigned base=reinterpret_cast<unsigned>(GetModuleHandleW(nullptr));auto rva=[&](const void* p){return reinterpret_cast<unsigned>(p)-base;};
    auto method=[&](auto fn){unsigned p=0;static_assert(sizeof(fn)==4);memcpy(&p,&fn,4);return p-base;};
    std::ostringstream b;b<<"{\"reader\":"<<rva(reinterpret_cast<void*>(ResolverReader))<<",\"callback\":"<<rva(reinterpret_cast<void*>(ResolverCallback))
      <<",\"append\":"<<method(&Table::Append)<<",\"callbackCaller\":"<<rva(resolverCallbackReturn)<<",\"appendCaller\":"<<rva(resolverReaderAppendReturn)<<",\"resolver\":{";
    b<<"\"resolver\":"<<method(&Resolver::Resolve)<<",\"match\":"<<rva(reinterpret_cast<void*>(Match))<<",\"holder\":"<<rva(reinterpret_cast<void*>(Holder))
      <<",\"copy\":"<<rva(reinterpret_cast<void*>(CopyHandle))<<",\"assign\":"<<rva(reinterpret_cast<void*>(AssignHandle))<<",\"slot\":"<<rva(reinterpret_cast<void*>(GetSlot))
      <<",\"fallback\":"<<rva(reinterpret_cast<void*>(Fallback))<<",\"release\":"<<rva(reinterpret_cast<void*>(ReleaseHandle))<<",\"nameAppend\":"<<rva(reinterpret_cast<void*>(AppendName));
    for(const auto& entry:callSites)b<<",\""<<entry.first<<"\":"<<entry.second-base;b<<"}}";
    if(!Write(dir+"/bindings.json",b.str())||!Wait(dir+"/go1"))return 3;
    if(!Write(dir+"/first.json",ResolverExercise(std::string(argv[2])=="invalid_list"))||!Wait(dir+"/go2"))return 4;
    if(!Write(dir+"/after.json",ResolverExercise(false)))return 5;DeleteCriticalSection(&guard);return errors==0?0:1;
}
