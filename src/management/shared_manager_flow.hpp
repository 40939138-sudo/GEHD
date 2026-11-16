#pragma once
#include <string>
#include <stdexcept>
namespace managerflow {
inline bool blank(const std::wstring& value) { return value.find_first_not_of(L" \t\r\n")==value.npos; }
inline std::wstring activation(const std::string& state,bool manual) {
 if(state.empty()||state=="UNINSTALLED") {
  if(!manual)throw std::runtime_error("INSTALL_FIRST");return L"adopt";
 }
 if(state!="ENABLED"&&state!="DISABLED")throw std::runtime_error("RESUME_REQUIRED");
 if(manual)throw std::runtime_error("ALREADY_MANAGED");return L"enable";
}
// Current installation and selected operation source must never alias in the UI.
inline std::wstring dataFor(const std::wstring& action,const std::wstring& current,const std::wstring& selected) {
 if(action==L"verify") { if(blank(current))throw std::runtime_error("SELECT_CURRENT_DATA_FIRST");return current; }
 if(action==L"adopt") { auto value=blank(selected)?current:selected;if(blank(value))throw std::runtime_error("SELECT_OPERATION_DATA_FIRST");return value; }
 if(action==L"move"||action==L"locate"||action==L"repair"||action==L"update") {
  if(blank(selected))throw std::runtime_error("SELECT_OPERATION_DATA_FIRST");return selected;
 }
 return {};
}
}
