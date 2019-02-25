//
// This is free and unencumbered software released into the public domain.
//

#include "AboutWindow.h"
#include "Resources.h"


typedef struct ABOUTDLGINFO
{
    HMSGLOOP hMsgLoop;
    DWORD wndId;

} ABOUTDLGINFO;


static INT_PTR WINAPI AboutWmInitDialog(HWND hWnd, const ABOUTDLGINFO *lParam)
{
    SetWindowLongPtr(hWnd, DWLP_USER, (LONG_PTR)lParam->hMsgLoop);
    SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)lParam->wndId);

    HWND hParent = GetParent(hWnd);

    if (hParent)
    {
        SendMessage(hParent, WM_APP_WINDOWCREATED, MAKEWPARAM(lParam->wndId, TRUE), (LPARAM)hWnd);
    }

    HWND hEditWndAbout = GetDlgItem(hWnd, IDC_EDIT_ABOUT);

    if (hEditWndAbout)
    {
        LPCWSTR lpAbout;

        if (GetString(IDS_ABOUT_APP, &lpAbout))
        {
            SendMessage(hEditWndAbout, WM_SETTEXT, 0, (LPARAM)lpAbout);
        }

        ReplaceWndProc(hEditWndAbout, MultilineEditWndProc);
    }

    return TRUE;
}

static void WINAPI AboutMwDestroy(HWND hWnd)
{
    HWND hParent = GetParent(hWnd);

    if (hParent)
    {
        LONG_PTR wndId = GetWindowLongPtr(hWnd, GWLP_USERDATA);

        SendMessage(hParent, WM_APP_WINDOWCREATED, MAKEWPARAM(wndId, FALSE), (LPARAM)hWnd);
    }
}

static void WINAPI AboutMwActivate(HWND hWnd, WPARAM wParam)
{
    HMSGLOOP hMsgLoop = (HMSGLOOP)GetWindowLongPtr(hWnd, DWLP_USER);

    DWORD fActive = LOWORD(wParam);
    BOOL fMinimized = HIWORD(wParam);

    if (fActive != WA_INACTIVE) // The window is being activated
    {
        if (!fMinimized)
        {
            SetMsgLoop(hMsgLoop, hWnd, NULL);
        }
    }
	else if (fActive == WA_INACTIVE) // The window is being deactivated
	{
        RestoreMsgLoop(hMsgLoop);
	}
}

/*
static INT_PTR CALLBACK AboutMwCtlColorDlg(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    return (INT_PTR)GetSysColorBrush(COLOR_3DLIGHT);
}
*/

static INT_PTR CALLBACK ModelessAboutDlgProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch(msg)
    {
        // http://msdn.microsoft.com/en-us/library/bb432504(v=vs.85).aspx

     /*   case WM_CTLCOLORDLG:
        case WM_CTLCOLORBTN:
        case WM_CTLCOLORSTATIC:
        WM_CTLCOLOREDIT:
        WM_CTLCOLORSCROLLBAR
            return AboutMwCtlColorDlg(hWnd, msg, wParam, lParam); */

        case WM_COMMAND:
            switch(LOWORD(wParam))
            {
                case IDCANCEL:
                    DestroyWindow(hWnd);
                    break;
            }

            break;
        case WM_ACTIVATE:
            AboutMwActivate(hWnd, wParam);

            break;
        case WM_DESTROY:
            AboutMwDestroy(hWnd);

            break;
        case WM_INITDIALOG:
            return AboutWmInitDialog(hWnd, (ABOUTDLGINFO*)lParam);
    }

    return FALSE;
}

HWND WINAPI ShowAbout(HWND hWndAbout, DWORD wndAboutId, HWND hWndParent, HMSGLOOP hMsgLoop)
{
    ABOUTDLGINFO info;

    if (hWndAbout)
    {
        SendMessage(hWndAbout, DM_REPOSITION, 0, 0);                            
        SetActiveWindow(hWndAbout);

        return hWndAbout;
    }

    info.hMsgLoop = hMsgLoop;
    info.wndId = wndAboutId;

    return CreateDialogParam(GetCurrentModuleHandle(), MAKEINTRESOURCE(IDD_ABOUT), hWndParent, ModelessAboutDlgProc, (LPARAM)&info);
}