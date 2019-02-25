//
// This is free and unencumbered software released into the public domain.
//

#include "FilePropertiesWindow.h"
#include "Resources.h"
#include <Prsht.h>


struct FILEPROPINFOEX
{
    FILEPROPINFO prop; // must be the first field in the struct
    BOOL trackPasswordCue; // enable automatic password cue
    BOOL p1HasText;
    BOOL p2HasText;
    CHAR utf8PassBuffer[(FILE_MAX_PASSWORD + 1) * 4];  // UTF8 password, not NULL terminatated. Populated after the prop window is shown
    DWORD passwordLength; // Password length excluding the terminating NULL character
    TCHAR password[FILE_MAX_PASSWORD + 1]; // Buffer for the password string including the terminating NULL character
    TCHAR passwordConfirm[FILE_MAX_PASSWORD + 1]; // Buffer for the confirm password string including the terminating NULL character
    PROPSHEETHEADER	header;
    PROPSHEETPAGE pages[2];
};

typedef struct FILEPROPINFOEX FILEPROPINFOEX;


static void WINAPI SetPasswordCue(HWND hPassWnd, HWND hPassWnd2, BOOL showCue)
{
    LPCWSTR lpCue = EMPTYSTRING;

    if (showCue && !GetString(IDS_FILE_NEWPASSWORD_CUE, &lpCue))
    {
        return;
    }

    if (SendMessage(hPassWnd, EM_SETCUEBANNER, TRUE, (LPARAM)lpCue))
    {
        SendMessage(hPassWnd2, EM_SETCUEBANNER, TRUE, (LPARAM)lpCue);
    }
}

static INT_PTR CALLBACK FileGeneralPropSheetPageInit(HWND hwndDlg, LPARAM lParam)
{
    FILEPROPINFOEX* info = (FILEPROPINFOEX*)((PROPSHEETPAGE*)lParam)->lParam;
    SetWindowLongPtr(hwndDlg, GWLP_USERDATA, (LONG_PTR)info);

    HWND hWndPass = GetDlgItem(hwndDlg, IDC_EDIT_FILE_PASSWORD);
    SendMessage(hWndPass, EM_LIMITTEXT, FILE_MAX_PASSWORD, 0);

    HWND hWndConfPass = GetDlgItem(hwndDlg, IDC_EDIT_FILE_CONFIRM_PASSWORD);
    SendMessage(hWndConfPass, EM_LIMITTEXT, FILE_MAX_PASSWORD, 0);

    // WINBUG: Cue banner first char is cut off on XP, setting margin to 0 fixes it.
    SendMessage(hWndPass, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(0, 0));
    SendMessage(hWndConfPass, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(0, 0));

    if (info->prop.readonly)
    {
        EnableWindow(hWndPass, FALSE);
        EnableWindow(hWndConfPass, FALSE);
    }
    else if (info->prop.passExists)
    {
        info->trackPasswordCue = TRUE;
        SetPasswordCue(hWndPass, hWndConfPass, info->trackPasswordCue);
    }

    return TRUE;
}

static INT_PTR CALLBACK FileGeneralPropSheetPageApply(HWND hwndDlg, LPARAM lParam)
{
    UINT msgId;
    FILEPROPINFOEX* info = (FILEPROPINFOEX*)GetWindowLongPtr(hwndDlg, GWLP_USERDATA);

    if (info->prop.readonly)
    {
        return FALSE; // no need to validate passwords
    }

    DWORD p1Length = (DWORD)SendDlgItemMessage(hwndDlg, IDC_EDIT_FILE_PASSWORD, WM_GETTEXT, sizeof(info->password) / sizeof(info->password[0]), (LPARAM)info->password);

    if (!info->prop.passExists && !p1Length)
    {
        msgId = IDS_PASSWORD_NOT_PROVIDED_MSG;
    }
    else
    {
        DWORD p2Length = (DWORD)SendDlgItemMessage(hwndDlg, IDC_EDIT_FILE_CONFIRM_PASSWORD, WM_GETTEXT, sizeof(info->passwordConfirm) / sizeof(info->passwordConfirm[0]), (LPARAM)info->passwordConfirm);

        if (!info->prop.passExists && !p2Length)
        {
            msgId = IDS_PASSWORD_CONFIRM_NOT_PROVIDED_MSG;
        }
        else if ((p1Length != p2Length) || (memcmp(info->password, info->passwordConfirm, p1Length * sizeof(TCHAR)) != 0))
        {
            msgId = IDS_PASSWORDS_NOT_MATCH_MSG;
        }
        else
        {
            info->passwordLength = p1Length;

            return FALSE; // passwords match!
        }
    }

    info->passwordLength = 0;

    // TODO: don't send set current sel if it's already current page
    SendMessage(((LPPSHNOTIFY)lParam)->hdr.hwndFrom, PSM_SETCURSELID, 0, IDD_FILE_GENERAL);

    ResourceMessageBox(((LPPSHNOTIFY)lParam)->hdr.hwndFrom, msgId, IDS_FILE_PROPERTIES, MB_OK | MB_ICONERROR);

    SendDlgItemMessage(hwndDlg, IDC_EDIT_FILE_PASSWORD, WM_SETTEXT, 0, (LPARAM)EMPTYSTRING);
    SendDlgItemMessage(hwndDlg, IDC_EDIT_FILE_CONFIRM_PASSWORD, WM_SETTEXT, 0, (LPARAM)EMPTYSTRING);

    SetWindowLongPtr(hwndDlg, DWLP_MSGRESULT, PSNRET_INVALID_NOCHANGEPAGE);
    return TRUE;
}

