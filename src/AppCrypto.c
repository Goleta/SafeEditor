//
// 2015 Aug 22
//
// This is free and unencumbered software released into the public domain.
//

#include "AppCrypto.h"
#include <Wincrypt.h>
#include <bcrypt.h>
#include <DelayImp.h>
#include "AppBase.h"
#include "LibraryLoader.h"


typedef struct ICryptoApiProv
{
    ICryptoProv icp;
    HCRYPTPROV prov;

} ICryptoApiProv;

static BOOL WINAPI ICryptoApiProv_Release(ICryptoProv *This)
{
    ICryptoApiProv *thisObj = (ICryptoApiProv*)This;

    if (!thisObj)
    {
        return TRUE;
    }

    if (thisObj->prov && CryptReleaseContext(thisObj->prov, 0))
    {
        thisObj->prov = NULL;
    }

    return FreeBlock(thisObj);
}

static LPWSTR WINAPI ICryptoApiProv_GetName(ICryptoProv *This)
{
    return _T("CryptoAPI");
}

static BOOL WINAPI ICryptoApiProv_Random(ICryptoProv *This, void* buffer, UINT32 length)
{
    ICryptoApiProv *thisObj = (ICryptoApiProv*)This;

    return thisObj && CryptGenRandom(thisObj->prov, length, (BYTE*)buffer);
}

#define FILE_MAX_PASSWORD 127

typedef struct HMAC_SHA256_PLAINTEXTKEYBLOB
{
    BLOBHEADER hdr;
    DWORD      dwKeySize;
    BYTE       rgbKeyData[max((FILE_MAX_PASSWORD + 1) * 4, SHA256_BLOCK_SIZE)];
} HMAC_SHA256_PLAINTEXTKEYBLOB;

static HCRYPTKEY WINAPI CryptImportHmacSha256Key(HCRYPTPROV prov, const UINT8 *key, DWORD keyLength)
{
    HMAC_SHA256_PLAINTEXTKEYBLOB blob;

    if (!key || (keyLength > sizeof(blob.rgbKeyData)))
    {
        return NULL;
    }

    blob.hdr.bType = PLAINTEXTKEYBLOB;
    blob.hdr.bVersion = CUR_BLOB_VERSION;
    blob.hdr.reserved = 0;
    blob.hdr.aiKeyAlg = CALG_RC2;
    blob.dwKeySize = keyLength;

    Movs8(blob.rgbKeyData, key, keyLength);

    if (keyLength < SHA256_BLOCK_SIZE)
    {
        DWORD dwPad = SHA256_BLOCK_SIZE - keyLength;

        Stos8(&blob.rgbKeyData[keyLength], 0, dwPad);

        blob.dwKeySize += dwPad;
    }

    DWORD dwDataLen = FIELD_OFFSET(HMAC_SHA256_PLAINTEXTKEYBLOB, rgbKeyData) + blob.dwKeySize;
    HCRYPTKEY hKey = NULL;

    BOOL status = CryptImportKey(prov, (BYTE*)&blob, dwDataLen, NULL, CRYPT_IPSEC_HMAC_KEY, &hKey);

    SecureZeroMemory(blob.rgbKeyData, keyLength);

    return status ? hKey : NULL;
}

static HCRYPTHASH WINAPI CryptCreateHmacSha256Hash(HCRYPTPROV prov, HCRYPTKEY hKey)
{
    if (!hKey)
    {
        return NULL;
    }

    HCRYPTHASH hHash = NULL;
    HMAC_INFO info;

    ZeroMemory(&info, sizeof(info));

    info.HashAlgid = CALG_SHA_256;

    if (CryptCreateHash(prov, CALG_HMAC, hKey, 0, &hHash) &&
        CryptSetHashParam(hHash, HP_HMAC_INFO, (BYTE*)&info, 0))
    {
        return hHash;
    }

    if (hHash)
    {
        DWORD dwError = GetLastError();

        CryptDestroyHash(hHash);

        SetLastError(dwError);
    }

    return NULL;
}

static FORCEINLINE HCRYPTHASH WINAPI CryptCloneHash(HCRYPTHASH hHash)
{
    HCRYPTHASH hHashClone;

    return CryptDuplicateHash(hHash, 0, 0, &hHashClone) ? hHashClone : NULL;
}

typedef struct CRYPTOAPI_PBKDF2_SHA256_STATE
{
    UINT32 okey[SHA256_HASH_SIZE / sizeof(UINT32)];
    UINT32 obuf[SHA256_HASH_SIZE / sizeof(UINT32)];
    UINT32 count;
    UINT32 be_count;
} CRYPTOAPI_PBKDF2_SHA256_STATE;

