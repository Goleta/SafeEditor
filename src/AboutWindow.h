//
// This is free and unencumbered software released into the public domain.
//

#ifndef __ABOUTWINDOW_H__
#define __ABOUTWINDOW_H__

#include "UICommon.h"

#ifdef __cplusplus
extern "C" {
#endif


HWND WINAPI ShowAbout(HWND hWndAbout, DWORD wndAboutId, HWND hWndParent, HMSGLOOP hMsgLoop);


#ifdef __cplusplus
}
#endif

#endif // __ABOUTWINDOW_H__