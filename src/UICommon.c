//
// 2012 Dec 8
//
// This is free and unencumbered software released into the public domain.
//

#include "UICommon.h"


void WINAPI ReplaceWndProc(HWND hWnd, WNDPROC proc)
{
    SetWindowLongPtr(hWnd, GWLP_USERDATA, SetWindowLongPtr(hWnd, GWLP_WNDPROC, (LONG_PTR)(proc)));
}

LRESULT CALLBACK TreeViewEditWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if (msg == WM_GETDLGCODE)
	{
		return DLGC_WANTALLKEYS;
	}

	/* http://support.microsoft.com/kb/104069

	case WM_COMMAND:
     hwndCtl = LOWORD(lParam);

     // If notification is from a control and the control is no longer this
     //   window's child, pass it on to the new parent.
     if (hwndCtl && !IsChild(hWnd, hwndCtl))
         SendMessage(GetParent(hwndCtl), WM_COMMAND, wParam,  lParam);
     else Do normal processing;
	 */

	return CallWindowProc((WNDPROC)GetWindowLongPtr(hWnd, GWLP_USERDATA), hWnd, msg, wParam, lParam);
}

LRESULT CALLBACK MultilineEditWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_CHAR && wParam == 1) // Ctrl-A
    {
        SendMessage(hWnd, EM_SETSEL, 0, -1);
        return 0;
    }

	LRESULT result = CallWindowProc((WNDPROC)GetWindowLongPtr(hWnd, GWLP_USERDATA), hWnd, msg, wParam, lParam);

	if (msg == WM_GETDLGCODE)
	{
        /*
        // In multiline mode, the edit control sends a WM_CLOSE message
        // to the parent when the user presses Esc. If an edit control
        // is nested a few controls down, some part of the dialog will
        // disappear when the user presses Esc. Normally, in a (modal)
        // dialog, the IsDialogMessage() function will intercept the
        // Esc key and do the handling for you.
        */

		if (lParam && (((PMSG)lParam)->message == WM_KEYDOWN) && ((((PMSG)lParam)->wParam == VK_TAB) || (((PMSG)lParam)->wParam == VK_ESCAPE)))
		{
			result &= ~(DLGC_WANTMESSAGE | DLGC_WANTTAB | DLGC_HASSETSEL);
		}
		else
		{
			result &= ~(DLGC_WANTTAB | DLGC_HASSETSEL);
		}
	}

	return result;
}

int WINAPI ResourceMessageBox(HWND hWnd, UINT textResId, UINT captionResId, UINT uType)
{
    LPCWSTR textStr;
    LPCWSTR captionStr;

    if (GetString(textResId, &textStr) && GetString(captionResId, &captionStr))
    {
        // TODO: get text direction from the parent window
        return MessageBox(hWnd, textStr, captionStr, uType);
    }

    return 0;
}

DWORD WINAPI GetEditControlSelectionLength(HWND hWnd)
{
    DWORD startSel;
    DWORD endSel;

    SendMessage(hWnd, EM_GETSEL, (WPARAM)&startSel, (LPARAM)&endSel);

    return endSel - startSel;
}

HFONT WINAPI CreateIconTitleFont()
{
    /*
    // Use SystemParametersInfo function with uiAction = SPI_GETNONCLIENTMETRICS.
    // Fonts are available from NONCLIENTMETRICS structure members.
    */

    LOGFONT font;

    if (SystemParametersInfo(SPI_GETICONTITLELOGFONT, sizeof(font), &font, 0))
    {
	    return CreateFontIndirect(&font);
    }

    return NULL;
}

// LVM_SETTEXTCOLOR
// LVM_SETBKCOLOR

BOOL WINAPI SetTreeViewColors(HWND hWnd, BOOL isEnabled)
{
    /*
    // For some reason on Win7, disabled TreeView background
    // color is white, it's not consistent with other controls.
    // We set proper background color for disabled TreeView here.
    */ 

    // TVM_SETTEXTCOLOR
    return SendMessage(hWnd, TVM_SETBKCOLOR, 0, isEnabled ? -1 : GetSysColor(COLOR_BTNFACE));
}

SIZE_T WINAPI TrimWhiteSpace(LPTSTR str)
{
    if (!str || !*str) // handle NULL and empty strings
    {
        return 0;
    }

    LPTSTR start = str;

    while (_istspace(*start)) { start++; }

    if (!*start) // handle all spaces
    {
        *str = 0;
        return 0;
    }

    LPTSTR end = start + _tcslen(start);

    while ((--end > start) && _istspace(*end));
    
    end++;
    SIZE_T count = end - start; // number of chars in trimmed string excluding null terminator

    if (start != str)
    {
        memmove(str, start, count * sizeof(TCHAR));
        str[count] = 0;
    }
    else if (*end)
    {
        *end = 0; // write new null terminator if it's not already there
    } 

    return count;
}


typedef struct SENDMSGTODESC
{
    UINT msg;
    WPARAM wParam;
    LPARAM lParam;
    WNDENUMMATCHPROC lpEnumMatchFunc;
} SENDMSGTODESC;


static BOOL CALLBACK SendMessageToDescendantsEnumProc(HWND hWnd, LPARAM lParam)
{
    SENDMSGTODESC *param = (SENDMSGTODESC*)lParam;

    if (!param->lpEnumMatchFunc || param->lpEnumMatchFunc(hWnd))
    {
        SendMessage(hWnd, param->msg, param->wParam, param->lParam);
    }

    return TRUE;
}

void WINAPI SendMessageToDescendants(HWND hWndParent, UINT msg, WPARAM wParam, LPARAM lParam, WNDENUMMATCHPROC lpEnumMatchFunc)
{
    SENDMSGTODESC s;

    s.msg = msg;
    s.wParam = wParam;
    s.lParam = lParam;
    s.lpEnumMatchFunc = lpEnumMatchFunc;
    
    EnumChildWindows(hWndParent, SendMessageToDescendantsEnumProc, (LPARAM)&s);
}


static const TCHAR EditClassName[] = _T("Edit");

BOOL WINAPI IsEditWindow(HWND hWnd)
{
    TCHAR className[COUNT_OF(EditClassName) + 1];

    if (GetClassName(hWnd, className, COUNT_OF(className)))
    {
        for (UINT i = 0; i < COUNT_OF(EditClassName); i++)
        {
            if (className[i] != EditClassName[i])
            {
                return FALSE;
            }
        }

        return TRUE;
    }

    return FALSE;
}