static BOOL WINAPI ICryptoApiProv_Pbkdf2Compute(ICryptoProv *This, const void *password, UINT32 passwordLength, const void *salt, UINT32 saltLength, void *key, UINT32 keyLength, UINT32 rounds)
{
    CRYPTOAPI_PBKDF2_SHA256_STATE st;

    if ((passwordLength && !password) || !salt || !saltLength || !key || !keyLength || !rounds)
    {
        return FALSE;
    }

    HCRYPTKEY hKey = NULL;
    HCRYPTHASH hHash = NULL;
    HCRYPTHASH h1 = NULL;
    BOOL result = FALSE;
    DWORD pdwDataLen = SHA256_HASH_SIZE;

    ICryptoApiProv *thisObj = (ICryptoApiProv*)This;

    if ((hKey = CryptImportHmacSha256Key(thisObj->prov, (UINT8*)password, passwordLength)) &&
        (hHash = CryptCreateHmacSha256Hash(thisObj->prov, hKey)))
    {
        for (st.count = 1;; st.count++)
        {
            st.be_count = Swap32(st.count);

            if (!(h1 = CryptCloneHash(hHash)) ||
                !CryptHashData(h1, (BYTE*)salt, saltLength, 0) ||
                !CryptHashData(h1, (BYTE*)&st.be_count, sizeof(st.be_count), 0) ||
                !CryptGetHashParam(h1, HP_HASHVAL, (BYTE*)st.obuf, &pdwDataLen, 0))
            {
                goto cleanup;
            }

            CryptDestroyHash(h1);
            h1 = NULL;

            Movs32(st.okey, st.obuf, COUNT_OF(st.obuf));

            for (UINT32 i = 1; i < rounds; i++)
            {
                if (!(h1 = CryptCloneHash(hHash)) ||
                    !CryptHashData(h1, (BYTE*)st.obuf, sizeof(st.obuf), 0) ||
                    !CryptGetHashParam(h1, HP_HASHVAL, (BYTE*)st.obuf, &pdwDataLen, 0))
                {
                    goto cleanup;
                }

                CryptDestroyHash(h1);
                h1 = NULL;

                for (UINT32 j = 0; j < COUNT_OF(st.obuf); j++)
                {
                    st.okey[j] ^= st.obuf[j];
                }
            }

            UINT32 r = min(keyLength, SHA256_HASH_SIZE);

            Movs8(key, st.okey, r);

            keyLength -= r;

            if (!keyLength)
            {
                break;
            }

            key = ((UINT8*)key) + r;
        }

        result = TRUE;
    }

cleanup:

    SecureZeroMemory(&st, sizeof(st));

    DWORD dwError = GetLastError();

    if (h1)
    {
        CryptDestroyHash(h1);
    }

    if (hHash)
    {
        CryptDestroyHash(hHash);
    }

    if (hKey)
    {
        CryptDestroyKey(hKey);
    }

    SetLastError(dwError);

    return result;
}

static const UINT32 ZeroIV[4] =
{
    0x0, 0x0, 0x0, 0x0
};

typedef struct AES256_PLAINTEXTKEYBLOB
{
    BLOBHEADER hdr;
    DWORD      dwKeySize;
    BYTE       rgbKeyData[AES256_KEY_SIZE];
} AES256_PLAINTEXTKEYBLOB;

static HCRYPTKEY WINAPI CryptImportAes256Key(HCRYPTPROV prov, const UINT8 key[AES256_KEY_SIZE])
{
    AES256_PLAINTEXTKEYBLOB blob;

    if (!key)
    {
        return NULL;
    }

    blob.hdr.bType = PLAINTEXTKEYBLOB;
    blob.hdr.bVersion = CUR_BLOB_VERSION;
    blob.hdr.reserved = 0;
    blob.hdr.aiKeyAlg = CALG_AES_256;
    blob.dwKeySize = AES256_KEY_SIZE;

    Movs8(blob.rgbKeyData, key, AES256_KEY_SIZE);

    DWORD dwDataLen = FIELD_OFFSET(AES256_PLAINTEXTKEYBLOB, rgbKeyData) + AES256_KEY_SIZE;
    HCRYPTKEY hKey = NULL;

    BOOL status = CryptImportKey(prov, (BYTE*)&blob, dwDataLen, NULL, 0, &hKey);

    SecureZeroMemory(blob.rgbKeyData, AES256_KEY_SIZE);

    if (status)
    {
        DWORD dwMode = CRYPT_MODE_CBC;
        //  DWORD dwPadding = PKCS5_PADDING;
        CryptSetKeyParam(hKey, KP_MODE, (BYTE*)&dwMode, 0);
        //  CryptSetKeyParam(hKey, KP_PADDING, (BYTE*)&dwPadding, 0);
        CryptSetKeyParam(hKey, KP_IV, (BYTE*)ZeroIV, 0);
    }

    return status ? hKey : NULL;
}

