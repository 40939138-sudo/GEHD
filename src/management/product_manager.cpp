// Native, nonresident frontend. All mutations are delegated to pack-control.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define UNICODE
#define _UNICODE
#include <windows.h>
#include <shobjidl.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <memory>
#include <iomanip>
#include "product_files.hpp"
namespace fs=std::filesystem;
namespace {
constexpr UINT Finished=WM_APP+1;
enum {GameEdit=100,DataEdit,GameBrowse,DataBrowse,Status,Enable,Disable,Move,MoveBack,Locate,Verify,Resume,Uninstall,Repair,Update,Rollback,SyncMode,WorkerMode,LearnMode,Relearn};
HWND mainWindow{},gameEdit{},dataEdit{},logEdit{};HFONT font{};bool busy=false;fs::path managerRoot;std::vector<HWND>controls;
struct Result {DWORD exit=1;std::wstring output;};
struct Task {std::wstring action,mode;fs::path game,data;};
std::wstring text(HWND h){int n=GetWindowTextLengthW(h);std::wstring s(n+1,L'\0');GetWindowTextW(h,s.data(),n+1);s.resize(n);return s;}
std::wstring decode(const std::string&s){if(s.empty())return {};int n=MultiByteToWideChar(CP_UTF8,0,s.data(),int(s.size()),nullptr,0);std::wstring out(n,L'\0');MultiByteToWideChar(CP_UTF8,0,s.data(),int(s.size()),out.data(),n);return out;}
std::wstring quote(const fs::path&p){auto s=p.wstring();if(s.find_first_of(L"\"\r\n")!=s.npos)throw std::runtime_error("INVALID_PATH");return L"\""+s+L"\"";}
void log(const std::wstring&s){SetWindowTextW(logEdit,s.c_str());}
void setBusy(bool value){busy=value;for(auto h:controls)EnableWindow(h,!value);}
void refreshData(){try{
 auto game=product::absolute(text(gameEdit));auto path=game/L"GEHD/state/installation.tsv";
 if(fs::exists(path)){auto v=product::lines(path,8192);if(v.size()==11&&v[0]=="GEHD_INSTALL_V1")SetWindowTextW(dataEdit,(v[10]=="UNINSTALLED"?game/L"GEHD/data":fs::path(product::wide(v[3]))).c_str());}
 else SetWindowTextW(dataEdit,(game/L"GEHD/data").c_str());
}catch(...){}}
bool choose(HWND target){bool chosen=false;IFileDialog*dialog=nullptr;if(FAILED(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog))))return false;
 DWORD options{};dialog->GetOptions(&options);dialog->SetOptions(options|FOS_PICKFOLDERS|FOS_FORCEFILESYSTEM|FOS_NOCHANGEDIR);
 if(SUCCEEDED(dialog->Show(mainWindow))){IShellItem*item=nullptr;if(SUCCEEDED(dialog->GetResult(&item))){PWSTR path=nullptr;if(SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH,&path))){SetWindowTextW(target,path);chosen=true;CoTaskMemFree(path);}item->Release();}}
 dialog->Release();if(target==gameEdit&&chosen)refreshData();return chosen;
}
std::wstring explanation(const Result&r){
 if(r.exit==0){
  if(r.output.rfind(L"gameRunning=",0)==0){
   std::wistringstream in(r.output);std::wstring flags,mode,path,release,size;std::getline(in,flags);std::getline(in,mode);std::getline(in,path);std::getline(in,release);std::getline(in,size);
   for(auto value:{&flags,&mode,&path,&release,&size})if(!value->empty()&&value->back()==L'\r')value->pop_back();
   std::wstring out=flags.find(L"gameRunning=1")!=flags.npos?L"遊戲：執行中\r\n":L"遊戲：已關閉\r\n";
   if(flags.find(L"managed=0")!=flags.npos)return out+L"模組：尚未接管。手動解壓完成後，請按「啟用／接管」。";
   if(mode==L"UNINSTALLED")return out+L"模組：已移除。重新解壓完整資料包後，可按「啟用／接管」重新安裝。";
   std::wstring label=mode==L"ENABLED"?L"已啟用":mode==L"DISABLED"?L"已停用":L"工作尚未完成，請繼續未完成工作";
   out+=L"模組："+label+L"\r\n資料位置："+path+L"\r\n資料版本："+release+L"\r\n";
   try{if(size.rfind(L"dataBytes=",0)!=0)throw std::runtime_error("SIZE");auto bytes=std::stoull(size.substr(10));std::wostringstream formatted;formatted<<std::fixed<<std::setprecision(2)<<(double(bytes)/(1024.0*1024.0*1024.0));out+=L"資料清單大小："+formatted.str()+L" GiB";}catch(...){out+=L"資料大小：目前無法讀取，請確認資料夾可用。";}
   std::wstring extra;std::getline(in,extra);if(extra.find(L"gameId=")!=extra.npos){auto split=extra.find(L" loadingMode=");if(split!=extra.npos){out+=L"\r\n目標遊戲："+extra.substr(7,split-7);auto start=split+13;auto end=extra.find(L" ",start);auto modeValue=extra.substr(start,end-start);out+=L"\r\n下次啟動模式："+(modeValue==L"sync"?std::wstring(L"同步"):modeValue==L"workers"?std::wstring(L"僅準備工作"):std::wstring(L"學習預讀"));}}
   return out;
  }
  return L"操作完成。正常遊玩請從 Steam 啟動，不需讓管理器常駐。\r\n\r\n"+r.output;
 }
 struct Hint{const wchar_t*key;const wchar_t*message;};
 const Hint hints[]={
 {L"RUN_INSTALLER_TO_RESUME_TOOLS",L"管理工具版本更新尚未完成。請關閉管理器，重新執行同一版本的安裝器以繼續。"},
 {L"RESUME_BUNDLE_UPDATE_FIRST",L"管理工具版本更新尚未完成。請關閉管理器，重新執行原安裝器以繼續。"},
 {L"LEGACY_INSTALL_RESUME_REQUIRED",L"舊部署遷移尚未完成。請關閉管理器，重新執行原安裝器以繼續。"},
 {L"CLOSE_GAME_FIRST",L"請先正常退出遊戲，再重試。"},
 {L"PACKAGE_GAME_IDENTITY",L"資料包屬於另一款遊戲，未修改所選安裝。"},
 {L"LOADING_OPTIONS",L"預讀設定格式或歸屬不符，已保留原檔。"},
 {L"GAME_EXE_IDENTITY",L"遊戲版本與此模組包不符，未啟用或切換。"},
 {L"RESUME_",L"有尚未完成的工作。請按「繼續未完成工作」。"},
 {L"RECOVER_",L"部署尚未完整。請先停用，再重新啟用。"},
 {L"NEW_DATA_COMMITTED",L"新資料位置已完成切換；舊副本尚待清理。請按「繼續未完成工作」。"},
 {L"OWNERSHIP",L"檔案歸屬或內容有變動，已停止自動修改並保留檔案。"},
 {L"FOREIGN_LOADER",L"遊戲目錄已有其他載入器，無法直接覆蓋。"},
 {L"EXISTING_CONFIG",L"已有其他安裝留下的路徑設定，須先依原安裝方式停用。"},
 {L"INSUFFICIENT_SPACE",L"目的磁碟空間不足，舊資料仍保留。"},
 {L"DESTINATION_EXISTS",L"搬移目的地已存在。請填入新的資料夾名稱；若要使用現成副本，請選「使用現有資料夾」。"},
 {L"LOCATE_RELEASE",L"選取的資料夾不是目前版本的完整資料包。"},
 {L"REPAIR_RELEASE_MISMATCH",L"修復必須使用相同版本的本地資料包。若要換版本，請使用更新。"},
 {L"MAINTENANCE_MEDIA",L"維護來源已移動或內容改變。請還原所選本地資料包後續作。"},
 {L"ROLLBACK_LOCATION_CHANGED",L"資料已搬至另一位置。請先移回原維護位置，再回復上次維護。"},
 {L"MISSING_METADATA",L"找不到完整資料或安裝紀錄。請確認資料位置與本地安裝媒體。"}};
 for(auto&h:hints)if(r.output.find(h.key)!=r.output.npos)return std::wstring(h.message)+L"\r\n\r\n詳細資訊：\r\n"+r.output;
 return L"操作未完成；請依下列原因修正後重試。\r\n\r\n"+r.output;
}
DWORD WINAPI worker(void*raw){std::unique_ptr<Task>task(static_cast<Task*>(raw));auto result=std::make_unique<Result>();try{
 auto exe=managerRoot/L"bin/pack-control.exe";product::ordinary(exe);
 std::wstring command=quote(exe)+L" --product "+task->action+L" --game "+quote(task->game);
 if(!task->mode.empty())command+=L" --mode "+task->mode;
 if(!task->data.empty())command+=L" --data "+quote(task->data);
 SECURITY_ATTRIBUTES sa{sizeof(sa),nullptr,TRUE};HANDLE readPipe=nullptr,writePipe=nullptr;
 if(!CreatePipe(&readPipe,&writePipe,&sa,0))throw std::runtime_error("CREATE_PIPE");
 product::Handle reader,writer;reader.h=readPipe;writer.h=writePipe;SetHandleInformation(reader.h,HANDLE_FLAG_INHERIT,0);
 STARTUPINFOW startup{};startup.cb=sizeof(startup);startup.dwFlags=STARTF_USESTDHANDLES;startup.hStdOutput=writer.h;startup.hStdError=writer.h;startup.hStdInput=GetStdHandle(STD_INPUT_HANDLE);
 PROCESS_INFORMATION process{};BOOL ok=CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW,nullptr,managerRoot.c_str(),&startup,&process);
 CloseHandle(writer.h);writer.h=INVALID_HANDLE_VALUE;if(!ok)throw std::runtime_error("START_CONTROLLER");
 product::Handle child,thread;child.h=process.hProcess;thread.h=process.hThread;
 std::string output;char buffer[4096];DWORD got{};while(ReadFile(reader.h,buffer,sizeof(buffer),&got,nullptr)&&got){if(output.size()<1024*1024)output.append(buffer,got);}
 WaitForSingleObject(child.h,INFINITE);GetExitCodeProcess(child.h,&result->exit);result->output=decode(output);
}catch(const std::exception&e){result->output=decode(e.what());}
 PostMessageW(mainWindow,Finished,0,reinterpret_cast<LPARAM>(result.release()));return 0;
}
void start(int id){if(busy)return;try{
 auto task=std::make_unique<Task>();task->game=product::absolute(text(gameEdit));auto chosen=text(dataEdit);
 if(id==SyncMode||id==WorkerMode||id==LearnMode){task->action=L"loading-mode";task->mode=id==SyncMode?L"sync":id==WorkerMode?L"workers":L"learned";}
 else if(id==Relearn)task->action=L"relearn";
 else if(id==Status)task->action=L"status";
 else if(id==Enable){
  auto receipt=task->game/L"GEHD/state/installation.tsv";bool adoption=!fs::exists(receipt);
  if(!adoption){auto v=product::lines(receipt,8192);adoption=v.size()==11&&(v[10]=="UNINSTALLED"||v[10]=="ADOPTING");}
  if(adoption){task->action=L"adopt";task->data=product::absolute(chosen);}else task->action=L"enable";
 }else if(id==Disable)task->action=L"disable";
 else if(id==Move||id==MoveBack){task->action=L"move";task->data=id==MoveBack?task->game/L"GEHD/data":product::absolute(chosen);}
 else if(id==Locate){task->action=L"locate";task->data=product::absolute(chosen);}
 else if(id==Verify){task->action=L"verify";task->data=product::absolute(chosen);}
 else if(id==Resume){
  task->action=L"resume";auto receipt=task->game/L"GEHD/state/installation.tsv";
  if(fs::exists(receipt)){auto v=product::lines(receipt,8192);if(v.size()==11&&(v[10]=="ADOPTING"||v[10]=="UNINSTALLED")){task->action=L"adopt";task->data=product::absolute(v[10]=="UNINSTALLED"?chosen:product::wide(v[3]));}}
 }else if(id==Repair||id==Update){
  if(!choose(dataEdit))return;task->action=id==Repair?L"repair":L"update";task->data=product::absolute(text(dataEdit));
 }else if(id==Rollback){
  if(MessageBoxW(mainWindow,L"回復上次維護前的檔案？\n修復前的檔案可能已損壞；回復修復操作後會保持停用。",L"回復上次維護",MB_YESNO|MB_ICONQUESTION)!=IDYES)return;task->action=L"rollback";
 }else if(id==Uninstall){
  if(MessageBoxW(mainWindow,L"移除高清載入檔及未修改的高清資料？\n原遊戲、中文模組、額外或已修改的檔案會保留。",L"移除高清模組",MB_YESNO|MB_ICONQUESTION)!=IDYES)return;
  task->action=L"uninstall";
 }else return;
 setBusy(true);log(L"處理中。完整資料核對或搬移可能需要較長時間，請勿啟動遊戲。\r\n管理器會在操作完成後顯示結果。");
 HANDLE h=CreateThread(nullptr,0,worker,task.get(),0,nullptr);if(!h){setBusy(false);throw std::runtime_error("START_WORKER");}task.release();CloseHandle(h);
}catch(const std::exception&e){log(L"無法開始操作：\r\n"+decode(e.what()));}}
HWND child(const wchar_t*cls,const wchar_t*label,DWORD style,int x,int y,int w,int h,int id=0){auto control=CreateWindowExW(cls==std::wstring(L"EDIT")?WS_EX_CLIENTEDGE:0,cls,label,WS_CHILD|WS_VISIBLE|style,x,y,w,h,mainWindow,reinterpret_cast<HMENU>(INT_PTR(id)),GetModuleHandleW(nullptr),nullptr);SendMessageW(control,WM_SETFONT,WPARAM(font),TRUE);if(id)controls.push_back(control);return control;}
LRESULT CALLBACK window(HWND h,UINT message,WPARAM w,LPARAM l){switch(message){
 case WM_CREATE:{mainWindow=h;font=CreateFontW(-18,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Microsoft JhengHei UI");
  child(L"STATIC",L"GER／GE2RB 高清模組管理器",0,20,16,740,28);
  child(L"STATIC",L"遊戲資料夾",0,20,60,105,24);gameEdit=child(L"EDIT",L"",ES_AUTOHSCROLL|WS_TABSTOP,125,56,550,28,GameEdit);child(L"BUTTON",L"選擇…",WS_TABSTOP,686,56,82,28,GameBrowse);
  child(L"STATIC",L"資料位置",0,20,102,105,24);dataEdit=child(L"EDIT",L"",ES_AUTOHSCROLL|WS_TABSTOP,125,98,550,28,DataEdit);child(L"BUTTON",L"選擇…",WS_TABSTOP,686,98,82,28,DataBrowse);
  child(L"STATIC",L"搬移時填入新的目的資料夾；使用現有副本時，選取含 mapping.tsv 的資料夾。",0,20,138,750,28);
  struct Button{int id;const wchar_t*label;};const Button first[]={{Status,L"查看狀態"},{Enable,L"啟用／接管"},{Disable,L"停用"},{Verify,L"核對資料"},{Resume,L"繼續未完成工作"}};
  int x=20;for(auto&b:first){int width=b.id==Resume?180:130;child(L"BUTTON",b.label,WS_TABSTOP,x,180,width,34,b.id);x+=width+10;}
  const Button second[]={{Move,L"搬移資料"},{MoveBack,L"移回預設位置"},{Locate,L"使用現有資料夾"},{Uninstall,L"移除高清模組"}};
  x=20;for(auto&b:second){child(L"BUTTON",b.label,WS_TABSTOP,x,226,175,34,b.id);x+=185;}
  const Button third[]={{Repair,L"用資料夾修復"},{Update,L"用資料夾更新"},{Rollback,L"回復上次維護"}};
  x=20;for(auto&b:third){child(L"BUTTON",b.label,WS_TABSTOP,x,272,235,34,b.id);x+=250;}
  const Button fourth[]={{SyncMode,L"同步模式"},{WorkerMode,L"僅準備工作"},{LearnMode,L"學習預讀"},{Relearn,L"清除預讀／重學"}};
  x=20;for(auto&b:fourth){child(L"BUTTON",b.label,WS_TABSTOP,x,318,175,34,b.id);x+=185;}
  logEdit=child(L"EDIT",L"選好遊戲資料夾後可查看狀態。平常仍從 Steam 啟動遊戲。",ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL|WS_VSCROLL|WS_TABSTOP,20,370,750,265);
  auto defaultGame=managerRoot.parent_path();SetWindowTextW(gameEdit,defaultGame.c_str());refreshData();return 0;}
 case WM_COMMAND:if(HIWORD(w)==BN_CLICKED){if(LOWORD(w)==GameBrowse)choose(gameEdit);else if(LOWORD(w)==DataBrowse)choose(dataEdit);else start(LOWORD(w));}return 0;
 case Finished:{std::unique_ptr<Result>r(reinterpret_cast<Result*>(l));setBusy(false);log(explanation(*r));if(r->exit==0)refreshData();return 0;}
 case WM_CLOSE:if(busy){MessageBoxW(h,L"工作仍在進行，請等待結果。",L"正在處理",MB_OK|MB_ICONINFORMATION);return 0;}DestroyWindow(h);return 0;
 case WM_DESTROY:DeleteObject(font);PostQuitMessage(0);return 0;
 }return DefWindowProcW(h,message,w,l);}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int show){
 wchar_t path[32768];DWORD n=GetModuleFileNameW(nullptr,path,32768);if(!n||n>=32768)return 1;managerRoot=fs::path(std::wstring(path,n)).parent_path();
 CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);SetProcessDPIAware();WNDCLASSW wc{};wc.lpfnWndProc=window;wc.hInstance=instance;wc.lpszClassName=L"GEHD_Product_Manager";wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hbrBackground=HBRUSH(COLOR_BTNFACE+1);RegisterClassW(&wc);
 HWND h=CreateWindowExW(0,wc.lpszClassName,L"GER／GE2RB 高清模組管理器",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,CW_USEDEFAULT,CW_USEDEFAULT,810,692,nullptr,nullptr,instance,nullptr);if(!h){CoUninitialize();return 1;}ShowWindow(h,show);MSG msg{};while(GetMessageW(&msg,nullptr,0,0)>0){if(!IsDialogMessageW(h,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}}CoUninitialize();return int(msg.wParam);
}
