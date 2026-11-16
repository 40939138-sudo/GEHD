#pragma once
// Included inside namespace product, after the deployment primitives.
struct Change {
 std::string relative,beforeHash="-",beforeId="-",afterHash="-",afterId="-";
 uint64_t bytes=0;
};
struct Maintenance {
 std::string id,kind,phase="STAGING",oldRelease,newRelease,oldMode,sourceId,stageId="-",planHash;
 fs::path source,data,stage,folder;
 std::vector<std::string> record(const Installation&s)const {
  return {"GEHD_MAINTENANCE_V1",s.token,id,kind,phase,oldRelease,newRelease,oldMode,
   utf8(source.wstring()),sourceId,utf8(data.wstring()),utf8(stage.wstring()),stageId,planHash};
 }
};
inline bool maintenanceRelative(const std::string&s){return relativeAllowed(s)||s=="release.files"||s=="pack.identity";}
inline void saveMaintenance(const Installation&s,const Maintenance&m){saveLines(stateRoot(s.game)/L"maintenance.tsv",m.record(s));}
inline Maintenance readMaintenance(const Installation&s){
 auto v=lines(stateRoot(s.game)/L"maintenance.tsv",16384);
 need(v.size()==14&&v[0]=="GEHD_MAINTENANCE_V1"&&v[1]==s.token&&tokenText(v[2]),"MAINTENANCE_RECORD");
 Maintenance m;m.id=v[2];m.kind=v[3];m.phase=v[4];m.oldRelease=v[5];m.newRelease=v[6];m.oldMode=v[7];
 m.source=product::absolute(wide(v[8]));m.sourceId=v[9];m.data=product::absolute(wide(v[10]));m.stage=product::absolute(wide(v[11]));m.stageId=v[12];m.planHash=v[13];
 m.folder=stateRoot(s.game)/(L"maintenance-"+wide(m.id));
 need((m.kind=="REPAIR"||m.kind=="UPDATE")&&digestText(m.oldRelease)&&digestText(m.newRelease)&&digestText(m.planHash),"MAINTENANCE_IDENTITY");
 need(m.oldMode=="ENABLED"||m.oldMode=="DISABLED","MAINTENANCE_PREVIOUS_MODE");
 need(samePath(m.stage,fs::path(m.data.wstring()+L".gehd-maint-"+wide(m.id))),"MAINTENANCE_SCOPE");
 dataLocation(s.game,m.data);ordinary(m.stage);ordinary(m.folder);return m;
}
inline bool maintenanceDone(const Maintenance&m){return m.phase=="COMPLETE"||m.phase=="ROLLED_BACK";}
inline void writeChanges(const fs::path&path,const std::vector<Change>&changes){
 std::vector<std::string>v{"GEHD_CHANGES_V1"};
 for(auto&c:changes)v.push_back(c.relative+'\t'+c.beforeHash+'\t'+c.beforeId+'\t'+c.afterHash+'\t'+c.afterId+'\t'+std::to_string(c.bytes));
 saveLines(path,v);
}
inline std::vector<Change> readChanges(const Maintenance&m){
 auto path=m.folder/(m.phase=="STAGING"?L"changes.tsv":L"ready.tsv");need(fileHash(path)==m.planHash,"MAINTENANCE_PLAN_HASH");
 auto rows=lines(path);need(rows.size()>1&&rows[0]=="GEHD_CHANGES_V1","MAINTENANCE_PLAN");
 std::set<std::string>seen;std::vector<Change>out;
 for(size_t i=1;i<rows.size();++i){
  std::istringstream row(rows[i]);Change c;std::string tail;
  need(bool(row>>c.relative>>c.beforeHash>>c.beforeId>>c.afterHash>>c.afterId>>c.bytes)&&!(row>>tail),"MAINTENANCE_CHANGE_ROW");
  need(maintenanceRelative(c.relative)&&seen.insert(c.relative).second&&(c.beforeHash=="-"||digestText(c.beforeHash))&&(c.afterHash=="-"||digestText(c.afterHash)),"MAINTENANCE_CHANGE_IDENTITY");
  need(c.beforeHash!="-"||c.afterHash!="-","MAINTENANCE_EMPTY_CHANGE");
  out.push_back(c);
 }
 return out;
}
inline std::vector<File> allPackageFiles(const Package&p){
 auto files=p.files;for(auto name:{"release.files","pack.identity"})files.push_back({name,fileHash(p.root/name),fs::file_size(p.root/name)});return files;
}
inline void exactOrMissing(const fs::path&p,const std::string&id,const std::string&hash){
 ordinary(p);if(id=="-")need(!fs::exists(p),"MAINTENANCE_NEW_FILE_CONFLICT");
 else need(fs::exists(p)&&fileId(p)==id&&fileHash(p)==hash,"MAINTENANCE_FILE_CHANGED");
}
inline void resumeMaintenance(Installation&s){
 gameReady(s.game);auto m=readMaintenance(s);
 if(maintenanceDone(m)){std::cout<<m.phase<<'\n';return;}
 need(samePath(m.data,s.data),"MAINTENANCE_ACTIVE_LOCATION");checkOwner(s,m.data,s.dataId);
 auto changes=readChanges(m);
 if(m.phase=="STAGING"){
  need(s.release==m.oldRelease,"MAINTENANCE_OLD_RELEASE");checkDeployment(s);
  auto source=package(m.source);need(source.release==m.newRelease&&fileId(m.source)==m.sourceId,"MAINTENANCE_MEDIA_CHANGED");
  if(m.stageId=="-"){
   if(!fs::exists(m.stage))need(fs::create_directory(m.stage),"MAINTENANCE_STAGE_CREATE");
   need(fs::is_directory(m.stage)&&fs::is_empty(m.stage),"MAINTENANCE_UNCLAIMED_STAGE");m.stageId=fileId(m.stage);saveMaintenance(s,m);
  }
  need(fileId(m.stage)==m.stageId,"MAINTENANCE_STAGE_IDENTITY");
  uint64_t missing=0;for(auto&c:changes)if(c.afterHash!="-"&&!fs::exists(m.stage/L"payload"/c.relative))missing+=c.bytes;
  need(availableSpace(m.stage)>=missing+16*1024*1024,"INSUFFICIENT_SPACE");
  for(auto&c:changes)if(c.afterHash!="-"){
   copyVerified(source.root/c.relative,m.stage/L"payload"/c.relative,c.afterHash);checkpoint("maintenance-copy");
  }
  // IDs are committed in a separate immutable ready plan. The original intent remains recoverable.
  auto ready=m.folder/L"ready.tsv";
  for(auto&c:changes)if(c.afterHash!="-")c.afterId=fileId(m.stage/L"payload"/c.relative);
  writeChanges(ready,changes);m.planHash=fileHash(ready);
  m.phase="PREPARED";
  // Publishing this record selects ready.tsv; no active data has changed yet.
  saveMaintenance(s,m);checkpoint("maintenance-prepared");
 }
 if(m.phase=="PREPARED"){
  for(auto&c:changes){exactOrMissing(m.data/c.relative,c.beforeId,c.beforeHash);if(c.afterHash!="-")exactOrMissing(m.stage/L"payload"/c.relative,c.afterId,c.afterHash);}
  disable(s);m.phase="APPLYING";saveMaintenance(s,m);checkpoint("maintenance-disabled");
 }
 if(m.phase=="APPLYING"){
  need(s.mode=="DISABLED"&&s.release==m.oldRelease,"MAINTENANCE_APPLY_STATE");checkDeployment(s);need(fileId(m.stage)==m.stageId,"MAINTENANCE_STAGE_IDENTITY");
  for(auto&c:changes){
   auto active=m.data/c.relative,backup=m.stage/L"backup"/c.relative,payload=m.stage/L"payload"/c.relative;
   ordinary(active);ordinary(backup);ordinary(payload);
   if(c.afterHash!="-"&&fs::exists(active)&&fileId(active)==c.afterId){exactOrMissing(active,c.afterId,c.afterHash);if(c.beforeId!="-")exactOrMissing(backup,c.beforeId,c.beforeHash);continue;}
   if(c.beforeId!="-"){
    if(fs::exists(backup)){exactOrMissing(backup,c.beforeId,c.beforeHash);need(!fs::exists(active),"MAINTENANCE_TARGET_CONFLICT");}
    else{exactOrMissing(active,c.beforeId,c.beforeHash);fs::create_directories(backup.parent_path());need(MoveFileExW(active.c_str(),backup.c_str(),MOVEFILE_WRITE_THROUGH),"MAINTENANCE_BACKUP");checkpoint("maintenance-backup");}
   }else need(!fs::exists(active)&&!fs::exists(backup),"MAINTENANCE_TARGET_CONFLICT");
   if(c.afterHash!="-"){
    exactOrMissing(payload,c.afterId,c.afterHash);fs::create_directories(active.parent_path());need(MoveFileExW(payload.c_str(),active.c_str(),MOVEFILE_WRITE_THROUGH),"MAINTENANCE_PUBLISH");
   }
   checkpoint("maintenance-file");
  }
  m.phase="APPLIED";saveMaintenance(s,m);checkpoint("maintenance-applied");
 }
 if(m.phase=="APPLIED"){
  auto p=package(m.data);need(p.release==m.newRelease,"MAINTENANCE_RESULT_RELEASE");verify(p);
  s.release=m.newRelease;cacheRelease(s,p);saveInstall(s);checkpoint("maintenance-receipt");
  m.phase="ENABLING";saveMaintenance(s,m);
 }
 if(m.phase=="ENABLING"){
  need(s.release==m.newRelease,"MAINTENANCE_NEW_RELEASE");
  if(m.oldMode=="ENABLED"){disable(s);enable(s);}else need(s.mode=="DISABLED","MAINTENANCE_DISABLED_STATE");
  checkpoint("maintenance-enabled");m.phase="COMPLETE";saveMaintenance(s,m);
 }
 need(m.phase=="COMPLETE","MAINTENANCE_RESUME_STATE");std::cout<<"MAINTENANCE_COMPLETE_BACKUP_RETAINED "<<utf8(m.stage.wstring())<<'\n';
}
inline void rollbackMaintenance(Installation&s){
 gameReady(s.game);auto m=readMaintenance(s);need(samePath(m.data,s.data),"ROLLBACK_LOCATION_CHANGED");checkOwner(s,m.data,s.dataId);checkDeployment(s);
 if(m.phase=="ROLLED_BACK"){std::cout<<"ALREADY_ROLLED_BACK\n";return;}
 if(m.phase=="STAGING"||m.phase=="PREPARED"){
  // No active data was changed; a PREPARED transaction may already have disabled deployment.
  if(m.oldMode=="ENABLED"&&s.mode=="DISABLED")enable(s);
  m.phase="ROLLED_BACK";saveMaintenance(s,m);std::cout<<"UNAPPLIED_MAINTENANCE_CANCELLED_STAGE_RETAINED\n";return;
 }
 auto changes=readChanges(m);need(fileId(m.stage)==m.stageId,"MAINTENANCE_STAGE_IDENTITY");
 if(m.phase!="ROLLING_BACK"){
  need(m.phase=="APPLYING"||m.phase=="APPLIED"||m.phase=="ENABLING"||m.phase=="COMPLETE","ROLLBACK_STATE");
  disable(s);m.phase="ROLLING_BACK";saveMaintenance(s,m);
 }
 for(auto it=changes.rbegin();it!=changes.rend();++it){
  auto&c=*it;auto active=m.data/c.relative,backup=m.stage/L"backup"/c.relative;
  ordinary(active);ordinary(backup);
  if(c.beforeId!="-"&&fs::exists(active)&&fileId(active)==c.beforeId){exactOrMissing(active,c.beforeId,c.beforeHash);continue;}
  if(fs::exists(active)){
   need(c.afterId!="-","ROLLBACK_UNOWNED_TARGET");exactOrMissing(active,c.afterId,c.afterHash);
   // Preserve the superseded candidate too; rollback never overwrites user changes.
   auto reverted=m.stage/L"reverted"/c.relative;ordinary(reverted);need(!fs::exists(reverted),"ROLLBACK_CANDIDATE_CONFLICT");fs::create_directories(reverted.parent_path());
   need(MoveFileExW(active.c_str(),reverted.c_str(),MOVEFILE_WRITE_THROUGH),"ROLLBACK_REMOVE_CANDIDATE");checkpoint("rollback-candidate");
  }
  if(c.beforeId!="-"){
   exactOrMissing(backup,c.beforeId,c.beforeHash);fs::create_directories(active.parent_path());need(MoveFileExW(backup.c_str(),active.c_str(),MOVEFILE_WRITE_THROUGH),"ROLLBACK_RESTORE");
  }
  checkpoint("rollback-file");
 }
 s.release=m.oldRelease;saveInstall(s);
 if(m.kind=="UPDATE"){
  auto restored=package(m.data);need(restored.release==m.oldRelease,"ROLLBACK_RELEASE");verify(restored);
  if(m.oldMode=="ENABLED"){disable(s);enable(s);}
 }else if(s.mode!="DISABLED")disable(s); // A repair rollback may deliberately restore corrupt input.
 m.phase="ROLLED_BACK";saveMaintenance(s,m);std::cout<<(m.kind=="UPDATE"?"UPDATE_ROLLED_BACK\n":"REPAIR_INPUT_RESTORED_DISABLED\n");
}
inline void beginMaintenance(Installation&s,const fs::path&sourcePath,bool repair){
 gameReady(s.game);checkDeployment(s);need(s.mode=="ENABLED"||s.mode=="DISABLED","RECOVER_DEPLOYMENT_FIRST");checkOwner(s,s.data,s.dataId);
 auto source=package(sourcePath);verify(source);need(!nested(s.data,source.root)&&!nested(source.root,s.data),"MAINTENANCE_MEDIA_OVERLAP");
 if(repair)need(source.release==s.release,"REPAIR_RELEASE_MISMATCH");
 auto old=package(cachedRelease(s));need(old.release==s.release,"MAINTENANCE_BASELINE");
 if(!repair){auto active=package(s.data);need(active.release==s.release,"UPDATE_BASELINE_CHANGED");verify(active);}
 auto next=allPackageFiles(source),previous=allPackageFiles(old);std::unordered_map<std::string,File>after;
 for(auto&f:next)after.emplace(f.relative,f);
 std::vector<Change>changes;
 for(auto&f:next){
  auto path=s.data/f.relative;ordinary(path);Change c;c.relative=f.relative;c.afterHash=f.hash;c.bytes=f.bytes;
  if(fs::exists(path)){need(fs::is_regular_file(path),"MAINTENANCE_TARGET_TYPE");c.beforeId=fileId(path);c.beforeHash=fileHash(path);if(c.beforeHash==f.hash)continue;}
  changes.push_back(c);
 }
 for(auto&f:previous)if(!after.count(f.relative)){
  auto path=s.data/f.relative;ordinary(path);if(!fs::exists(path))continue;
  if(!fs::is_regular_file(path)||fileHash(path)!=f.hash){std::cout<<"MODIFIED_FILE_RETAINED "<<utf8(path.wstring())<<'\n';continue;}
  Change c;c.relative=f.relative;c.beforeHash=f.hash;c.beforeId=fileId(path);changes.push_back(c);
 }
 if(changes.empty()){std::cout<<"DATA_ALREADY_VERIFIED\n";return;}
 Maintenance m;m.id=randomId();m.kind=repair?"REPAIR":"UPDATE";m.oldRelease=s.release;m.newRelease=source.release;m.oldMode=s.mode;m.source=source.root;m.sourceId=fileId(source.root);m.data=s.data;m.stage=fs::path(s.data.wstring()+L".gehd-maint-"+wide(m.id));m.folder=stateRoot(s.game)/(L"maintenance-"+wide(m.id));
 need(!fs::exists(m.stage)&&fs::create_directory(m.folder),"MAINTENANCE_FRESH_TRANSACTION");writeChanges(m.folder/L"changes.tsv",changes);m.planHash=fileHash(m.folder/L"changes.tsv");saveMaintenance(s,m);checkpoint("maintenance-recorded");resumeMaintenance(s);
}
