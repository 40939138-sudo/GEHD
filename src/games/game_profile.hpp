#pragma once
#include <cstdint>
#include <string_view>
#include <array>

// Trusted, compiled game descriptions. Never take a hook address from a pack.
namespace gameprofile {
struct Description {
 const char* id;
 const wchar_t* executable;
 const wchar_t* steamDirectory;
 const char* executableHash;
 const char* d3dxHash;
 std::uint32_t textureIat;
};
inline constexpr Description ge2rb{
 "GE2RB",L"GE2RB.exe",L"GOD EATER 2 Rage Burst",
 "586F9A03E75C818165520D82A1BB1289668669D4B708C97857ADE6DF5DE6BFBB",
 "0B28546BE22C71834501F7D7185EDE5D79742457331C7EE09EFC14490DD64F5F",0x209f02c};
inline constexpr Description ger{
 "GER",L"GER.exe",L"GOD EATER RESURRECTION",
 "7AF658FE078ED60803F6AADC4B6A5FAE9C8427868438E4951096D87346A50596",
 "0B28546BE22C71834501F7D7185EDE5D79742457331C7EE09EFC14490DD64F5F",0x207b02c};
inline constexpr const Description* registry[]{&ge2rb,&ger};
inline constexpr const Description* find(std::string_view id){
 for(auto p:registry)if(id==p->id)return p;
 return nullptr;
}
inline constexpr bool accepts(const Description&game,std::string_view id,std::string_view exe){
 return id==game.id&&exe==game.executableHash;
}
inline constexpr std::array<char,128> descriptor(const Description&game){
 std::array<char,128> bytes{};size_t n=0;
 for(auto s:{std::string_view("GEHD_GAME_DESCRIPTOR_V1\n"),std::string_view(game.id),std::string_view("\n"),std::string_view(game.executableHash),std::string_view("\n")})for(char c:s)bytes[n++]=c;
 return bytes;
}
#ifdef GEHD_GAME_GER
inline constexpr const Description& selected=ger;
#else
inline constexpr const Description& selected=ge2rb;
#endif
}
