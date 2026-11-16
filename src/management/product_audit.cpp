#include "product_files.hpp"
#include <iostream>
int wmain(int argc,wchar_t**argv){try{
 product::need(argc==2,"Usage: pack-audit.exe data-directory");
 auto p=product::package(argv[1]);product::verify(p,[](size_t n,size_t total){if(n%256==0)std::cerr<<n<<'/'<<total<<'\n';});
 std::cout<<"{\"verified\":true,\"files\":"<<p.files.size()<<",\"bytes\":"<<p.bytes<<",\"release\":\""<<p.release<<"\"}\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
