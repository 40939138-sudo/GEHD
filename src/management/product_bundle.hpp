#pragma once
// Included inside namespace product. Inno owns its registration/uninstaller;
// the native controller owns published data, deployment, shell receipts and extraction stages.
struct Bundle {fs::path root;std::string id,release,shellHash,dllHash;std::vector<File>files;};
inline bool pendingShell(const fs::path&game);
inline void checkPendingShell(const fs::path&game,const Bundle&b);
inline fs::path pendingShellStage(const fs::path&game,const Bundle&b);
inline void checkShellOwner(const Installation&s);
inline bool shellRelative(const std::string&name){
 return name=="bin/d3d9.dll"||name=="bin/pack-control.exe"||name=="GEHD_Manager.exe"||name=="README.txt"||name=="LICENSES/Inno-Setup.txt"||name=="LICENSES/7-Zip.txt"||name=="LICENSES/OpenSSL.txt";
}
inline Bundle bundle(const fs::path&root){
 Bundle b;b.root=product::absolute(root);auto v=lines(b.root/L"bundle.identity",1024);
 need(v.size()==4&&v[0]=="GEHD_BUNDLE_V1"&&v[1]==GameHash&&digestText(v[2])&&digestText(v[3]),"BUNDLE_IDENTITY");
 b.release=v[2];b.shellHash=v[3];b.id=fileHash(b.root/L"bundle.identity");need(fileHash(b.root/L"shell.files")==b.shellHash,"SHELL_INDEX_HASH");
 auto rows=lines(b.root/L"shell.files",65536);need(rows.size()>3&&rows[0]=="GEHD_SHELL_V1","SHELL_INDEX");std::set<std::string>seen;
 for(size_t i=1;i<rows.size();++i){std::istringstream row(rows[i]);File f;std::string tail;need(bool(row>>f.relative>>f.bytes>>f.hash)&&!(row>>tail)&&shellRelative(f.relative)&&seen.insert(f.relative).second&&digestText(f.hash)&&f.bytes>0&&f.bytes<64*1024*1024,"SHELL_ENTRY");if(f.relative=="bin/d3d9.dll")b.dllHash=f.hash;b.files.push_back(f);}
 need(seen.count("bin/d3d9.dll")&&seen.count("bin/pack-control.exe")&&seen.count("GEHD_Manager.exe"),"SHELL_REQUIRED_FILES");return b;
}
inline void verifyShell(const Bundle&b,const fs::path&root,bool allowMissing=false){
 for(auto&f:b.files){auto p=root/f.relative;ordinary(p);if(allowMissing&&!fs::exists(p))continue;need(fs::is_regular_file(p)&&fs::file_size(p)==f.bytes&&fileHash(p)==f.hash,"SHELL_CONTENT_CONFLICT");}
}
inline bool managerRunning(){
 Handle h;h.h=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);need(h.h!=INVALID_HANDLE_VALUE,"PROCESS_INVENTORY");PROCESSENTRY32W e{};e.dwSize=sizeof(e);need(Process32FirstW(h.h,&e)!=FALSE,"PROCESS_INVENTORY");
 do{if(_wcsicmp(e.szExeFile,L"GEHD_Manager.exe")==0)return true;}while(Process32NextW(h.h,&e));return false;
}
#include "product_legacy.hpp"
inline std::string installPreflight(const fs::path&game,const Bundle&b){
 gameReady(game);need(!managerRunning(),"CLOSE_MANAGER_FIRST");ordinary(game/L"GEHD");
 if(pendingShell(game)){checkPendingShell(game,b);return "SHELL_RESUME";}
 if(legacyIncomplete(game)){legacyPending(game,b);return "LEGACY_RESUME";}
 if(fs::exists(stateRoot(game)/L"installation.tsv")){
  auto s=installation(game);checkDeployment(s);
  need(s.mode=="ENABLED"||s.mode=="DISABLED"||s.mode=="ADOPTING"||s.mode=="UNINSTALLED","RECOVER_DEPLOYMENT_FIRST");
  if(fs::exists(stateRoot(game)/L"move.tsv"))need(readMove(s).phase=="CLEANED","RESUME_PENDING_MOVE_FIRST");
  if(fs::exists(stateRoot(game)/L"maintenance.tsv"))need(maintenanceDone(readMaintenance(s)),"RESUME_MAINTENANCE_FIRST");
  if(s.mode=="UNINSTALLED"){verifyShell(b,game/L"GEHD",true);return "REINSTALL";}
  if(s.mode=="ADOPTING"){need(samePath(s.data,game/L"GEHD/data")&&s.release==b.release&&s.dllHash==b.dllHash&&bundle(game/L"GEHD").id==b.id,"RESUME_EXISTING_ADOPTION_FIRST");return "RESUME_ADOPTION";}
  checkOwner(s,s.data,s.dataId);
  auto old=bundle(game/L"GEHD");if(old.id!=b.id){checkShellOwner(s);return "UPGRADE";}
  verifyShell(b,game/L"GEHD",true);need(s.dllHash==b.dllHash,"LOADER_RECEIPT_CONFLICT");return "MANAGED";
 }
 verifyShell(b,game/L"GEHD",true);
 if(fs::exists(game/L"gehd-pack-path.txt")){legacyProbe(game);return "LEGACY";}
 if(fs::exists(game/L"d3d9.dll"))need(fileHash(game/L"d3d9.dll")==b.dllHash,"FOREIGN_LOADER");
 auto data=game/L"GEHD/data";
 if(fs::exists(data)){auto p=package(data);need(p.release==b.release,"EXISTING_DATA_RELEASE_CONFLICT");verify(p);need(!fs::exists(data/L".gehd-owner"),"OTHER_DATA_OWNER");return "ADOPT_EXISTING";}
 return "NEW";
}
inline fs::path prepareBundleStage(const fs::path&game,const Bundle&b){
 auto mode=installPreflight(game,b);if(mode=="SHELL_RESUME")return pendingShellStage(game,b);
 auto stage=game/(L".GEHD-stage-"+wide(randomId()));ordinary(stage);need(fs::create_directory(stage),"CREATE_INSTALL_STAGE");
 newText(stage/L".gehd-stage",std::string("GEHD_STAGE_V1\n")+utf8(game.wstring())+'\n'+b.id+'\n'+fileId(stage)+'\n');return stage;
}
inline void checkBundleStage(const fs::path&game,const fs::path&stage,const Bundle&b){
 auto path=product::absolute(stage);need(samePath(path.parent_path(),game),"INSTALL_STAGE_SCOPE");auto name=utf8(path.filename().wstring());need(name.rfind(".GEHD-stage-",0)==0&&tokenText(name.substr(12)),"INSTALL_STAGE_NAME");
 auto v=lines(path/L".gehd-stage",4096);need(v.size()==4&&v[0]=="GEHD_STAGE_V1"&&samePath(wide(v[1]),game)&&v[2]==b.id&&fileId(path)==v[3],"INSTALL_STAGE_OWNERSHIP");
}
inline void ownShell(const Installation&s,const Bundle&b){
 verifyShell(b,s.game/L"GEHD");std::vector<std::string>v{"GEHD_SHELL_OWNER_V1",s.token,b.id,fileId(s.game/L"GEHD")};
 auto files=b.files;files.push_back({"bundle.identity",b.id,fs::file_size(b.root/L"bundle.identity")});files.push_back({"shell.files",b.shellHash,fs::file_size(b.root/L"shell.files")});
 for(auto&f:files){auto p=s.game/L"GEHD"/f.relative;need(fileHash(p)==f.hash,"SHELL_PUBLISHED_IDENTITY");v.push_back(f.relative+'\t'+fileId(p)+'\t'+f.hash);}
 saveLines(stateRoot(s.game)/L"shell-owner.tsv",v);
}
inline void validateBundledShell(const fs::path&game,const Package&p){
 auto root=game/L"GEHD";bool identity=fs::exists(root/L"bundle.identity"),index=fs::exists(root/L"shell.files");
 if(!identity&&!index)return;need(identity&&index,"INCOMPLETE_BUNDLED_SHELL");auto b=bundle(root);need(b.release==p.release&&b.dllHash==fileHash(localLoader(game)),"BUNDLED_SHELL_RELEASE");verifyShell(b,root);
}
inline void claimShellIfBundled(const Installation&s){if(fs::exists(s.game/L"GEHD/bundle.identity")){auto b=bundle(s.game/L"GEHD");need(b.release==s.release&&b.dllHash==s.dllHash,"BUNDLED_SHELL_RELEASE");ownShell(s,b);}}
#include "product_shell.hpp"
inline void installBundle(const fs::path&game,const fs::path&stage){
 auto b=bundle(stage/L"GEHD");checkBundleStage(game,stage,b);auto mode=installPreflight(game,b);
 if(mode=="SHELL_RESUME"){auto s=installation(game);resumeShellUpdate(s);return;}
 validateSelectedLoader(stage/L"GEHD/bin/d3d9.dll");
 if(mode=="UPGRADE"){auto s=installation(game);beginShellUpdate(s,stage,b);return;}
 verifyShell(b,stage/L"GEHD");publishFreshShell(game,b);verifyShell(b,game/L"GEHD");
 need(fileHash(game/L"GEHD/bundle.identity")==b.id&&fileHash(game/L"GEHD/shell.files")==b.shellHash,"SHELL_METADATA_NOT_PUBLISHED");
 auto stagedData=stage/L"GEHD/data",dest=game/L"GEHD/data";
 if(mode=="MANAGED"){
  auto s=installation(game);
  if(!fs::exists(stagedData)){need(s.release==b.release,"INSTALL_STAGE_DATA_MISSING");verify(package(s.data));ownShell(s,b);std::cout<<"INSTALL_RECEIPTS_RECOVERED\n";return;}
  auto p=package(stagedData);need(p.release==b.release,"INSTALL_DATA_RELEASE");
  beginMaintenance(s,stagedData,s.release==b.release);s=installation(game);ownShell(s,b);std::cout<<"INSTALLED_MANAGED\n";return;
 }
 if(!fs::exists(dest)){
  auto p=package(stagedData);need(p.release==b.release,"INSTALL_DATA_RELEASE");verify(p);
  need(MoveFileExW(stagedData.c_str(),dest.c_str(),MOVEFILE_WRITE_THROUGH),"PUBLISH_INSTALL_DATA");checkpoint("install-data-published");
 }
 auto p=package(dest);need(p.release==b.release,"INSTALL_DATA_RELEASE");verify(p);
 if(mode=="LEGACY"||mode=="LEGACY_RESUME")retireLegacy(game,b);
 if(mode=="REINSTALL"){
  auto s=installation(game);need(s.mode=="UNINSTALLED","REINSTALL_STATE");
  for(auto name:{L"uninstall.tsv",L"maintenance.tsv",L"move.tsv"}){auto journal=stateRoot(game)/name;
   if(fs::exists(journal)){auto saved=journal;saved+=L".retired-"+wide(randomId());need(MoveFileExW(journal.c_str(),saved.c_str(),MOVEFILE_WRITE_THROUGH),"RETIRE_COMPLETED_RECORD");}
  }
  s.data=dest;s.dataId=fileId(dest);s.release=p.release;s.mode="ADOPTING";s.dllHash=b.dllHash;saveInstall(s);
 }
 auto s=adopt(game,dest);checkpoint("install-adopted");ownShell(s,b);std::cout<<"INSTALLED_ENABLED\n";
}
inline void cleanupBundleStage(const fs::path&game,const fs::path&stage){
 // Use the installed, bound metadata so retry does not depend on files already removed.
 gameReady(game);auto b=bundle(game/L"GEHD");checkBundleStage(game,stage,b);auto s=installation(game);need(s.release==b.release,"STAGE_RELEASE_NOT_INSTALLED");checkDeployment(s);checkOwner(s,s.data,s.dataId);verify(package(s.data));
 auto stagedData=stage/L"GEHD/data";
 if(fs::exists(stagedData)){
  auto p=package(cachedRelease(s));need(p.release==b.release,"STAGE_DATA_RELEASE");auto files=allPackageFiles(p);
  for(auto&f:files){auto path=stagedData/f.relative;ordinary(path);if(fs::exists(path))need(fileHash(path)==f.hash,"STAGE_DATA_CHANGED");}
  for(auto&f:files){auto path=stagedData/f.relative;if(fs::exists(path))need(DeleteFileW(path.c_str()),"STAGE_DATA_CLEANUP");checkpoint("install-cleanup-file");}
 }
 for(auto&f:b.files){auto p=stage/L"GEHD"/f.relative;if(fs::exists(p)){need(fileHash(p)==f.hash,"STAGE_SHELL_CHANGED");need(DeleteFileW(p.c_str()),"STAGE_SHELL_CLEANUP");}checkpoint("install-cleanup-shell");}
 if(fs::exists(stage/L"d3d9.dll")){need(fileHash(stage/L"d3d9.dll")==b.dllHash,"STAGE_PROXY_CHANGED");need(DeleteFileW((stage/L"d3d9.dll").c_str()),"STAGE_PROXY_CLEANUP");}
 for(auto name:{L"shell.files",L"bundle.identity"}){auto path=stage/L"GEHD"/name;if(fs::exists(path)){need(fileHash(path)==fileHash(b.root/name),"STAGE_METADATA_CHANGED");need(DeleteFileW(path.c_str()),"STAGE_METADATA_CLEANUP");}checkpoint("install-cleanup-metadata");}
 need(DeleteFileW((stage/L".gehd-stage").c_str()),"STAGE_MARKER_CLEANUP");
 std::error_code ec;for(auto name:{L"GEHD/data/2x",L"GEHD/data/4x",L"GEHD/data",L"GEHD/bin",L"GEHD/LICENSES",L"GEHD"}){auto p=stage/name;ordinary(p);if(fs::is_directory(p)){fs::remove(p,ec);ec.clear();}}fs::remove(stage,ec);
 std::cout<<"STAGE_CLEANED_UNKNOWN_FILES_RETAINED\n";
}
inline void uninstallBundle(Installation&s){
 need(!managerRunning(),"CLOSE_MANAGER_FIRST");
 auto rows=lines(stateRoot(s.game)/L"shell-owner.tsv",65536);need(rows.size()>4&&rows[0]=="GEHD_SHELL_OWNER_V1"&&rows[1]==s.token&&digestText(rows[2])&&rows[3]==fileId(s.game/L"GEHD"),"SHELL_OWNERSHIP");
 struct Owned{std::string relative,id,hash;};std::vector<Owned>files;std::set<std::string>seen;
 for(size_t i=4;i<rows.size();++i){std::istringstream row(rows[i]);Owned f;std::string tail;need(bool(row>>f.relative>>f.id>>f.hash)&&!(row>>tail)&&(shellRelative(f.relative)||f.relative=="bundle.identity"||f.relative=="shell.files")&&seen.insert(f.relative).second&&digestText(f.hash),"SHELL_OWNER_ENTRY");ordinary(s.game/L"GEHD"/f.relative);files.push_back(f);}
 uninstall(s);
 for(auto&f:files){auto p=s.game/L"GEHD"/f.relative;if(fs::exists(p)){
  if(fileId(p)==f.id&&fileHash(p)==f.hash)need(DeleteFileW(p.c_str()),"SHELL_REMOVE_PENDING");
  else std::cout<<"MODIFIED_SHELL_FILE_RETAINED "<<f.relative<<'\n';
 }}
 std::cout<<"BUNDLE_UNINSTALLED_RECEIPTS_RETAINED\n";
}
