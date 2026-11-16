#include "shared_manager_flow.hpp"
#include <iostream>
#include <stdexcept>
int main(){
 using namespace managerflow;int checks=0;
 auto require=[&](bool ok){if(!ok)throw std::runtime_error("flow regression");++checks;};
 auto rejects=[&](const wchar_t* action,const wchar_t* current,const wchar_t* selected){
  try{dataFor(action,current,selected);}catch(const std::runtime_error&){++checks;return;}
  throw std::runtime_error("missing path was accepted");
 };
 const auto current=L"D:\\Game\\GEHD\\data",target=L"E:\\HD new",source=L"E:\\Extracted package";
 // A pending move or repair choice must not redirect integrity verification.
 require(dataFor(L"verify",current,target)==current);
 require(dataFor(L"repair",current,source)==source);
 require(dataFor(L"update",current,source)==source);
 require(dataFor(L"move",current,target)==target);
 require(dataFor(L"locate",current,target)==target);
 require(dataFor(L"adopt",current,L"")==current);
 require(dataFor(L"adopt",current,source)==source);
 for(auto action:{L"move",L"locate",L"repair",L"update"})rejects(action,current,L" \t");
 rejects(L"verify",L"",source);rejects(L"adopt",L"",L" ");
 require(dataFor(L"status",current,target).empty());
 require(dataFor(L"rollback-bundle",current,target).empty());
 require(blank(L" \r\n\t")&&!blank(L"D:\\中文資料"));
 require(activation("",true)==L"adopt");require(activation("UNINSTALLED",true)==L"adopt");
 require(activation("DISABLED",false)==L"enable");require(activation("ENABLED",false)==L"enable");
 for(auto state:{"","UNINSTALLED","ADOPTING"}){
  bool refused=false;try{activation(state,false);}catch(const std::runtime_error&){refused=true;}require(refused);
 }
 for(auto state:{"ENABLED","DISABLED","ADOPTING"}){
  bool refused=false;try{activation(state,true);}catch(const std::runtime_error&){refused=true;}require(refused);
 }
 std::cout<<checks<<" path isolation and missing-input checks passed\n";
}
