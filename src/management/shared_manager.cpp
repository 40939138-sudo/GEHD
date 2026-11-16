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
#include "shared_distribution.hpp"
#include "shared_manager_flow.hpp"
#include <shellapi.h>
namespace fs=std::filesystem;
namespace {
constexpr UINT Finished=WM_APP+1;
enum {GameEdit=100,DataEdit,GameBrowse,DataBrowse,Status,Enable,Disable,Move,MoveBack,Locate,Verify,Resume,Uninstall,Repair,Update,Rollback,SyncMode,WorkerMode,LearnMode,Relearn,PackageEdit,PackageBrowse,InstallPack,BundleRollback,TargetEdit,AdoptManual,TabInstall,TabDaily,TabData,TabLoading};
HWND mainWindow{},gameEdit{},dataEdit{},packageEdit{},targetEdit{},logEdit{};HFONT font{};bool busy=false;fs::path managerRoot;std::vector<HWND>controls;
struct PageControl {HWND handle;int page;};std::vector<PageControl> pageControls;int activePage=0;
struct Result {DWORD exit=1;std::wstring output;};
struct Task {std::wstring action,mode;fs::path game,data,pack;};
std::wstring text(HWND h){int n=GetWindowTextLengthW(h);std::wstring s(n+1,L'\0');GetWindowTextW(h,s.data(),n+1);s.resize(n);return s;}
std::wstring decode(const std::string&s){if(s.empty())return {};int n=MultiByteToWideChar(CP_UTF8,0,s.data(),int(s.size()),nullptr,0);std::wstring out(n,L'\0');MultiByteToWideChar(CP_UTF8,0,s.data(),int(s.size()),out.data(),n);return out;}
std::wstring quote(const fs::path&p){auto s=p.wstring();if(s.find_first_of(L"\"\r\n")!=s.npos)throw std::runtime_error("INVALID_PATH");return L"\""+s+L"\"";}
void log(const std::wstring&s){SetWindowTextW(logEdit,s.c_str());}
void setBusy(bool value){busy=value;for(auto h:controls)EnableWindow(h,!value);}
void refreshData(){
 SetWindowTextW(dataEdit,L"");if(managerflow::blank(text(gameEdit)))return;
 try{auto game=product::absolute(text(gameEdit));auto path=game/L"GEHD/state/installation.tsv";
 if(fs::exists(path)){auto v=product::lines(path,8192);if(v.size()==11&&v[0]=="GEHD_INSTALL_V1"&&v[10]!="UNINSTALLED")SetWindowTextW(dataEdit,fs::path(product::wide(v[3])).c_str());}
 else if(fs::exists(game/L"GEHD/data"))SetWindowTextW(dataEdit,(game/L"GEHD/data").c_str());
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
   if(flags.find(L"managed=0")!=flags.npos)return out+L"模組：尚未接管。請在「安裝／版本」選遊戲包安裝；已手動解壓者可至「資料管理」選取資料後接管。";
   if(mode==L"UNINSTALLED")return out+L"模組：已移除。請在「安裝／版本」選擇遊戲包重新安裝。";
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
 {L"INSTALL_FIRST",L"此遊戲尚未安裝高清模組。請至「安裝／版本」安裝遊戲包；手動解壓者請至「資料管理」接管。"},
 {L"NO_PENDING_WORK",L"此遊戲已移除模組，沒有待繼續的安裝工作。請選擇遊戲包重新安裝。"},
 {L"ALREADY_MANAGED",L"所選遊戲已接管。要改用另一份相同版本資料，請按「改用現有資料副本」。"},
 {L"CLOSE_MANAGER_FIRST",L"請關閉遊戲目錄內的舊管理器 GEHD_Manager.exe，再從共用管理器重試。"},
 {L"SELECT_GAME_PACK_FIRST",L"請在「安裝／版本」選擇對應遊戲包資料夾。"},
 {L"GAME_PACK_",L"遊戲包不完整或內容不符。請選取含 game-pack.tsv、Setup 與全部 7z 的同一資料夾。"},
 {L"SHARED_CONTROLLER_HASH",L"共用管理工具內容不符，請重新解壓完整管理器。"},
 {L"SELECT_GAME_FIRST",L"請先選擇上方的遊戲資料夾（內含 GER.exe 或 GE2RB.exe）。"},
 {L"SELECT_OPERATION_DATA_FIRST",L"請在「資料管理」填入操作資料夾；搬移請填新目的地，接管／修復請選已解壓的高清資料。"},
 {L"SELECT_CURRENT_DATA_FIRST",L"目前沒有可核對的資料位置。請先安裝或接管資料，再查看狀態。"},
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
Result execute(const Task&request){auto task=&request;auto result=std::make_unique<Result>();try{
 if(task->action==L"install-pack"){auto r=sharedmedia::install(managerRoot,task->game,task->pack);return {r.exit,r.output};}
 auto exe=sharedmedia::controller(managerRoot);
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
 return *result;
}
DWORD WINAPI worker(void*raw){std::unique_ptr<Task>task(static_cast<Task*>(raw));auto result=std::make_unique<Result>(execute(*task));
 PostMessageW(mainWindow,Finished,0,reinterpret_cast<LPARAM>(result.release()));return 0;
}
void start(int id){if(busy)return;try{
 product::need(!managerflow::blank(text(gameEdit)),"SELECT_GAME_FIRST");
 auto task=std::make_unique<Task>();task->game=product::absolute(text(gameEdit));auto chosen=text(targetEdit);auto current=text(dataEdit);
 auto selectedData=[&](const wchar_t* action){return product::absolute(managerflow::dataFor(action,current,chosen));};
 if(id==InstallPack){product::need(!managerflow::blank(text(packageEdit)),"SELECT_GAME_PACK_FIRST");task->action=L"install-pack";task->pack=product::absolute(text(packageEdit));}
 else if(id==BundleRollback){if(MessageBoxW(mainWindow,L"依所選遊戲的本地備份回復上次完整版本？\n不需要重新下載遊戲資料包。",L"回復完整版本",MB_YESNO|MB_ICONQUESTION|MB_DEFBUTTON2)!=IDYES)return;task->action=L"rollback-bundle";}
 else if(id==SyncMode||id==WorkerMode||id==LearnMode){task->action=L"loading-mode";task->mode=id==SyncMode?L"sync":id==WorkerMode?L"workers":L"learned";}
 else if(id==Relearn){if(MessageBoxW(mainWindow,L"清除已保存的預讀紀錄，讓後續遊玩重新學習？",L"清除預讀紀錄",MB_YESNO|MB_ICONQUESTION|MB_DEFBUTTON2)!=IDYES)return;task->action=L"relearn";}
 else if(id==Status)task->action=L"status";
 else if(id==Enable||id==AdoptManual){
  auto receipt=task->game/L"GEHD/state/installation.tsv";std::string state;
  if(fs::exists(receipt)){auto v=product::lines(receipt,8192);product::need(v.size()==11&&v[0]=="GEHD_INSTALL_V1","MISSING_METADATA");state=v[10];}
  task->action=managerflow::activation(state,id==AdoptManual);if(task->action==L"adopt")task->data=selectedData(L"adopt");
 }else if(id==Disable)task->action=L"disable";
 else if(id==Move||id==MoveBack){task->action=L"move";task->data=id==MoveBack?task->game/L"GEHD/data":selectedData(L"move");}
 else if(id==Locate){task->action=L"locate";task->data=selectedData(L"locate");}
 else if(id==Verify){task->action=L"verify";task->data=selectedData(L"verify");}
 else if(id==Resume){
  task->action=L"resume";auto receipt=task->game/L"GEHD/state/installation.tsv";
  if(fs::exists(receipt)){auto v=product::lines(receipt,8192);if(v.size()==11&&(v[10]=="ADOPTING"||v[10]=="UNINSTALLED")){task->action=L"adopt";product::need(v[10]!="UNINSTALLED","NO_PENDING_WORK");task->data=product::absolute(product::wide(v[3]));}}
 }else if(id==Repair||id==Update){
  task->action=id==Repair?L"repair":L"update";task->data=selectedData(task->action.c_str());
 }else if(id==Rollback){
  if(MessageBoxW(mainWindow,L"回復上次維護前的檔案？\n修復前的檔案可能已損壞；回復修復操作後會保持停用。",L"回復上次維護",MB_YESNO|MB_ICONQUESTION|MB_DEFBUTTON2)!=IDYES)return;task->action=L"rollback";
 }else if(id==Uninstall){
  if(MessageBoxW(mainWindow,L"移除高清載入檔及未修改的高清資料？\n原遊戲、中文模組、額外或已修改的檔案會保留。",L"移除高清模組",MB_YESNO|MB_ICONQUESTION|MB_DEFBUTTON2)!=IDYES)return;
  task->action=L"uninstall";
 }else return;
 setBusy(true);log(L"處理中。完整資料核對或搬移可能需要較長時間，請勿啟動遊戲。\r\n管理器會在操作完成後顯示結果。");
 HANDLE h=CreateThread(nullptr,0,worker,task.get(),0,nullptr);if(!h){setBusy(false);throw std::runtime_error("START_WORKER");}task.release();CloseHandle(h);
}catch(const std::exception&e){log(explanation({1,decode(e.what())}));}}
HWND child(const wchar_t*cls,const wchar_t*label,DWORD style,int x,int y,int w,int h,int id=0){auto control=CreateWindowExW(cls==std::wstring(L"EDIT")?WS_EX_CLIENTEDGE:0,cls,label,WS_CHILD|WS_VISIBLE|style,x,y,w,h,mainWindow,reinterpret_cast<HMENU>(INT_PTR(id)),GetModuleHandleW(nullptr),nullptr);SendMessageW(control,WM_SETFONT,WPARAM(font),TRUE);if(id)controls.push_back(control);return control;}
void page(int value){activePage=value;for(auto&c:pageControls)ShowWindow(c.handle,c.page==value?SW_SHOW:SW_HIDE);
 for(int i=0;i<4;++i)SendMessageW(GetDlgItem(mainWindow,TabInstall+i),BM_SETCHECK,i==value?BST_CHECKED:BST_UNCHECKED,0);
}
HWND item(int group,const wchar_t*cls,const wchar_t*label,DWORD style,int x,int y,int width,int height,int id=0){auto h=child(cls,label,style,x,y,width,height,id);pageControls.push_back({h,group});return h;}
LRESULT CALLBACK window(HWND h,UINT message,WPARAM w,LPARAM l){switch(message){
 case WM_CREATE:{mainWindow=h;font=CreateFontW(-18,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Microsoft JhengHei UI");
  child(L"STATIC",L"GER／GE2RB 高清模組管理器",0,20,16,750,28);
  child(L"STATIC",L"遊戲資料夾",0,20,60,110,24);gameEdit=child(L"EDIT",L"",ES_AUTOHSCROLL|WS_TABSTOP,135,56,540,28,GameEdit);child(L"BUTTON",L"選擇…",WS_TABSTOP,686,56,84,28,GameBrowse);
  child(L"STATIC",L"目前資料位置",0,20,102,110,24);dataEdit=child(L"EDIT",L"",ES_AUTOHSCROLL|ES_READONLY|WS_TABSTOP,135,98,635,28,DataEdit);
  child(L"STATIC",L"位置依安裝紀錄顯示，空白代表尚未找到資料；實際可用狀態請按「查看狀態」。",0,20,138,750,26);
  child(L"BUTTON",L"查看狀態",WS_TABSTOP,20,176,175,34,Status);
  child(L"BUTTON",L"繼續未完成工作",WS_TABSTOP,210,176,200,34,Resume);
  const wchar_t*labels[]={L"安裝／版本",L"日常使用",L"資料管理",L"載入設定"};
  for(int i=0;i<4;++i)child(L"BUTTON",labels[i],WS_TABSTOP|BS_RADIOBUTTON|BS_PUSHLIKE,20+190*i,230,180,36,TabInstall+i);
  item(0,L"STATIC",L"首次安裝或整包更新：選擇對應遊戲包，再按安裝。完成後從 Steam 啟動。",0,20,282,750,26);
  item(0,L"STATIC",L"遊戲包資料夾",0,20,324,115,26);packageEdit=item(0,L"EDIT",L"",ES_AUTOHSCROLL|WS_TABSTOP,135,320,540,28,PackageEdit);item(0,L"BUTTON",L"選擇…",WS_TABSTOP,686,320,84,28,PackageBrowse);
  item(0,L"STATIC",L"選含 game-pack.tsv、安裝器與全部 7z 的資料夾，不必先解開 7z。",0,20,360,750,26);
  item(0,L"BUTTON",L"安裝／更新所選遊戲包",WS_TABSTOP,20,400,360,36,InstallPack);
  item(0,L"BUTTON",L"回復上次完整版本",WS_TABSTOP,400,400,370,36,BundleRollback);
  item(0,L"STATIC",L"完整版本回復會使用本地備份還原工具與資料；不需要原下載包。",0,20,450,750,40);
  item(1,L"STATIC",L"已安裝後的日常開關；停用保留高清資料，之後可再次啟用。",0,20,282,750,26);
  item(1,L"BUTTON",L"啟用高清",WS_TABSTOP,20,326,235,36,Enable);
  item(1,L"BUTTON",L"停用高清",WS_TABSTOP,275,326,235,36,Disable);
  item(1,L"BUTTON",L"核對目前資料",WS_TABSTOP,530,326,240,36,Verify);
  item(1,L"STATIC",L"移除會刪除本模組擁有且未變動的檔案，按下後仍需確認。",0,20,394,750,26);
  item(1,L"BUTTON",L"移除高清模組…",WS_TABSTOP,20,436,235,36,Uninstall);
  item(2,L"STATIC",L"進階資料操作：操作資料夾與上方目前位置分開，不會因選取而切換安裝。",0,20,282,750,26);
  item(2,L"STATIC",L"操作資料夾",0,20,324,115,26);targetEdit=item(2,L"EDIT",L"",ES_AUTOHSCROLL|WS_TABSTOP,135,320,540,28,TargetEdit);item(2,L"BUTTON",L"選擇…",WS_TABSTOP,686,320,84,28,DataBrowse);
  item(2,L"STATIC",L"搬移：填尚不存在的新目的地。接管／修復／更新：選已解壓的高清資料夾。",0,20,358,750,26);
  struct Button{int id;const wchar_t*label;};const Button storage[]={{Move,L"搬到操作資料夾"},{MoveBack,L"移回遊戲內預設位置"},{Locate,L"改用現有資料副本"},{Repair,L"從操作資料夾修復"},{Update,L"從操作資料夾更新"},{Rollback,L"回復上次資料維護"}};
  for(int i=0;i<6;++i)item(2,L"BUTTON",storage[i].label,WS_TABSTOP,20+(i%3)*255,400+(i/3)*46,240,36,storage[i].id);
  item(2,L"BUTTON",L"接管手動解壓資料",WS_TABSTOP,20,492,240,36,AdoptManual);
  item(2,L"STATIC",L"資料維護回復只撤銷上次修復／資料更新，與完整版本回復不同。",0,275,492,495,44);
  item(3,L"STATIC",L"預設使用同步模式。以下進階模式並非在每台電腦都比較快。",0,20,282,750,26);
  item(3,L"BUTTON",L"同步（預設）",WS_TABSTOP,20,326,235,36,SyncMode);
  item(3,L"BUTTON",L"CPU 準備 worker",WS_TABSTOP,275,326,235,36,WorkerMode);
  item(3,L"BUTTON",L"學習預讀",WS_TABSTOP,530,326,240,36,LearnMode);
  item(3,L"STATIC",L"設定於下次啟動遊戲生效；可用「查看狀態」確認所選模式。",0,20,386,750,26);
  item(3,L"BUTTON",L"清除預讀紀錄／重學…",WS_TABSTOP,20,432,300,36,Relearn);
  item(3,L"STATIC",L"清除只重設預讀紀錄，不移除高清貼圖。",0,340,438,430,40);
  child(L"STATIC",L"操作結果",0,20,550,750,24);
  logEdit=child(L"EDIT",L"請先選遊戲資料夾。首次使用從「安裝／版本」開始；平常從 Steam 啟動。",ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL|WS_VSCROLL|WS_TABSTOP,20,580,750,115);
  page(0);auto defaultGame=managerRoot.parent_path();if(fs::exists(defaultGame/L"GER.exe")||fs::exists(defaultGame/L"GE2RB.exe")){SetWindowTextW(gameEdit,defaultGame.c_str());refreshData();}return 0;}
 case WM_COMMAND:
  if(LOWORD(w)==GameEdit&&HIWORD(w)==EN_CHANGE){if(dataEdit)refreshData();if(targetEdit)SetWindowTextW(targetEdit,L"");if(packageEdit)SetWindowTextW(packageEdit,L"");if(logEdit)log(L"已切換遊戲目錄；請查看狀態，或選擇此遊戲的資料包。");return 0;}
  if(HIWORD(w)==BN_CLICKED){auto id=LOWORD(w);if(busy)return 0;
   if(id>=TabInstall&&id<=TabLoading)page(id-TabInstall);
   else if(id==GameBrowse)choose(gameEdit);else if(id==DataBrowse)choose(targetEdit);else if(id==PackageBrowse)choose(packageEdit);
   else start(id);
  }return 0;
 case Finished:{std::unique_ptr<Result>r(reinterpret_cast<Result*>(l));setBusy(false);log(explanation(*r));if(r->exit==0)refreshData();return 0;}
 case WM_CLOSE:if(busy){MessageBoxW(h,L"工作仍在進行，請等待結果。",L"正在處理",MB_OK|MB_ICONINFORMATION);return 0;}DestroyWindow(h);return 0;
 case WM_DESTROY:DeleteObject(font);PostQuitMessage(0);return 0;
 }return DefWindowProcW(h,message,w,l);}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int show){
 wchar_t path[32768];DWORD n=GetModuleFileNameW(nullptr,path,32768);if(!n||n>=32768)return 1;managerRoot=fs::path(std::wstring(path,n)).parent_path();
 int argc=0;auto argv=CommandLineToArgvW(GetCommandLineW(),&argc);
 if(argc>1){
  Result result;try{
   product::need(argc>=3&&std::wstring(argv[1])==L"--shared","SHARED_ARGUMENTS");Task task;task.action=argv[2];
   product::need(task.action==L"install-pack"||task.action==L"status"||task.action==L"rollback-bundle","SHARED_ACTION");
   std::set<std::wstring> keys;for(int i=3;i<argc;i+=2){product::need(i+1<argc,"SHARED_ARGUMENTS");std::wstring key=argv[i];product::need(keys.insert(key).second,"SHARED_ARGUMENTS");
    if(key==L"--game")task.game=product::absolute(argv[i+1]);else if(key==L"--pack")task.pack=product::absolute(argv[i+1]);else throw std::runtime_error("SHARED_ARGUMENTS");}
   product::need(!task.game.empty()&&(task.action!=L"install-pack"||!task.pack.empty()),"SHARED_ARGUMENTS");result=execute(task);
  }catch(const std::exception&e){result.output=decode(e.what());}
  LocalFree(argv);int count=WideCharToMultiByte(CP_UTF8,0,result.output.data(),int(result.output.size()),nullptr,0,nullptr,nullptr);std::string bytes(count,0);WideCharToMultiByte(CP_UTF8,0,result.output.data(),int(result.output.size()),bytes.data(),count,nullptr,nullptr);DWORD written{};WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),bytes.data(),DWORD(bytes.size()),&written,nullptr);return int(result.exit);
 }
 LocalFree(argv);
 CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);SetProcessDPIAware();WNDCLASSW wc{};wc.lpfnWndProc=window;wc.hInstance=instance;wc.lpszClassName=L"GEHD_Product_Manager";wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hbrBackground=HBRUSH(COLOR_BTNFACE+1);RegisterClassW(&wc);
 HWND h=CreateWindowExW(0,wc.lpszClassName,L"GER／GE2RB 高清模組管理器",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,CW_USEDEFAULT,CW_USEDEFAULT,810,760,nullptr,nullptr,instance,nullptr);if(!h){CoUninitialize();return 1;}ShowWindow(h,show);MSG msg{};while(GetMessageW(&msg,nullptr,0,0)>0){if(!IsDialogMessageW(h,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}}CoUninitialize();return int(msg.wParam);
}
