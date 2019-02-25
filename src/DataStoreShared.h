//
// 2013 June 12
//
// This is free and unencumbered software released into the public domain.
//

#ifndef __DATASTORESHARED_H__
#define __DATASTORESHARED_H__

#ifdef __cplusplus
extern "C" {
#endif


struct KEYINFO
{
    UINT32 passLen;
    UINT32 keyLen;
    void *pass;
    void *key;
};

typedef struct KEYINFO KEYINFO;


#ifdef __cplusplus
}
#endif

#endif // __DATASTORESHARED_H__