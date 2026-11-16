#pragma once
// Included inside product after Bundle. A legacy receipt can authorize only
// its two proven root hardlinks; the old texture tree remains untouched.
struct Legacy {fs::path root;std::vector<std::string> record;};
inline void legacyRecord(const fs::path&game,const Legacy&old){
 auto&v=old.record;need(v.size()==8&&v[0]=="GEHD_NATIVE_DEPLOY_V1"&&(v[1]=="ENABLED"||v[1]=="PREPARED")&&samePath(wide(v[2]),game),"LEGACY_RECEIPT_SCOPE");
 need(v[3].rfind("stage-",0)==0&&v[3].size()<100&&v[3].find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789-")==v[3].npos&&digestText(v[4])&&digestText(v[5]),"LEGACY_RECEIPT_FIELDS");
 ordinary(old.root);need(!nested(old.root,game)&&!nested(game,old.root),"LEGACY_SOURCE_OVERLAP");
 for(size_t i=0;i<2;++i){auto name=i?L"gehd-pack-path.txt":L"d3d9.dll";auto saved=old.root/v[3]/name;
  need(fileId(saved)==v[6+i]&&fileHash(saved)==v[4+i],"LEGACY_STAGE_OWNERSHIP");
  checkOwnedFile(game/name,v[6+i],v[4+i]);
 }
 auto config=lines(old.root/v[3]/L"gehd-pack-path.txt",65536);need(config.size()==1&&samePath(wide(config[0]),old.root),"LEGACY_CONFIG_SCOPE");
}
inline Legacy legacyProbe(const fs::path&game){
 auto config=lines(game/L"gehd-pack-path.txt",65536);need(config.size()==1,"EXISTING_CONFIG_NOT_ADOPTED");
 Legacy old;old.root=product::absolute(wide(config[0]));ordinary(old.root);
 need(fs::exists(old.root/L"deployment-native.txt"),"EXISTING_CONFIG_NOT_ADOPTED");
 old.record=lines(old.root/L"deployment-native.txt",65536);legacyRecord(game,old);
 need(fs::exists(game/L"d3d9.dll")&&fs::exists(game/L"gehd-pack-path.txt"),"RECOVER_LEGACY_DEPLOYMENT_FIRST");return old;
}
inline fs::path legacyJournal(const fs::path&game){return stateRoot(game)/L"legacy-migration.tsv";}
inline Legacy legacyPending(const fs::path&game,const Bundle&b){
 auto v=lines(legacyJournal(game),65536);need(v.size()==7&&v[0]=="GEHD_LEGACY_MIGRATION_V1"&&samePath(wide(v[1]),game)&&v[3]==b.id&&digestText(v[5]),"LEGACY_MIGRATION_BINDING");
 need(v[6]=="PREPARED"||v[6]=="DISABLED"||v[6]=="COMPLETE","LEGACY_MIGRATION_PHASE");
 Legacy old;old.root=product::absolute(wide(v[2]));need(fileId(old.root)==v[4],"LEGACY_DIRECTORY_IDENTITY");
 auto original=stateRoot(game)/L"legacy-original.tsv";need(fileHash(original)==v[5],"LEGACY_ORIGINAL_RECEIPT");old.record=lines(original,65536);
 // Once complete, the current root links belong to the new installation.
 if(v[6]!="COMPLETE")legacyRecord(game,old);
 auto current=lines(old.root/L"deployment-native.txt",65536),disabled=old.record;disabled[1]="DISABLED";
 need(current==old.record||current==disabled,"LEGACY_RECEIPT_CHANGED");return old;
}
inline bool legacyIncomplete(const fs::path&game){if(!fs::exists(legacyJournal(game)))return false;auto v=lines(legacyJournal(game),65536);need(v.size()==7,"LEGACY_MIGRATION_BINDING");return v[6]!="COMPLETE";}
inline void retireLegacy(const fs::path&game,const Bundle&b){
 if(!fs::exists(legacyJournal(game))){
  auto old=legacyProbe(game);auto original=stateRoot(game)/L"legacy-original.tsv";
  if(fs::exists(original))need(lines(original,65536)==old.record,"LEGACY_ORIGINAL_CONFLICT");else need(CopyFileW((old.root/L"deployment-native.txt").c_str(),original.c_str(),TRUE),"LEGACY_RECEIPT_BACKUP");
  saveLines(legacyJournal(game),{"GEHD_LEGACY_MIGRATION_V1",utf8(game.wstring()),utf8(old.root.wstring()),b.id,fileId(old.root),fileHash(original),"PREPARED"});checkpoint("legacy-recorded");
 }
 auto old=legacyPending(game,b);auto v=lines(legacyJournal(game),65536);if(v[6]=="COMPLETE")return;
 if(v[6]=="PREPARED"){
  for(auto name:{L"d3d9.dll",L"gehd-pack-path.txt"}){auto p=game/name;if(fs::exists(p))need(DeleteFileW(p.c_str()),"LEGACY_REMOVE_PENDING");checkpoint(name==std::wstring(L"d3d9.dll")?"legacy-dll-removed":"legacy-config-removed");}
  auto disabled=old.record;disabled[1]="DISABLED";saveLines(old.root/L"deployment-native.txt",disabled);checkpoint("legacy-receipt-disabled");
  v[6]="DISABLED";saveLines(legacyJournal(game),v);
 }
 need(!fs::exists(game/L"d3d9.dll")&&!fs::exists(game/L"gehd-pack-path.txt"),"LEGACY_NEW_ROOT_CONFLICT");
 // Commit retirement before adoption: the new deployment has its own journal.
 v[6]="COMPLETE";saveLines(legacyJournal(game),v);checkpoint("legacy-retired");
}
