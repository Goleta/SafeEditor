//
// This is free and unencumbered software released into the public domain.
//

#ifndef __FILEACCESSWINDOW_H__
#define __FILEACCESSWINDOW_H__

#include "UICommon.h"
#include "DataStore.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct FILEACCESSPARAMS
{
    LPARAM lParam;
    KEYINFO keyInfo;

} FILEACCESSPARAMS;


#define VARESULT_OK 0
#define VARESULT_ERROR 1
#define VARESULT_INVALID_PASSWORD 2
#define VARESULT_FILE_IN_USE 3

typedef UINT32 (CALLBACK* ValidateAccess)(const FILEACCESSPARAMS *params);

INT_PTR WINAPI ShowFileAccess(HWND hWndParent, LPCTSTR filePath, ValidateAccess callback, LPARAM lParam);


#ifdef __cplusplus
}
#endif

#endif // __FILEACCESSWINDOW_H__