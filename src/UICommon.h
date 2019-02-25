//
// This is free and unencumbered software released into the public domain.
//

#ifndef __UICOMMON_A9549562FE2041BE97A51756061612A2_H__
#define __UICOMMON_A9549562FE2041BE97A51756061612A2_H__

#include "AppBase.h"
#include <commctrl.h>
#include <commdlg.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
// Notifies parent window of a child
// window creation or destruction.
//
// wParam - LOWORD(wParam) is the child window identifier.
//          HIWORD(wParam) is a BOOL value that indicates:
//              TRUE if the window has been created,
//              FALSE if the window is being destroyed.
//
// lParam - the child window handle
*/
#define WM_APP_WINDOWCREATED (WM_APP + 0)


void WINAPI ReplaceWndProc(HWND hWnd, WNDPROC proc);

//
// http://blogs.msdn.com/oldnewthing/archive/2004/01/30/65013.aspx
//
int WINAPI GetString(UINT uID, LPCWSTR *str);

#pragma message("String resources must be compiled with the /n option to be NULL terminated.")


// DECLARE_HANDLE(HMSGLOOP);

typedef struct MSGLOOP *HMSGLOOP;

BOOL WINAPI SetMsgLoop(HMSGLOOP hMsgLoop, HWND hWnd, HACCEL hAccel);
BOOL WINAPI UpdateMsgLoopAccel(HMSGLOOP hMsgLoop, HWND hWnd, HACCEL hAccel);
BOOL WINAPI RestoreMsgLoop(HMSGLOOP hMsgLoop);


LRESULT CALLBACK TreeViewEditWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK MultilineEditWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

int WINAPI ResourceMessageBox(HWND hWnd, UINT textResId, UINT captionResId, UINT uType);

DWORD WINAPI GetEditControlSelectionLength(HWND hWnd);

HFONT WINAPI CreateIconTitleFont();

BOOL WINAPI SetTreeViewColors(HWND hWnd, BOOL isEnabled);

SIZE_T WINAPI TrimWhiteSpace(LPTSTR str);

typedef BOOL (CALLBACK* WNDENUMMATCHPROC)(HWND);
void WINAPI SendMessageToDescendants(HWND hWndParent, UINT msg, WPARAM wParam, LPARAM lParam, WNDENUMMATCHPROC lpEnumMatchFunc);

// BOOL WINAPI GetScreenDPI(SIZE *dpi);

BOOL WINAPI IsEditWindow(HWND hWnd);

int DpiScaleX(int value);
int DpiScaleY(int value);


#ifdef __cplusplus
}
#endif

#endif // __UICOMMON_A9549562FE2041BE97A51756061612A2_H__