static FORCEINLINE HCRYPTKEY WINAPI CryptCloneKey(HCRYPTKEY hKey)
{
    HCRYPTKEY  hKeyClone;

    return CryptDuplicateKey(hKey, 0, 0, &hKeyClone) ? hKeyClone : NULL;
}

typedef struct ICryptoApiKey
{
    ICryptoKey ick;
    HCRYPTKEY aes256Key;
    HCRYPTKEY sha256Key;
    HCRYPTHASH sha256;
    UINT32 extraBytesLen;
    UINT8 extraBytes[1];

} ICryptoApiKey;

static BOOL WINAPI ICryptoApiKey_Release(ICryptoKey *This)
{
    ICryptoApiKey *thisObj = (ICryptoApiKey*)This;

    if (!thisObj)
    {
        return TRUE;
    }

    if (thisObj->aes256Key)
    {
        CryptDestroyKey(thisObj->aes256Key);
    }

    if (thisObj->sha256Key)
    {
        CryptDestroyKey(thisObj->sha256Key);
    }

    if (thisObj->sha256)
    {
        CryptDestroyHash(thisObj->sha256);
    }
    
    SecureZeroMemory(thisObj->extraBytes, thisObj->extraBytesLen);

    return FreeBlock(thisObj);
}

static ICryptoKey* WINAPI ICryptoApiKey_Clone(ICryptoKey *This)
{
    const ICryptoApiKey *thisObj = (ICryptoApiKey*)This;

    ICryptoApiKey *keyObj = (ICryptoApiKey*)AllocClearBlock(FIELD_OFFSET(ICryptoApiKey, extraBytes) + thisObj->extraBytesLen);

    if (!keyObj)
    {
        return NULL;
    }

    keyObj->ick = thisObj->ick;

    if (!(keyObj->aes256Key = CryptCloneKey(thisObj->aes256Key)) ||
        !(keyObj->sha256Key = CryptCloneKey(thisObj->sha256Key)) ||
        !(keyObj->sha256 = CryptCloneHash(thisObj->sha256)))
    {
        keyObj->ick.lpVtbl->Release(&keyObj->ick);

        return NULL;
    }

    keyObj->extraBytesLen = thisObj->extraBytesLen;

    CopyMemory(keyObj->extraBytes, thisObj->extraBytes, keyObj->extraBytesLen);

    return &keyObj->ick;
}

static BOOL WINAPI ICryptoApiKey_EncryptOrDecryptPage(ICryptoKey *This, void *dataIn, void *dataOut, UINT32 length, BOOL encrypt)
{
    const ICryptoApiKey *thisObj = (ICryptoApiKey*)This;

    HCRYPTKEY aes256Key = CryptCloneKey(thisObj->aes256Key);
    BOOL result = FALSE;

    if (aes256Key)
    {
        DWORD dwDataLen = length;

        if (dataIn != dataOut)
        {
            CopyMemory(dataOut, dataIn, length);
        }

        result = encrypt ? CryptEncrypt(aes256Key, NULL, FALSE, 0, (BYTE*)dataOut, &dwDataLen, length) :
                           CryptDecrypt(aes256Key, NULL, FALSE, 0, (BYTE*)dataOut, &dwDataLen);

        CryptDestroyKey(aes256Key);
    }

    return result;
}

static BOOL WINAPI ICryptoApiKey_EncryptPage(ICryptoKey *This, void *dataIn, void *dataOut, UINT32 length)
{
    return ICryptoApiKey_EncryptOrDecryptPage(This, dataIn, dataOut, length, TRUE);
}

static BOOL WINAPI ICryptoApiKey_DecryptPage(ICryptoKey *This, void *dataIn, void *dataOut, UINT32 length)
{
    return ICryptoApiKey_EncryptOrDecryptPage(This, dataIn, dataOut, length, FALSE);
}

