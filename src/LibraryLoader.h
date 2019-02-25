//
// This is free and unencumbered software released into the public domain.
//

#ifndef __MODULELOADER_1A05AF57EA0A40238397D75A90C4D338_H__
#define __MODULELOADER_1A05AF57EA0A40238397D75A90C4D338_H__

#include <windows.h>


#ifdef __cplusplus
extern "C" {
#endif


BOOL WINAPI RestrictLibrarySearchPaths();

HMODULE WINAPI LoadSystemLibrary(LPCTSTR lpFileName);


#ifdef __cplusplus
}
#endif


#endif // __MODULELOADER_1A05AF57EA0A40238397D75A90C4D338_H__
