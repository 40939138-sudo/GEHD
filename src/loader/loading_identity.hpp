#pragma once
#include "persistent_profile.hpp"
#include "loading_variant_paths.hpp"
#include "game_profile.hpp"
#include <span>
namespace loading {
// This invalidates prediction metadata on ordinary language install/update.
// Large archive stamps are NOT content validation; actual DDS always gets SHA.
inline std::string variantStamp(const std::filesystem::path&base,const gameprofile::Description&game=gameprofile::selected){
 static constexpr const char* gerPaths[]={"bin.qpck","bin_patch.qpck","conf.qpck","data.qpck"};
 auto paths=std::string_view(game.id)=="GE2RB"?std::span<const char* const>(variantPaths):std::span<const char* const>(gerPaths);
 std::ostringstream out;size_t contentBytes=0;
 for(auto relative:paths){auto p=base/relative;persist::ordinary(p);DWORD a=GetFileAttributesW(persist::native(p).c_str());
  out<<relative<<'\n';if(a==INVALID_FILE_ATTRIBUTES){auto e=GetLastError();persist::check(e==ERROR_FILE_NOT_FOUND||e==ERROR_PATH_NOT_FOUND,"VARIANT_ACCESS");out<<"absent\n";continue;}
  persist::check(!(a&FILE_ATTRIBUTE_DIRECTORY),"VARIANT_DIRECTORY");persist::Handle f;f.h=CreateFileW(persist::native(p).c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);persist::check(f.h!=INVALID_HANDLE_VALUE,"VARIANT_OPEN");
  BY_HANDLE_FILE_INFORMATION i{};persist::check(GetFileInformationByHandle(f.h,&i),"VARIANT_INFO");uint64_t size=(uint64_t(i.nFileSizeHigh)<<32)|i.nFileSizeLow;
  out<<i.dwVolumeSerialNumber<<':'<<i.nFileIndexHigh<<':'<<i.nFileIndexLow<<':'<<size<<':'<<i.ftLastWriteTime.dwHighDateTime<<':'<<i.ftLastWriteTime.dwLowDateTime<<'\n';
  if(size<=1024*1024){contentBytes+=size_t(size);persist::check(contentBytes<=16*1024*1024,"VARIANT_LIMIT");std::string b(size_t(size),'\0');DWORD got=0;persist::check(ReadFile(f.h,b.data(),DWORD(b.size()),&got,nullptr)&&got==b.size(),"VARIANT_READ");out<<cpuprep::fullHash(b.data(),b.size())<<'\n';}
 }
 auto body=out.str();return cpuprep::fullHash(body.data(),body.size());
}
inline std::string bindingKey(const std::string&exe,const std::string&manifest,const std::string&release,const std::string&variant,const gameprofile::Description&game=gameprofile::selected){
 auto body=std::string("GEHD_LOADING_FRAMEWORK_2\nPROFILE_1\nPREDICTOR_1\n")+game.id+'\n'+exe+'\n'+manifest+'\n'+release+'\n'+variant;
 return cpuprep::fullHash(body.data(),body.size());
}
}
