#pragma once
#include <string>
#include <vector>
#include <stdexcept>
#include "game_profile.hpp"
namespace loadingoptions {
inline bool token(const std::string&s){return s.size()==32&&s.find_first_not_of("0123456789ABCDEF")==s.npos;}
inline bool mode(const std::string&s){return s=="sync"||s=="workers"||s=="learned";}
struct Options {
 std::string game,owner,mode="sync",epoch="00000000000000000000000000000000";
 std::vector<std::string> record()const{return {"GEHD_LOADING_OPTIONS_V1",game,owner,mode,epoch};}
};
inline Options parse(const std::vector<std::string>&v,const gameprofile::Description&game){
 if(v.size()!=5||v[0]!="GEHD_LOADING_OPTIONS_V1"||v[1]!=game.id||!token(v[2])||!mode(v[3])||!token(v[4]))throw std::runtime_error("LOADING_OPTIONS_INVALID");
 return {v[1],v[2],v[3],v[4]};
}
}
