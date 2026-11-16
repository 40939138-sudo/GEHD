// Standalone Windows deployment; no scripting host or policy changes.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>
#include <bcrypt.h>
#include <filesystem>
#include <fstream>
#include <vector>
#include <string>
#include <sstream>
#include <iostream>
#include <stdexcept>
#include "product_control.hpp"
namespace fs=std::filesystem;
constexpr char exeHash[]="586F9A03E75C818165520D82A1BB1289668669D4B708C97857ADE6DF5DE6BFBB";
void require(bool ok,const char*why){if(!ok)throw std::runtime_error(why);}
bool isHash(const std::string&s){return s.size()==64&&s.find_first_not_of("0123456789ABCDEF")==s.npos;}
std::vector<char> read(const fs::path&p,size_t cap){std::ifstream f(p,std::ios::binary|std::ios::ate);require(bool(f),"Cannot read required file");auto n=f.tellg();require(n>=0&&uint64_t(n)<=cap,"File size limit");std::vector<char>b(size_t(n),0);f.seekg(0);f.read(b.data(),std::streamsize(b.size()));require(bool(f),"Short read");return b;}
std::string hash(const fs::path&p){auto b=read(p,128*1024*1024);BCRYPT_ALG_HANDLE alg{};require(BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0,"SHA256 provider");UCHAR digest[32];auto r=BCryptHash(alg,nullptr,0,(PUCHAR)b.data(),ULONG(b.size()),digest,32);BCryptCloseAlgorithmProvider(alg,0);require(r>=0,"SHA256");std::string s;const char*hex="0123456789ABCDEF";for(auto c:digest){s+=hex[c>>4];s+=hex[c&15];}return s;}
std::string utf8(const std::wstring&w){int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,w.data(),int(w.size()),nullptr,0,nullptr,nullptr);require(n>0,"UTF8 path");std::string b(n,0);WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,w.data(),int(w.size()),b.data(),n,nullptr,nullptr);require(b.find_first_of("\r\n") == b.npos,"Newline in path");return b;}
void ordinary(fs::path p){for(;!p.empty();){auto a=GetFileAttributesW(p.c_str());require(a!=INVALID_FILE_ATTRIBUTES&&!(a&FILE_ATTRIBUTE_REPARSE_POINT),"Missing or reparse path");auto next=p.parent_path();if(next==p)break;p=next;}}
std::string identity(const fs::path&p){ordinary(p);HANDLE f=CreateFileW(p.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr);require(f!=INVALID_HANDLE_VALUE,"Open identity");BY_HANDLE_FILE_INFORMATION i{};bool ok=GetFileInformationByHandle(f,&i)!=FALSE;CloseHandle(f);require(ok,"Read identity");std::ostringstream out;out<<i.dwVolumeSerialNumber<<':'<<i.nFileIndexHigh<<':'<<i.nFileIndexLow;return out.str();}
bool running(){HANDLE h=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);require(h!=INVALID_HANDLE_VALUE,"Process inventory unavailable");PROCESSENTRY32W e{};e.dwSize=sizeof(e);bool found=false;if(Process32FirstW(h,&e)){do{if(_wcsicmp(e.szExeFile,L"GE2RB.exe")==0){found=true;break;}}while(Process32NextW(h,&e));}else{CloseHandle(h);throw std::runtime_error("Process enumeration failed");}CloseHandle(h);return found;}
std::vector<std::string> lines(const fs::path&p){auto b=read(p,65536);std::istringstream in(std::string(b.begin(),b.end()));std::string s;std::vector<std::string>v;while(std::getline(in,s)){if(!s.empty()&&s.back()=='\r')s.pop_back();v.push_back(s);}return v;}
void writeNew(const fs::path&p,const std::string&s){HANDLE f=CreateFileW(p.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);require(f!=INVALID_HANDLE_VALUE,"Refusing overwrite");DWORD n{};bool ok=WriteFile(f,s.data(),DWORD(s.size()),&n,nullptr)&&n==s.size()&&FlushFileBuffers(f);CloseHandle(f);require(ok,"Write failed");}
void save(const fs::path&p,const std::vector<std::string>&v){auto pending=p;pending+=L"."+std::to_wstring(GetCurrentProcessId())+L".pending";std::string s;for(auto&x:v)s+=x+'\n';writeNew(pending,s);require(MoveFileExW(pending.c_str(),p.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH),"Publish receipt");}
int wmain(int argc,wchar_t**argv){try{
 if(argc>1&&std::wstring(argv[1])==L"--product")return product::control(argc-1,argv+1);
 require(argc==2||argc==3,"Usage: pack-control.exe enable|disable|status [game-directory]");std::wstring action=argv[1];require(action==L"enable"||action==L"disable"||action==L"status","Unknown action");
 wchar_t self[32768];DWORD n=GetModuleFileNameW(nullptr,self,32768);require(n&&n<32768,"Executable path");fs::path root=fs::path(std::wstring(self,n)).parent_path();fs::path game=argc==3?fs::absolute(argv[2]):fs::path(L"D:\\steam\\steamapps\\common\\GOD EATER 2 Rage Burst");ordinary(root);ordinary(game);
 auto receipt=root/L"deployment-native.txt";std::vector<std::string> record;if(fs::exists(receipt)){record=lines(receipt);require(record.size()==8&&record[0]=="GEHD_NATIVE_DEPLOY_V1"&&record[2]==utf8(game.wstring()),"Receipt scope mismatch");require(record[3].rfind("stage-",0)==0&&record[3].size()<100&&record[3].find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789-")==std::string::npos,"Stage scope mismatch");require(isHash(record[4])&&isHash(record[5]),"Receipt hashes");}
 auto active=running();auto dll=game/L"d3d9.dll",config=game/L"gehd-pack-path.txt";
 if(action==L"status"){std::cout<<"{\"gameRunning\":"<<(active?"true":"false")<<",\"dllPresent\":"<<(fs::exists(dll)?"true":"false")<<",\"configPresent\":"<<(fs::exists(config)?"true":"false")<<",\"receipt\":\""<<(record.empty()?"NONE":record[1])<<"\"}\n";return 0;}
 require(!active,"Close GE2RB normally before enabling or disabling");
 auto owned=[&](const fs::path&dest,const wchar_t*name,size_t index){if(!fs::exists(dest))return false;require(!record.empty(),"Unowned file exists");fs::path stage=root/record[3]/name;require(fs::exists(stage)&&identity(dest)==record[index+2]&&identity(stage)==record[index+2]&&hash(dest)==record[index],"Ownership or content conflict; nothing removed");return true;};
 if(action==L"disable"){
  bool a=owned(dll,L"d3d9.dll",4),b=owned(config,L"gehd-pack-path.txt",5);
  if(a)require(DeleteFileW(dll.c_str()),"Cannot remove owned loader");if(b)require(DeleteFileW(config.c_str()),"Cannot remove owned config");
  if(!record.empty()){record[1]="DISABLED";save(receipt,record);}std::cout<<"Disabled; original game files unchanged.\n";return 0;
 }
 require(hash(game/L"GE2RB.exe")==exeHash,"Game EXE identity changed");auto pins=lines(root/L"pack.pins");require(pins.size()==3&&pins[0]=="GEHD_PACK_PINS_V1"&&isHash(pins[1])&&isHash(pins[2]),"Invalid package pins");require(hash(root/L"loader/d3d9.dll")==pins[1]&&hash(root/L"mapping.tsv")==pins[2],"Package files changed");
 if(!record.empty()&&record[1]=="ENABLED"){require(owned(dll,L"d3d9.dll",4)&&owned(config,L"gehd-pack-path.txt",5),"Partial deployment: disable first");std::cout<<"Already enabled.\n";return 0;}
 require(!fs::exists(dll)&&!fs::exists(config),"Existing files will not be overwritten");auto stageName="stage-"+std::to_string(GetTickCount64())+"-"+std::to_string(GetCurrentProcessId());auto stage=root/stageName;require(fs::create_directory(stage),"Fresh staging directory required");require(CopyFileW((root/L"loader/d3d9.dll").c_str(),(stage/L"d3d9.dll").c_str(),TRUE),"Stage loader");writeNew(stage/L"gehd-pack-path.txt",utf8(root.wstring()));
 record={"GEHD_NATIVE_DEPLOY_V1","PREPARED",utf8(game.wstring()),stageName,hash(stage/L"d3d9.dll"),hash(stage/L"gehd-pack-path.txt"),identity(stage/L"d3d9.dll"),identity(stage/L"gehd-pack-path.txt")};save(receipt,record);
 require(CreateHardLinkW(config.c_str(),(stage/L"gehd-pack-path.txt").c_str(),nullptr),"Publish config");require(CreateHardLinkW(dll.c_str(),(stage/L"d3d9.dll").c_str(),nullptr),"Publish loader");require(owned(dll,L"d3d9.dll",4)&&owned(config,L"gehd-pack-path.txt",5),"Published identity mismatch");record[1]="ENABLED";save(receipt,record);std::cout<<"Texture trial pack enabled.\n";return 0;
 }catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