static BOOL WINAPI ICryptoApiKey_SignPage(ICryptoKey *This, const void *data, UINT32 length, UINT32 pageNo, UINT8 hash[SHA256_HASH_SIZE])
{
    const ICryptoApiKey *thisObj = (ICryptoApiKey*)This;

    HCRYPTHASH h = CryptCloneHash(thisObj->sha256);
    DWORD dwDataLen = SHA256_HASH_SIZE;

    BOOL result = h && CryptHashData(h, (BYTE*)data, length, 0) &&
        CryptHashData(h, (BYTE*)&pageNo, sizeof(pageNo), 0) &&
        CryptGetHashParam(h, HP_HASHVAL, hash, &dwDataLen, 0);

    if (h)
    {
        CryptDestroyHash(h);
    }

    return result;
}

static UINT8* WINAPI ICryptoApiKey_GetExtraBytesPtr(ICryptoKey *This)
{
    ICryptoApiKey *thisObj = (ICryptoApiKey*)This;

    if (!thisObj->extraBytesLen)
    {
        return NULL;
    }

    return thisObj->extraBytes;
}

static const ICryptoKeyVtbl ICryptoApiKeyVtbl = {
    ICryptoApiKey_Release,
    ICryptoApiKey_Clone,
    ICryptoApiKey_EncryptPage,
    ICryptoApiKey_DecryptPage,
    ICryptoApiKey_SignPage,
    ICryptoApiKey_GetExtraBytesPtr
};

static ICryptoKey* WINAPI ICryptoApiProv_InitKey(ICryptoProv *This, const UINT8 encKey[AES256_KEY_SIZE], const UINT8 signKey[SHA256_BLOCK_SIZE], UINT32 extraBytesLen, void *extraBytes)
{
    const ICryptoApiProv *thisObj = (ICryptoApiProv*)This;

    ICryptoApiKey *keyObj = (ICryptoApiKey*)AllocClearBlock(FIELD_OFFSET(ICryptoApiKey, extraBytes) + extraBytesLen);

    if (!keyObj)
    {
        return NULL;
    }

    keyObj->ick.lpVtbl = &ICryptoApiKeyVtbl;

    if (!(keyObj->aes256Key = CryptImportAes256Key(thisObj->prov, encKey)) ||
        !(keyObj->sha256Key = CryptImportHmacSha256Key(thisObj->prov, signKey, SHA256_BLOCK_SIZE)) ||
        !(keyObj->sha256 = CryptCreateHmacSha256Hash(thisObj->prov, keyObj->sha256Key)))
    {
        keyObj->ick.lpVtbl->Release(&keyObj->ick);

        return NULL;
    }

    keyObj->extraBytesLen = extraBytesLen;

    if (extraBytes)
    {
        CopyMemory(keyObj->extraBytes, extraBytes, keyObj->extraBytesLen);
    }
    
    return &keyObj->ick;
}

static const ICryptoProvVtbl ICryptoApiProvVtbl = {
    ICryptoApiProv_Release,
    ICryptoApiProv_GetName,
    ICryptoApiProv_Random,
    ICryptoApiProv_Pbkdf2Compute,
    ICryptoApiProv_InitKey
};


static ICryptoProv* WINAPI ICryptoApiProv_Init(const OSVERSIONINFOEX *osVersion)
{
  /*  DWORD osMajor = 5;
    DWORD osMinor = 1;
    // DWORD osMajorSp = 3;

    BOOL isSupportedOs = (osVersion->dwPlatformId == VER_PLATFORM_WIN32_NT) &&
                         ((osVersion->dwMajorVersion > osMajor) ||
                         ((osVersion->dwMajorVersion == osMajor) &&
                         (osVersion->dwMinorVersion >= osMinor))); // &&
                        // (osVersion->wServicePackMajor >= osMajorSp)));

    if (!isSupportedOs)
    {
      ///  return NULL;
    }
    */

    HMODULE lib = LoadSystemLibrary(_T("ADVAPI32.dll"));

    if (!lib)
    {
        return FALSE;
    }

    // delayhlp.cpp
    if (FAILED(__HrLoadAllImportsForDll("ADVAPI32.dll")))
    {
        return FALSE;
    }

    LPCWSTR provType = /*(osVersion->dwMajorVersion == 5 && osVersion->dwMinorVersion == 1) ? MS_ENH_RSA_AES_PROV_XP :*/ MS_ENH_RSA_AES_PROV;

    ICryptoApiProv *prov = (ICryptoApiProv*)AllocClearBlock(sizeof(ICryptoApiProv));

    if (!prov)
    {
        return NULL;
    }

    prov->icp.lpVtbl = &ICryptoApiProvVtbl;

    if (!CryptAcquireContext(&prov->prov, NULL, provType, PROV_RSA_AES, CRYPT_VERIFYCONTEXT | CRYPT_SILENT))
    {
        prov->icp.lpVtbl->Release(&prov->icp);

        return NULL;
    }
    
    return &prov->icp;
}




