//
// This is free and unencumbered software released into the public domain.
//

#ifndef __MAINWINDOW_H__
#define __MAINWINDOW_H__

#include "UICommon.h"

#ifdef __cplusplus
extern "C" {
#endif


ATOM WINAPI RegisterMainWindowClass();

HWND WINAPI CreateMainWindow(HMSGLOOP hMsgLoop);

BOOL WINAPI OpenDbFile(HWND hWnd, LPCWSTR filePath);


#ifdef __cplusplus
}
#endif

#endif // __MAINWINDOW_H__