#pragma once
// Included inside product after ownShell. Tool updates run from the installer's
// temporary controller so the installed controller/manager can be replaced.
inline std::vector<File> shellFiles(const Bundle&b){auto files=b.files;files.push_back({"bundle.identity",b.id,fs::file_size(b.root/L"bundle.identity")});files.push_back({"shell.files",b.shellHash,fs::file_size(b.root/L"shell.files")});return files;}
inline void checkShellOwner(const Installation&s){
 auto b=bundle(s.game/L"GEHD");auto rows=lines(stateRoot(s.game)/L"shell-owner.tsv",65536);
 need(rows.size()==shellFiles(b).size()+4&&rows[0]=="GEHD_SHELL_OWNER_V1"&&rows[1]==s.token&&rows[2]==b.id&&rows[3]==fileId(s.game/L"GEHD"),"SHELL_OWNERSHIP");
 std::set<std::string>seen;for(size_t i=4;i<rows.size();++i){std::istringstream row(rows[i]);std::string name,id,hash,tail;need(bool(row>>name>>id>>hash)&&!(row>>tail)&&(shellRelative(name)||name=="bundle.identity"||name=="shell.files")&&seen.insert(name).second,"SHELL_OWNER_ENTRY");exactOrMissing(s.game/L"GEHD"/name,id,hash);}
 verifyShell(b,s.game/L"GEHD");
}
inline void publishFreshShell(const fs::path&game,const Bundle&b){
 for(auto&f:shellFiles(b))copyVerified(b.root/f.relative,game/L"GEHD"/f.relative,f.hash);
}
struct ShellUpdate {
 std::string id,phase="STAGING",stageId,newBundle,oldBundle,oldDll,newDll,oldMode,oldRelease,maintenanceId="-",planHash;
 fs::path stage,folder;
 std::vector<std::string>record(const Installation&s)const{return {"GEHD_BUNDLE_UPDATE_V1",s.token,id,phase,utf8(stage.wstring()),stageId,newBundle,oldBundle,oldDll,newDll,oldMode,oldRelease,maintenanceId,planHash};}
};
inline fs::path shellJournal(const fs::path&game){return stateRoot(game)/L"bundle-update.tsv";}
inline bool shellDone(const ShellUpdate&u){return u.phase=="COMPLETE"||u.phase=="ROLLED_BACK";}
inline bool pendingShell(const fs::path&game){if(!fs::exists(shellJournal(game)))return false;auto v=lines(shellJournal(game),65536);need(v.size()==14&&v[0]=="GEHD_BUNDLE_UPDATE_V1","BUNDLE_UPDATE_RECORD");return v[3]!="COMPLETE"&&v[3]!="ROLLED_BACK";}
inline void saveShellUpdate(const Installation&s,const ShellUpdate&u){saveLines(shellJournal(s.game),u.record(s));}
inline ShellUpdate readShellUpdate(const Installation&s){
 auto v=lines(shellJournal(s.game),65536);need(v.size()==14&&v[0]=="GEHD_BUNDLE_UPDATE_V1"&&v[1]==s.token&&tokenText(v[2]),"BUNDLE_UPDATE_RECORD");
 ShellUpdate u;u.id=v[2];u.phase=v[3];u.stage=product::absolute(wide(v[4]));u.stageId=v[5];u.newBundle=v[6];u.oldBundle=v[7];u.oldDll=v[8];u.newDll=v[9];u.oldMode=v[10];u.oldRelease=v[11];u.maintenanceId=v[12];u.planHash=v[13];u.folder=stateRoot(s.game)/(L"bundle-"+wide(u.id));
 need(digestText(u.newBundle)&&digestText(u.oldBundle)&&digestText(u.oldDll)&&digestText(u.newDll)&&digestText(u.oldRelease)&&digestText(u.planHash)&&(u.oldMode=="ENABLED"||u.oldMode=="DISABLED")&&(u.maintenanceId=="-"||tokenText(u.maintenanceId)),"BUNDLE_UPDATE_FIELDS");
 need(samePath(u.stage.parent_path(),s.game),"BUNDLE_UPDATE_STAGE_SCOPE");ordinary(u.folder);return u;
}
inline void checkPendingShell(const fs::path&game,const Bundle&b){auto s=installation(game);auto u=readShellUpdate(s);need(u.phase!="ROLLING_BACK","RESUME_BUNDLE_ROLLBACK_FIRST");need(u.newBundle==b.id,"RESUME_ORIGINAL_BUNDLE_FIRST");}
inline fs::path pendingShellStage(const fs::path&game,const Bundle&b){auto s=installation(game);auto u=readShellUpdate(s);need(u.newBundle==b.id&&fileId(u.stage)==u.stageId,"SHELL_SOURCE_STAGE_ID");checkBundleStage(game,u.stage,b);return u.stage;}
inline std::vector<Change> shellChanges(const ShellUpdate&u){
 auto path=u.folder/(u.phase=="STAGING"?L"changes.tsv":L"ready.tsv");need(fileHash(path)==u.planHash,"SHELL_PLAN_HASH");auto rows=lines(path,65536);need(rows.size()>1&&rows[0]=="GEHD_CHANGES_V1","SHELL_PLAN");std::vector<Change>out;std::set<std::string>seen;
 for(size_t i=1;i<rows.size();++i){std::istringstream row(rows[i]);Change c;std::string tail;need(bool(row>>c.relative>>c.beforeHash>>c.beforeId>>c.afterHash>>c.afterId>>c.bytes)&&!(row>>tail)&&(shellRelative(c.relative)||c.relative=="bundle.identity"||c.relative=="shell.files")&&seen.insert(c.relative).second&&(digestText(c.beforeHash)||(c.relative=="LICENSES/OpenSSL.txt"&&c.beforeHash=="-"&&c.beforeId=="-"))&&digestText(c.afterHash),"SHELL_CHANGE");out.push_back(c);}return out;
}
inline void outsideInstalledController(const fs::path&game){wchar_t exe[32768];DWORD n=GetModuleFileNameW(nullptr,exe,32768);need(n&&n<32768,"CONTROLLER_PATH");need(!samePath(fs::path(std::wstring(exe,n)),game/L"GEHD/bin/pack-control.exe"),"RUN_INSTALLER_TO_RESUME_TOOLS");need(!managerRunning(),"CLOSE_MANAGER_FIRST");}
inline void rollbackShellUpdate(Installation&s);
inline void resumeShellUpdate(Installation&s){
 gameReady(s.game);outsideInstalledController(s.game);auto u=readShellUpdate(s);if(shellDone(u)){std::cout<<u.phase<<'\n';return;}
 if(u.phase=="ROLLING_BACK"){rollbackShellUpdate(s);return;}
 auto b=bundle(u.folder/L"new-bundle");need(b.id==u.newBundle,"SHELL_NEW_BUNDLE");auto changes=shellChanges(u);
 if(u.phase=="STAGING"){
  checkShellOwner(s);checkDeployment(s);need(fileId(u.stage)==u.stageId,"SHELL_SOURCE_STAGE_ID");checkBundleStage(s.game,u.stage,b);verifyShell(b,u.stage/L"GEHD");
  need(availableSpace(u.folder)>=16*1024*1024+2*1024*1024,"INSUFFICIENT_SPACE");
  for(auto&c:changes){copyVerified(u.stage/L"GEHD"/c.relative,u.folder/L"payload"/c.relative,c.afterHash);c.afterId=fileId(u.folder/L"payload"/c.relative);checkpoint("bundle-copy");}
  writeChanges(u.folder/L"ready.tsv",changes);u.planHash=fileHash(u.folder/L"ready.tsv");u.phase="DATA";saveShellUpdate(s,u);checkpoint("bundle-prepared");
 }
 if(u.phase=="DATA"){
  auto source=u.stage/L"GEHD/data";need(package(source).release==b.release,"BUNDLE_DATA_SOURCE");
  bool matched=false;
  if(fs::exists(stateRoot(s.game)/L"maintenance.tsv")){auto m=readMaintenance(s);matched=samePath(m.source,source)&&m.newRelease==b.release&&m.phase!="ROLLED_BACK";
   if(matched){u.maintenanceId=m.id;saveShellUpdate(s,u);resumeMaintenance(s);}
  }
  if(!matched){beginMaintenance(s,source,false);if(fs::exists(stateRoot(s.game)/L"maintenance.tsv")){auto m=readMaintenance(s);if(samePath(m.source,source)&&m.newRelease==b.release)u.maintenanceId=m.id;}}
  s=installation(s.game);need(s.release==b.release,"BUNDLE_DATA_RESULT");u.phase="PREPARED";saveShellUpdate(s,u);checkpoint("bundle-data-complete");
 }
 if(u.phase=="PREPARED"){
  checkShellOwner(s);for(auto&c:changes){exactOrMissing(s.game/L"GEHD"/c.relative,c.beforeId,c.beforeHash);exactOrMissing(u.folder/L"payload"/c.relative,c.afterId,c.afterHash);}
  disable(s);u.phase="SHELL";saveShellUpdate(s,u);checkpoint("bundle-disabled");
 }
 if(u.phase=="SHELL"){
  need(s.mode=="DISABLED","BUNDLE_ACTIVE_DURING_UPDATE");checkDeployment(s);
  for(auto&c:changes){auto active=s.game/L"GEHD"/c.relative,backup=u.folder/L"backup"/c.relative,payload=u.folder/L"payload"/c.relative;ordinary(active);ordinary(backup);ordinary(payload);
   if(fs::exists(active)&&fileId(active)==c.afterId){exactOrMissing(active,c.afterId,c.afterHash);exactOrMissing(backup,c.beforeId,c.beforeHash);continue;}
   if(c.beforeId=="-"){need(!fs::exists(active)&&!fs::exists(backup),"SHELL_NEW_TARGET_CONFLICT");}
   else if(fs::exists(backup)){exactOrMissing(backup,c.beforeId,c.beforeHash);need(!fs::exists(active),"SHELL_TARGET_CONFLICT");}
   else{exactOrMissing(active,c.beforeId,c.beforeHash);fs::create_directories(backup.parent_path());need(MoveFileExW(active.c_str(),backup.c_str(),MOVEFILE_WRITE_THROUGH),"SHELL_BACKUP");checkpoint("bundle-backup");}
   exactOrMissing(payload,c.afterId,c.afterHash);fs::create_directories(active.parent_path());need(MoveFileExW(payload.c_str(),active.c_str(),MOVEFILE_WRITE_THROUGH),"SHELL_PUBLISH");checkpoint("bundle-file");if(c.relative=="LICENSES/OpenSSL.txt")checkpoint("bundle-license-file");
  }
  verifyShell(b,s.game/L"GEHD");need(bundle(s.game/L"GEHD").id==u.newBundle,"SHELL_PUBLISHED_BUNDLE");s.dllHash=u.newDll;saveInstall(s);u.phase="ENABLING";saveShellUpdate(s,u);checkpoint("bundle-receipt");
 }
 if(u.phase=="ENABLING"){
  ownShell(s,b);if(u.oldMode=="ENABLED"){disable(s);enable(s);}checkpoint("bundle-enabled");u.phase="COMPLETE";saveShellUpdate(s,u);
 }
 need(u.phase=="COMPLETE","BUNDLE_UPDATE_PHASE");std::cout<<"BUNDLE_UPDATE_COMPLETE_BACKUP_RETAINED\n";
}
inline void beginShellUpdate(Installation&s,const fs::path&stage,const Bundle&b){
 outsideInstalledController(s.game);checkShellOwner(s);checkDeployment(s);auto old=bundle(s.game/L"GEHD");verify(package(s.data));verify(package(stage/L"GEHD/data"));
 ShellUpdate u;u.id=randomId();u.stage=stage;u.stageId=fileId(stage);u.newBundle=b.id;u.oldBundle=old.id;u.oldDll=s.dllHash;u.newDll=b.dllHash;u.oldMode=s.mode;u.oldRelease=s.release;u.folder=stateRoot(s.game)/(L"bundle-"+wide(u.id));
 std::set<std::string>oldNames;for(auto&f:shellFiles(old))oldNames.insert(f.relative);
 std::vector<Change>changes;for(auto&f:shellFiles(b)){auto active=s.game/L"GEHD"/f.relative;Change c;c.relative=f.relative;c.afterHash=f.hash;c.bytes=f.bytes;
  if(!oldNames.count(f.relative)){need(f.relative=="LICENSES/OpenSSL.txt"&&!fs::exists(active),"SHELL_NEW_TARGET_CONFLICT");changes.push_back(c);continue;}
  need(fs::exists(active),"REPAIR_OLD_SHELL_FIRST");auto hash=fileHash(active);if(hash==f.hash)continue;c.beforeHash=hash;c.beforeId=fileId(active);changes.push_back(c);}
 need(!changes.empty(),"EMPTY_BUNDLE_UPDATE");fs::create_directories(u.folder/L"new-bundle");fs::create_directories(u.folder/L"old-bundle");
 for(auto name:{L"bundle.identity",L"shell.files"}){need(CopyFileW((b.root/name).c_str(),(u.folder/L"new-bundle"/name).c_str(),TRUE),"CACHE_NEW_BUNDLE");need(CopyFileW((old.root/name).c_str(),(u.folder/L"old-bundle"/name).c_str(),TRUE),"CACHE_OLD_BUNDLE");}
 writeChanges(u.folder/L"changes.tsv",changes);u.planHash=fileHash(u.folder/L"changes.tsv");saveShellUpdate(s,u);checkpoint("bundle-recorded");resumeShellUpdate(s);
}
inline void rollbackShellUpdate(Installation&s){
 gameReady(s.game);outsideInstalledController(s.game);auto u=readShellUpdate(s);if(u.phase=="ROLLED_BACK"){std::cout<<"BUNDLE_ALREADY_ROLLED_BACK\n";return;}
 auto old=bundle(u.folder/L"old-bundle");need(old.id==u.oldBundle,"SHELL_OLD_BUNDLE");
 bool rollbackData=false;
 if(fs::exists(stateRoot(s.game)/L"maintenance.tsv")){
  auto m=readMaintenance(s);bool matches=samePath(m.source,u.stage/L"GEHD/data")&&m.oldRelease==u.oldRelease;
  if(u.maintenanceId!="-")need(m.id==u.maintenanceId&&matches,"BUNDLE_DATA_MAINTENANCE_CHANGED");
  if(matches){need(samePath(m.data,s.data),"ROLLBACK_LOCATION_CHANGED");rollbackData=true;u.maintenanceId=m.id;}
 }else need(u.maintenanceId=="-","BUNDLE_DATA_MAINTENANCE_MISSING");
 if(!rollbackData)need(s.release==u.oldRelease,"BUNDLE_DATA_CHANGED");
 if(u.phase=="STAGING"){u.phase="ROLLED_BACK";saveShellUpdate(s,u);std::cout<<"UNAPPLIED_BUNDLE_CANCELLED\n";return;}
 auto changes=shellChanges(u);
 if(u.phase!="ROLLING_BACK"){disable(s);u.phase="ROLLING_BACK";saveShellUpdate(s,u);checkpoint("bundle-rollback-disabled");}
 for(auto it=changes.rbegin();it!=changes.rend();++it){auto&c=*it;auto active=s.game/L"GEHD"/c.relative,backup=u.folder/L"backup"/c.relative;ordinary(active);ordinary(backup);
  if(fs::exists(active)&&fileId(active)==c.beforeId){exactOrMissing(active,c.beforeId,c.beforeHash);continue;}
  if(fs::exists(active)){exactOrMissing(active,c.afterId,c.afterHash);auto reverted=u.folder/L"reverted"/c.relative;need(!fs::exists(reverted),"SHELL_ROLLBACK_CONFLICT");fs::create_directories(reverted.parent_path());need(MoveFileExW(active.c_str(),reverted.c_str(),MOVEFILE_WRITE_THROUGH),"SHELL_ROLLBACK_SAVE");checkpoint("bundle-rollback-candidate");if(c.relative=="LICENSES/OpenSSL.txt")checkpoint("bundle-rollback-license");}
  if(c.beforeId!="-"){exactOrMissing(backup,c.beforeId,c.beforeHash);need(MoveFileExW(backup.c_str(),active.c_str(),MOVEFILE_WRITE_THROUGH),"SHELL_ROLLBACK_RESTORE");}else need(!fs::exists(backup),"SHELL_NEW_BACKUP_CONFLICT");checkpoint("bundle-rollback-file");
 }
 verifyShell(old,s.game/L"GEHD");need(bundle(s.game/L"GEHD").id==old.id,"SHELL_ROLLBACK_BUNDLE");s.dllHash=u.oldDll;saveInstall(s);
 if(rollbackData)rollbackMaintenance(s);
 need(s.release==u.oldRelease,"BUNDLE_ROLLBACK_RELEASE");verify(package(s.data));ownShell(s,old);
 if(u.oldMode=="ENABLED"){disable(s);enable(s);}else if(s.mode!="DISABLED")disable(s);
 u.phase="ROLLED_BACK";saveShellUpdate(s,u);std::cout<<"BUNDLE_ROLLED_BACK_CANDIDATE_RETAINED\n";
}
