#pragma once
#include "product_files.hpp"
#include <limits>

// Distribution entry only. Loader, controller and per-game installer stay pinned.
namespace sharedmedia {
struct Archive {std::string name,sha;std::uintmax_t bytes;};
struct Pack {const gameprofile::Description* game;std::string release,setupSha;std::vector<Archive> archives;};
inline Pack inspect(const std::filesystem::path&root,bool requireArchives=true){
 product::ordinary(root);
 auto v=product::lines(root/L"game-pack.tsv",32768);
 product::need(v.size()>=6&&v[0]=="GEHD_GAME_PACK_V1","GAME_PACK_FORMAT");
 auto game=gameprofile::find(v[1]);product::need(game!=nullptr,"GAME_PACK_GAME");
 product::need(product::digestText(v[2])&&product::digestText(v[3]),"GAME_PACK_DIGEST");
 product::need(v[4]==std::to_string(v.size()-5)&&v.size()<=70,"GAME_PACK_COUNT");
 Pack p{game,v[2],v[3],{}};std::set<std::string>names;bool core=false;
 for(size_t i=5;i<v.size();++i){
  const auto&a=v[i];auto first=a.find('\t'),last=a.rfind('\t');
  product::need(first!=a.npos&&last!=first,"GAME_PACK_ARCHIVE");
  auto name=a.substr(0,first),size=a.substr(first+1,last-first-1),sha=a.substr(last+1);
  bool data=name.size()==15&&name.rfind("GEHD_Data_",0)==0&&name.substr(12)==".7z"&&name[10]>='0'&&name[10]<='9'&&name[11]>='0'&&name[11]<='9';
  product::need((name=="GEHD_Core.7z"||data)&&names.insert(name).second,"GAME_PACK_ARCHIVE_NAME");
  product::need(!size.empty()&&size.find_first_not_of("0123456789")==size.npos&&product::digestText(sha),"GAME_PACK_ARCHIVE_METADATA");
  auto bytes=std::stoull(size);product::need(bytes>0,"GAME_PACK_ARCHIVE_SIZE");
  if(name=="GEHD_Core.7z")core=true;
  auto file=root/product::wide(name);product::ordinary(file);
  if(requireArchives)product::need(std::filesystem::is_regular_file(file)&&std::filesystem::file_size(file)==bytes,"GAME_PACK_MISSING_OR_SIZE");
  p.archives.push_back({name,sha,bytes});
 }
 product::need(core&&p.archives.size()>1,"GAME_PACK_INCOMPLETE");
 auto setup=root/L"GEHD_Setup.exe";product::ordinary(setup);
 product::need(product::fileHash(setup)==p.setupSha,"GAME_PACK_INSTALLER_HASH");
 // Each unchanged installer pins and verifies the actual archive hashes itself.
 return p;
}
inline void target(const Pack&p,const std::filesystem::path&game){
 product::ordinary(game);auto exe=game/p.game->executable;product::ordinary(exe);
 product::need(std::filesystem::is_regular_file(exe),"PACKAGE_GAME_IDENTITY");
 product::need(product::fileHash(exe)==p.game->executableHash,"GAME_EXE_IDENTITY");
}
inline std::wstring quoted(const std::filesystem::path&p){
 auto s=p.wstring();product::need(s.find_first_of(L"\"\r\n")==s.npos,"INVALID_PATH");return L"\""+s+L"\"";
}
struct Result {DWORD exit=1;std::wstring output;};
inline Result run(const std::filesystem::path&exe,std::wstring arguments,const std::filesystem::path&cwd){
 product::ordinary(exe);std::wstring cmd=quoted(exe)+L" "+arguments;
 SECURITY_ATTRIBUTES sa{sizeof(sa),nullptr,TRUE};HANDLE rp{},wp{};
 product::need(CreatePipe(&rp,&wp,&sa,0)!=0,"CREATE_PIPE");
 product::Handle reader,writer;reader.h=rp;writer.h=wp;SetHandleInformation(rp,HANDLE_FLAG_INHERIT,0);
 STARTUPINFOW si{};si.cb=sizeof(si);si.dwFlags=STARTF_USESTDHANDLES;si.hStdOutput=wp;si.hStdError=wp;si.hStdInput=GetStdHandle(STD_INPUT_HANDLE);
 PROCESS_INFORMATION pi{};BOOL ok=CreateProcessW(exe.c_str(),cmd.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW,nullptr,cwd.c_str(),&si,&pi);
 CloseHandle(writer.h);writer.h=INVALID_HANDLE_VALUE;product::need(ok!=0,"START_PACKAGE_TOOL");
 product::Handle child,thread;child.h=pi.hProcess;thread.h=pi.hThread;
 std::string output;char buf[4096];DWORD got{};
 while(ReadFile(reader.h,buf,sizeof(buf),&got,nullptr)&&got){if(output.size()<1024*1024)output.append(buf,got);}
 WaitForSingleObject(child.h,INFINITE);Result r;GetExitCodeProcess(child.h,&r.exit);
 // Output contains newlines, so do not use the path-only UTF-8 converter.
 if(!output.empty()){int n=MultiByteToWideChar(CP_UTF8,0,output.data(),int(output.size()),nullptr,0);r.output.resize(n);MultiByteToWideChar(CP_UTF8,0,output.data(),int(output.size()),r.output.data(),n);}
 return r;
}
inline std::filesystem::path controller(const std::filesystem::path&manager){
 auto exe=manager/L"bin/pack-control.exe";product::ordinary(exe);
 product::need(product::fileHash(exe)=="8133542F55941F37D35EF2F9A9A70CB422767FC888CBA85909D287DB473946A0","SHARED_CONTROLLER_HASH");return exe;
}
inline Result install(const std::filesystem::path&manager,const std::filesystem::path&game,const std::filesystem::path&root){
 auto pack=inspect(root);target(pack,game);auto ctl=controller(manager);
 product::need(!std::filesystem::equivalent(root,game),"GAME_PACK_SOURCE_IS_TARGET");
 auto logRoot=manager/L"logs";product::ordinary(logRoot);std::filesystem::create_directories(logRoot);
 auto log=logRoot/(L"install-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64())+L".log");
 auto r=run(root/L"GEHD_Setup.exe",L"/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /SP- /DIR="+quoted(game)+L" /LOG="+quoted(log),root);
 r.output+=L"\r\n安裝紀錄："+log.wstring();
 if(r.exit!=0){
  r.output+=L"\r\n安裝未完成，退出碼："+std::to_wstring(r.exit);
  std::ifstream f(log,std::ios::binary|std::ios::ate);
  if(f){auto length=f.tellg();auto take=std::min<std::streamoff>(std::streamoff(length),8192);f.seekg(length-take);std::string tail(size_t(take),'\0');f.read(tail.data(),take);
   int n=MultiByteToWideChar(CP_UTF8,0,tail.data(),int(tail.size()),nullptr,0);std::wstring decoded(n,L'\0');MultiByteToWideChar(CP_UTF8,0,tail.data(),int(tail.size()),decoded.data(),n);r.output+=L"\r\n"+decoded;}
 }
 if(r.exit==0){
  auto status=run(ctl,L"--product status --game "+quoted(game),manager);
  auto expected=product::wide(pack.release);
  if(status.exit!=0||status.output.find(L"\nENABLED\r\n")==status.output.npos||status.output.find(expected)==status.output.npos){r.exit=43;r.output+=L"\r\nINSTALL_STATE_MISMATCH\r\n"+status.output;}
  else r.output+=L"\r\n"+status.output;
 }
 return r;
}
}
