//
// This is free and unencumbered software released into the public domain.
//

#ifndef __FILEPROPERTIESWINDOW_H__
#define __FILEPROPERTIESWINDOW_H__

#include "UICommon.h"
#include "DataStore.h"

#ifdef __cplusplus
extern "C" {
#endif


struct FILEPROPINFO
{
    BOOL readonly; // TRUE to make all fields readonly (for readonly files)
    BOOL passExists; // TRUE to allow zero length passwords and show "leave blank to keep existing" cue
    KEYINFO keyInfo; // When props window is OK/Apply, this struct is set
    TCHAR title[FILE_MAX_TITLE + 1]; // NULL terminated title
    TCHAR description[FILE_MAX_DESCRIPTION + 1]; // NULL terminated description
};

typedef struct FILEPROPINFO FILEPROPINFO;


__declspec(restrict) FILEPROPINFO* WINAPI CreateFilePropInfo(HWND hWndParent);

BOOL WINAPI DestroyFilePropInfo(FILEPROPINFO *info);

INT_PTR WINAPI ShowFileProp(FILEPROPINFO *info);


#ifdef __cplusplus
}
#endif

#endif // __FILEPROPERTIESWINDOW_H__