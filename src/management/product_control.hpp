#pragma once
#include "product_files.hpp"
#include <tlhelp32.h>
#include <iostream>
#include <atomic>
#include "loading_options.hpp"
namespace product {
inline void selectGame(const fs::path&g,bool allowChanged=false){
 const gameprofile::Description*found=nullptr;
 if(allowChanged&&fs::exists(g/L"GEHD/state/installation.tsv")){
  auto record=lines(g/L"GEHD/state/installation.tsv",8192);
  need(record.size()==11&&record[0]=="GEHD_INSTALL_V1"&&_wcsicmp(product::absolute(wide(record[1])).c_str(),g.c_str())==0&&digestText(record[4]),"INSTALLATION_RECORD");
  auto cache=g/L"GEHD/state"/(L"release-"+wide(record[4]));auto identity=lines(cache/L"pack.identity",1024);
  need(identity.size()==5&&identity[0]=="GEHD_PRODUCT_V1"&&identity[4]==record[4]&&fileHash(cache/L"release.files")==record[4],"CLEANUP_GAME_BINDING");
  found=gameprofile::find(identity[1]);need(found&&identity[2]==found->executableHash,"CLEANUP_GAME_BINDING");
 }else for(auto candidate:gameprofile::registry)if(fs::exists(g/candidate->executable)){need(!found,"AMBIGUOUS_GAME_DIRECTORY");need(fileHash(g/candidate->executable)==candidate->executableHash,"GAME_EXE_IDENTITY");found=candidate;}
 need(found!=nullptr,"GAME_EXE_IDENTITY");targetGame=found;GameHash=found->executableHash;
}
inline bool tokenText(const std::string&s){return s.size()==32&&s.find_first_not_of("0123456789ABCDEF")==s.npos;}
inline std::string randomId(){UCHAR b[16];need(BCryptGenRandom(nullptr,b,16,BCRYPT_USE_SYSTEM_PREFERRED_RNG)>=0,"RANDOM_ID");std::string s;const char*h="0123456789ABCDEF";for(auto c:b){s+=h[c>>4];s+=h[c&15];}return s;}
inline std::string fileId(const fs::path&p){ordinary(p);Handle f;f.h=CreateFileW(p.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS,nullptr);need(f.h!=INVALID_HANDLE_VALUE,"IDENTITY_OPEN");BY_HANDLE_FILE_INFORMATION i{};need(GetFileInformationByHandle(f.h,&i)!=FALSE,"IDENTITY_READ");return std::to_string(i.dwVolumeSerialNumber)+":"+std::to_string(i.nFileIndexHigh)+":"+std::to_string(i.nFileIndexLow);}
inline bool running(){Handle h;h.h=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);need(h.h!=INVALID_HANDLE_VALUE,"PROCESS_INVENTORY");PROCESSENTRY32W e{};e.dwSize=sizeof(e);need(Process32FirstW(h.h,&e)!=FALSE,"PROCESS_INVENTORY");do{if(_wcsicmp(e.szExeFile,targetGame?targetGame->executable:gameprofile::ge2rb.executable)==0)return true;}while(Process32NextW(h.h,&e));return false;}
inline bool samePath(const fs::path&a,const fs::path&b){return _wcsicmp(product::absolute(a).c_str(),product::absolute(b).c_str())==0;}
inline bool nested(const fs::path&a,const fs::path&b){auto x=product::absolute(a).wstring()+L"\\",y=product::absolute(b).wstring()+L"\\";return y.size()>=x.size()&&_wcsnicmp(x.c_str(),y.c_str(),x.size())==0;}
inline void newText(const fs::path&p,const std::string&s){ordinary(p);Handle f;f.h=CreateFileW(p.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);need(f.h!=INVALID_HANDLE_VALUE,"REFUSE_OVERWRITE");DWORD n{};need(WriteFile(f.h,s.data(),DWORD(s.size()),&n,nullptr)&&n==s.size()&&FlushFileBuffers(f.h),"WRITE_FAILED");}
inline void saveLines(const fs::path&p,const std::vector<std::string>&v){std::string s;for(auto&x:v){need(x.find_first_of("\r\n")==x.npos,"RECORD_LINE");s+=x+'\n';}auto temp=p;temp+=L"."+wide(randomId())+L".pending";newText(temp,s);need(MoveFileExW(temp.c_str(),p.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH),"RECORD_COMMIT");}
inline void checkpoint(const char*name){
#ifdef GEHD_PRODUCT_CONTROL_TEST
 char value[128]{};if(GetEnvironmentVariableA("GEHD_TEST_STOP",value,sizeof(value))&&std::string(value)==name)throw std::runtime_error(std::string("TEST_STOP:")+name);
#else
 (void)name;
#endif
}
inline uint64_t availableSpace(const fs::path&path){
 ULARGE_INTEGER free{};need(product::GetDiskFreeSpaceExW(path.c_str(),&free,nullptr,nullptr),"DISK_SPACE_UNAVAILABLE");
#ifdef GEHD_PRODUCT_CONTROL_TEST
 char value[8]{};if(GetEnvironmentVariableA("GEHD_TEST_NO_SPACE",value,sizeof(value))&&std::string(value)=="1")return 0;
#endif
 return free.QuadPart;
}
struct Installation {
 std::string token,release,dataId,dllHash,dllId="-",configHash="-",configId="-",mode="DISABLED";
 fs::path game,data;
 std::vector<std::string> record()const{return {"GEHD_INSTALL_V1",utf8(game.wstring()),token,utf8(data.wstring()),release,dataId,dllHash,dllId,configHash,configId,mode};}
};
inline Installation installation(const fs::path&game){auto v=lines(game/L"GEHD/state/installation.tsv",8192);need(v.size()==11&&v[0]=="GEHD_INSTALL_V1"&&samePath(wide(v[1]),game)&&tokenText(v[2])&&digestText(v[4])&&digestText(v[6]),"INSTALLATION_RECORD");Installation s;s.game=game;s.token=v[2];s.data=product::absolute(wide(v[3]));s.release=v[4];s.dataId=v[5];s.dllHash=v[6];s.dllId=v[7];s.configHash=v[8];s.configId=v[9];s.mode=v[10];need(s.mode=="ENABLED"||s.mode=="DISABLED"||s.mode=="PREPARED"||s.mode=="ADOPTING"||s.mode=="UNINSTALLING"||s.mode=="UNINSTALLED","INSTALLATION_STATE");return s;}
inline fs::path stateRoot(const fs::path&g){return g/L"GEHD/state";}
inline void saveInstall(const Installation&s){saveLines(stateRoot(s.game)/L"installation.tsv",s.record());}
inline void gameReady(const fs::path&g);
inline loadingoptions::Options loadingOptions(const Installation&s){
 auto path=stateRoot(s.game)/L"loading-options.tsv";loadingoptions::Options o;o.game=targetGame->id;o.owner=s.token;
 if(fs::exists(path)){o=loadingoptions::parse(lines(path,1024),*targetGame);need(o.owner==s.token,"LOADING_OPTIONS_OWNERSHIP");}return o;
}
inline void setLoadingOptions(const Installation&s,const std::string&mode,bool reset){
 gameReady(s.game);auto o=loadingOptions(s);if(!mode.empty()){need(loadingoptions::mode(mode),"LOADING_MODE");o.mode=mode;}if(reset)o.epoch=randomId();
 saveLines(stateRoot(s.game)/L"loading-options.tsv",o.record());std::cout<<"NEXT_START_LOADING_MODE="<<o.mode<<" PROFILE_RESET="<<reset<<'\n';
}
inline fs::path cachedRelease(const Installation&s){return stateRoot(s.game)/(L"release-"+wide(s.release));}
inline void cacheRelease(const Installation&s,const Package&p){
 auto root=cachedRelease(s);ordinary(root);fs::create_directories(root);
 for(auto name:{L"mapping.tsv",L"release.files",L"pack.identity"}){
  auto src=p.root/name,dest=root/name;auto hash=fileHash(src);
  if(fs::exists(dest))need(fileHash(dest)==hash,"RELEASE_CACHE_CONFLICT");
  else need(CopyFileW(src.c_str(),dest.c_str(),TRUE),"RELEASE_CACHE_COPY");
 }
 need(package(root).release==s.release,"RELEASE_CACHE_IDENTITY");
}
inline std::string ownerText(const Installation&s){return "GEHD_DATA_OWNER_V1\n"+s.token+"\n"+utf8(s.game.wstring())+"\n";}
inline void checkOwner(const Installation&s,const fs::path&root,const std::string&id){need(fileId(root)==id,"DATA_DIRECTORY_IDENTITY");auto v=lines(root/L".gehd-owner",4096);need(v.size()==3&&v[0]=="GEHD_DATA_OWNER_V1"&&v[1]==s.token&&samePath(wide(v[2]),s.game),"DATA_OWNERSHIP");}
inline void ownData(const Installation&s,const fs::path&p){if(fs::exists(p/L".gehd-owner")){auto v=lines(p/L".gehd-owner",4096);need(v.size()==3&&v[0]=="GEHD_DATA_OWNER_V1"&&v[1]==s.token&&samePath(wide(v[2]),s.game),"OTHER_DATA_OWNER");}else newText(p/L".gehd-owner",ownerText(s));}
inline void checkOwnedFile(const fs::path&p,const std::string&id,const std::string&hash){if(fs::exists(p)){need(id!="-"&&fileId(p)==id&&fileHash(p)==hash,"DEPLOYMENT_OWNERSHIP_CONFLICT");}}
inline void checkDeployment(const Installation&s){checkOwnedFile(s.game/L"d3d9.dll",s.dllId,s.dllHash);checkOwnedFile(s.game/L"gehd-pack-path.txt",s.configId,s.configHash);}
inline void gameReady(const fs::path&g){ordinary(g);need(!running(),"CLOSE_GAME_FIRST");need(targetGame&&fileHash(g/targetGame->executable)==GameHash,"GAME_EXE_IDENTITY");}
inline void validateSelectedLoader(const fs::path&path){
 ordinary(path);std::ifstream f(fs::path(nativePath(path)),std::ios::binary|std::ios::ate);need(bool(f),"LOADER_READ");auto size=f.tellg();need(size>0&&uint64_t(size)<=16*1024*1024,"LOADER_SIZE");std::string bytes(size_t(size),0);f.seekg(0);f.read(bytes.data(),bytes.size());need(bool(f)&&targetGame,"LOADER_READ");
 auto marker=gameprofile::descriptor(*targetGame);std::string expected(marker.data());auto at=bytes.find("GEHD_GAME_DESCRIPTOR_V1\n");need(at!=bytes.npos&&bytes.compare(at,expected.size(),expected)==0&&bytes.find("GEHD_GAME_DESCRIPTOR_V1\n",at+1)==bytes.npos,"LOADER_GAME_IDENTITY");
}
inline fs::path localLoader(const fs::path&g){return g/L"GEHD/bin/d3d9.dll";}
inline void dataLocation(const fs::path&game,const fs::path&data){
 need(!nested(data,game),"DATA_CONTAINS_GAME");
 need(!nested(game,data)||samePath(data,game/L"GEHD/data"),"DATA_INSIDE_PROTECTED_GAME_AREA");ordinary(data);
 // External data may not be moved into another game's installation tree.
 // An updated/unknown executable still marks a protected game directory.
 for(auto parent=product::absolute(data);;){
  if(parent==parent.root_path()||!samePath(parent,game))for(auto known:gameprofile::registry)need(!fs::exists(parent/known->executable),"DATA_INSIDE_OTHER_GAME");
  auto next=parent.parent_path();if(next==parent||next.empty())break;parent=next;
 }
}
inline void disable(Installation&s){need(!running(),"CLOSE_GAME_FIRST");checkDeployment(s);if(fs::exists(s.game/L"d3d9.dll"))need(DeleteFileW((s.game/L"d3d9.dll").c_str()),"REMOVE_LOADER");checkpoint("disable-dll");if(fs::exists(s.game/L"gehd-pack-path.txt"))need(DeleteFileW((s.game/L"gehd-pack-path.txt").c_str()),"REMOVE_CONFIG");s.mode="DISABLED";s.dllId=s.configId=s.configHash="-";saveInstall(s);}
inline void enable(Installation&s){gameReady(s.game);checkDeployment(s);auto p=package(s.data);need(p.release==s.release,"RELEASE_CHANGED");checkOwner(s,s.data,s.dataId);verify(p);need(fileHash(localLoader(s.game))==s.dllHash,"LOADER_CHANGED");
 if(s.mode=="ENABLED"&&fs::exists(s.game/L"d3d9.dll")&&fs::exists(s.game/L"gehd-pack-path.txt"))return;
 need(!fs::exists(s.game/L"d3d9.dll")&&!fs::exists(s.game/L"gehd-pack-path.txt"),"RECOVER_PARTIAL_DEPLOYMENT_FIRST");
 auto stage=stateRoot(s.game)/(L"deploy-"+wide(randomId()));need(fs::create_directory(stage),"CREATE_DEPLOY_STAGE");
 need(CopyFileW(localLoader(s.game).c_str(),(stage/L"d3d9.dll").c_str(),TRUE),"COPY_LOADER");newText(stage/L"gehd-pack-path.txt",utf8(s.data.wstring()));
 s.dllId=fileId(stage/L"d3d9.dll");s.configId=fileId(stage/L"gehd-pack-path.txt");s.configHash=fileHash(stage/L"gehd-pack-path.txt");s.mode="PREPARED";saveInstall(s);checkpoint("enable-prepared");
 need(CreateHardLinkW((s.game/L"gehd-pack-path.txt").c_str(),(stage/L"gehd-pack-path.txt").c_str(),nullptr),"PUBLISH_CONFIG");checkpoint("enable-config");
 need(CreateHardLinkW((s.game/L"d3d9.dll").c_str(),(stage/L"d3d9.dll").c_str(),nullptr),"PUBLISH_LOADER");checkpoint("enable-dll");s.mode="ENABLED";saveInstall(s);
}
inline void validateBundledShell(const fs::path&game,const Package&p);
inline void claimShellIfBundled(const Installation&s);
inline Installation adopt(const fs::path&g,const fs::path&data){gameReady(g);dataLocation(g,data);auto p=package(data);verify(p);validateSelectedLoader(localLoader(g));validateBundledShell(g,p);
 if(fs::exists(stateRoot(g)/L"installation.tsv")){
  auto s=installation(g);
  if(s.mode=="UNINSTALLED"){
   need(!fs::exists(g/L"gehd-pack-path.txt"),"EXISTING_CONFIG_NOT_ADOPTED");need(!fs::exists(p.root/L".gehd-owner"),"OTHER_DATA_OWNER");
   auto dllHash=fileHash(localLoader(g));std::string dllId="-";
   if(fs::exists(g/L"d3d9.dll")){need(fileHash(g/L"d3d9.dll")==dllHash,"FOREIGN_LOADER");dllId=fileId(g/L"d3d9.dll");}
   // A completed uninstall keeps recovery history. Explicit re-adoption claims
   // the freshly extracted package, while retaining obsolete journals by name.
   for(auto name:{L"uninstall.tsv",L"maintenance.tsv",L"move.tsv"}){auto journal=stateRoot(g)/name;
    if(fs::exists(journal)){auto saved=journal;saved+=L".retired-"+wide(randomId());need(MoveFileExW(journal.c_str(),saved.c_str(),MOVEFILE_WRITE_THROUGH),"RETIRE_COMPLETED_RECORD");checkpoint("readopt-record-retired");}
   }
   s.data=p.root;s.dataId=fileId(p.root);s.release=p.release;s.dllHash=dllHash;s.dllId=dllId;s.configId=s.configHash="-";s.mode="ADOPTING";saveInstall(s);checkpoint("readopt-recorded");
  }
  need(s.mode=="ADOPTING"&&samePath(s.data,p.root)&&s.release==p.release&&s.dataId==fileId(p.root),"ALREADY_MANAGED");checkDeployment(s);cacheRelease(s,p);ownData(s,p.root);disable(s);enable(s);claimShellIfBundled(s);return s;
 }
 Installation s;s.game=g;s.data=p.root;s.release=p.release;s.dataId=fileId(p.root);s.dllHash=fileHash(localLoader(g));s.token=randomId();
 need(!fs::exists(g/L"gehd-pack-path.txt"),"EXISTING_CONFIG_NOT_ADOPTED");
 if(fs::exists(g/L"d3d9.dll")){need(fileHash(g/L"d3d9.dll")==s.dllHash,"FOREIGN_LOADER");s.dllId=fileId(g/L"d3d9.dll");s.mode="PREPARED";}
 ordinary(stateRoot(g));fs::create_directories(stateRoot(g));need(!fs::exists(p.root/L".gehd-owner"),"OTHER_DATA_OWNER");s.mode="ADOPTING";saveInstall(s);checkpoint("adopt-recorded");cacheRelease(s,p);ownData(s,p.root);checkpoint("adopt-owned");
 // Explicit adoption is the only path that claims an already placed proxy.
 if(s.dllId!="-")disable(s);enable(s);claimShellIfBundled(s);return s;
}
struct Move {
 std::string phase="COPYING",id,oldId,newId="-",release,operation="MOVE";fs::path oldPath,dest,stage,folder;
 std::vector<std::string>record(const Installation&s)const{return {"GEHD_MOVE_V2",s.token,phase,id,utf8(oldPath.wstring()),oldId,utf8(dest.wstring()),utf8(stage.wstring()),newId,release,operation};}
};
inline void saveMove(const Installation&s,const Move&t){saveLines(stateRoot(s.game)/L"move.tsv",t.record(s));}
inline Move readMove(const Installation&s){auto v=lines(stateRoot(s.game)/L"move.tsv",8192);need(v.size()==11&&v[0]=="GEHD_MOVE_V2"&&v[1]==s.token&&tokenText(v[3])&&digestText(v[9]),"MOVE_RECORD");Move t;t.phase=v[2];t.id=v[3];t.oldPath=product::absolute(wide(v[4]));t.oldId=v[5];t.dest=product::absolute(wide(v[6]));t.stage=product::absolute(wide(v[7]));t.newId=v[8];t.release=v[9];t.operation=v[10];need(t.operation=="MOVE"||t.operation=="LOCATE","MOVE_OPERATION");t.folder=stateRoot(s.game)/(L"move-"+wide(t.id));need(t.stage==fs::path(t.dest.wstring()+L".gehd-stage-"+wide(t.id)),"MOVE_STAGE_SCOPE");need(!nested(t.oldPath,t.dest)&&!nested(t.dest,t.oldPath),"MOVE_OVERLAP");dataLocation(s.game,t.oldPath);dataLocation(s.game,t.dest);return t;}
inline void copyVerified(const fs::path&src,const fs::path&dest,const std::string&hash){ordinary(src);ordinary(dest);if(fs::exists(dest)){need(fs::is_regular_file(dest),"DESTINATION_TYPE");if(fileHash(dest)==hash)return;throw std::runtime_error("DESTINATION_CONTENT_CONFLICT");}
 fs::create_directories(dest.parent_path());auto part=dest;part+=L".gehd-part";ordinary(part);
 // Windows owns the copy transport; unfinished per-file scratch may be overwritten only in our fresh transaction stage.
 need(CopyFileW(src.c_str(),part.c_str(),FALSE),"COPY_FAILED");need(fileHash(part)==hash,"COPY_HASH_MISMATCH");need(MoveFileExW(part.c_str(),dest.c_str(),MOVEFILE_WRITE_THROUGH),"COPY_PUBLISH");
}
inline void cleanData(const Installation&s,const fs::path&root,const std::string&dirId,const Package&snapshot){
 if(!fs::exists(root))return;checkOwner(s,root,dirId);
 auto files=snapshot.files;files.push_back({"release.files",fileHash(snapshot.root/L"release.files"),fs::file_size(snapshot.root/L"release.files")});files.push_back({"pack.identity",fileHash(snapshot.root/L"pack.identity"),fs::file_size(snapshot.root/L"pack.identity")});
 // Validate the complete deletion set before deleting any item. Missing files are resumable cleanup.
 for(auto&f:files){auto p=root/f.relative;if(fs::exists(p))need(fileHash(p)==f.hash,"CLEANUP_MODIFIED_FILE_RETAINED");}
 for(auto&f:files){auto p=root/f.relative;if(fs::exists(p))need(DeleteFileW(p.c_str()),"CLEANUP_PENDING");checkpoint("cleanup-file");}
 // Keep the owner marker until the deletion phase is durably committed.
}
inline void finishCleanData(const Installation&s,const fs::path&root,const std::string&dirId){
 if(!fs::exists(root))return;need(fileId(root)==dirId,"DATA_DIRECTORY_IDENTITY");
 if(fs::exists(root/L".gehd-owner")){checkOwner(s,root,dirId);need(DeleteFileW((root/L".gehd-owner").c_str()),"CLEANUP_OWNER");}
 checkpoint("cleanup-owner");
 std::error_code ec;
 for(auto name:{L"2x",L"4x"}){auto dir=root/name;ordinary(dir);if(fs::is_directory(dir)){fs::remove(dir,ec);ec.clear();}}
 fs::remove(root,ec); // Remove only empty directories; unknown files, including a file named 2x, survive.
 if(fs::exists(root))std::cout<<"OLD_UNMANAGED_FILES_RETAINED "<<utf8(root.wstring())<<'\n';
}
inline void resumeMove(Installation&s){gameReady(s.game);auto t=readMove(s);auto snapshot=package(t.folder/L"baseline");need(snapshot.release==t.release,"MOVE_BASELINE");ordinary(t.dest);ordinary(t.stage);
 if(t.phase=="LOCATING"){
  need(t.operation=="LOCATE"&&samePath(s.data,t.oldPath)&&s.release==t.release,"LOCATE_STATE");checkDeployment(s);
  need(fileId(t.dest)==t.newId,"LOCATE_DIRECTORY_CHANGED");auto p=package(t.dest);need(p.release==t.release,"LOCATE_RELEASE");verify(p);ownData(s,t.dest);
  t.phase="PUBLISHED";saveMove(s,t);checkpoint("locate-published");
 }
 if(t.phase=="COPYING"){
  need(samePath(s.data,t.oldPath)&&s.release==t.release,"MOVE_SOURCE_STATE");checkDeployment(s);checkOwner(s,t.oldPath,t.oldId);auto source=package(t.oldPath);need(source.release==t.release,"MOVE_SOURCE_CHANGED");
  if(t.newId=="-"){
   need(!fs::exists(t.dest),"DESTINATION_EXISTS");
   if(!fs::exists(t.stage))need(fs::create_directory(t.stage),"CREATE_MOVE_STAGE");
   // The only unclaimed resumable stage is the empty, randomly named directory recorded by this transaction.
   need(fs::is_directory(t.stage)&&fs::is_empty(t.stage),"UNCLAIMED_STAGE_NOT_EMPTY");checkpoint("stage-created");
   t.newId=fileId(t.stage);saveMove(s,t);checkpoint("stage-recorded");
  }
  need(fileId(t.stage)==t.newId,"MOVE_STAGE_IDENTITY");ownData(s,t.stage);checkOwner(s,t.stage,t.newId);
  uint64_t missing=0;for(auto&f:source.files)if(!fs::exists(t.stage/f.relative))missing+=f.bytes;
  need(availableSpace(t.stage)>=missing+16*1024*1024,"INSUFFICIENT_SPACE");
  size_t i=0;for(auto&f:source.files){need(fileHash(source.root/f.relative)==f.hash,"SOURCE_CHANGED");copyVerified(source.root/f.relative,t.stage/f.relative,f.hash);checkpoint("copy-file");if(++i%128==0)std::cerr<<"COPY "<<i<<'/'<<source.files.size()<<'\n';}
  copyVerified(source.root/L"release.files",t.stage/L"release.files",t.release);copyVerified(source.root/L"pack.identity",t.stage/L"pack.identity",fileHash(source.root/L"pack.identity"));verify(package(t.stage));t.phase="VERIFIED";saveMove(s,t);checkpoint("move-verified");
 }
 if(t.phase=="VERIFIED"){
  gameReady(s.game);if(fs::exists(t.stage)){checkOwner(s,t.stage,t.newId);need(!fs::exists(t.dest),"DESTINATION_EXISTS");need(MoveFileExW(t.stage.c_str(),t.dest.c_str(),MOVEFILE_WRITE_THROUGH),"PUBLISH_DATA");}
  checkOwner(s,t.dest,t.newId);need(package(t.dest).release==t.release,"DESTINATION_RELEASE");t.phase="PUBLISHED";saveMove(s,t);checkpoint("move-published");
 }
 if(t.phase=="PUBLISHED"){
  gameReady(s.game);checkOwner(s,t.dest,t.newId);verify(package(t.dest));
  if(s.mode=="ENABLED"){
   auto config=t.folder/L"new-path.txt";if(!fs::exists(config))newText(config,utf8(t.dest.wstring()));
   auto content=lines(config,32768);need(content.size()==1&&content[0]==utf8(t.dest.wstring()),"SWITCH_PATH_CONTENT");auto id=fileId(config),hash=fileHash(config);
   auto active=s.game/L"gehd-pack-path.txt";
   if(!fs::exists(active)||fileId(active)!=id){
    checkDeployment(s);auto pending=t.folder/L"publish-path.txt";
    if(fs::exists(pending))need(fileId(pending)==id&&fileHash(pending)==hash,"SWITCH_STAGE_CONFLICT");
    else need(CreateHardLinkW(pending.c_str(),config.c_str(),nullptr),"STAGE_SWITCH_PATH");
    need(MoveFileExW(pending.c_str(),active.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH),"SWITCH_PATH");checkpoint("path-replaced");
   }
   // Record the exact intended identity before committing installation state.
   s.configId=fileId(active);s.configHash=fileHash(active);need(s.configHash==hash,"SWITCH_CONTENT");
  }
  s.data=t.dest;s.dataId=t.newId;s.release=t.release;saveInstall(s);checkpoint("move-switched");t.phase="COMMITTED";saveMove(s,t);
 }
 if(t.phase=="COMMITTED"){
  gameReady(s.game);need(samePath(s.data,t.dest)&&s.release==t.release,"COMMIT_STATE");checkOwner(s,t.dest,t.newId);verify(package(t.dest));
  if(t.operation=="MOVE")try{cleanData(s,t.oldPath,t.oldId,snapshot);}catch(...){std::cerr<<"NEW_DATA_COMMITTED_OLD_COPY_CLEANUP_PENDING\n";throw;}
  t.phase="CLEANED";saveMove(s,t);checkpoint("move-cleaned");
 }
 need(t.phase=="CLEANED","UNKNOWN_MOVE_PHASE");if(t.operation=="MOVE")finishCleanData(s,t.oldPath,t.oldId);std::cout<<(t.operation=="MOVE"?"MOVE_COMPLETE\n":"LOCATION_CHANGED_OLD_COPY_RETAINED\n");
}
inline void beginMove(Installation&s,const fs::path&dest){gameReady(s.game);checkDeployment(s);need(s.mode!="PREPARED","RECOVER_DEPLOYMENT_FIRST");
 if(fs::exists(stateRoot(s.game)/L"move.tsv")){auto old=readMove(s);need(old.phase=="CLEANED","RESUME_PENDING_MOVE_FIRST");}
 auto p=package(s.data);need(p.release==s.release,"SOURCE_RELEASE");checkOwner(s,s.data,s.dataId);verify(p);
 Move t;t.id=randomId();t.oldPath=s.data;t.oldId=s.dataId;t.dest=product::absolute(dest);t.stage=fs::path(t.dest.wstring()+L".gehd-stage-"+wide(t.id));t.release=s.release;t.folder=stateRoot(s.game)/(L"move-"+wide(t.id));
 need(!nested(t.oldPath,t.dest)&&!nested(t.dest,t.oldPath),"MOVE_OVERLAP");dataLocation(s.game,t.dest);need(!fs::exists(t.dest)&&!fs::exists(t.stage),"DESTINATION_EXISTS");need(fs::exists(t.dest.parent_path()),"DESTINATION_PARENT_MISSING");
 fs::create_directories(t.folder/L"baseline");for(auto name:{L"mapping.tsv",L"release.files",L"pack.identity"})need(CopyFileW((p.root/name).c_str(),(t.folder/L"baseline"/name).c_str(),TRUE),"SAVE_BASELINE");saveMove(s,t);checkpoint("move-prepared");resumeMove(s);
}
inline void locate(Installation&s,const fs::path&dest){
 gameReady(s.game);checkDeployment(s);need(s.mode!="PREPARED","RECOVER_DEPLOYMENT_FIRST");dataLocation(s.game,dest);
 auto p=package(dest);need(p.release==s.release,"LOCATE_RELEASE");verify(p);
 if(samePath(s.data,p.root)){checkOwner(s,p.root,s.dataId);std::cout<<"LOCATION_UNCHANGED\n";return;}
 need(!nested(s.data,p.root)&&!nested(p.root,s.data),"MOVE_OVERLAP");
 if(fs::exists(p.root/L".gehd-owner"))checkOwner(s,p.root,fileId(p.root));
 Move t;t.id=randomId();t.operation="LOCATE";t.phase="LOCATING";t.oldPath=s.data;t.oldId=s.dataId;t.dest=p.root;t.newId=fileId(p.root);t.release=s.release;t.stage=fs::path(t.dest.wstring()+L".gehd-stage-"+wide(t.id));t.folder=stateRoot(s.game)/(L"move-"+wide(t.id));
 auto baseline=package(cachedRelease(s));need(baseline.release==s.release,"RELEASE_CACHE_IDENTITY");
 fs::create_directories(t.folder/L"baseline");for(auto name:{L"mapping.tsv",L"release.files",L"pack.identity"})need(CopyFileW((baseline.root/name).c_str(),(t.folder/L"baseline"/name).c_str(),TRUE),"SAVE_BASELINE");
 saveMove(s,t);checkpoint("locate-recorded");resumeMove(s);
}
inline void uninstall(Installation&s){
 need(!running(),"CLOSE_GAME_FIRST");checkDeployment(s);dataLocation(s.game,s.data);
 if(s.mode=="UNINSTALLED"){std::cout<<"ALREADY_UNINSTALLED\n";return;}
 auto baseline=package(cachedRelease(s));need(baseline.release==s.release,"UNINSTALL_BASELINE");
 // Persist uninstall intent separately: disable() is itself resumable and resets installation mode.
 auto journal=stateRoot(s.game)/L"uninstall.tsv";
 if(!fs::exists(journal))saveLines(journal,{"GEHD_UNINSTALL_V1",s.token,s.release,"PREPARED"});
 auto record=lines(journal,4096);need(record.size()==4&&record[0]=="GEHD_UNINSTALL_V1"&&record[1]==s.token&&record[2]==s.release,"UNINSTALL_RECORD");
 if(record[3]=="PREPARED"){
  disable(s);s.mode="UNINSTALLING";saveInstall(s);record[3]="DATA";saveLines(journal,record);checkpoint("uninstall-disabled");
 }
 if(record[3]=="DATA"){
  if(fs::exists(s.data)){
   checkOwner(s,s.data,s.dataId);auto files=baseline.files;
   for(auto name:{"release.files","pack.identity"})files.push_back({name,fileHash(baseline.root/name),fs::file_size(baseline.root/name)});
   for(auto&f:files){auto path=s.data/f.relative;ordinary(path);if(fs::exists(path)){
    if(fs::is_regular_file(path)&&fileHash(path)==f.hash)need(DeleteFileW(path.c_str()),"UNINSTALL_DATA_PENDING");
    else std::cout<<"MODIFIED_FILE_RETAINED "<<utf8(path.wstring())<<'\n';
   }checkpoint("uninstall-file");}
  }
  record[3]="CLEANED";saveLines(journal,record);checkpoint("uninstall-cleaned");
 }
 need(record[3]=="CLEANED","UNINSTALL_PHASE");finishCleanData(s,s.data,s.dataId);s.mode="UNINSTALLED";saveInstall(s);std::cout<<"UNINSTALLED_TOOLS_AND_RECEIPTS_RETAINED\n";
}
#include "product_maintenance.hpp"
#include "product_bundle.hpp"
inline int control(int argc,wchar_t**argv){try{
 need(argc>=2,"Usage: --product action --game directory [--data directory] [--bundle metadata-directory]");std::wstring action=argv[1];std::string requestedMode;fs::path game,data,bundleRoot;for(int i=2;i<argc;i+=2){need(i+1<argc,"OPTION_VALUE");std::wstring key=argv[i];if(key==L"--mode")requestedMode=utf8(argv[i+1]);else if(key==L"--game")game=argv[i+1];else if(key==L"--data")data=argv[i+1];else if(key==L"--bundle")bundleRoot=argv[i+1];else throw std::runtime_error("UNKNOWN_OPTION");}
 if(!game.empty()){game=product::absolute(game);ordinary(game);selectGame(game,action==L"status"||action==L"disable"||action==L"recover-deployment"||action==L"uninstall"||action==L"uninstall-bundle"||action==L"resume");}
 if(action==L"verify"){need(!data.empty(),"DATA_REQUIRED");auto p=package(data);verify(p);std::cout<<"VERIFIED "<<p.files.size()<<' '<<p.release<<'\n';return 0;}
 need(!game.empty(),"GAME_REQUIRED");game=product::absolute(game);ordinary(game);
 if(action==L"preflight-install"||action==L"prepare-stage"){
  need(!bundleRoot.empty(),"BUNDLE_REQUIRED");auto b=bundle(bundleRoot);
  if(action==L"preflight-install")std::cout<<installPreflight(game,b)<<'\n';else std::cout<<utf8(prepareBundleStage(game,b).filename().wstring())<<'\n';return 0;
 }
 if(action==L"verify-stage"){
  gameReady(game);need(!data.empty(),"STAGE_REQUIRED");auto b=bundle(data/L"GEHD");checkBundleStage(game,data,b);verifyShell(b,b.root);need(fileHash(data/L"d3d9.dll")==b.dllHash,"STAGE_PROXY_IDENTITY");auto p=package(data/L"GEHD/data");need(p.release==b.release,"STAGE_DATA_RELEASE");verify(p);std::cout<<"STAGE_VERIFIED\n";return 0;
 }
 if(action==L"status"){bool managed=fs::exists(stateRoot(game)/L"installation.tsv");std::cout<<"gameRunning="<<running()<<" managed="<<managed<<'\n';if(managed){auto s=installation(game);checkDeployment(s);std::cout<<s.mode<<'\n'<<utf8(s.data.wstring())<<'\n'<<s.release<<'\n';try{if(s.mode=="UNINSTALLED")std::cout<<"dataBytes=not-installed\n";else{auto p=package(s.data);need(p.release==s.release,"RELEASE_CHANGED");std::cout<<"dataBytes="<<p.bytes<<'\n';}}catch(...){std::cout<<"dataBytes=unavailable\n";}auto o=loadingOptions(s);std::cout<<"gameId="<<targetGame->id<<" loadingMode="<<o.mode<<" nextStartOnly=1\n";}return 0;}
 need(!running(),"CLOSE_GAME_FIRST");ordinary(stateRoot(game));fs::create_directories(stateRoot(game));Handle lock;lock.h=CreateFileW((stateRoot(game)/L"control.lock").c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);need(lock.h!=INVALID_HANDLE_VALUE,"ANOTHER_MANAGER_RUNNING");
 if(action==L"install"){need(!data.empty(),"STAGE_REQUIRED");installBundle(game,data);return 0;}
 if(action==L"cleanup-stage"){need(!data.empty(),"STAGE_REQUIRED");cleanupBundleStage(game,data);return 0;}
 need(!legacyIncomplete(game),"LEGACY_INSTALL_RESUME_REQUIRED");
 if(action==L"rollback-bundle"){auto s=installation(game);rollbackShellUpdate(s);return 0;}
 if(pendingShell(game)){need(action==L"resume","RESUME_BUNDLE_UPDATE_FIRST");auto s=installation(game);resumeShellUpdate(s);return 0;}
 if(action==L"adopt"){need(!data.empty(),"DATA_REQUIRED");adopt(game,data);std::cout<<"ADOPTED_ENABLED\n";return 0;}
 auto s=installation(game);
 need(s.mode!="ADOPTING","RESUME_ADOPTION_FIRST");
 if(action==L"uninstall-bundle"){
  if(fs::exists(stateRoot(game)/L"move.tsv"))need(readMove(s).phase=="CLEANED","RESUME_PENDING_MOVE_FIRST");
  if(fs::exists(stateRoot(game)/L"maintenance.tsv"))need(maintenanceDone(readMaintenance(s)),"RESUME_MAINTENANCE_FIRST");
  uninstallBundle(s);return 0;
 }
 if(fs::exists(stateRoot(game)/L"uninstall.tsv")){
  need(action==L"uninstall"||action==L"resume","RESUME_UNINSTALL_FIRST");uninstall(s);return 0;
 }
 need(s.mode!="UNINSTALLED"&&s.mode!="UNINSTALLING","RESUME_UNINSTALL_FIRST");
 if(fs::exists(stateRoot(game)/L"move.tsv")&&readMove(s).phase!="CLEANED"){
  need(action==L"resume","RESUME_PENDING_MOVE_FIRST");resumeMove(s);return 0;
 }
 if(fs::exists(stateRoot(game)/L"maintenance.tsv")){
  auto m=readMaintenance(s);
  if(action==L"rollback"){rollbackMaintenance(s);return 0;}
  if(!maintenanceDone(m)){
   need(action==L"resume","RESUME_MAINTENANCE_FIRST");
   if(m.phase=="ROLLING_BACK")rollbackMaintenance(s);else resumeMaintenance(s);return 0;
  }
 }
 if(action==L"loading-mode"||action==L"relearn"){need(action==L"relearn"||!requestedMode.empty(),"LOADING_MODE_REQUIRED");setLoadingOptions(s,requestedMode,action==L"relearn");}
 else if(action==L"enable")enable(s);else if(action==L"disable"||action==L"recover-deployment")disable(s);else if(action==L"move"){need(!data.empty(),"DATA_REQUIRED");beginMove(s,data);}else if(action==L"locate"){need(!data.empty(),"DATA_REQUIRED");locate(s,data);}else if(action==L"repair"||action==L"update"){need(!data.empty(),"DATA_REQUIRED");beginMaintenance(s,data,action==L"repair");}else if(action==L"uninstall")uninstall(s);else if(action==L"resume"){
  if(fs::exists(stateRoot(game)/L"move.tsv")&&readMove(s).phase!="CLEANED")resumeMove(s);
  else if(fs::exists(stateRoot(game)/L"maintenance.tsv"))resumeMaintenance(s);
  else if(fs::exists(stateRoot(game)/L"move.tsv"))resumeMove(s);
  else std::cout<<"NO_PENDING_WORK\n";
 }else throw std::runtime_error("UNKNOWN_ACTION");
 return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
}
