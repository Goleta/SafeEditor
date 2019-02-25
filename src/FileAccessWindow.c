//
// This is free and unencumbered software released into the public domain.
//

#include "FileAccessWindow.h"
#include "Resources.h"


typedef struct FILEACCESSPARAMSEX
{
    ValidateAccess callback;
    LPCTSTR filePath;
    DWORD widePassLen; // Length of the password in chars excluding NULL
    WCHAR widePassBuffer[FILE_MAX_PASSWORD + 1]; // NULL terminated password
    CHAR utf8PassBuffer[(FILE_MAX_PASSWORD + 1) * 4]; // UTF8 password, not NULL terminatated
    TCHAR passwordChar;
    FILEACCESSPARAMS params;

} FILEACCESSPARAMSEX;


static INT_PTR CALLBACK FileAccessWmInitDialog(HWND hWnd, LPARAM lParam)
{
    LPCWSTR lpCue;
    HWND hWndPassword = GetDlgItem(hWnd, IDC_EDIT_ACCESS_PASSWORD);

    SendMessage(hWndPassword, EM_LIMITTEXT, FILE_MAX_PASSWORD, 0);

    if (GetString(IDS_FILE_PASSWORD_CUE, &lpCue))
    {
        SendMessage(hWndPassword, EM_SETCUEBANNER, FALSE, (LPARAM)lpCue);
    }

    // WINBUG: cue banner first char is cut off on XP, setting margin to 0 fixes it.
    SendMessage(hWndPassword, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(0, 0));

    SetWindowLongPtr(hWnd, DWLP_USER, (LONG_PTR)lParam); // Store FILEACCESSPARAMSEX pointer

    FILEACCESSPARAMSEX *p = (FILEACCESSPARAMSEX*)lParam;
    p->passwordChar = (TCHAR)SendMessage(hWndPassword, EM_GETPASSWORDCHAR, 0, 0);

    SendDlgItemMessage(hWnd, IDC_EDIT_FILEPATH, WM_SETTEXT, 0, (LPARAM)p->filePath);

    return TRUE;
}

static void WINAPI FileAccessShowPasswordClicked(HWND hWnd, LPARAM lParam)
{
    TCHAR passChar = NULL;

    if (SendMessage((HWND)lParam, BM_GETCHECK, 0, 0) != BST_CHECKED)
    {
        passChar = ((FILEACCESSPARAMSEX*)GetWindowLongPtr(hWnd, DWLP_USER))->passwordChar;
    }

    HWND hWndPassword = GetDlgItem(hWnd, IDC_EDIT_ACCESS_PASSWORD);

    SendMessage(hWndPassword, EM_SETPASSWORDCHAR, passChar, 0);

    InvalidateRect(hWndPassword, NULL, FALSE);
}

static BOOL WINAPI FileAccessValidatePassword(HWND hWnd)
{
    FILEACCESSPARAMSEX *ex = (FILEACCESSPARAMSEX*)GetWindowLongPtr(hWnd, DWLP_USER);
    HWND hWndPassword = GetDlgItem(hWnd, IDC_EDIT_ACCESS_PASSWORD);

    ex->widePassLen = (DWORD)SendMessage(hWndPassword, WM_GETTEXT, sizeof(ex->widePassBuffer) / sizeof(ex->widePassBuffer[0]), (LPARAM)ex->widePassBuffer);
    BOOL hasPass = ex->widePassLen != 0;

    // Length of the UTF8 password in bytes excluding NULL terminator
    ex->params.keyInfo.passLen = ex->widePassLen ? WideCharToMultiByte(CP_UTF8, 0, ex->widePassBuffer, ex->widePassLen, ex->utf8PassBuffer, sizeof(ex->utf8PassBuffer), NULL, NULL) : 0;
    ex->params.keyInfo.pass = ex->utf8PassBuffer;

    ex->widePassLen = 0;
    SecureZeroMemory(ex->widePassBuffer, sizeof(ex->widePassBuffer));

    UINT32 resId;

    switch (ex->callback(&ex->params))
    {
        case VARESULT_OK:
            resId = 0;
            break;
        case VARESULT_ERROR:
            resId = IDS_UNEXPECTEDERROR;
            break;
        case VARESULT_FILE_IN_USE:
            resId = IDS_FILEINUSE;
            break;
        default:        // VARESULT_INVALID_PASSWORD                        
            resId = IDS_FILE_PASSWORD_INVALID_MSG;
            break;
    }

    ex->params.keyInfo.passLen = 0;
    SecureZeroMemory(ex->utf8PassBuffer, sizeof(ex->utf8PassBuffer));

    if (resId)
    {
        ResourceMessageBox(hWnd, resId, IDS_FILE_ACCESS, MB_OK | MB_ICONERROR);

        HWND hFocus = GetFocus();

        if (hFocus != hWndPassword)
        {
            SetFocus(hWndPassword);
        }

        if (hasPass && (GetWindowLong(hWndPassword, GWL_STYLE) & ES_PASSWORD))
        {
            SendMessage(hWndPassword, WM_SETTEXT, 0, (LPARAM)EMPTYSTRING);
        }

        return FALSE;
    }

    return TRUE;
}

static INT_PTR CALLBACK FileAccessDlgProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch(msg)
    {
        case WM_COMMAND:
            switch (LOWORD(wParam))
            {
                case IDOK:
                    if (!FileAccessValidatePassword(hWnd))
                    {
                        return TRUE;
                    }
                    // WARNING: Fall through case for successful password!
                case IDCANCEL:
                    EndDialog(hWnd, LOWORD(wParam));
                    return TRUE;

                case IDC_CHECK_SHOW_PASSWORD:
                    if (HIWORD(wParam) == BN_CLICKED)
                    {
                        FileAccessShowPasswordClicked(hWnd, lParam);
                    }

                    return TRUE;
            }

            break;
        case WM_INITDIALOG:
            return FileAccessWmInitDialog(hWnd, lParam);
    }

    return FALSE;
}

INT_PTR WINAPI ShowFileAccess(HWND hWndParent, LPCTSTR filePath, ValidateAccess callback, LPARAM lParam)
{
    if (!hWndParent || !filePath || !filePath[0] || !callback)
    {
        return 0;
    }

    FILEACCESSPARAMSEX *ex = (FILEACCESSPARAMSEX*)AllocBlock(sizeof(FILEACCESSPARAMSEX));

    if (ex)
    {
        ex->filePath = filePath;
        ex->callback = callback;
        ex->params.lParam = lParam;

        INT_PTR result = DialogBoxParam(GetCurrentModuleHandle(), MAKEINTRESOURCE(IDD_FILEACCESS), hWndParent, FileAccessDlgProc, (LPARAM)ex);

        FreeBlock(ex);

        return result;
    }

    return -1;
}