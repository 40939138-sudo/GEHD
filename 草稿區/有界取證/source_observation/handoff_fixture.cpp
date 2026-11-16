// Self-owned x86 callback/output-handle/list ABI controls. No game or COM objects.
#define NOMINMAX
#include <windows.h>
#include <intrin.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <sstream>
#include <cstddef>
struct Handle {void* entity{};};
struct List {unsigned opaque[2]{};Handle* data{};unsigned count{},capacity{32};};
struct Table {List* list;__declspec(noinline) void Append(Handle* value);};
static_assert(sizeof(void*)==4&&offsetof(List,data)==8&&offsetof(List,count)==12&&offsetof(List,capacity)==16);
struct Plan {int mode;unsigned count;Table* other;};
using Callback=Handle* (__cdecl*)(Handle*,Plan*,void*);
static PVOID volatile callbackReturn;static PVOID volatile appendReturn;static CRITICAL_SECTION guard;
static std::vector<std::string> rows;static volatile LONG errors;
static void Run(const char* name,int mode,unsigned count);
static void* Entity(unsigned n){return reinterpret_cast<void*>(0x10001000u+n*0x100u);}
extern "C" __declspec(noinline) Handle* __cdecl Resolve(Handle* out,Plan* plan,void*) {
    InterlockedExchangePointer(&callbackReturn,_ReturnAddress());
    if(plan->mode==7)Run("inner",0,1);
    out->entity=plan->mode==2?nullptr:Entity(1);
    static Handle other{Entity(1)};
    SetLastError(0x12345678);
    return plan->mode==5?&other:out;
}
extern "C" __declspec(noinline) void __fastcall AppendList(List* list,void*,Handle* value) {
    InterlockedExchangePointer(&appendReturn,_ReturnAddress());
    if(list->count<list->capacity&&list->capacity<=32)list->data[list->count++]=*value;
    SetLastError(0x87654321);
}
__declspec(naked) void Table::Append(Handle*) {
    __asm {
        push ebp
        mov ebp,esp
        mov ecx,[ecx]
        pop ebp
        jmp AppendList
    }
}
extern "C" __declspec(noinline) Handle* __cdecl Alternate(Handle* out,Plan* plan,void* name){return Resolve(out,plan,name);}
extern "C" __declspec(noinline) unsigned __cdecl Reader(Table* table,Plan* plan,void*,Callback callback,void*,void*) {
    for(unsigned i=0;i<plan->count;++i) {
        Handle out{},chosen{};
        if(callback){Handle* result=callback(&out,plan,nullptr);if(GetLastError()!=0x12345678)InterlockedIncrement(&errors);chosen=*result;}
        if(!chosen.entity)chosen.entity=Entity(9);
        if(plan->mode==1)chosen.entity=Entity(i+1);
        if(plan->mode==4)chosen.entity=Entity(2);
        Table* destination=plan->mode==6?plan->other:table;
        destination->Append(&chosen);
        if(GetLastError()!=0x87654321)InterlockedIncrement(&errors);
    }
    return plan->mode==10?0xa5000000:0xa5000001;
}
static std::string Ptr(const void* p){char b[32];sprintf_s(b,"0x%x",reinterpret_cast<unsigned>(p));return b;}
static void Run(const char* name,int mode,unsigned count) {
    Handle values[32]{},otherValues[32]{};List list{},otherList{};list.data=values;otherList.data=otherValues;
    Table table{&list},other{&otherList};Plan plan{mode,count,&other};if(mode==8)list.capacity=33;
    unsigned result=Reader(&table,&plan,nullptr,mode==3?nullptr:mode==9?Alternate:Resolve,nullptr,nullptr);
    bool ok=result==(mode==10?0xa5000000:0xa5000001);auto target=mode==6?&other:&table;
    ok=target->list->count==(mode==8?0:count)&&ok;
    std::ostringstream row;row<<"{\"name\":\""<<name<<"\",\"thread\":"<<GetCurrentThreadId()<<",\"table\":\""<<Ptr(&table)<<"\",\"actualTable\":\""<<Ptr(target)<<"\",\"entities\":[";
    for(unsigned i=0;i<target->list->count;++i){void* expected=Entity(mode==1?i+1:mode==2||mode==3?9:mode==4?2:1);
        ok=target->list->data[i].entity==expected&&ok;if(i)row<<",";row<<"\""<<Ptr(target->list->data[i].entity)<<"\"";}
    row<<"]}";if(!ok)InterlockedIncrement(&errors);EnterCriticalSection(&guard);rows.push_back(row.str());LeaveCriticalSection(&guard);
}
static DWORD WINAPI Worker(void*){Run("thread",0,1);return 0;}
static std::string Exercise(bool invalid) {
    rows.clear();errors=0;
    if(invalid)Run("invalid_list",8,1);
    else {
        Run("shared",0,2);Run("changed_items",1,2);Run("empty",2,1);Run("absent",3,1);
        Run("changed",4,1);Run("bad_output",5,1);Run("wrong_table",6,1);Run("outer",7,1);
        Run("unobserved",9,1);
        Run("partial",10,1);
        HANDLE ts[2]={CreateThread(nullptr,0,Worker,nullptr,0,nullptr),CreateThread(nullptr,0,Worker,nullptr,0,nullptr)};
        if(!ts[0]||!ts[1])InterlockedIncrement(&errors);else WaitForMultipleObjects(2,ts,TRUE,INFINITE);
        for(HANDLE t:ts)if(t)CloseHandle(t);
        Handle h{};Plan p{0,1,nullptr};Resolve(&h,&p,nullptr);Handle storage[1]{};List l{};l.data=storage;l.capacity=1;Table t{&l};t.Append(&h);
    }
    std::ostringstream result;result<<"{\"ok\":"<<(errors==0?"true":"false")<<",\"errors\":"<<errors<<",\"records\":[";
    for(size_t i=0;i<rows.size();++i){if(i)result<<",";result<<rows[i];}result<<"]}";return result.str();
}
static bool Write(const std::string& p,const std::string& s){FILE* f=nullptr;if(fopen_s(&f,p.c_str(),"wb"))return false;bool ok=fwrite(s.data(),1,s.size(),f)==s.size();return fclose(f)==0&&ok;}
static bool Wait(const std::string& p){auto end=GetTickCount64()+45000;while(GetTickCount64()<end){if(GetFileAttributesA(p.c_str())!=INVALID_FILE_ATTRIBUTES)return true;Sleep(10);}return false;}
int main(int argc,char** argv) {
    if(argc!=3)return 2;std::string dir=argv[1];InitializeCriticalSection(&guard);Run("calibrate",0,1);
    auto append=&Table::Append;unsigned appendAddress=0;static_assert(sizeof(append)==4);memcpy(&appendAddress,&append,4);
    unsigned base=reinterpret_cast<unsigned>(GetModuleHandleW(nullptr));char b[512];
    sprintf_s(b,"{\"reader\":%u,\"callback\":%u,\"append\":%u,\"callbackCaller\":%u,\"appendCaller\":%u}",
      reinterpret_cast<unsigned>(&Reader)-base,reinterpret_cast<unsigned>(&Resolve)-base,appendAddress-base,
      reinterpret_cast<unsigned>(callbackReturn)-base,reinterpret_cast<unsigned>(appendReturn)-base);
    if(!Write(dir+"/bindings.json",b)||!Wait(dir+"/go1"))return 3;
    if(!Write(dir+"/first.json",Exercise(std::string(argv[2])=="invalid_list"))||!Wait(dir+"/go2"))return 4;
    if(!Write(dir+"/after.json",Exercise(false)))return 5;
    DeleteCriticalSection(&guard);return errors==0?0:1;
}
