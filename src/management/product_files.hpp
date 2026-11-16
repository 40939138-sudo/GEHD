#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <set>
#include <sstream>
#include <stdexcept>
#include <functional>
#include <unordered_map>
#include "game_profile.hpp"
#ifdef GEHD_PRODUCT_SHA_OPENSSL
#include "openssl_loader_sha.hpp"
#endif
namespace product {
namespace fs {using std::filesystem::path;using std::filesystem::absolute;}
inline const gameprofile::Description* targetGame=nullptr;
inline const char* GameHash=gameprofile::ge2rb.executableHash;
inline void need(bool value,const char*why){if(!value)throw std::runtime_error(why);}
inline bool digestText(const std::string&s){return s.size()==64&&s.find_first_not_of("0123456789ABCDEF")==s.npos;}
// Keep logical paths in receipts/UI, and use explicit extended-length paths at the I/O boundary.
// This avoids requiring a machine-wide LongPathsEnabled change or altering the game manifest.
inline std::wstring nativePath(const fs::path&p){
 auto s=std::filesystem::absolute(p).lexically_normal().wstring();
 need(s.size()>=3&&s[1]==L':'&&s[2]==L'\\',"LOCAL_ABSOLUTE_PATH_REQUIRED");
 return L"\\\\?\\"+s;
}
namespace fs {
 inline bool exists(const path&p){return std::filesystem::exists(nativePath(p));}
 inline bool is_directory(const path&p){return std::filesystem::is_directory(nativePath(p));}
 inline bool is_regular_file(const path&p){return std::filesystem::is_regular_file(nativePath(p));}
 inline bool is_empty(const path&p){return std::filesystem::is_empty(nativePath(p));}
 inline bool create_directory(const path&p){return std::filesystem::create_directory(nativePath(p));}
 inline bool create_directories(const path&p){return std::filesystem::create_directories(nativePath(p));}
 inline uintmax_t file_size(const path&p){return std::filesystem::file_size(nativePath(p));}
 inline bool remove(const path&p,std::error_code&ec){return std::filesystem::remove(nativePath(p),ec);}
}
inline DWORD GetFileAttributesW(LPCWSTR p){return ::GetFileAttributesW(nativePath(p).c_str());}
inline HANDLE CreateFileW(LPCWSTR p,DWORD access,DWORD share,LPSECURITY_ATTRIBUTES security,DWORD disposition,DWORD flags,HANDLE templ){return ::CreateFileW(nativePath(p).c_str(),access,share,security,disposition,flags,templ);}
inline BOOL CopyFileW(LPCWSTR source,LPCWSTR dest,BOOL failIfExists){
 BOOL ok=::CopyFileW(nativePath(source).c_str(),nativePath(dest).c_str(),failIfExists);
 if(!ok)throw std::runtime_error("COPY_FAILED win32="+std::to_string(GetLastError()));return ok;
}
inline BOOL MoveFileExW(LPCWSTR source,LPCWSTR dest,DWORD flags){return ::MoveFileExW(nativePath(source).c_str(),nativePath(dest).c_str(),flags);}
inline BOOL DeleteFileW(LPCWSTR path){return ::DeleteFileW(nativePath(path).c_str());}
inline BOOL CreateHardLinkW(LPCWSTR dest,LPCWSTR source,LPSECURITY_ATTRIBUTES security){return ::CreateHardLinkW(nativePath(dest).c_str(),nativePath(source).c_str(),security);}
inline BOOL GetDiskFreeSpaceExW(LPCWSTR path,PULARGE_INTEGER available,PULARGE_INTEGER total,PULARGE_INTEGER free){return ::GetDiskFreeSpaceExW(nativePath(path).c_str(),available,total,free);}
inline std::string utf8(const std::wstring&w){
 need(w.find_first_of(L"\r\n\t\0",0,4)==w.npos,"PATH_CONTROL_CHARACTER");
 int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,w.data(),int(w.size()),nullptr,0,nullptr,nullptr);need(n>0,"PATH_UTF8");
 std::string s(n,0);need(WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,w.data(),int(w.size()),s.data(),n,nullptr,nullptr)==n,"PATH_UTF8");return s;
}
inline std::wstring wide(const std::string&s){
 need(s.find_first_of("\r\n\t\0",0,4)==s.npos,"PATH_CONTROL_CHARACTER");
 int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),int(s.size()),nullptr,0);need(n>0,"PATH_UTF8");
 std::wstring w(n,0);need(MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),int(s.size()),w.data(),n)==n,"PATH_UTF8");return w;
}
inline fs::path absolute(const fs::path&p){
 auto a=fs::absolute(p).lexically_normal();auto s=a.wstring();
 need(s.size()>3&&s[1]==L':'&&s[2]==L'\\',"LOCAL_ABSOLUTE_PATH_REQUIRED");utf8(s);return a;
}
inline void ordinary(fs::path p){
 p=product::absolute(p);
 for(;;){DWORD a=GetFileAttributesW(p.c_str());if(a!=INVALID_FILE_ATTRIBUTES)need(!(a&FILE_ATTRIBUTE_REPARSE_POINT),"REPARSE_PATH");else{DWORD e=GetLastError();need(e==ERROR_FILE_NOT_FOUND||e==ERROR_PATH_NOT_FOUND,"PATH_ACCESS");}
  auto parent=p.parent_path();if(parent==p||parent.empty())break;p=parent;
 }
}
inline std::vector<std::string> lines(const fs::path&p,size_t cap=32*1024*1024){
 ordinary(p);std::ifstream f(fs::path(nativePath(p)),std::ios::binary|std::ios::ate);need(bool(f),"MISSING_METADATA");auto size=f.tellg();need(size>0&&uint64_t(size)<=cap,"METADATA_SIZE");
 std::string b(size_t(size),0);f.seekg(0);f.read(b.data(),b.size());need(bool(f)&&b.find('\0')==b.npos,"METADATA_READ");
 std::vector<std::string>out;std::istringstream in(b);std::string s;while(std::getline(in,s)){if(!s.empty()&&s.back()=='\r')s.pop_back();out.push_back(s);}return out;
}
struct Handle {HANDLE h=INVALID_HANDLE_VALUE;~Handle(){if(h!=INVALID_HANDLE_VALUE)CloseHandle(h);}};
#ifdef GEHD_PRODUCT_SHA_OPENSSL
// Keep the same write/delete-denying handle across fallback. Backend failure
// restarts CNG from zero; file I/O failure must remain an error.
#ifdef GEHD_PRODUCT_SHA_TEST
inline unsigned shaFailAfterChunks=0;
inline unsigned shaOptimizedFiles=0,shaFallbackFiles=0;
#endif
inline int optimizedFileDigest(HANDLE file,UCHAR*out){
 try{
  std::call_once(opensslsha::once,opensslsha::initialize);if(!opensslsha::ready)return 0;
  opensslsha::Stream stream;std::vector<UCHAR>buffer(1024*1024);
  for(;;){DWORD count=0;if(!ReadFile(file,buffer.data(),DWORD(buffer.size()),&count,nullptr))return -1;if(!count)break;
   stream.add(buffer.data(),count);
#ifdef GEHD_PRODUCT_SHA_TEST
   if(shaFailAfterChunks&&!--shaFailAfterChunks)return 0;
#endif
  }
  auto digest=stream.finish();memcpy(out,digest.data(),digest.size());
#ifdef GEHD_PRODUCT_SHA_TEST
  ++shaOptimizedFiles;
#endif
  return 1;
 }catch(const std::exception&){return 0;}
}
#endif
inline std::string fileHash(const fs::path&p){
 ordinary(p);Handle f;f.h=CreateFileW(p.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);need(f.h!=INVALID_HANDLE_VALUE,"FILE_READ");
 UCHAR digest[32];bool optimized=false;
#ifdef GEHD_PRODUCT_SHA_OPENSSL
 int status=optimizedFileDigest(f.h,digest);need(status>=0,"FILE_HASH_READ");optimized=status==1;
 if(!optimized){LARGE_INTEGER zero{};need(SetFilePointerEx(f.h,zero,nullptr,FILE_BEGIN),"FILE_HASH_RESTART");
#ifdef GEHD_PRODUCT_SHA_TEST
  ++shaFallbackFiles;
#endif
 }
#endif
 if(!optimized){
 BCRYPT_ALG_HANDLE alg{};BCRYPT_HASH_HANDLE hash{};need(BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0,"HASH_PROVIDER");
 std::vector<UCHAR>buffer(1024*1024);bool ok=BCryptCreateHash(alg,&hash,nullptr,0,nullptr,0,0)>=0;
 while(ok){DWORD n=0;ok=ReadFile(f.h,buffer.data(),DWORD(buffer.size()),&n,nullptr)!=FALSE;if(!ok||!n)break;ok=BCryptHashData(hash,buffer.data(),n,0)>=0;}
 if(ok)ok=BCryptFinishHash(hash,digest,32,0)>=0;if(hash)BCryptDestroyHash(hash);BCryptCloseAlgorithmProvider(alg,0);need(ok,"FILE_HASH");
 }
 static const char hex[]="0123456789ABCDEF";std::string s;for(auto c:digest){s+=hex[c>>4];s+=hex[c&15];}return s;
}
inline bool relativeAllowed(const std::string&p){
 if(p=="mapping.tsv")return true;
 return p.size()==71&&(p.rfind("2x/",0)==0||p.rfind("4x/",0)==0)&&digestText(p.substr(3,64))&&p.substr(67)==".dds";
}
struct File {std::string relative,hash;uint64_t bytes=0;};
struct Package {fs::path root;std::string release,mapHash,gameId;std::vector<File>files;uint64_t bytes=0;};
inline Package package(const fs::path&path){
 Package p;p.root=product::absolute(path);ordinary(p.root);
 auto identity=lines(p.root/L"pack.identity",1024);
 need(identity.size()==5&&identity[0]=="GEHD_PRODUCT_V1"&&digestText(identity[3])&&digestText(identity[4]),"PACKAGE_IDENTITY");
 auto game=gameprofile::find(identity[1]);need(game&&gameprofile::accepts(*game,identity[1],identity[2])&&(!targetGame||targetGame==game),"PACKAGE_GAME_IDENTITY");p.gameId=game->id;
 p.mapHash=identity[3];p.release=identity[4];
 need(fileHash(p.root/L"release.files")==p.release,"RELEASE_INDEX_HASH");
 auto index=lines(p.root/L"release.files");need(index.size()>1&&index.size()<=200002&&index[0]=="GEHD_FILES_V1","RELEASE_INDEX");
 std::set<std::string>names;bool hasMap=false;
 for(size_t i=1;i<index.size();++i){
  auto& s=index[i];auto a=s.find('\t'),b=s.find('\t',a==s.npos?0:a+1);need(a!=s.npos&&b!=s.npos&&s.find('\t',b+1)==s.npos,"RELEASE_ROW");
  File f;f.relative=s.substr(0,a);std::string number=s.substr(a+1,b-a-1);f.hash=s.substr(b+1);
  need(relativeAllowed(f.relative)&&names.insert(f.relative).second&&digestText(f.hash)&&!number.empty()&&number.find_first_not_of("0123456789")==number.npos,"RELEASE_ENTRY");
  f.bytes=std::stoull(number);need(f.bytes>0&&f.bytes<=256ull*1024*1024,"FILE_SIZE_LIMIT");
  if(f.relative=="mapping.tsv"){need(f.hash==p.mapHash,"MAPPING_IDENTITY");hasMap=true;}
  p.bytes+=f.bytes;p.files.push_back(f);
 }
 need(hasMap,"MAPPING_MISSING");need(fileHash(p.root/L"mapping.tsv")==p.mapHash,"MAPPING_HASH");
 auto mapping=lines(p.root/L"mapping.tsv");need(mapping.size()>1&&mapping[0]=="GEHD_TEXTURE_PACK_MIXED_V1","MAPPING_FORMAT");
 std::unordered_map<std::string,File>byName;for(auto&f:p.files)byName.emplace(f.relative,f);
 std::set<std::string>keys;
 for(size_t i=1;i<mapping.size();++i){
  std::istringstream row(mapping[i]);std::string key,repl,tail;uint64_t sourceSize,size,w,h,m,fmt,scale;
  need(bool(row>>key>>repl>>sourceSize>>size>>w>>h>>m>>fmt>>scale)&&!(row>>tail)&&digestText(key)&&digestText(repl)&&keys.insert(key).second&&(scale==2||scale==4)&&sourceSize>0&&sourceSize<=256ull*1024*1024&&w>0&&h>0&&w<=16384&&h<=16384&&m>0&&m<=15,"MAPPING_ROW");
  auto name=std::to_string(scale)+"x/"+key+".dds";auto found=byName.find(name);
  need(found!=byName.end()&&found->second.hash==repl&&found->second.bytes==size,"MAPPING_INDEX_MISMATCH");
 }
 need(mapping.size()==p.files.size(),"INDEX_EXTRA_OR_MISSING");return p;
}
inline void verify(const Package&p,const std::function<void(size_t,size_t)>&progress={}){
 size_t i=0;for(auto&f:p.files){auto path=p.root/fs::path(f.relative);ordinary(path);need(fs::is_regular_file(path)&&fs::file_size(path)==f.bytes,"FILE_SIZE_MISMATCH");need(fileHash(path)==f.hash,"FILE_HASH_MISMATCH");if(progress)progress(++i,p.files.size());}
 // Metadata must still bind the same release after the full scan.
 auto again=package(p.root);need(again.release==p.release&&again.mapHash==p.mapHash,"PACKAGE_CHANGED");
}
}
