//
// This is free and unencumbered software released into the public domain.
//

#ifndef __APPDEBUG_H__
#define __APPDEBUG_H__

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif


#define PRINTDEBUG 0


#if (PRINTDEBUG)

#define PrintDebugMsg(str) OutputDebugString(str);

#else

#define PrintDebugMsg(str)

#endif


#ifdef __cplusplus
}
#endif

#endif // __APPDEBUG_H__