//
// CNG/BCRYPT API --------------------------
//

typedef struct ICngKey
{
    ICryptoKey ick;
    BCRYPT_KEY_HANDLE aes256Key;
    BCRYPT_HASH_HANDLE sha256;
    ULONG aes256ObjLen;
    ULONG sha256ObjLen;
    UINT32 extraBytesLen;
    PUCHAR aes256KeyObj;
    PUCHAR aes256KeyObjTmp;
    PUCHAR sha256Obj;
    PUCHAR sha256ObjTmp;
    PUCHAR extraBytes;
    UCHAR data[1];

} ICngKey;

static BOOL WINAPI ICngKey_Release(ICryptoKey *This)
{
    ICngKey *thisObj = (ICngKey*)This;

    if (!thisObj)
    {
        return TRUE;
    }

    if (thisObj->sha256 && BCRYPT_SUCCESS(BCryptDestroyHash(thisObj->sha256)))
    {
        thisObj->sha256 = NULL;
    }

    if (thisObj->aes256Key && BCRYPT_SUCCESS(BCryptDestroyKey(thisObj->aes256Key)))
    {
        thisObj->aes256Key = NULL;
    }

    SecureZeroMemory(thisObj->extraBytes, thisObj->extraBytesLen);

    return FreeBlock(thisObj);
}

static ICngKey* WINAPI AllocICngKey(UINT32 aes256ObjLen, UINT32 hmacSha256ObjLen, UINT32 extraBytesLen)
{
    ICngKey* hKey = (ICngKey*)AllocClearBlock(FIELD_OFFSET(ICngKey, data) + (aes256ObjLen * 2) + (hmacSha256ObjLen * 2) + extraBytesLen);

    if (hKey)
    {
        hKey->aes256KeyObj = hKey->data;
        hKey->aes256KeyObjTmp = hKey->aes256KeyObj + aes256ObjLen;
        hKey->sha256Obj = hKey->aes256KeyObjTmp + aes256ObjLen;
        hKey->sha256ObjTmp = hKey->sha256Obj + hmacSha256ObjLen;
        hKey->extraBytes = hKey->sha256ObjTmp + hmacSha256ObjLen;

        hKey->aes256ObjLen = aes256ObjLen;
        hKey->sha256ObjLen = hmacSha256ObjLen;
        hKey->extraBytesLen = extraBytesLen;
    }

    return hKey;
}

static ICryptoKey* WINAPI ICngKey_Clone(ICryptoKey *This)
{
    const ICngKey *thisObj = (ICngKey*)This;

    ICngKey *keyObj = AllocICngKey(thisObj->aes256ObjLen, thisObj->sha256ObjLen, thisObj->extraBytesLen);

    if (!keyObj)
    {
        return NULL;
    }

    keyObj->ick = thisObj->ick;

    if (!BCRYPT_SUCCESS(BCryptDuplicateKey(thisObj->aes256Key, &keyObj->aes256Key, keyObj->aes256KeyObj, keyObj->aes256ObjLen, 0)) ||
        !BCRYPT_SUCCESS(BCryptDuplicateHash(thisObj->sha256, &keyObj->sha256, keyObj->sha256Obj, keyObj->sha256ObjLen, 0)))
    {
        keyObj->ick.lpVtbl->Release(&keyObj->ick);

        return NULL;
    }
    
    CopyMemory(keyObj->extraBytes, thisObj->extraBytes, keyObj->extraBytesLen);

    return &keyObj->ick;

}

static BOOL WINAPI ICngKey_EncryptOrDecryptPage(ICryptoKey *This, void *dataIn, void *dataOut, UINT32 length, BOOL encrypt)
{
    ICngKey *thisObj = (ICngKey*)This;

    ULONG cbResult;
    BCRYPT_KEY_HANDLE hKeyTmp = NULL;

    BOOL result = BCRYPT_SUCCESS(BCryptDuplicateKey(thisObj->aes256Key, &hKeyTmp, thisObj->aes256KeyObjTmp, thisObj->aes256ObjLen, 0)) &&
        (encrypt ?
        BCRYPT_SUCCESS(BCryptEncrypt(hKeyTmp, (PUCHAR)dataIn, length, NULL, NULL, 0, (PUCHAR)dataOut, length, &cbResult, 0)) :
        BCRYPT_SUCCESS(BCryptDecrypt(hKeyTmp, (PUCHAR)dataIn, length, NULL, NULL, 0, (PUCHAR)dataOut, length, &cbResult, 0)));

    if (hKeyTmp)
    {
        BCryptDestroyKey(hKeyTmp);
    }

    return result;
}

