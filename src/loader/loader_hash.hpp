#pragma once
#ifdef GEHD_HASH_OPENSSL
#include "openssl_loader_sha.hpp"
#endif
// BCryptHash already requires Windows 10. The SHA-256 pseudo-handle avoids
// repeated provider open/close and has no owned handle or shared mutable hash.
namespace loaderhash {
#ifdef GEHD_OPTIMIZATION_TEST
std::atomic<unsigned> calls{},pseudoCalls{},opens{},closes{};
bool useReuse=true,failPseudo=false,failOpen=false,failLegacyHash=false;
#endif
inline NTSTATUS legacy(const void*p,ULONG n,unsigned char* digest){
#if defined(GEHD_HASH_OPENSSL) && defined(GEHD_SHA_INTEGRATION_TEST)
 ++opensslsha::cngCalls;
 if(opensslsha::failCngAt&&opensslsha::calls==opensslsha::failCngAt)return NTSTATUS(0xc0000001u);
#endif
 BCRYPT_ALG_HANDLE algorithm{};
#ifdef GEHD_OPTIMIZATION_TEST
 ++opens;if(failOpen)return NTSTATUS(0xc0000001u);
#endif
 auto result=BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0);
 if(result<0)return result;
#ifdef GEHD_OPTIMIZATION_TEST
 result=failLegacyHash?NTSTATUS(0xc0000001u):BCryptHash(algorithm,nullptr,0,(PUCHAR)p,n,digest,32);
 ++closes;
#else
 result=BCryptHash(algorithm,nullptr,0,(PUCHAR)p,n,digest,32);
#endif
 BCryptCloseAlgorithmProvider(algorithm,0);return result;
}
inline NTSTATUS digest(const void*p,ULONG n,unsigned char* output){
#ifdef GEHD_OPTIMIZATION_TEST
 ++calls;
#endif
#ifdef GEHD_HASH_OPENSSL
 if(opensslsha::tryDigest(p,n,output))return 0;
#endif
#ifdef GEHD_HASH_REUSE
#ifdef GEHD_OPTIMIZATION_TEST
 if(useReuse){++pseudoCalls;auto result=failPseudo?NTSTATUS(0xc0000001u):BCryptHash(BCRYPT_SHA256_ALG_HANDLE,nullptr,0,(PUCHAR)p,n,output,32);if(result>=0)return result;}
#else
 auto result=BCryptHash(BCRYPT_SHA256_ALG_HANDLE,nullptr,0,(PUCHAR)p,n,output,32);if(result>=0)return result;
#endif
 // Keep the established provider path available if the optimized call fails.
#endif
 return legacy(p,n,output);
}
}
