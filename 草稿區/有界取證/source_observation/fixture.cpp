// Source-owned x86 ABI fixture. No game files, graphics device, or COM calls.
#define NOMINMAX
#include <windows.h>
#include <intrin.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <cstddef>
struct FileTexture {
    unsigned char prefix[0x44]{};
    void* texture{};
    void* surface{};
    __declspec(noinline) unsigned Load(unsigned char* data, unsigned size, unsigned mode);
};
static_assert(offsetof(FileTexture,texture)==0x44 && offsetof(FileTexture,surface)==0x48 && sizeof(void*)==4);
static void* callReturn;
static volatile LONG calls=0, argumentErrors=0, lastErrorErrors=0;
static FileTexture nestedOwner;
static unsigned char nestedInput[4]={1,2,3,4};
extern "C" __declspec(noinline) long __stdcall Api(void* device,void* input,unsigned size,
 unsigned width,unsigned height,unsigned levels,unsigned usage,unsigned format,unsigned pool,
 unsigned filter,unsigned mipFilter,unsigned color,void* info,void* palette,void** output) {
    callReturn=_ReturnAddress(); InterlockedIncrement(&calls);
    if(device!=reinterpret_cast<void*>(0x1234)||width!=0xfffffffe||height!=0xfffffffe||levels!=0xfffffffd||
       usage!=0||format!=0||pool!=1||filter!=1||mipFilter!=0xffffffff||color!=0||!info||palette)
        InterlockedIncrement(&argumentErrors);
    auto data=static_cast<unsigned char*>(input);
    Sleep(12);
    if(size==0 || data[0]==4) {SetLastError(0x76543210);return static_cast<long>(0x80004005);}
    if(data[0]==3) nestedOwner.Load(nestedInput,sizeof(nestedInput),0);
    *output=data[0]==5?nullptr:reinterpret_cast<void*>(0x10001000u+data[0]*0x100u);
    if(data[0]==6) data[1]^=0x55;
    SetLastError(0x76543210);return 0;
}
__declspec(noinline) long Alternate(unsigned char* data,unsigned size,void** output) {
    unsigned info[8]{};
    return Api(reinterpret_cast<void*>(0x1234),data,size,0xfffffffe,0xfffffffe,0xfffffffd,0,0,1,1,0xffffffff,0,info,nullptr,output);
}
__declspec(noinline) unsigned FileTexture::Load(unsigned char* data,unsigned size,unsigned mode) {
    if(!size)return 0xa5000000;
    unsigned info[8]{};void* other=reinterpret_cast<void*>(0x5555);
    void** output=mode==1?&other:&texture;
    long hr;
    if(mode==2)hr=Alternate(data,size,output);
    else hr=Api(reinterpret_cast<void*>(0x1234),data,size,0xfffffffe,0xfffffffe,0xfffffffd,0,0,1,1,0xffffffff,0,info,nullptr,output);
    if(GetLastError()!=0x76543210)InterlockedIncrement(&lastErrorErrors);
    if(hr<0)return 0xa5000000;
    surface=*output?reinterpret_cast<void*>(reinterpret_cast<unsigned>(*output)+4):nullptr;
    return 0xa5000001;
}
struct Work {FileTexture owner;unsigned char bytes[4]{8,2,3,4};unsigned result{};};
static HANDLE gate;
DWORD WINAPI Worker(void* p) {auto w=static_cast<Work*>(p);WaitForSingleObject(gate,INFINITE);w->result=w->owner.Load(w->bytes,4,0);return 0;}
bool Exercise() {
    calls=0;argumentErrors=0;lastErrorErrors=0;
    FileTexture owner{},other{};unsigned char a[4]={1,2,3,4},same[4]={1,2,3,4},different[4]={2,2,3,4};
    unsigned char nest[4]={3,2,3,4},fail[4]={4,2,3,4},nullResult[4]={5,2,3,4},change[4]={6,2,3,4};
    bool ok=owner.Load(a,4,0)==0xa5000001 && owner.texture==reinterpret_cast<void*>(0x10001100);
    ok=other.Load(same,4,0)==0xa5000001 && other.texture==owner.texture && ok;
    // Reuse the same address for another logical loading interval, without promising an object generation.
    owner.texture=reinterpret_cast<void*>(0x8888);owner.surface=nullptr;
    ok=owner.Load(different,4,0)==0xa5000001&&owner.texture==reinterpret_cast<void*>(0x10001200)&&ok;
    ok=owner.Load(nest,4,0)==0xa5000001&&owner.texture==reinterpret_cast<void*>(0x10001300)&&ok;
    owner.texture=reinterpret_cast<void*>(0x9999);
    ok=owner.Load(fail,4,0)==0xa5000000&&owner.texture==reinterpret_cast<void*>(0x9999)&&ok;
    ok=owner.Load(nullResult,4,0)==0xa5000001&&owner.texture==nullptr&&ok;
    ok=owner.Load(change,4,0)==0xa5000001&&change[1]==(2^0x55)&&ok;
    ok=owner.Load(a,4,1)==0xa5000001&&ok; // Valid API output, wrong owner slot.
    ok=owner.Load(a,4,2)==0xa5000001&&ok; // Different call site inside the same loader.
    unsigned info[8]{};
    ok=Api(reinterpret_cast<void*>(0x1234),reinterpret_cast<void*>(1),0,0xfffffffe,0xfffffffe,0xfffffffd,
      0,0,1,1,0xffffffff,0,info,nullptr,reinterpret_cast<void**>(1))<0&&ok; // Reject before reading invalid pointers.
    ok=owner.Load(a,0,0)==0xa5000000&&ok;
    Work x{},y{};gate=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    HANDLE threads[2]={CreateThread(nullptr,0,Worker,&x,0,nullptr),CreateThread(nullptr,0,Worker,&y,0,nullptr)};
    if(!gate||!threads[0]||!threads[1])return false;
    SetEvent(gate);WaitForMultipleObjects(2,threads,TRUE,INFINITE);
    CloseHandle(threads[0]);CloseHandle(threads[1]);CloseHandle(gate);
    return ok&&x.result==0xa5000001&&y.result==0xa5000001&&calls==13&&argumentErrors==0&&lastErrorErrors==0;
}
bool Write(const std::string& p,const std::string& s) {
    FILE* f=nullptr;if(fopen_s(&f,p.c_str(),"wb"))return false;
    bool ok=fwrite(s.data(),1,s.size(),f)==s.size();return fclose(f)==0&&ok;
}
bool Wait(const std::string& p) {
    auto end=GetTickCount64()+45000;
    while(GetTickCount64()<end){if(GetFileAttributesA(p.c_str())!=INVALID_FILE_ATTRIBUTES)return true;Sleep(10);}return false;
}
int main(int argc,char** argv) {
    if(argc!=2)return 2;std::string dir=argv[1];
    FileTexture calibration{};unsigned char input[4]={1,2,3,4};calibration.Load(input,4,0);
    auto fn=&FileTexture::Load;unsigned address=0;static_assert(sizeof(fn)==4);memcpy(&address,&fn,4);
    unsigned base=reinterpret_cast<unsigned>(GetModuleHandleW(nullptr));char json[512];
    sprintf_s(json,"{\"loader\":%u,\"api\":%u,\"caller\":%u}\n",address-base,
      reinterpret_cast<unsigned>(&Api)-base,reinterpret_cast<unsigned>(callReturn)-base);
    if(!Write(dir+"/bindings.json",json)||!Wait(dir+"/go1"))return 3;
    bool first=Exercise();
    sprintf_s(json,"{\"ok\":%s,\"apiCalls\":%ld,\"argumentErrors\":%ld,\"lastErrorErrors\":%ld}\n",first?"true":"false",calls,argumentErrors,lastErrorErrors);
    if(!Write(dir+"/first.json",json)||!Wait(dir+"/go2"))return 4;
    bool after=Exercise();Write(dir+"/done.json",after?"{\"ok\":true}\n":"{\"ok\":false}\n");
    return first&&after?0:5;
}
