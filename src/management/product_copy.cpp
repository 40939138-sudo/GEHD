// Streaming, resumable materialization of a builder-bound DDS copy plan.
#include "product_files.hpp"
#include <iostream>
#include <chrono>
int wmain(int argc,wchar_t**argv){try{
 using namespace product;
 need(argc==4,"Usage: product-copy source data copy-plan.tsv");auto source=product::absolute(argv[1]),dest=product::absolute(argv[2]);ordinary(source);ordinary(dest);
 auto rows=lines(argv[3]);need(rows.size()>1&&rows[0]=="GEHD_COPY_PLAN_V1","COPY_PLAN");
 std::vector<File>files;std::set<std::string>seen;
 for(size_t i=1;i<rows.size();++i){std::istringstream row(rows[i]);File f;std::string tail;need(bool(row>>f.relative>>f.bytes>>f.hash)&&!(row>>tail),"COPY_PLAN_ROW");need(relativeAllowed(f.relative)&&f.relative!="mapping.tsv"&&digestText(f.hash)&&seen.insert(f.relative).second&&f.bytes>0&&f.bytes<=256ull*1024*1024,"COPY_PLAN_ENTRY");files.push_back(f);}
 auto sourceText=source.wstring()+L"\\",destText=dest.wstring()+L"\\";
 need(_wcsicmp(source.c_str(),dest.c_str())!=0&&!(destText.size()>=sourceText.size()&&_wcsnicmp(sourceText.c_str(),destText.c_str(),sourceText.size())==0),"COPY_SOURCE_DESTINATION_OVERLAP");
 for(auto name:{L"2x",L"4x"}){ordinary(dest/name);fs::create_directories(dest/name);}
 size_t reused=0,copied=0;uint64_t bytes=0;auto start=std::chrono::steady_clock::now();
 for(auto&f:files){
  auto src=source/f.relative,out=dest/f.relative;ordinary(src);ordinary(out);need(fs::is_regular_file(src)&&fs::file_size(src)==f.bytes,"SOURCE_SIZE");
  if(fs::exists(out)&&fs::is_regular_file(out)&&fs::file_size(out)==f.bytes&&fileHash(out)==f.hash){++reused;bytes+=f.bytes;continue;}
  auto suffix=L"."+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64());
  if(fs::exists(out)){
   need(fs::is_regular_file(out),"COPY_DESTINATION_TYPE");auto retained=out;retained+=suffix+L".interrupted";
   need(product::MoveFileExW(out.c_str(),retained.c_str(),MOVEFILE_WRITE_THROUGH),"PRESERVE_INTERRUPTED_FILE");
  }
  auto part=out;part+=suffix+L".copying";
  need(product::CopyFileW(src.c_str(),part.c_str(),TRUE),"COPY_FAILED");need(fs::file_size(part)==f.bytes&&fileHash(part)==f.hash,"COPY_HASH_MISMATCH");
  need(product::MoveFileExW(part.c_str(),out.c_str(),0),"PUBLISH_COPY");++copied;bytes+=f.bytes;
  if((copied+reused)%256==0)std::cerr<<"VERIFIED "<<(copied+reused)<<'/'<<files.size()<<" reused="<<reused<<'\n';
 }
 double seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
 std::cout<<"{\"schema\":\"GEHD_NATIVE_COPY_RESULT_V1\",\"files\":"<<files.size()<<",\"copied\":"<<copied<<",\"reused\":"<<reused<<",\"bytes\":"<<bytes<<",\"seconds\":"<<seconds<<"}\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