// EN_MAXTEXT

static void WINAPI FileGeneralPasswordChanged(HWND hwndDlg, WPARAM wParam, HWND lParam)
{
    FILEPROPINFOEX* info = (FILEPROPINFOEX*)GetWindowLongPtr(hwndDlg, GWLP_USERDATA);
    
    if (!info->trackPasswordCue)
    {
        return;
    }

    BOOL prevNoCue = info->p1HasText || info->p2HasText;
    BOOL wndHasText = SendMessage(lParam, WM_GETTEXTLENGTH, 0, 0) > 0;
    int dlgOther = IDC_EDIT_FILE_PASSWORD;

    switch (LOWORD(wParam))
    {
        case IDC_EDIT_FILE_PASSWORD:           
            info->p1HasText = wndHasText;
            dlgOther = IDC_EDIT_FILE_CONFIRM_PASSWORD;

            break;
        case IDC_EDIT_FILE_CONFIRM_PASSWORD:
            info->p2HasText = wndHasText;

            break;
    }

    BOOL noCue = info->p1HasText || info->p2HasText;

    if (prevNoCue ^ noCue)
    {
        HWND hWndOther = GetDlgItem(hwndDlg, dlgOther);

        if (hWndOther)
        {
            SetPasswordCue(lParam, hWndOther, !noCue);
        }
    }
}

static INT_PTR CALLBACK FileGeneralPropSheetPageProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(wParam);

    switch (uMsg)
    {
        case WM_INITDIALOG:
            return FileGeneralPropSheetPageInit(hwndDlg, lParam);

        case WM_NOTIFY:
            switch (((LPNMHDR)lParam)->idFrom)
			{
                case 0:
                    switch (((LPNMHDR)lParam)->code)
			        {
                        case PSN_APPLY:
                            return FileGeneralPropSheetPageApply(hwndDlg, lParam);
                    }

                    break;
            }

            break;
        case WM_COMMAND:
            switch (LOWORD(wParam)) // control ID
            {
                case IDC_EDIT_FILE_PASSWORD:
                case IDC_EDIT_FILE_CONFIRM_PASSWORD:
                    switch (HIWORD(wParam)) // notification code
                    {
                        case EN_UPDATE:
                            FileGeneralPasswordChanged(hwndDlg, wParam, (HWND)lParam);

                            return 0;
                    }

                    break;
            }

            break;
    }

    return FALSE;
}

static INT_PTR CALLBACK FileMetadataPropSheetPageInit(HWND hwndDlg, LPARAM lParam)
{
    FILEPROPINFOEX* info = (FILEPROPINFOEX*)((PROPSHEETPAGE*)lParam)->lParam;
    SetWindowLongPtr(hwndDlg, GWLP_USERDATA, (LONG_PTR)info);

    HWND hWndTitle = GetDlgItem(hwndDlg, IDC_EDIT_FILE_TITLE);
    SendMessage(hWndTitle, EM_LIMITTEXT, FILE_MAX_TITLE, 0);

    if (info->prop.title[0])
    {
        SendMessage(hWndTitle, WM_SETTEXT, 0, (LPARAM)info->prop.title);
    }

    HWND hWndDesc = GetDlgItem(hwndDlg, IDC_EDIT_FILE_DESCRIPTION);
    SendMessage(hWndDesc, EM_LIMITTEXT, FILE_MAX_DESCRIPTION, 0);
    ReplaceWndProc(hWndDesc, MultilineEditWndProc);

    if (info->prop.description[0])
    {
        SendMessage(hWndDesc, WM_SETTEXT, 0, (LPARAM)info->prop.description);
    }

    if (info->prop.readonly)
    {
        SendMessage(hWndTitle, EM_SETREADONLY, TRUE, 0);
        SendMessage(hWndDesc, EM_SETREADONLY, TRUE, 0);
    }

    // WINBUG: Cue banner first char is cut off on XP, setting margin to 0 fixes it.
    SendMessage(hWndTitle, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(0, 0));
    SendMessage(hWndDesc, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(0, 0));

    return TRUE;
}

