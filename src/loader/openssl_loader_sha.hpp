#pragma once
// Opt-in loader candidate. Link the pinned, unmodified OpenSSL 3.5.9 x86
// sha256_block_data_order object and retain its Apache-2.0 license.
#include <intrin.h>
#include <array>
#include <cstdint>
#include <mutex>
#include <atomic>
#include <cstring>
#include <algorithm>
#include <stdexcept>
extern "C" {
inline unsigned int OPENSSL_ia32cap_P[10]={};
void __cdecl sha256_block_data_order(void*,const void*,size_t);
}
namespace opensslsha {
using Digest=std::array<unsigned char,32>;
class Stream {
 uint32_t state[8]={0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
 unsigned char tail[64]{};size_t used=0;uint64_t total=0;bool finished=false;
public:
 void add(const unsigned char*p,size_t n){if(finished||n>256u*1024*1024||total>UINT64_MAX/8-n||(!p&&n))throw std::runtime_error("SHA input");total+=n;if(!n)return;
  if(used){size_t take=std::min(n,64-used);memcpy(tail+used,p,take);used+=take;p+=take;n-=take;if(used==64){sha256_block_data_order(state,tail,1);used=0;}}
  size_t whole=n/64;if(whole){sha256_block_data_order(state,p,whole);p+=whole*64;n-=whole*64;}if(n){memcpy(tail,p,n);used=n;}
 }
 Digest finish(){if(finished)throw std::runtime_error("SHA finished");finished=true;tail[used++]=0x80;if(used>56){memset(tail+used,0,64-used);sha256_block_data_order(state,tail,1);used=0;}
  memset(tail+used,0,56-used);uint64_t bits=total*8;for(unsigned i=0;i<8;++i)tail[63-i]=static_cast<unsigned char>(bits>>(8*i));sha256_block_data_order(state,tail,1);Digest d{};for(unsigned i=0;i<8;++i)for(unsigned j=0;j<4;++j)d[4*i+j]=static_cast<unsigned char>(state[i]>>(24-8*j));return d;
 }
};
inline Digest raw(const void*p,size_t n){Stream s;s.add(static_cast<const unsigned char*>(p),n);return s.finish();}
inline std::once_flag once;
inline bool ready=false;
inline unsigned selected=0; // 0 = established CNG fallback, 2 = SSSE3, 3 = AVX/BMI.
#ifdef GEHD_SHA_INTEGRATION_TEST
// Fixture-only controls. Policy/init fault must be set before the first call;
// per-call faults change only while workers are quiescent. No product exports.
inline int policy=-1;
inline bool failSelfTest=false;
inline std::atomic<unsigned> initializations{},calls{},optimizedCalls{},cngCalls{};
inline unsigned failBackendAt=0,failCngAt=0;
#endif
inline bool answer(const char*input,const char*expected){auto d=raw(input,strlen(input));const char*hex="0123456789abcdef";for(unsigned i=0;i<32;++i)if(hex[d[i]>>4]!=expected[i*2]||hex[d[i]&15]!=expected[i*2+1])return false;return true;}
inline unsigned choose(bool ssse3,bool avxOs,bool bmi,bool intel,int limit){
 if(limit==0)return 0;
 if(ssse3&&avxOs&&bmi&&intel&&limit!=2)return 3;
 return ssse3?2u:0u;
}
inline void initialize(){
#ifdef GEHD_SHA_INTEGRATION_TEST
 ++initializations;
#endif
 int r[4];__cpuid(r,0);int max=r[0];bool intel=r[1]==0x756e6547&&r[3]==0x49656e69&&r[2]==0x6c65746e;
 bool ssse3=false,avxOs=false,bmi=false;
 if(max>=1){__cpuid(r,1);ssse3=(r[2]&(1<<9))&&(r[3]&(1<<24))&&(r[3]&(1<<26));if((r[2]&(1<<28))&&(r[2]&(1<<27))&&(r[2]&(1<<26)))avxOs=(_xgetbv(0)&6)==6;}
 if(max>=7){__cpuidex(r,7,0);bmi=(r[1]&(1<<3))&&(r[1]&(1<<8));}
 int limit=-1;
#ifdef GEHD_SHA_INTEGRATION_TEST
 limit=policy;
#endif
 selected=choose(ssse3,avxOs,bmi,intel,limit);
 if(!selected)return;
 // OpenSSL's Intel bit is a dispatch hint; enable only checked instructions.
 OPENSSL_ia32cap_P[0]=1u<<24;
 if(selected==3){OPENSSL_ia32cap_P[0]|=1u<<30;OPENSSL_ia32cap_P[1]=1u<<28;OPENSSL_ia32cap_P[2]=(1u<<3)|(1u<<8);}
 else OPENSSL_ia32cap_P[1]=1u<<9;
 bool valid=answer("","e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855")&&answer("abc","ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad")&&answer("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq","248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
#ifdef GEHD_SHA_INTEGRATION_TEST
 if(failSelfTest)valid=false;
#endif
 ready=valid;
}
inline bool tryDigest(const void*p,size_t n,unsigned char*out)noexcept{
#ifdef GEHD_SHA_INTEGRATION_TEST
 auto number=++calls;
#endif
 try{std::call_once(once,initialize);if(!ready||(!p&&n)||!out||n>256u*1024*1024)return false;
#ifdef GEHD_SHA_INTEGRATION_TEST
 if(failBackendAt&&number==failBackendAt)return false;
#endif
 auto d=raw(p,n);memcpy(out,d.data(),d.size());
#ifdef GEHD_SHA_INTEGRATION_TEST
 ++optimizedCalls;
#endif
 return true;}catch(...){return false;}
}
}