static BOOL WINAPI ICngKey_EncryptPage(ICryptoKey *This, void *dataIn, void *dataOut, UINT32 length)
{
    return ICngKey_EncryptOrDecryptPage(This, dataIn, dataOut, length, TRUE);
}

static BOOL WINAPI ICngKey_DecryptPage(ICryptoKey *This, void *dataIn, void *dataOut, UINT32 length)
{
    return ICngKey_EncryptOrDecryptPage(This, dataIn, dataOut, length, FALSE);
}

static BOOL WINAPI ICngKey_SignPage(ICryptoKey *This, const void *data, UINT32 length, UINT32 pageNo, UINT8 hash[SHA256_HASH_SIZE])
{
    ICngKey *thisObj = (ICngKey*)This;

    BCRYPT_HASH_HANDLE hHashTmp = NULL;

    BOOL result = BCRYPT_SUCCESS(BCryptDuplicateHash(thisObj->sha256, &hHashTmp, thisObj->sha256ObjTmp, thisObj->sha256ObjLen, 0)) &&
        BCRYPT_SUCCESS(BCryptHashData(hHashTmp, (PUCHAR)data, length, 0)) &&
        BCRYPT_SUCCESS(BCryptHashData(hHashTmp, (PUCHAR)&pageNo, sizeof(pageNo), 0)) &&
        BCRYPT_SUCCESS(BCryptFinishHash(hHashTmp, (PUCHAR)hash, SHA256_HASH_SIZE, 0));

    if (hHashTmp)
    {
        BCryptDestroyHash(hHashTmp);
    }

    return result;
}

static UINT8* WINAPI ICngKey_GetExtraBytesPtr(ICryptoKey *This)
{
    ICngKey *thisObj = (ICngKey*)This;

    if (!thisObj->extraBytesLen)
    {
        return NULL;
    }

    return thisObj->extraBytes;
}


static const ICryptoKeyVtbl ICngKeyVtbl = {
    ICngKey_Release,
    ICngKey_Clone,
    ICngKey_EncryptPage,
    ICngKey_DecryptPage,
    ICngKey_SignPage,
    ICngKey_GetExtraBytesPtr
};


typedef struct ICngProv
{
    ICryptoProv icp;
    BCRYPT_ALG_HANDLE rngAlg;
    BCRYPT_ALG_HANDLE hmacSha256Alg;
    BCRYPT_ALG_HANDLE aes256Alg;

} ICngProv;

static BOOL WINAPI ICngProv_Release(ICryptoProv *This)
{
    ICngProv *thisObj = (ICngProv*)This;

    if (!thisObj)
    {
        return TRUE;
    }

    if (thisObj->rngAlg && BCRYPT_SUCCESS(BCryptCloseAlgorithmProvider(thisObj->rngAlg, 0)))
    {
        thisObj->rngAlg = NULL;
    }

    if (thisObj->hmacSha256Alg && BCRYPT_SUCCESS(BCryptCloseAlgorithmProvider(thisObj->hmacSha256Alg, 0)))
    {
        thisObj->hmacSha256Alg = NULL;
    }

    if (thisObj->aes256Alg && BCRYPT_SUCCESS(BCryptCloseAlgorithmProvider(thisObj->aes256Alg, 0)))
    {
        thisObj->aes256Alg = NULL;
    }

    return FreeBlock(thisObj);
}

static LPWSTR WINAPI ICngProv_GetName(ICryptoProv *This)
{
    return _T("CNG");
}

static BOOL WINAPI ICngProv_Random(ICryptoProv *This, void* buffer, UINT32 length)
{
    ICngProv *thisObj = (ICngProv*)This;

    return thisObj && BCRYPT_SUCCESS(BCryptGenRandom(thisObj->rngAlg, (PUCHAR)buffer, length, 0));
}

typedef struct CNG_PBKDF2_SHA256_STATE
{
    UINT32 okey[SHA256_HASH_SIZE / sizeof(UINT32)];
    UINT32 obuf[SHA256_HASH_SIZE / sizeof(UINT32)];
    UINT32 count;
    UINT32 be_count;
    UCHAR hashObj[1];
} CNG_PBKDF2_SHA256_STATE;