static INT_PTR CALLBACK FileMetadataPropSheetPageApply(HWND hwndDlg, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);

    FILEPROPINFOEX* info = (FILEPROPINFOEX*)GetWindowLongPtr(hwndDlg, GWLP_USERDATA);

    if (!info->prop.readonly)
    {
        SendDlgItemMessage(hwndDlg, IDC_EDIT_FILE_TITLE, WM_GETTEXT, sizeof(info->prop.title) / sizeof(info->prop.title[0]), (LPARAM)info->prop.title);
        SendDlgItemMessage(hwndDlg, IDC_EDIT_FILE_DESCRIPTION, WM_GETTEXT, sizeof(info->prop.description) / sizeof(info->prop.description[0]), (LPARAM)info->prop.description);
    }

    return FALSE;
}

static INT_PTR CALLBACK FileMetadataPropSheetPageProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(wParam);

    switch (uMsg)
    {
        case WM_INITDIALOG:
            return FileMetadataPropSheetPageInit(hwndDlg, lParam);

        case WM_NOTIFY:
            switch (((LPNMHDR)lParam)->idFrom)
			{
                case 0:
                    switch (((LPNMHDR)lParam)->code)
			        {
                        case PSN_APPLY:
                            return FileMetadataPropSheetPageApply(hwndDlg, lParam);
                    }

                    break;
            }

            break;
    }

    return FALSE;
}

__declspec(restrict) FILEPROPINFO* WINAPI CreateFilePropInfo(HWND hWndParent)
{
    FILEPROPINFOEX* __restrict info = (FILEPROPINFOEX*)AllocClearBlock(sizeof(FILEPROPINFOEX));

    if (info)
    {
        info->pages[0].dwSize = sizeof(PROPSHEETPAGE);
        info->pages[0].dwFlags = PSP_PREMATURE;
        info->pages[0].pszTemplate = MAKEINTRESOURCE(IDD_FILE_GENERAL);
        info->pages[0].pfnDlgProc = FileGeneralPropSheetPageProc;
        info->pages[0].lParam = (LPARAM)info;

        info->pages[1].dwSize = sizeof(PROPSHEETPAGE);
        info->pages[1].dwFlags = PSP_PREMATURE;
        info->pages[1].pszTemplate = MAKEINTRESOURCE(IDD_FILE_METADATA);
        info->pages[1].pfnDlgProc = FileMetadataPropSheetPageProc;
        info->pages[1].lParam = (LPARAM)info;

        info->header.dwSize = sizeof(PROPSHEETHEADER);
        info->header.dwFlags = PSH_PROPSHEETPAGE | PSH_NOAPPLYNOW | PSH_NOCONTEXTHELP;
        info->header.hInstance = GetCurrentModuleHandle();
        info->header.pszCaption = MAKEINTRESOURCE(IDS_FILE_PROPERTIES);
        info->header.nPages = sizeof(info->pages) / sizeof(info->pages[0]);
        info->header.ppsp = info->pages;
        info->header.hwndParent = hWndParent;
    }

    return (FILEPROPINFO*)info;
}

BOOL WINAPI DestroyFilePropInfo(FILEPROPINFO *info)
{
    if (info)
    {
        SecureZeroMemory(info, sizeof(FILEPROPINFOEX));

        return FreeBlock(info);
    }

    return TRUE;
}

INT_PTR WINAPI ShowFileProp(FILEPROPINFO *info)
{
    /*
    // http://msdn.microsoft.com/en-us/library/windows/desktop/bb774538(v=vs.85).aspx
    */
    
    if (!info)
    {
        return -1;
    }

    FILEPROPINFOEX *ex = (FILEPROPINFOEX*)info;
    ex->header.nStartPage = !!(info->passExists || info->readonly);
    ex->prop.keyInfo.passLen = 0;
    ex->prop.keyInfo.pass = ex->utf8PassBuffer;

    INT_PTR result = PropertySheet(&ex->header);
    
    if (result > 0 && !ex->prop.readonly)
    {
        if (ex->passwordLength)
        {
            ex->prop.keyInfo.passLen = WideCharToMultiByte(CP_UTF8, 0, ex->password, ex->passwordLength, ex->utf8PassBuffer, sizeof(ex->utf8PassBuffer), NULL, NULL);

            if (!ex->prop.keyInfo.passLen)
            {
                return -1; // conversion to UTF8 failed
            }
        }

        TrimWhiteSpace(ex->prop.title);
        TrimWhiteSpace(ex->prop.description);
    }

    return result;
}