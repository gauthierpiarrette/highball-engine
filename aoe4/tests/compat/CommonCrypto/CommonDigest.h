/* Test-only CommonCrypto compatibility shim for Linux. */
#include <openssl/sha.h>
typedef SHA256_CTX CC_SHA256_CTX;
typedef unsigned int CC_LONG;
#define CC_SHA256_Init SHA256_Init
#define CC_SHA256_Update SHA256_Update
#define CC_SHA256_Final SHA256_Final