/*
// BCryptDeriveKeyPBKDF2 alternative is only available on Win7 and higher.
*/
static BOOL WINAPI ICngProv_Pbkdf2Compute(ICryptoProv *This, const void *password, UINT32 passwordLength, const void *salt, UINT32 saltLength, void* key, UINT32 keyLength, UINT32 rounds)
{
    ICngProv *thisObj = (ICngProv*)This;

    if ((passwordLength && !password) || !salt || !saltLength || !key || !keyLength || !rounds)
    {
        return FALSE;
    }

    ULONG pcbResult;
    DWORD objLength;
    CNG_PBKDF2_SHA256_STATE *st = NULL;
    BCRYPT_HASH_HANDLE hHash = NULL;
    BCRYPT_HASH_HANDLE h1 = NULL;
    BOOL result = FALSE;

    if (BCRYPT_SUCCESS(BCryptGetProperty(thisObj->hmacSha256Alg, BCRYPT_OBJECT_LENGTH, (PUCHAR)&objLength, sizeof(objLength), &pcbResult, 0)))
    {
        st = (CNG_PBKDF2_SHA256_STATE*)AllocBlock(FIELD_OFFSET(CNG_PBKDF2_SHA256_STATE, hashObj) + (objLength * 2));

        if (st && BCRYPT_SUCCESS(BCryptCreateHash(thisObj->hmacSha256Alg, &hHash, st->hashObj, objLength, (PUCHAR)password, passwordLength, 0)))
        {
            for (st->count = 1;; st->count++)
            {
                st->be_count = Swap32(st->count);

                if (!BCRYPT_SUCCESS(BCryptDuplicateHash(hHash, &h1, st->hashObj + objLength, objLength, 0)) ||
                    !BCRYPT_SUCCESS(BCryptHashData(h1, (PUCHAR)salt, saltLength, 0)) ||
                    !BCRYPT_SUCCESS(BCryptHashData(h1, (PUCHAR)&st->be_count, sizeof(st->be_count), 0)) ||
                    !BCRYPT_SUCCESS(BCryptFinishHash(h1, (PUCHAR)st->obuf, sizeof(st->obuf), 0)))
                {
                    goto cleanup;
                }

                BCryptDestroyHash(h1);
                h1 = NULL;

                Movs32(st->okey, st->obuf, COUNT_OF(st->obuf));

                for (UINT32 i = 1; i < rounds; i++)
                {
                    if (!BCRYPT_SUCCESS(BCryptDuplicateHash(hHash, &h1, st->hashObj + objLength, objLength, 0)) ||
                        !BCRYPT_SUCCESS(BCryptHashData(h1, (PUCHAR)st->obuf, sizeof(st->obuf), 0)) ||
                        !BCRYPT_SUCCESS(BCryptFinishHash(h1, (PUCHAR)st->obuf, sizeof(st->obuf), 0)))
                    {
                        goto cleanup;
                    }

                    BCryptDestroyHash(h1);
                    h1 = NULL;

                    for (UINT32 j = 0; j < COUNT_OF(st->obuf); j++)
                    {
                        st->okey[j] ^= st->obuf[j];
                    }
                }

                UINT32 r = min(keyLength, SHA256_HASH_SIZE);

                Movs8(key, st->okey, r);

                keyLength -= r;

                if (!keyLength)
                {
                    break;
                }

                key = ((UINT8*)key) + r;
            }

            result = TRUE;
        }
    }

cleanup:

    if (h1)
    {
        BCryptDestroyHash(h1);
    }

    if (hHash)
    {
        BCryptDestroyHash(hHash);
    }

    if (st)
    {
        SecureZeroMemory(st, FIELD_OFFSET(CNG_PBKDF2_SHA256_STATE, hashObj));

        FreeBlock(st);
    }

    return result;
}

