#define GEHD_PACK_FIXTURE
#include "pack_loader.cpp"
#include <iostream>
int wmain(int argc,wchar_t**argv){try{
 if(argc!=3)throw std::runtime_error("corpus output");unsigned checks=0;
 auto require=[&](bool good,const char*message){if(!good)throw std::runtime_error(message);++checks;};
 auto corpus=std::filesystem::path(argv[1]),out=std::filesystem::path(argv[2]);
 require(!std::filesystem::exists(out),"fresh fixture required");std::filesystem::create_directories(out);
 auto map=read(corpus/L"mapping.tsv",32*1024*1024);require(!map.empty(),"mapping fixture");
 {std::ofstream f(out/L"mapping.tsv",std::ios::binary);f.write((const char*)map.data(),map.size());}
 root=out;auto mapHash=hash(map.data(),map.size());auto release=hash("release",7);
 auto identity=[&](const char*game,const char*exe,const std::string&mapping){std::ofstream f(out/L"pack.identity",std::ios::binary);f<<"GEHD_PRODUCT_V1\n"<<game<<'\n'<<exe<<'\n'<<mapping<<'\n'<<release<<'\n';};
 for(auto g:gameprofile::registry){
  identity(g->id,g->executableHash,mapHash);require(loadProductManifest()==(g==&gameprofile::selected),"only compiled game accepts package");
  identity(g->id,gameprofile::selected.executableHash,mapHash);require(loadProductManifest()==(g==&gameprofile::selected),"game id cannot borrow exe identity");
 }
 identity("UNKNOWN",exeHash,mapHash);require(!loadProductManifest(),"unknown game rejected");
 identity(gameprofile::selected.id,exeHash,std::string(64,'A'));require(!loadProductManifest(),"changed mapping rejected");
 identity(gameprofile::selected.id,std::string(64,'0').c_str(),mapHash);require(!loadProductManifest(),"wrong exe rejected");
 require(gameprofile::find("GER")==&gameprofile::ger&&gameprofile::find("GE2RB")==&gameprofile::ge2rb&&!gameprofile::find("ger"),"exact registry");
 require(gameprofile::ger.textureIat==0x207b02c&&gameprofile::ge2rb.textureIat==0x209f02c,"pinned entrypoints");
 auto a=loading::bindingKey(exeHash,mapHash,release,release,gameprofile::ge2rb),b=loading::bindingKey(exeHash,mapHash,release,release,gameprofile::ger);
 require(a!=b,"profiles isolate game even with identical other fields");
 auto variant=out/L"variant";std::filesystem::create_directories(variant);auto ger=loading::variantStamp(variant,gameprofile::ger);
 std::filesystem::create_directories(variant/L"CE_DATA/GE2");{std::ofstream f(variant/L"CE_DATA/GE2/GECV_STR.bin");f<<"GE2-only";}
 require(ger==loading::variantStamp(variant,gameprofile::ger),"GER does not scan GE2 language paths");
 {std::ofstream f(variant/L"bin_patch.qpck");f<<"GER update";}
 require(ger!=loading::variantStamp(variant,gameprofile::ger),"GER archive version invalidation");
 auto optionsDir=out/L"options";std::filesystem::create_directories(optionsDir/L"GEHD/state");require(loadLoadingOptions(optionsDir).mode=="sync","conservative default loading options");
 auto configPath=optionsDir/L"GEHD/state/loading-options.tsv";
 auto writeOptions=[&](const char*game,const char*mode){std::ofstream f(configPath);f<<"GEHD_LOADING_OPTIONS_V1\n"<<game<<"\n"<<std::string(32,'A')<<'\n'<<mode<<'\n'<<std::string(32,'B')<<'\n';};
 for(auto mode:{"sync","workers","learned"}){writeOptions(gameprofile::selected.id,mode);auto options=loadLoadingOptions(optionsDir);require(options.mode==mode&&options.epoch==std::string(32,'B'),"per-install mode and reset epoch");}
 for(auto game:{"UNKNOWN",gameprofile::selected.id}){writeOptions(game,game==gameprofile::selected.id?"invalid":"learned");bool rejected=false;try{loadLoadingOptions(optionsDir);}catch(...){rejected=true;}require(rejected,"invalid options fail closed to init synchronous fallback");}
 std::cout<<"{\"passed\":true,\"game\":\""<<gameprofile::selected.id<<"\",\"checks\":"<<checks<<"}\n";return 0;
 }catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
