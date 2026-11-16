// Shares the already qualified callback/table fixture; only material ABI controls are added.
#ifndef GEHD_HANDOFF_TYPES_INCLUDED
#define main HandoffFixtureMain
#include "handoff_fixture.cpp"
#undef main
#endif
struct Stream {std::vector<unsigned short> indices;size_t at{};int mode{};};
static thread_local int materialMode;
static PVOID volatile readReturn,lookupReturn;
extern "C" __declspec(noinline) unsigned __cdecl ReadIndex(unsigned short* out,Stream* stream,Table*) {
    InterlockedExchangePointer(&readReturn,_ReturnAddress());materialMode=stream->mode;
    if(stream->at>=stream->indices.size())return 0;
    *out=stream->indices[stream->at++];return stream->mode==1?1:2;
}
extern "C" __declspec(noinline) Handle* __fastcall Lookup(Table* table,void*,unsigned index) {
    InterlockedExchangePointer(&lookupReturn,_ReturnAddress());
    if(materialMode==2)index=0;
    return index<table->list->count?table->list->data+index:nullptr;
}
struct Material {
    unsigned char prefix[0x64]{};List* user{};unsigned char gap[0x1c]{};List* fixed{};
    __declspec(noinline) void Fixed(Stream*,Table*);
    __declspec(noinline) void User(Stream*,Table*);
};
static_assert(offsetof(Material,user)==0x64&&offsetof(Material,fixed)==0x84);
__declspec(noinline) void Material::Fixed(Stream* stream,Table* table) {
    fixed->count=0;Handle otherItems[32]{};List otherList{};otherList.data=otherItems;Table other{&otherList};
    otherItems[0].entity=Entity(2);otherList.count=1;
    for(size_t i=0;i<stream->indices.size();i++) {
        unsigned short index=0xffff;ReadIndex(&index,stream,table);Handle h{Entity(9)};
        if(index!=0xffff){Handle* found=Lookup(stream->mode==4?&other:table,nullptr,index);if(found)h=*found;}
        if(stream->mode==3)h.entity=Entity(2);
        AppendList(stream->mode==5?&otherList:fixed,nullptr,&h);
    }
}
__declspec(noinline) void Material::User(Stream* stream,Table* table) {
    Handle temporaryItems[32]{};List temporary{};temporary.data=temporaryItems;Table wrapper{&temporary};
    for(size_t i=0;i<stream->indices.size();i++) {
        unsigned short index=0xffff;ReadIndex(&index,stream,table);Handle h{Entity(9)};
        if(index!=0xffff){Handle* found=Lookup(table,nullptr,index);if(found)h=*found;}
        wrapper.Append(&h);
    }
    user->count=temporary.count;for(unsigned i=0;i<temporary.count;i++)user->data[i]=temporary.data[i];
    if(stream->mode==6&&user->count)user->data[0].entity=Entity(9);
}
static std::vector<std::string> materialRows;
static void MaterialCase(const char* name,bool user,Table* table,const std::vector<unsigned short>& indices,int mode) {
    Handle items[32]{};List destination{};destination.data=items;if(mode==7)destination.capacity=33;
    Material material{};material.fixed=&destination;material.user=&destination;Stream stream{indices,0,mode};
    if(user)material.User(&stream,table);else material.Fixed(&stream,table);
    std::vector<void*> expected;
    if(mode!=5&&mode!=7)for(auto index:indices)expected.push_back(index==0xffff?Entity(9):mode==2?Entity(1):mode==3||mode==4?Entity(2):table->list->data[index].entity);
    if(mode==6&&!expected.empty())expected[0]=Entity(9);
    bool ok=destination.count==expected.size()&&stream.at==indices.size()&&GetLastError()==0x87654321;
    for(size_t i=0;i<expected.size();i++)ok=destination.data[i].entity==expected[i]&&ok;
    if(!ok)InterlockedIncrement(&errors);
    std::ostringstream row;row<<"{\"name\":\""<<name<<"\",\"kind\":\""<<(user?"user":"fixed")<<"\",\"thread\":"<<GetCurrentThreadId()
      <<",\"owner\":\""<<Ptr(&material)<<"\",\"table\":\""<<Ptr(table)<<"\",\"list\":\""<<Ptr(&destination)<<"\",\"entities\":[";
    for(unsigned i=0;i<destination.count;i++){if(i)row<<",";row<<"\""<<Ptr(destination.data[i].entity)<<"\"";}row<<"]}";
    EnterCriticalSection(&guard);materialRows.push_back(row.str());LeaveCriticalSection(&guard);
}
static DWORD WINAPI MaterialWorker(void*) {
    Handle values[2]{{Entity(1)},{Entity(2)}};List l{};l.data=values;l.count=2;l.capacity=2;Table t{&l};
    MaterialCase("thread",true,&t,{1,0},0);return 0;
}
static std::string MaterialExercise(bool invalid) {
    errors=0;materialRows.clear();Handle items[32]{};List list{};list.data=items;Table table{&list};Plan plan{1,2,nullptr};
    if(Reader(&table,&plan,nullptr,Resolve,nullptr,nullptr)!=0xa5000001)InterlockedIncrement(&errors);
    if(invalid)MaterialCase("invalid_list",false,&table,{0},7);
    else {
        MaterialCase("fixed",false,&table,{1,0,1},0);MaterialCase("user",true,&table,{0,1,0},0);
        MaterialCase("sentinel",false,&table,{0xffff,1},0);MaterialCase("short",false,&table,{0},1);
        MaterialCase("wrong_slot",false,&table,{1},2);MaterialCase("changed",false,&table,{0},3);
        MaterialCase("wrong_table",false,&table,{0},4);MaterialCase("wrong_destination",false,&table,{0},5);
        MaterialCase("changed_final",true,&table,{0},6);
        HANDLE ts[2]={CreateThread(nullptr,0,MaterialWorker,nullptr,0,nullptr),CreateThread(nullptr,0,MaterialWorker,nullptr,0,nullptr)};
        if(!ts[0]||!ts[1])InterlockedIncrement(&errors);else WaitForMultipleObjects(2,ts,TRUE,INFINITE);for(HANDLE t:ts)if(t)CloseHandle(t);
    }
    std::ostringstream out;out<<"{\"ok\":"<<(errors==0?"true":"false")<<",\"records\":[";
    for(size_t i=0;i<materialRows.size();i++){if(i)out<<",";out<<materialRows[i];}out<<"]}";return out.str();
}
int main(int argc,char** argv) {
    if(argc!=3)return 2;std::string dir=argv[1];InitializeCriticalSection(&guard);Run("calibration",0,1);
    unsigned base=reinterpret_cast<unsigned>(GetModuleHandleW(nullptr));
    auto rva=[&](const void* p){return reinterpret_cast<unsigned>(p)-base;};
    auto method=[&](auto fn){unsigned p=0;static_assert(sizeof(fn)==4);memcpy(&p,&fn,4);return p-base;};
    unsigned cbCaller=rva(callbackReturn),apCaller=rva(appendReturn);
    Handle data[2]{{Entity(1)},{Entity(2)}};List l{};l.data=data;l.count=2;l.capacity=2;Table t{&l};
    MaterialCase("fixed_calibration",false,&t,{0},0);unsigned fr=rva(readReturn),fl=rva(lookupReturn),fa=rva(appendReturn);
    MaterialCase("user_calibration",true,&t,{0},0);unsigned ur=rva(readReturn),ul=rva(lookupReturn),ua=rva(appendReturn);
    std::ostringstream b;b<<"{\"reader\":"<<rva(reinterpret_cast<void*>(Reader))<<",\"callback\":"<<rva(reinterpret_cast<void*>(Resolve))<<",\"append\":"<<method(&Table::Append)
      <<",\"callbackCaller\":"<<cbCaller<<",\"appendCaller\":"<<apCaller<<",\"materials\":{\"fixed\":"<<method(&Material::Fixed)<<",\"user\":"<<method(&Material::User)
      <<",\"readIndex\":"<<rva(reinterpret_cast<void*>(ReadIndex))<<",\"lookup\":"<<rva(reinterpret_cast<void*>(Lookup))<<",\"append\":"<<rva(reinterpret_cast<void*>(AppendList))
      <<",\"fixedReadCaller\":"<<fr<<",\"fixedLookupCaller\":"<<fl<<",\"fixedAppendCaller\":"<<fa<<",\"userReadCaller\":"<<ur<<",\"userLookupCaller\":"<<ul<<",\"userAppendCaller\":"<<ua<<"}}";
    if(!Write(dir+"/bindings.json",b.str())||!Wait(dir+"/go1"))return 3;
    if(!Write(dir+"/first.json",MaterialExercise(std::string(argv[2])=="invalid_list"))||!Wait(dir+"/go2"))return 4;
    if(!Write(dir+"/after.json",MaterialExercise(false)))return 5;DeleteCriticalSection(&guard);return errors==0?0:1;
}
