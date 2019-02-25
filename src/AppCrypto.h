//
// 2015 Aug 22
//
// This is free and unencumbered software released into the public domain.
//

#ifndef __APPCRYPTO__7F82244EFCA44ED085ABD7AC400C184F_H__
#define __APPCRYPTO__7F82244EFCA44ED085ABD7AC400C184F_H__

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif


#define SHA256_HASH_SIZE 32
#define SHA256_BLOCK_SIZE 64

#define AES256_KEY_SIZE 32
#define AES256_BLOCK_SIZE 16


typedef struct ICryptoKeyVtbl ICryptoKeyVtbl;
typedef struct ICryptoKey ICryptoKey;

struct ICryptoKeyVtbl
{
    BOOL (WINAPI *Release)(ICryptoKey *This);
    ICryptoKey* (WINAPI *Clone)(ICryptoKey *This);
    BOOL (WINAPI *EncryptPage)(ICryptoKey *This, void *dataIn, void *dataOut, UINT32 length);
    BOOL (WINAPI *DecryptPage)(ICryptoKey *This, void *dataIn, void *dataOut, UINT32 length);
    BOOL (WINAPI *SignPage)(ICryptoKey *This, const void *data, UINT32 length, UINT32 pageNo, UINT8 hash[SHA256_HASH_SIZE]);
    UINT8* (WINAPI *GetExtraBytesPtr)(ICryptoKey *This);
};

struct ICryptoKey
{
    const ICryptoKeyVtbl* lpVtbl;
};


typedef struct ICryptoProvVtbl ICryptoProvVtbl;
typedef struct ICryptoProv ICryptoProv;

struct ICryptoProvVtbl
{
    BOOL (WINAPI *Release)(ICryptoProv *This);
    LPWSTR (WINAPI *GetName)(ICryptoProv *This);
    BOOL (WINAPI *Random)(ICryptoProv *This, void* buffer, UINT32 length);
    BOOL (WINAPI *Pbkdf2Compute)(ICryptoProv *This, const void *password, UINT32 passwordLength, const void *salt, UINT32 saltLength, void* key, UINT32 keyLength, UINT32 rounds);
    ICryptoKey* (WINAPI *InitKey)(ICryptoProv *This, const UINT8 encKey[AES256_KEY_SIZE], const UINT8 signKey[SHA256_BLOCK_SIZE], UINT32 extraBytesLen, void *extraBytes);
};

struct ICryptoProv
{
    const ICryptoProvVtbl *lpVtbl;
};


ICryptoProv* WINAPI InitCryptoProv(const OSVERSIONINFOEX *osVersion);



static FORCEINLINE BOOL WINAPI CryptoProvRelease(ICryptoProv *This)
{
    return This->lpVtbl->Release(This);
}

static FORCEINLINE LPWSTR WINAPI CryptoProvGetName(ICryptoProv *This)
{
    return This->lpVtbl->GetName(This);
}

static FORCEINLINE BOOL WINAPI CryptoProvRandom(ICryptoProv *This, void* buffer, UINT32 length)
{
    return This->lpVtbl->Random(This, buffer, length);
}

static FORCEINLINE BOOL WINAPI CryptoProvPbkdf2Compute(ICryptoProv *This, const void *password, UINT32 passwordLength, const void *salt, UINT32 saltLength, void *key, UINT32 keyLength, UINT32 rounds)
{
    return This->lpVtbl->Pbkdf2Compute(This, password, passwordLength, salt, saltLength, key, keyLength, rounds);
}

static FORCEINLINE ICryptoKey* WINAPI CryptoProvInitKey(ICryptoProv *This, const UINT8 encKey[AES256_KEY_SIZE], const UINT8 signKey[SHA256_BLOCK_SIZE], UINT32 extraBytesLen, void *extraBytes)
{
    return This->lpVtbl->InitKey(This, encKey, signKey, extraBytesLen, extraBytes);
}



static FORCEINLINE BOOL WINAPI CryptoKeyRelease(ICryptoKey *This)
{
    return This->lpVtbl->Release(This);
}

static FORCEINLINE ICryptoKey* WINAPI CryptoKeyClone(ICryptoKey *This)
{
    return This->lpVtbl->Clone(This);
}

static FORCEINLINE BOOL WINAPI CryptoKeyEncryptPage(ICryptoKey *This, void *dataIn, void *dataOut, UINT32 length)
{
    return This->lpVtbl->EncryptPage(This, dataIn, dataOut, length);
}

static FORCEINLINE BOOL WINAPI CryptoKeyDecryptPage(ICryptoKey *This, void *dataIn, void *dataOut, UINT32 length)
{
    return This->lpVtbl->DecryptPage(This, dataIn, dataOut, length);
}

static FORCEINLINE BOOL WINAPI CryptoKeySignPage(ICryptoKey *This, const void *data, UINT32 length, UINT32 pageNo, UINT8 hash[SHA256_HASH_SIZE])
{
    return This->lpVtbl->SignPage(This, data, length, pageNo, hash);
}

static FORCEINLINE UINT8* WINAPI CryptoKeyGetExtraBytesPtr(ICryptoKey *This)
{
    return This->lpVtbl->GetExtraBytesPtr(This);
}



#ifdef __cplusplus
}
#endif

#endif // __APPCRYPTO__7F82244EFCA44ED085ABD7AC400C184F_H__