static ICryptoKey* WINAPI ICngProv_InitKey(ICryptoProv *This, const UINT8 encKey[AES256_KEY_SIZE], const UINT8 signKey[SHA256_BLOCK_SIZE], UINT32 extraBytesLen, void *extraBytes)
{
    const ICngProv *thisObj = (ICngProv*)This;
    
    UINT32 aes256ObjLen;
    UINT32 hmacSha256ObjLen;
    ULONG pcbResult;

    if (!BCRYPT_SUCCESS(BCryptGetProperty(thisObj->aes256Alg, BCRYPT_OBJECT_LENGTH, (PUCHAR)&aes256ObjLen, sizeof(aes256ObjLen), &pcbResult, 0)) ||
        !BCRYPT_SUCCESS(BCryptGetProperty(thisObj->hmacSha256Alg, BCRYPT_OBJECT_LENGTH, (PUCHAR)&hmacSha256ObjLen, sizeof(hmacSha256ObjLen), &pcbResult, 0)))
    {
        return NULL;
    }
    
    ICngKey *keyObj = AllocICngKey(aes256ObjLen, hmacSha256ObjLen, extraBytesLen);

    if (!keyObj)
    {
        return NULL;
    }

    keyObj->ick.lpVtbl = &ICngKeyVtbl;

    if (!BCRYPT_SUCCESS(BCryptGenerateSymmetricKey(thisObj->aes256Alg, &keyObj->aes256Key, keyObj->aes256KeyObj, keyObj->aes256ObjLen, (PUCHAR)encKey, AES256_KEY_SIZE, 0)) ||
        !BCRYPT_SUCCESS(BCryptCreateHash(thisObj->hmacSha256Alg, &keyObj->sha256, keyObj->sha256Obj, keyObj->sha256ObjLen, (PUCHAR)signKey, SHA256_BLOCK_SIZE, 0)))
    {
        keyObj->ick.lpVtbl->Release(&keyObj->ick);

        return NULL;
    }
    
    if (extraBytes)
    {
        CopyMemory(keyObj->extraBytes, extraBytes, keyObj->extraBytesLen);
    }

    return &keyObj->ick;
}

static const ICryptoProvVtbl ICngProvVtbl = {
    ICngProv_Release,
    ICngProv_GetName,
    ICngProv_Random,
    ICngProv_Pbkdf2Compute,
    ICngProv_InitKey
};


static ICryptoProv* WINAPI ICngProv_Init(const OSVERSIONINFOEX *osVersion)
{
 /*   if ((osVersion->dwPlatformId != VER_PLATFORM_WIN32_NT) || (osVersion->dwMajorVersion < 6))
    {
        return NULL;
    }
    */

    // https://msdn.microsoft.com/en-us/library/8yfshtha%28v=vs.120%29.aspx

    HMODULE lib = LoadSystemLibrary(_T("bcrypt.dll"));

    if (!lib)
    {
        return FALSE;
    }

    // delayhlp.cpp
    if (FAILED(__HrLoadAllImportsForDll("bcrypt.dll")))
    {
        return FALSE;
    }

    ICngProv *prov = (ICngProv*)AllocClearBlock(sizeof(ICngProv));

    if (!prov)
    {
        return NULL;
    }

    prov->icp.lpVtbl = &ICngProvVtbl;


    if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&prov->rngAlg, BCRYPT_RNG_ALGORITHM, MS_PRIMITIVE_PROVIDER, 0)) ||
        !BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&prov->hmacSha256Alg, BCRYPT_SHA256_ALGORITHM, MS_PRIMITIVE_PROVIDER, BCRYPT_ALG_HANDLE_HMAC_FLAG)) ||
        !BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&prov->aes256Alg, BCRYPT_AES_ALGORITHM, MS_PRIMITIVE_PROVIDER, 0)) ||
        !BCRYPT_SUCCESS(BCryptSetProperty(prov->aes256Alg, BCRYPT_CHAINING_MODE, (PUCHAR)BCRYPT_CHAIN_MODE_CBC, sizeof(BCRYPT_CHAIN_MODE_CBC), 0)))
    {
        prov->icp.lpVtbl->Release(&prov->icp);

        return NULL;
    }

    return &prov->icp;
}


typedef ICryptoProv* (WINAPI *IProvInitFunc)(const OSVERSIONINFOEX *osVersion);

static const IProvInitFunc AllProviders[] = { ICngProv_Init, ICryptoApiProv_Init };


ICryptoProv* WINAPI InitCryptoProv(const OSVERSIONINFOEX *osVersion)
{
    OSVERSIONINFOEX ver;
    
    if (!osVersion)
    {
        ver.dwOSVersionInfoSize = sizeof(ver);

        if (!GetVersionEx((LPOSVERSIONINFO)&ver))
        {
            return NULL;
        }

        osVersion = &ver;
    }
    
    for (UINT32 i = 0; i < COUNT_OF(AllProviders); i++)
    {
        IProvInitFunc func = AllProviders[i];

        ICryptoProv* prov = func(osVersion);

        if (prov)
        {
            return prov;
        }
    }

    return NULL;
}