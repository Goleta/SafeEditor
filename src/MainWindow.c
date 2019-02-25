//
// This is free and unencumbered software released into the public domain.
//

#include "MainWindow.h"
#include "AboutWindow.h"
#include "SplitterWindow.h"
#include "FilePropertiesWindow.h"
#include "FileAccessWindow.h"
#include "DataStore.h"
#include "AppCom.h"
#include "Resources.h"

#include <Windowsx.h>


static const TCHAR MAINWNDCLASSNAME[] = _T("SafeEditor");

#define APP_WINDOW_MIN_LOGICAL_WIDTH	360
#define APP_WINDOW_MIN_LOGICAL_HEIGHT	240

#define APP_DEFAULT_LOGICAL_WIDTH       900
#define APP_DEFAULT_LOGICAL_HEIGHT      650

#define APP_VSPLITTER_LOGICAL_WIDTH     230
#define APP_HSPLITTER_LOGICAL_HEIGHT    200

#define APP_PROPS_COL1_LOGICAL_WIDTH    128
#define APP_PROPS_COL2_LOGICAL_WIDTH    320


#define IDC_APP_FIRST		0xFF
#define IDC_APP_VSPLITTER	(IDC_APP_FIRST + 0)
#define IDC_APP_TREEVIEW	(IDC_APP_FIRST + 1)
#define IDC_APP_HSPLITTER	(IDC_APP_FIRST + 2)
#define IDC_APP_LISTVIEW	(IDC_APP_FIRST + 3)
#define IDC_APP_EDIT		(IDC_APP_FIRST + 4)
#define IDC_APP_STATUSBAR	(IDC_APP_FIRST + 5)
#define IDC_APP_ABOUT   	(IDC_APP_FIRST + 6)


#define MAX_DATETIME_BUFFER_COUNT 128 // including NULL terminator

typedef struct APPWINDOWINFO
{
    // Most used structs first

    POINT ptMinTrackSize;
    HWND hWndMain; // the window that owns this structure
	HWND statusBar;
	HWND vsplitter;
	HWND treeview;
	HWND hsplitter;
	HWND listview;
	HWND edit;
    HWND hWndFocus; // Main window child control focus

    DBHANDLE db;
	
    HWND hWndAbout; // Modeless about dialog window
    HMSGLOOP hMsgLoop;
    HACCEL hAccel; // main window accelarators
    HACCEL hAccelTreeView; // the main window + treeview accelerators

    // Least used structs last

    IDropTarget *editDropTarget;
    HFONT font; // font used for the Edit window
    BOOL windowCreated;

    TCHAR datetimeBuffer[MAX_DATETIME_BUFFER_COUNT]; // including NULL terminator

} APPWINDOWINFO;


static const DWORD ListViewRows[] =
{
    IDS_TITLE,
    IDS_DATECREATED,
    IDS_DATEMODIFIED,
    IDS_READONLY
};

typedef struct BOUNDS
{
	RECT a;
	RECT b;
} BOUNDS;

typedef union APPWINDOWINITINFO
{
	LVITEM lvitem;
	LVCOLUMN lvcolumn;
	BOUNDS bounds;
    MENUITEMINFO exitMenu;
} APPWINDOWINITINFO;

typedef struct OPENFILENAMEINFO
{
	OPENFILENAME ofn;
	TCHAR filename[MAX_PATH];
	TCHAR filter[1];
} OPENFILENAMEINFO;


static LPCWSTR WINAPI GetReadOnlyValue(BOOL isReadOnly)
{
    UINT strId = isReadOnly ? IDS_READONLY_YES : IDS_READONLY_NO;
    LPCWSTR str;

    if (GetString(strId, &str))
    {
        return str;
    }

    return NULL;
}

static void WINAPI EnableWorkspace(APPWINDOWINFO *e, BOOL enable)
{
 //   if (!enable)
 //   {
        // Must move focus before disabling windows
      //  SetFocus(e->hWndMain);
 //   }

    EnableWindow(e->treeview, enable);
    SetTreeViewColors(e->treeview, enable);
    EnableWindow(e->listview, enable);
    EnableWindow(e->edit, enable);
}

static void WINAPI RedrawWorkspace(APPWINDOWINFO *e)
{
    /*
    // When the edit/treeview/listview window state changes, the window
    // doesn't get repainted completely (barely visible previous background color
    // rectangle around the window), RedrawWindow fixes this for all children.
    */

    RedrawWindow(e->vsplitter, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
}

static void WINAPI MainWmWindowPosChanged(HWND hWnd)
{
	RECT clientBounds;
	RECT statusBarBounds;

	APPWINDOWINFO* e = (APPWINDOWINFO*)GetWindowLongPtr(hWnd, 0);

	SendMessage(e->statusBar, WM_SIZE, NULL, NULL);

	if (GetClientRect(e->statusBar, &statusBarBounds) && GetClientRect(hWnd, &clientBounds))
	{
		MoveWindow(e->vsplitter, 0, 0, clientBounds.right, clientBounds.bottom - statusBarBounds.bottom, TRUE);
	}
}

static void WINAPI MainWmNotifySplitter(HWND hWnd, const NMSPLITTER* lParam)
{
	HDWP hDwp = BeginDeferWindowPos(2);

	if (hDwp)
	{
		APPWINDOWINFO* e = (APPWINDOWINFO*)GetWindowLongPtr(hWnd, 0);

		if (lParam->hdr.code) // V splitter
		{
            DeferWindowPos(hDwp, e->treeview, NULL, 0, 0, lParam->position - lParam->barHalfThickness, lParam->height, SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_NOACTIVATE | SWP_NOMOVE);
			
            if (hDwp) hDwp = DeferWindowPos(hDwp, e->hsplitter, NULL, lParam->position + lParam->barHalfThickness, 0, lParam->width - lParam->position - lParam->barHalfThickness, lParam->height, SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_NOACTIVATE);
		}
		else // H splitter
		{
			DeferWindowPos(hDwp,  e->listview, NULL, 0, 0, lParam->width, lParam->position - lParam->barHalfThickness, SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_NOACTIVATE | SWP_NOMOVE);
			
            if (hDwp) hDwp = DeferWindowPos(hDwp, e->edit, NULL, 0, lParam->position + lParam->barHalfThickness, lParam->width, lParam->height - lParam->position - lParam->barHalfThickness, SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_NOACTIVATE);
		}

        if (hDwp) EndDeferWindowPos(hDwp);
	}
}

static void WINAPI AppTreeViewNewItem(APPWINDOWINFO* e, BOOL sibling) // true - new item, false - new subitem
{
    TVITEM tviParent;
    TVINSERTSTRUCT tvIns;
    RECORD rd;

    memset(&rd, 0, sizeof(rd)); // parent record is zero by default

    if (!GetString(IDS_NEWITEM, (LPCWSTR*)&rd.title)) // Get readonly pointer to default item title string
    {
        return;
    }
    
	tvIns.hParent = (HTREEITEM)SendMessage(e->treeview, TVM_GETNEXTITEM, TVGN_CARET, 0);

	if (sibling && tvIns.hParent)
	{
		tvIns.hInsertAfter = (HTREEITEM)SendMessage(e->treeview, TVM_GETNEXTITEM, TVGN_PREVIOUS, (LPARAM)tvIns.hParent);
		tvIns.hParent = (HTREEITEM)SendMessage(e->treeview, TVM_GETNEXTITEM, TVGN_PARENT, (LPARAM)tvIns.hParent);

		if (!tvIns.hInsertAfter)
		{
			tvIns.hInsertAfter = TVI_FIRST;
		}
	}
	else
	{
		tvIns.hInsertAfter = TVI_FIRST;
	}

	if (tvIns.hParent) // Set parent record ID if parent item exists
	{
        tviParent.mask = TVIF_PARAM | TVIF_STATE;
	    tviParent.hItem = tvIns.hParent;

        if (!SendMessage(e->treeview, TVM_GETITEM, 0, (LPARAM)&tviParent))
        {
            return;
        }

		rd.parentRecordId = (UINT32)tviParent.lParam;
	}

	if (!DbAddNewItem(e->db, &rd))
    {
        return;
    }
    
	if (!sibling && tvIns.hParent && (!(tviParent.state & TVIS_EXPANDEDONCE)))
	{
		LPARAM parentExpanded = SendMessage(e->treeview, TVM_EXPAND, TVE_EXPAND, (LPARAM)tvIns.hParent);

		if (parentExpanded)
		{
			tvIns.item.hItem = (HTREEITEM)SendMessage(e->treeview, TVM_GETNEXTITEM, TVGN_CHILD, (LPARAM)tvIns.hParent);

			if (tvIns.item.hItem)
			{
				if (SendMessage(e->treeview, TVM_SELECTITEM, TVGN_CARET, (LPARAM)tvIns.item.hItem))
				{
					SendMessage(e->treeview, TVM_EDITLABEL, 0, (LPARAM)tvIns.item.hItem);
				}
			}
		}

		return;
	}

	// The rest of the code should only execute if parent was expanded at least once
	// or node is a sibling

	tvIns.item.mask = TVIF_TEXT | TVIF_CHILDREN | TVIF_PARAM;
	tvIns.item.pszText = LPSTR_TEXTCALLBACK;
	tvIns.item.cChildren = I_CHILDRENCALLBACK;
	tvIns.item.lParam = rd.recordId; // store the record id in the treeview item
	tvIns.item.hItem = (HTREEITEM)SendMessage(e->treeview, TVM_INSERTITEM, 0, (LPARAM)&tvIns);

	if (tvIns.item.hItem)
	{
		if (SendMessage(e->treeview, TVM_SELECTITEM, TVGN_CARET, (LPARAM)tvIns.item.hItem))
		{
			SendMessage(e->treeview, TVM_EDITLABEL, 0, (LPARAM)tvIns.item.hItem);
		}
	}
}

static LRESULT WINAPI UpdateItemTitle(HWND listview, LPCTSTR text, DWORD itemIndex)
{
		LVITEM m;

	//	m.mask = LVIF_TEXT;
	//	m.iItem = 4;
		m.iSubItem = 1;
		m.pszText = (LPWSTR)text;

		return SendMessage(listview, LVM_SETITEMTEXT, itemIndex, (LPARAM)&m);
}

static void WINAPI FormatDateTime(APPWINDOWINFO* e, UINT64 datetime)
{
    FILETIME ft;
    SYSTEMTIME st;

    FILETIME utcTime = DateTimeToFileTime(datetime);

	if (FileTimeToLocalFileTime(&utcTime, &ft) && FileTimeToSystemTime(&ft, &st)) // http://support.microsoft.com/kb/188768
	{
		int dateLen = GetDateFormat(LOCALE_USER_DEFAULT, DATE_LONGDATE, &st, NULL, e->datetimeBuffer, MAX_DATETIME_BUFFER_COUNT);

        if (dateLen)
        {
            dateLen--; // remove NULL terminator from the end

            if (dateLen < (MAX_DATETIME_BUFFER_COUNT - 4))
            {
                int timeLen = GetTimeFormat(LOCALE_USER_DEFAULT, 0, &st, NULL, e->datetimeBuffer + dateLen + 2, MAX_DATETIME_BUFFER_COUNT - dateLen - 2);
            
                if (timeLen)
                {
                    // Separating date and time by 2 spaces

                    e->datetimeBuffer[dateLen] = ' ';
                    e->datetimeBuffer[dateLen + 1] = ' ';

                    return;
                }
            }

            e->datetimeBuffer[dateLen] = NULL;
            return;
        }
	}

    e->datetimeBuffer[0] = NULL;
}

static LRESULT WINAPI UpdateItemView(APPWINDOWINFO* e, UINT64 time, DWORD itemIndex)
{
    FormatDateTime(e, time);

	LVITEM m;
	m.iSubItem = 1;
	m.pszText = e->datetimeBuffer;

	return SendMessage(e->listview, LVM_SETITEMTEXT, itemIndex, (LPARAM)&m);
}

static void WINAPI AppTreeViewEditItem(APPWINDOWINFO* e, BOOL remove)
{
	TVITEM tvi;
    tvi.hItem = (HTREEITEM)SendMessage(e->treeview, TVM_GETNEXTITEM, TVGN_CARET, 0);

	if (tvi.hItem)
	{
        if (remove)
        {
            tvi.mask = TVIF_PARAM;

            if (SendMessage(e->treeview, TVM_GETITEM, 0, (LPARAM)&tvi))
            {
                if (SendMessage(e->treeview, TVM_DELETEITEM, 0, (LPARAM)tvi.hItem))
                {
                    DbRemoveItem(e->db, (UINT32)tvi.lParam);

                    if (SendMessage(e->treeview, TVM_GETCOUNT, 0, 0) == 0)
                    {
                        SendMessage(e->edit, WM_SETTEXT, 0, (LPARAM)EMPTYSTRING);
                        SendMessage(e->edit, EM_SETREADONLY, TRUE, 0);

                        LVITEM lvi;

		                lvi.iSubItem = 1;
		                lvi.pszText = (LPTSTR)EMPTYSTRING;

                        for (int i = 0; i < (sizeof(ListViewRows) / sizeof(ListViewRows[0])); i++)
                        {
		                    SendMessage(e->listview, LVM_SETITEMTEXT, i, (LPARAM)&lvi);
                        }
                    }
                }
            }
        }
        else // rename
        {
		    SendMessage(e->treeview, TVM_EDITLABEL, 0, (LPARAM)tvi.hItem);
        }
	}
}

/*
static UINT32 WINAPI GetCurrentRecordId(APPWINDOWINFO* e)
{
    TVITEM tvi;
	tvi.hItem = (HTREEITEM)SendMessage(e->treeview, TVM_GETNEXTITEM, TVGN_CARET, 0);

    if (tvi.hItem)
	{
        tvi.mask = TVIF_PARAM;

        if (SendMessage(e->treeview, TVM_GETITEM, 0, (LPARAM)&tvi))
        {
            return (UINT32)tvi.lParam;
        }
    }

    return INVALID_RECORD_ID;
}
*/

static void WINAPI AppTreeViewSetReadOnlyItem(APPWINDOWINFO* e)
{
    TVITEM tvi;
	tvi.hItem = (HTREEITEM)SendMessage(e->treeview, TVM_GETNEXTITEM, TVGN_CARET, 0);

	if (tvi.hItem)
	{
        tvi.mask = TVIF_PARAM;

        if (SendMessage(e->treeview, TVM_GETITEM, 0, (LPARAM)&tvi))
        {
            RECORD rd;
            rd.recordId = (UINT32)tvi.lParam;

            if (DbIsReadOnlyItem(e->db, rd.recordId, &rd.readonly))
            {
                rd.readonly = !rd.readonly;

                if (DbSetReadOnlyItem(e->db, &rd))
                {
                    UpdateItemView(e, rd.dateModified, 2);
                    UpdateItemTitle(e->listview, GetReadOnlyValue(rd.readonly), 3);

                    if (SendMessage(e->edit, EM_SETREADONLY, rd.readonly, 0))
                    {
                        // todo: why need this? set readonly doesn't seem to reset undo buffers
                        SendMessage(e->edit, EM_EMPTYUNDOBUFFER, 0, 0);
                    }
                }
            }
        }
	}
}

static LRESULT CALLBACK MainWmNotifyTvnNmRClick(const NMHDR* lParam)
{
//	TVHITTESTINFO info;
//	HTREEITEM hit = (HTREEITEM)SendMessage(lParam->hwndFrom, TVM_HITTEST, 0, &info);
	
	HTREEITEM item = (HTREEITEM)SendMessage(lParam->hwndFrom, TVM_GETNEXTITEM, TVGN_DROPHILITE, 0);

	if(item)
	{
		SendMessage(lParam->hwndFrom, TVM_SELECTITEM, TVGN_CARET, (LPARAM)item);
	}

    return 0;
}

static LRESULT CALLBACK MainWmNotifyTvnBeginLabelEdit(HWND hWnd, const NMTVDISPINFO* lParam)
{
    BOOL readonly;
    APPWINDOWINFO* e = (APPWINDOWINFO*)GetWindowLongPtr(hWnd, 0);

    BOOL rwFile = DbIsReadWrite(e->db);

    if (rwFile && DbIsReadOnlyItem(e->db, (UINT32)lParam->item.lParam, &readonly))
    {
        if (!readonly)
        {
            HWND edit = (HWND)SendMessage(lParam->hdr.hwndFrom, TVM_GETEDITCONTROL, 0, 0);

	        if (edit)
	        {
		        SendMessage(edit, EM_LIMITTEXT, RECORD_MAX_TITLE, 0);
		        ReplaceWndProc(edit, TreeViewEditWndProc);

                return FALSE; // allow label editing
	        }
        }
    }

    return TRUE; // prevent label editing
}

static BOOL WINAPI SaveNotesChanges(APPWINDOWINFO* e, UINT32 recordId)
{
    if (SendMessage(e->edit, EM_GETMODIFY, 0, 0))
    {
       RECORD rd;
       rd.dateModified = GetDateTimeNow();
       rd.recordId = recordId;

       SIZE_T notesLength = (SIZE_T)SendMessage(e->edit, WM_GETTEXTLENGTH, 0, 0) + 1;
       rd.notes = AllocString(notesLength);

       SendMessage(e->edit, WM_GETTEXT, notesLength, (LPARAM)rd.notes);

       TrimWhiteSpace(rd.notes);
       
       DbUpdateItemNotes(e->db, &rd);

       FreeBlock(rd.notes);

       SendMessage(e->edit, EM_SETMODIFY, FALSE, 0);

       return TRUE;
    }

    return FALSE;
}

struct RECORD2
{
    RECORD rd;
    BOOL rwFile;
    APPWINDOWINFO *awi;
};

typedef struct RECORD2 RECORD2;

static void CALLBACK RecordCallbackProc(const RECORD *item)
{
    RECORD2* rd = (RECORD2*)item;
    
    UpdateItemTitle(rd->awi->listview, rd->rd.title, 0);
    UpdateItemView(rd->awi, rd->rd.dateCreated, 1);
    UpdateItemView(rd->awi, rd->rd.dateModified, 2);
    UpdateItemTitle(rd->awi->listview, GetReadOnlyValue(rd->rd.readonly), 3);

    SendMessage(rd->awi->edit, WM_SETTEXT, 0, (LPARAM)rd->rd.notes);
    SendMessage(rd->awi->edit, EM_SETREADONLY, rd->rd.readonly || !rd->rwFile, 0);
}

static LRESULT CALLBACK MainWmNotifyTvnSelChanged(HWND hWnd, const NMTREEVIEW* lParam)
{
    RECORD2 rd;
	
    rd.awi = (APPWINDOWINFO*)GetWindowLongPtr(hWnd, 0);
    rd.rwFile = DbIsReadWrite(rd.awi->db);
    BOOL oldItemExists;

    if (rd.rwFile && DbItemExists(rd.awi->db, (UINT32)lParam->itemOld.lParam, &oldItemExists) && oldItemExists)
    {    
        SaveNotesChanges(rd.awi, (UINT32)lParam->itemOld.lParam);
    }

    rd.rd.recordId = (UINT32)lParam->itemNew.lParam;

    if (!DbGetItem(rd.awi->db, &rd.rd, RecordCallbackProc))
    {
        // MAGOLE TODO: clear listview if no items or failed to load and set edit window to readonly
    }

  /*  UINT32 level;
    int err = DbGetRecordLevel(rd.awi->db, rd.rd.recordId, &level);

    if (err != SQLITE_OK)
    {
        show_sqlite_error2(err, _T("tvn sel changed -> failed to get level"));
    }

    TCHAR str[100];
	_sntprintf(str, sizeof(str) / sizeof(str[0]), _T("level: %i"), level);
 */


    return 0;
}

static void WINAPI SetStatusTextFromResourceId(APPWINDOWINFO *e, UINT id)
{
    LPCWSTR statusStr;

    if (GetString(id, &statusStr))
    {
        SendMessage(e->statusBar, WM_SETTEXT, 0, (LPARAM)statusStr);
    }
}

static LRESULT WINAPI MainWmCreate(HWND hWnd, const CREATESTRUCT* lParam)
{
	APPWINDOWINITINFO t;

	APPWINDOWINFO* __restrict e = (APPWINDOWINFO*)AllocClearBlock(sizeof(APPWINDOWINFO));

	if (e)
	{
        e->ptMinTrackSize.x = DpiScaleX(APP_WINDOW_MIN_LOGICAL_WIDTH);
        e->ptMinTrackSize.y = DpiScaleY(APP_WINDOW_MIN_LOGICAL_HEIGHT);

		SetWindowLongPtr(hWnd, 0, (LONG_PTR)e);

        e->hWndMain = hWnd;

        if ((e->hAccel = LoadAccelerators(lParam->hInstance, MAKEINTRESOURCE(IDR_MAINACCELERATORS))) &&
            (e->hAccelTreeView = LoadAccelerators(lParam->hInstance, MAKEINTRESOURCE(IDR_MAINACCELERATORS_TREEVIEW))))
        {
            e->font = CreateIconTitleFont();

			if (e->font)
			{
                e->statusBar = CreateWindowEx(WS_EX_NOPARENTNOTIFY, STATUSCLASSNAME, NULL, WS_VISIBLE | WS_CHILD | SBARS_SIZEGRIP | WS_CLIPSIBLINGS | WS_CLIPCHILDREN, 0, 0, 0, 0, hWnd, (HMENU)IDC_APP_STATUSBAR, lParam->hInstance, NULL);

				if (e->statusBar)
				{
                    e->vsplitter = CreateSplitterWindow(hWnd, IDC_APP_VSPLITTER, TRUE, DpiScaleX(APP_VSPLITTER_LOGICAL_WIDTH));

					if (e->vsplitter)
					{
                        e->treeview = CreateWindowEx(WS_EX_NOPARENTNOTIFY | WS_EX_CLIENTEDGE, WC_TREEVIEW, NULL, WS_DISABLED | WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | TVS_HASBUTTONS | TVS_LINESATROOT | TVS_EDITLABELS | TVS_FULLROWSELECT | WS_TABSTOP | TVS_DISABLEDRAGDROP | TVS_SHOWSELALWAYS /* | TVS_NOHSCROLL */ /* | TVS_TRACKSELECT*/, 0, 0, 0, 0, e->vsplitter, (HMENU)IDC_APP_TREEVIEW, lParam->hInstance, NULL);

						if (e->treeview)
						{
                            SetTreeViewColors(e->treeview, FALSE);

                            e->hsplitter = CreateSplitterWindow(e->vsplitter, IDC_APP_HSPLITTER, FALSE, DpiScaleY(APP_HSPLITTER_LOGICAL_HEIGHT));

							if (e->hsplitter)
							{
                                e->listview = CreateWindowEx(WS_EX_NOPARENTNOTIFY | WS_EX_CLIENTEDGE, WC_LISTVIEW, NULL, WS_DISABLED | WS_VISIBLE | WS_CHILD | LVS_REPORT | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_TABSTOP | LVS_NOSORTHEADER | LVS_SINGLESEL | LVS_SHOWSELALWAYS /* | LVS_EDITLABELS */, 0, 0, 0, 0, e->hsplitter, (HMENU)IDC_APP_LISTVIEW, lParam->hInstance, NULL);

								if (e->listview)
								{
									t.lvcolumn.mask = LVCF_FMT | LVCF_WIDTH | LVCF_TEXT | LVCF_SUBITEM;
									t.lvcolumn.fmt = LVCFMT_LEFT;
                                    t.lvcolumn.cx = DpiScaleX(APP_PROPS_COL1_LOGICAL_WIDTH);
									t.lvcolumn.iSubItem = 0;

                                    if (GetString(IDS_PROPERTY, (LPCWSTR*)&t.lvcolumn.pszText) && (SendMessage(e->listview, LVM_INSERTCOLUMN, 0, (LPARAM)&t.lvcolumn) != -1) && GetString(IDS_VALUE, (LPCWSTR*)&t.lvcolumn.pszText))
									{
                                        t.lvcolumn.cx = DpiScaleX(APP_PROPS_COL2_LOGICAL_WIDTH);
										t.lvcolumn.iSubItem = 1;

										if ((SendMessage(e->listview, LVM_INSERTCOLUMN, 1, (LPARAM)&t.lvcolumn) != -1) && SendMessage(e->listview, LVM_SETITEMCOUNT, sizeof(ListViewRows) / sizeof(ListViewRows[0]), (LPARAM)LVSICF_NOINVALIDATEALL))
										{
											SendMessage(e->listview, LVM_SETEXTENDEDLISTVIEWSTYLE, LVS_EX_FULLROWSELECT | LVS_EX_LABELTIP /* | LVS_EX_DOUBLEBUFFER */, LVS_EX_FULLROWSELECT | LVS_EX_LABELTIP /* | LVS_EX_DOUBLEBUFFER */);

											t.lvitem.mask = LVIF_TEXT;
											t.lvitem.iItem = 0;
											t.lvitem.iSubItem = 0;

											for (DWORD i = 0; i < (sizeof(ListViewRows) / sizeof(ListViewRows[0])); i++)
											{
                                                if (!GetString(ListViewRows[i], (LPCWSTR*)&t.lvitem.pszText) || (SendMessage(e->listview, LVM_INSERTITEM, 0, (LPARAM)&t.lvitem) == -1))
												{
													return -1;
												}

												t.lvitem.iItem++;
											}

                                            e->edit = CreateWindowEx(WS_EX_NOPARENTNOTIFY | WS_EX_CLIENTEDGE, WC_EDIT, NULL, WS_DISABLED | ES_WANTRETURN | WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | WS_TABSTOP | ES_READONLY  /* (ES_NOHIDESEL)  | WS_HSCROLL  | ES_AUTOHSCROLL */, 0, 0, 0, 0, e->hsplitter, (HMENU)IDC_APP_EDIT, lParam->hInstance, NULL);

											if (e->edit)
											{
												SendMessage(e->edit, EM_LIMITTEXT, RECORD_MAX_NOTES, 0);
												SendMessage(e->edit, WM_SETFONT, (WPARAM)e->font, MAKELPARAM(FALSE, 0));
												ReplaceWndProc(e->edit, MultilineEditWndProc);                                                

												if (GetClientRect(e->statusBar, &t.bounds.a) && GetClientRect(hWnd, &t.bounds.b) && MoveWindow(e->vsplitter, 0, 0, t.bounds.b.right, t.bounds.b.bottom - t.bounds.a.bottom, FALSE))
												{                                                
                                                    e->hMsgLoop = (HMSGLOOP)lParam->lpCreateParams;

                                                    SetMsgLoop(e->hMsgLoop, hWnd, e->hAccel);
                                                    SetStatusTextFromResourceId(e, IDS_APP_READY);
                                                    RegisterDropWindow(e->edit, &e->editDropTarget);

                                                    e->windowCreated = TRUE;

													return 0;
												}
											}
										}
									}
								}
							}
						}
					}
				}
			}
        }
	}

	return -1;
}

static void WINAPI SaveNotesIfModified(APPWINDOWINFO* e)
{
    if (SendMessage(e->edit, EM_GETMODIFY, 0, 0))
    {
        TVITEM tvi;
        tvi.hItem = (HTREEITEM)SendMessage(e->treeview, TVM_GETNEXTITEM, TVGN_CARET, 0);

        if (tvi.hItem)
        {
            tvi.mask = TVIF_PARAM;

            if (SendMessage(e->treeview, TVM_GETITEM, 0, (LPARAM)&tvi))
            {
                SaveNotesChanges(e, (UINT32)tvi.lParam);
            }
        }
    }
}

static void WINAPI MainWmDestroy(HWND hWnd)
{
    APPWINDOWINFO* e = (APPWINDOWINFO*)GetWindowLongPtr(hWnd, 0);

    if (e)
    {
        UnregisterDropWindow(e->edit, e->editDropTarget);
    }
}

//
// http://blogs.msdn.com/b/oldnewthing/archive/2005/07/26/443384.aspx
//
static void WINAPI MainWmNcDestroy(HWND hWnd)
{
	APPWINDOWINFO* e = (APPWINDOWINFO*)SetWindowLongPtr(hWnd, 0, NULL);

	if (e)
	{
		if (e->font)
		{
			DeleteObject(e->font);
            e->font = NULL;
		}

        if (e->hAccel)
        {
            DestroyAcceleratorTable(e->hAccel);
            e->hAccel = NULL;
        }

        if (e->hAccelTreeView)
        {
            DestroyAcceleratorTable(e->hAccelTreeView);
            e->hAccelTreeView = NULL;
        }

		if (e->db)
		{
            DbClose(e->db, FALSE);
			e->db = NULL;
		}
        
        BOOL postQuit = e->windowCreated;

		FreeBlock(e);

        if (postQuit)
        {
            /*
            // If the window failed to be created in WM_CREATE,
            // then the quit message should not be posted.
            // This allows error messages to be displayed
            // by the code that tried to create the window.
            */
            PostQuitMessage(0);
        }
	}
}

static void WINAPI SaveFocus(HWND hWnd, HWND *hWndFocus)
{
    if (!*hWndFocus)
    {
        HWND hwndCurrentFocus = GetFocus();

        if (hwndCurrentFocus && IsChild(hWnd, hwndCurrentFocus)) // TODO: do i need IsChild?
        {
            *hWndFocus = hwndCurrentFocus;// SetWindowText(hWnd, _T("save focus"));
        }
    }
  //  else SetWindowText(hWnd, _T("focus already saved by another method"));
}

static void WINAPI RestoreFocus(HWND hWnd, HWND *hWndFocus)
{
    // IsWindow verifies if the window still exists. Edit control
    // in the TreeView for example gets destroyed after it was saved
    // when the window gets deactivated.

    // MAGOLE TODO: reliable way to fallback to treeview control if edit box was closed.

    HWND hwndCurrentFocus = *hWndFocus;
    *hWndFocus = NULL;

    if (!hwndCurrentFocus || hwndCurrentFocus == hWnd || !IsWindow(hwndCurrentFocus))
    {
        hwndCurrentFocus = GetNextDlgTabItem(hWnd, 0, FALSE);
    }

    if (hwndCurrentFocus)
    {
        SetFocus(hwndCurrentFocus);// SetWindowText(hWnd, _T("restore focus"));
    }
}

static void WINAPI MainWmSysCommand(HWND hWnd, WPARAM wParam)
{
    if ((wParam & 0xFFF0) == SC_MINIMIZE)
    {
        APPWINDOWINFO* e = (APPWINDOWINFO*)GetWindowLongPtr(hWnd, 0);

        SaveFocus(hWnd, &e->hWndFocus);
    }
}

static BOOL CALLBACK IsKnownControlId(HWND hWnd)
{
    switch (GetDlgCtrlID(hWnd))
    {
        case IDC_APP_TREEVIEW:
        case IDC_APP_LISTVIEW:
        case IDC_APP_EDIT:
        case IDC_APP_STATUSBAR:
            return TRUE;
    }

    return FALSE;
}

static void WINAPI MainWmSysColorChange(HWND hWnd)
{
    APPWINDOWINFO* e = (APPWINDOWINFO*)GetWindowLongPtr(hWnd, 0);

    SetTreeViewColors(e->treeview, IsWindowEnabled(e->treeview));

    SendMessageToDescendants(hWnd, WM_SYSCOLORCHANGE, 0, 0, IsKnownControlId);
}

static void WINAPI MainWmSettingChange(HWND hWnd, WPARAM wParam, LPARAM lParam)
{
    SendMessageToDescendants(hWnd, WM_SETTINGCHANGE, wParam, lParam, IsKnownControlId);
    
    switch (wParam)
    {
        case SPI_SETICONTITLELOGFONT:
            {
                HFONT hFont = CreateIconTitleFont();

                if (hFont)
                {
                    APPWINDOWINFO* e = (APPWINDOWINFO*)GetWindowLongPtr(hWnd, 0);
                    SendMessage(e->edit, WM_SETFONT, (WPARAM)hFont, MAKELPARAM(TRUE, 0));

                    HFONT hFontOld = e->font;
                    e->font = hFont;

                    DeleteObject(hFontOld);
                }
            }

            break;

        case SPI_SETNONCLIENTMETRICS:
            /*
            // Font or non client metrics changed for status bar, etc.
            */
            MainWmWindowPosChanged(hWnd);

            break;
    }
}

static void WINAPI MainWmAppWindowCreated(HWND hWnd, WPARAM wParam, HWND lParam)
{
    switch (LOWORD(wParam))
    {
        case IDC_APP_ABOUT:
            {
                APPWINDOWINFO* e = (APPWINDOWINFO*)GetWindowLongPtr(hWnd, 0);

                e->hWndAbout = HIWORD(wParam) ? lParam : NULL;
            }

            break;
    }
}

static void WINAPI MainWmCommandAbout(HWND hWnd)
{
    APPWINDOWINFO* e = (APPWINDOWINFO*)GetWindowLongPtr(hWnd, 0);

    ShowAbout(e->hWndAbout, IDC_APP_ABOUT, hWnd, e->hMsgLoop);
}

static void WINAPI MainWmActivate(HWND hWnd, WPARAM wParam)
{
	APPWINDOWINFO* e = (APPWINDOWINFO*)GetWindowLongPtr(hWnd, 0);

    DWORD fActive = LOWORD(wParam);
    BOOL fMinimized = HIWORD(wParam);

    if (fActive != WA_INACTIVE) // The window is being activated
    {
        if (!fMinimized)
        {
            RestoreFocus(hWnd, &e->hWndFocus);
        }
    }
	else if (fActive == WA_INACTIVE) // The window is being deactivated
	{
        if (!fMinimized) // Focus is NULL if window is mimimized
        {
            SaveFocus(hWnd, &e->hWndFocus);
        }
	}
}

static const TCHAR SAFEEDITORFILESFILTER[] = _T("*.se");
static const TCHAR ALLFILESFILTER[] = _T("*.*\0"); // must be double NULL terminated

static __declspec(restrict) LPOPENFILENAME CreateOpenFileNameInfo(HWND owner, BOOL openFile)
{
    // MAGOLE TODO: rewrite to have an array of extensions and filter names

	LPCWSTR filterName;
	SIZE_T filterNameLen = GetString(IDS_FILEFILTER, &filterName); // filterNameLen in WCHAR including null terminator

	if (filterNameLen)
	{
        LPCWSTR seFilterName;
        SIZE_T seFilterNameLen = GetString(IDS_SAFEEDITOR_FILE_FILTER, &seFilterName); // seFilterNameLen in WCHAR including null terminator

        if (seFilterNameLen)
        {
		    OPENFILENAMEINFO* __restrict info = (OPENFILENAMEINFO*)AllocClearBlock(FIELD_OFFSET(OPENFILENAMEINFO, filter) +
                                                ((seFilterNameLen + filterNameLen) * sizeof(TCHAR)) +
                                                sizeof(SAFEEDITORFILESFILTER) +
                                                sizeof(ALLFILESFILTER));

		    if (info)
		    {
			    info->ofn.lStructSize = sizeof(OPENFILENAME);
			    info->ofn.hwndOwner = owner;
                // info->ofn.hInstance = GetCurrentModuleHandle();
			    info->ofn.lpstrFilter = (LPTSTR)((ULONG_PTR)info + FIELD_OFFSET(OPENFILENAMEINFO, filter));
			    info->ofn.nFilterIndex = 1;
			    info->ofn.lpstrFile = (LPTSTR)((ULONG_PTR)info + FIELD_OFFSET(OPENFILENAMEINFO, filename));
			    // info->ofn.lpstrFile[0] = 0;
			    info->ofn.nMaxFile = sizeof(info->filename) / sizeof(info->filename[0]);
			    // info->ofn.lpstrTitle = NULL;
			    info->ofn.Flags = (openFile) ? (OFN_DONTADDTORECENT | OFN_ENABLESIZING | OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST) : (OFN_DONTADDTORECENT | OFN_ENABLESIZING | OFN_EXPLORER | OFN_PATHMUSTEXIST  | OFN_HIDEREADONLY | OFN_NOREADONLYRETURN /* | OFN_OVERWRITEPROMPT */); /* OFN_NOTESTFILECREATE */
                info->ofn.lpstrDefExt = _T("se");

                memcpy(info->filter, seFilterName, seFilterNameLen * sizeof(TCHAR));
                memcpy(&info->filter[seFilterNameLen], SAFEEDITORFILESFILTER, sizeof(SAFEEDITORFILESFILTER));
                
			    memcpy(&info->filter[seFilterNameLen + (sizeof(SAFEEDITORFILESFILTER) / sizeof(TCHAR))], filterName, filterNameLen * sizeof(TCHAR));
                memcpy(&info->filter[seFilterNameLen + (sizeof(SAFEEDITORFILESFILTER) / sizeof(TCHAR)) + filterNameLen], ALLFILESFILTER, sizeof(ALLFILESFILTER));

			    return (LPOPENFILENAME)info;
		    }
        }
	}

	return NULL;
}

struct CHILDID2
{
    CHILDRECORDID ci;
    APPWINDOWINFO *awi;
    TVINSERTSTRUCT tvIns;
};

typedef struct CHILDID2 CHILDID2;

static void CALLBACK ChildIdCallbackProc(const CHILDRECORDID *child)
{
    CHILDID2 *ci = (CHILDID2*)child;

    ci->tvIns.item.lParam = ci->ci.recordId;

    SendMessage(ci->awi->treeview, TVM_INSERTITEM, 0, (LPARAM)&ci->tvIns);
}

static BOOL WINAPI LoadChildNodes(APPWINDOWINFO* e, HTREEITEM hParent, UINT32 parentId)
{
    CHILDID2 ci;

    ci.ci.parentRecordId = parentId;
    ci.awi = e;

    memset(&ci.tvIns, 0, sizeof(ci.tvIns));

	ci.tvIns.item.mask = TVIF_TEXT | TVIF_CHILDREN | TVIF_PARAM;
	ci.tvIns.item.pszText = LPSTR_TEXTCALLBACK;
	ci.tvIns.item.cChildren = I_CHILDRENCALLBACK;

	ci.tvIns.hParent = hParent;
	ci.tvIns.hInsertAfter = TVI_FIRST;

	return DbGetItemChildrenIds(e->db, &ci.ci, ChildIdCallbackProc);
}

static LRESULT CALLBACK MainWmNotifyTvnItemExpanding(HWND hWnd, const LPNMTREEVIEW lParam)
{
	//	Some clarification of the TVIS_EXPANDEDONCE flag:
	//
	//	The first time an item is expanded, during processing of TVN_ITEMEXPANDING,
	//	you will see the TVIS_EXPANDED flag set, while the TVIS_EXPANDEDONCE flag is clear.
	//
	//	Every time after that, you will see both TVIS_EXPANDED and TVIS_EXPANDEDONCE flags set.
	//	The only proper way to clear the TVIS_EXPANDEDONCE flag is to pass the TVE_COLLAPSE
	//	and TVE_COLLAPSERESET flags to TVM_EXPAND. Additionally, TVE_COLLAPSERESET will
	//	also remove all child items.
	//
	//	You can think of TVIS_EXPANDEDONCE as the "has been expanded at least once already" flag.
	//	This flag is useful in situations where you want to dynamically add children to a
	//	tree item, but only right at the point of expansion.
	//
	//	One possible method is:
	//
	//	* On, TVN_ITEMEXPANDING, if action is TVE_EXPAND and state flag TVIS_EXPANDEDONCE is clear,
	//	  dynamically add children.
	//	* Do not add children on TVE_EXPAND when state flag TVIS_EXPANDEDONCE is set.
	//	* Do nothing when item is collapsed; specifically, do not remove children with TVE_COLLAPSERESET.
    	
	if ((lParam->action & TVE_EXPAND) && !(lParam->itemNew.state & TVIS_EXPANDEDONCE))
	{
        LoadChildNodes((APPWINDOWINFO*)GetWindowLongPtr(hWnd, 0), lParam->itemNew.hItem, (UINT32)lParam->itemNew.lParam);
	}

	return FALSE; // Return TRUE to prevent the list from expanding or collapsing.
}

static BOOL WINAPI FocusOnFirstNode(APPWINDOWINFO* e)
{
    SetFocus(e->treeview);

    HTREEITEM firstRecord = (HTREEITEM)SendMessage(e->treeview, TVM_GETNEXTITEM, TVGN_ROOT, NULL);

    if (firstRecord)
    {
	    return SendMessage(e->treeview, TVM_SELECTITEM, TVGN_CARET, (LPARAM)firstRecord);
    }

    return FALSE;
}


/*
SetDlgItemText
EN_MAXTEXT
LVN_ITEMCHANGED
WM_CHANGEUISTATE http://msdn.microsoft.com/en-us/library/windows/desktop/ms646342(v=vs.85).aspx
NM_SETFOCUS http://msdn.microsoft.com/en-us/library/windows/desktop/bb774881(v=vs.85).aspx

WM_ENDSESSION http://msdn.microsoft.com/en-us/library/windows/desktop/aa376889(v=vs.85).aspx
WM_QUERYENDSESSION

EM_SHOWBALLOONTIP, EDITBALLOONTIP http://msdn.microsoft.com/en-us/library/bb775466(v=vs.85).aspx
how to use balloons http://msdn.microsoft.com/en-us/library/windows/desktop/aa511451.aspx#inputProblem


http://msdn.microsoft.com/en-us/library/bb761639(v=vs.85)
http://msdn.microsoft.com/en-us/library/windows/desktop/ms644995(v=vs.85).aspx
*/

static BOOL WINAPI ClearWorkspace(APPWINDOWINFO *e)
{
    // Clear all items from the tree view
    if (SendMessage(e->treeview, TVM_DELETEITEM, 0, (LPARAM)TVI_ROOT))
    {
        if (SendMessage(e->edit, WM_SETTEXT, 0, (LPARAM)EMPTYSTRING))
        {
            LVITEM lvi;

		    lvi.iSubItem = 1;
		    lvi.pszText = (LPTSTR)EMPTYSTRING;

            for (int i = 0; i < (sizeof(ListViewRows) / sizeof(ListViewRows[0])); i++)
            {
		        SendMessage(e->listview, LVM_SETITEMTEXT, i, (LPARAM)&lvi);
            }

            return TRUE;
        }
    }

    return FALSE;
}

static void WINAPI SetAppTitle(HWND hWnd, LPCWSTR filePath)
{
    LPCWSTR titleFormat;
    BOOL hasTitle = FALSE;
	
    if (filePath && filePath[0] && GetString(IDS_APPNAME_WITH_FILEPATH, &titleFormat))
    {
        int len = _snwprintf(NULL, 0, titleFormat, filePath);

        if (len > 0)
        {
            LPWSTR title = AllocString(len + 1);

            if (title)
            {
                hasTitle = _snwprintf_s(title, len + 1, len, titleFormat, filePath) > 0;

                if (hasTitle)
                {
                    SetWindowTextW(hWnd, title);
                }

                FreeBlock(title);
            }
        }
    }

    if (!hasTitle && GetString(IDS_APPNAME, &titleFormat))
    {
        SetWindowTextW(hWnd, titleFormat);
    }
}

static BOOL WINAPI IsFileModified(APPWINDOWINFO* e)
{
    // MAGOLE TODO: check if notes text is really different

    return SendMessage(e->edit, EM_GETMODIFY, 0, 0) || DbIsDirty(e->db);
}

static int WINAPI AppCloseFile(HWND hWnd)
{
    APPWINDOWINFO* e = (APPWINDOWINFO*)GetWindowLongPtr(hWnd, 0);
    int dlgResult = IDNO;

    if (e->db)
    {
        if (IsFileModified(e))
        {
            dlgResult = ResourceMessageBox(hWnd, IDS_SAVE_CHANGES_MSG, IDS_APPNAME, MB_YESNOCANCEL | MB_ICONWARNING | MB_DEFBUTTON3);
        }

        if (dlgResult != IDCANCEL)
        {
            if (dlgResult == IDYES)
            {
                SaveNotesIfModified(e);
            }

            SendMessage(e->vsplitter, WM_SETREDRAW, FALSE, 0);

            ClearWorkspace(e);
            EnableWorkspace(e, FALSE);

            SendMessage(e->vsplitter, WM_SETREDRAW, TRUE, 0);

            RedrawWorkspace(e);

            SetAppTitle(hWnd, NULL);

            DbClose(e->db, dlgResult == IDYES);
            e->db = NULL;
        }
    }

    return dlgResult;
}

static void WINAPI MainWmClose(HWND hWnd)
{
    if (AppCloseFile(hWnd) != IDCANCEL)
    {
        DestroyWindow(hWnd);
    }
}

static LRESULT CALLBACK MainWmNotifyTvnEndLabelEdit(HWND hWnd, NMTVDISPINFO* lParam)
{
    // If label editing was canceled, the pszText member of the TVITEM structure is NULL

	if (TrimWhiteSpace(lParam->item.pszText))
	{
        RECORD rd;
		APPWINDOWINFO* e = (APPWINDOWINFO*)GetWindowLongPtr(hWnd, 0);

		rd.recordId = (UINT32)lParam->item.lParam;
		rd.title = lParam->item.pszText;
        
		BOOL titleChanged = DbUpdateItemTitle(e->db, &rd);
        
        if (titleChanged)
		{
			UpdateItemTitle(e->listview, rd.title, 0);
            UpdateItemView(e, rd.dateModified, 2);
		}

		return titleChanged;
	}

	return FALSE; // prevent label edit
}

static LRESULT CALLBACK MainWmNotifyTvnGetDispInfo(HWND hWnd, NMTVDISPINFO* lParam)
{
	APPWINDOWINFO* e = (APPWINDOWINFO*)GetWindowLongPtr(hWnd, 0);

	if (lParam->item.mask & TVIF_TEXT)
	{
		if (!DbGetItemTitle(e->db, (UINT32)lParam->item.lParam, lParam->item.pszText, lParam->item.cchTextMax))
		{
            /*
            // Insert [Failed to load] string instead on failure.
            // This can happen if DB file is corrupted.
            */

            LPCWSTR itemErrorText;
            int itemErrorTextLength = GetString(IDS_FAILEDTOLOADITEM, &itemErrorText);

            if (itemErrorTextLength)
            {
                itemErrorTextLength = min(itemErrorTextLength, lParam->item.cchTextMax) - 1;  // not including null terminator

                memcpy(lParam->item.pszText, itemErrorText, itemErrorTextLength * sizeof(WCHAR));
                lParam->item.pszText[itemErrorTextLength] = NULL;
            }

            // TODO: log failed to get disp info
		}
	}

	if (lParam->item.mask & TVIF_CHILDREN)
	{
		DbItemHasChildren(e->db, (UINT32)lParam->item.lParam, &lParam->item.cChildren);
	}

    return 0;
}

static UINT32 CALLBACK IsValidPassword(const FILEACCESSPARAMS *params)
{
    if (params->keyInfo.passLen)
    {
        APPWINDOWINFO* e = (APPWINDOWINFO*)params->lParam;

        if (!DbKey(e->db, &params->keyInfo))
        {
            return VARESULT_ERROR;
        }

        DBVERSIONINFO version;

        if (DbGetVersionInfo(e->db, &version))
        {
            if (DbIsReadWrite(e->db) && !DbAcquireExclusiveAccess(e->db))
            {
                return VARESULT_FILE_IN_USE;
            }
            
            return VARESULT_OK;
        }
        else if (version.busy)
        {
            return VARESULT_FILE_IN_USE;
        }        
    }

    return VARESULT_INVALID_PASSWORD;
}

static UINT WINAPI LoadRootNodes(APPWINDOWINFO *e)
{
    UINT errMsgId = 0;

    /*
    // Setting redraw on the parent instead of the TreeView
    // due to WINBUG: http://support.microsoft.com/kb/130611
    */
    SendMessage(e->vsplitter, WM_SETREDRAW, FALSE, 0);

    BOOL nodesLoaded = LoadChildNodes(e, TVI_ROOT, 0);

    EnableWorkspace(e, nodesLoaded);
                
    if (nodesLoaded)
    {
        FocusOnFirstNode(e);
    }
    else
    {
        // show_sqlite_error(e->db, _T("load db")); // DEBUG
        errMsgId = IDS_CORRUPTEDFILE;
        ClearWorkspace(e);
    }

    SendMessage(e->vsplitter, WM_SETREDRAW, TRUE, 0);
    RedrawWorkspace(e);

    return errMsgId;
}

static void CALLBACK DbFileChanged(void *userData, UINT32 state)
{
    if (state & DBSTATEINIT)
    {
        // We set handler right after DbOpen() call, no need
        // for the Init state to be processed.

        return;
    }

    APPWINDOWINFO* e = (APPWINDOWINFO*)userData;
    
    SetStatusTextFromResourceId(e, (state & DBSTATEDIRTY) ? IDS_FILE_CHANGED_STATUS : IDS_APP_READY);

   /*
   LPTSTR msg;

   switch (state)
    {
        case DBSTATEINIT:
            msg = _T("init");
            break;
        case DBSTATEDIRTY:
            msg = _T("dirty");
            break;
        case DBSTATESAVED:
            msg = _T("saved");
            break;
        case DBSTATECLOSED:
            msg = _T("closed");
            break;
        case DBSTATESAVED | DBSTATECLOSED:
            msg = _T("saved and closed");
            break;
    }
            
    SendMessage(e->statusBar, WM_SETTEXT, 0, (LPARAM)msg);
    */
}

static void WINAPI AppNewFile(HWND hWnd)
{
    if (AppCloseFile(hWnd) == IDCANCEL)
    {
        return;
    }
    
    UINT errMsgId = 0;
    LPOPENFILENAME ofn = CreateOpenFileNameInfo(hWnd, FALSE);

    if (ofn)
    {
        if (GetSaveFileName(ofn))
        {
            BOOL exit = FALSE;
            APPWINDOWINFO* e = (APPWINDOWINFO*)GetWindowLongPtr(hWnd, 0);
            e->db = DbOpen(ofn->lpstrFile, DB_OPEN_READWRITE | DB_OPEN_CREATE);

            if (!e->db)
			{
                errMsgId = IDS_SAVEFILEFAILED;
			}
            else if (!DbSetChangedCallback(e->db, DbFileChanged, e))
            {
                errMsgId = IDS_UNEXPECTEDERROR;
            }
            else
            {
                FILEPROPINFO *props = CreateFilePropInfo(hWnd);

                if (props)
                {
                    INT_PTR propsRes = ShowFileProp(props);

                    if (propsRes < 0)
                    {
                        errMsgId = IDS_UNEXPECTEDERROR;
                    }
                    else if (propsRes == 0) // user decided to exit
                    {
                        exit = TRUE;
                    }
                    else // changes were saved by the user
                    {
                        if (!DbKey(e->db, &props->keyInfo))
                        {
                            errMsgId = IDS_SETKEYFAILED;
                        }
                        else
                        {
                            DBVERSIONINFO version;
                            BOOL rwFile;

                            if (!DbGetVersionInfo(e->db, &version))
                            {
                                errMsgId = version.busy ? IDS_FILEINUSE : IDS_FILEORPASSWORDINVALID;
                            }
                            else if ((rwFile = DbIsReadWrite(e->db)) && !DbAcquireExclusiveAccess(e->db))
                            {
                                errMsgId = IDS_FILEINUSE;
                            }
                            else if (rwFile && !DbGetVersionInfo(e->db, &version)) // Query version again after locking the file
                            {
                                errMsgId = IDS_FILEORPASSWORDINVALID;
                            }
                            else if (!version.isSupported)
                            {
                                errMsgId = IDS_FILEVERSIONNOTSUPPORTED;
                            }
                            else // version.isSupported
                            {
                                if (version.isUpgradeNeeded)
                                {
                                    if (!rwFile)
                                    {
                                        errMsgId = IDS_READONLYFILESCHEMAUPDATE;
                                    }
                                    else
                                    {
                                        if ((version.schemaVersion > 0) && ResourceMessageBox(hWnd, IDS_SCHEMAUPDATENEEDED, IDS_APPNAME, MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES)
                                        {
                                            exit = TRUE;
                                        }
                                        else
                                        {
                                            if (!DbUpgradeSchema(e->db))
                                            {
                                                errMsgId = IDS_SCHEMAUPDATEFAILED;
                                            }
                                        }
                                    }
                                }

                                if (!errMsgId && !exit)
                                {
                                    if (!DbUpdateFileDetails(e->db, props->title, props->description))
                                    {
                                        ResourceMessageBox(hWnd, IDS_FILEDETAILSUPDATEFAILED, IDS_APPNAME, MB_OK | MB_ICONWARNING);
                                    }

                                    errMsgId = LoadRootNodes(e);
                                    UINT32 recCount;

                                    // If no errors loading file and no records in file,
                                    // create new record automatically.
                                    if ((errMsgId == 0) && DbGetItemCount(e->db, &recCount) && (recCount == 0))
                                    {
                                        AppTreeViewNewItem(e, FALSE);
                                    }
                                }
                            }
                        }
                    }

                    DestroyFilePropInfo(props);
                }
                else
                {
                    errMsgId = IDS_UNEXPECTEDERROR;
                }
            }

            if ((errMsgId || exit) && e->db)
            {
                DbClose(e->db, FALSE);
                e->db = NULL;
            }

            if (e->db)
            {
                SetAppTitle(hWnd, ofn->lpstrFile);
            }
        }
        else if (CommDlgExtendedError())
        {
            errMsgId = IDS_UNEXPECTEDERROR;
        }

        FreeBlock(ofn);
    }
    else
    {
        errMsgId = IDS_UNEXPECTEDERROR;
    }

    if (errMsgId)
    {
        ResourceMessageBox(hWnd, errMsgId, IDS_APPNAME, MB_OK | MB_ICONERROR);
    }
}

// TODO: should show save file message in status bar if text got modified in textbox.

static void WINAPI AppSaveFile(HWND hWnd)
{
    APPWINDOWINFO* e = (APPWINDOWINFO*)GetWindowLongPtr(hWnd, 0);

    SaveNotesIfModified(e);

    if (!DbSave(e->db))
    {
        ResourceMessageBox(hWnd, IDS_SAVEFILEFAILED, IDS_APPNAME, MB_OK | MB_ICONERROR);
    }
}

static UINT WINAPI OpenFileInternal(HWND hWnd, LPCWSTR filePath, UINT32 fileFlags)
{
    UINT errMsgId = 0;
    BOOL exit = FALSE;
    APPWINDOWINFO* e = (APPWINDOWINFO*)GetWindowLongPtr(hWnd, 0);

    e->db = DbOpen(filePath, fileFlags);

    if (!e->db)
    {
        errMsgId = IDS_OPENFILEFAILED;
    }
    else if (!DbSetChangedCallback(e->db, DbFileChanged, e))
    {
        errMsgId = IDS_UNEXPECTEDERROR;
    }
    else
    {
        INT_PTR accessRes = ShowFileAccess(hWnd, filePath, IsValidPassword, (LPARAM)e);

        if (accessRes == IDCANCEL) // user decided to exit dialog
        {
            exit = TRUE;
        }
        else if (accessRes <= 0)
        {
            errMsgId = IDS_UNEXPECTEDERROR;
        }
        else // IDOK
        {
            DBVERSIONINFO version;

            if (!DbGetVersionInfo(e->db, &version) || !version.isSupported)
            {
                errMsgId = IDS_FILEVERSIONNOTSUPPORTED;
            }
            else
            {
                if (version.isUpgradeNeeded)
                {
                    if (!DbIsReadWrite(e->db))
                    {
                        errMsgId = IDS_READONLYFILESCHEMAUPDATE;
                    }
                    else
                    {
                        if ((version.schemaVersion > 0) && ResourceMessageBox(hWnd, IDS_SCHEMAUPDATENEEDED, IDS_APPNAME, MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES)
                        {
                            exit = TRUE;
                        }
                        else
                        {
                            if (!DbUpgradeSchema(e->db))
                            {
                                errMsgId = IDS_SCHEMAUPDATEFAILED;
                            }
                        }
                    }
                }

                if (!errMsgId && !exit)
                {
                    errMsgId = LoadRootNodes(e);
                }
            }
        }
    }

    if ((errMsgId || exit) && e->db)
    {
        DbClose(e->db, FALSE);
        e->db = NULL;
    }

    if (e->db)
    {
        SetAppTitle(hWnd, filePath);
    }

    return errMsgId;
}

BOOL WINAPI OpenDbFile(HWND hWnd, LPCWSTR filePath)
{
    if (AppCloseFile(hWnd) == IDCANCEL)
    {
        return FALSE;
    }
    
    UINT errMsgId = OpenFileInternal(hWnd, filePath, DB_OPEN_READWRITE);

    return errMsgId == 0;
}

static void WINAPI AppOpenFile(HWND hWnd)
{
    if (AppCloseFile(hWnd) == IDCANCEL)
    {
        return;
    }

    UINT errMsgId = 0;
	LPOPENFILENAME ofn = CreateOpenFileNameInfo(hWnd, TRUE);

	if (ofn)
	{
		if (GetOpenFileName(ofn))
		{
            UINT32 fileFlags = (ofn->Flags & OFN_READONLY) ? DB_OPEN_READONLY : DB_OPEN_READWRITE;

            errMsgId = OpenFileInternal(hWnd, ofn->lpstrFile, fileFlags);
		}
		else if (CommDlgExtendedError())
		{
            errMsgId = IDS_UNEXPECTEDERROR;
		}

        FreeBlock(ofn);
    }
    else
    {
        errMsgId = IDS_UNEXPECTEDERROR;
    }

    if (errMsgId)
    {
        ResourceMessageBox(hWnd, errMsgId, IDS_APPNAME, MB_OK | MB_ICONERROR);
    }
}

static void WINAPI FilePropertiesMsg(HWND hWnd)
{
    UINT errMsgId = 0;
    FILEPROPINFO *props = CreateFilePropInfo(hWnd);

    if (props)
    {
        APPWINDOWINFO* e = (APPWINDOWINFO*)GetWindowLongPtr(hWnd, 0);

        props->passExists = TRUE;
        props->readonly = !DbIsReadWrite(e->db);

        RECORD rd;
        rd.title = props->title;
        rd.notes = props->description;
        
        if (!DbGetFileDetails(e->db, &rd))
        {
            errMsgId = IDS_GETFILEDETAILSFAILED;
        }
        else
        {
            INT_PTR propsRes = ShowFileProp(props);

            if (propsRes < 0)
            {
                errMsgId = IDS_UNEXPECTEDERROR;
            }
            else if (propsRes > 0 && !props->readonly) // changes were saved by the user
            {
                if (!DbUpdateFileDetails(e->db, props->title, props->description))
                {
                    errMsgId = IDS_FILEDETAILSUPDATEFAILED;
                }
                else if (props->keyInfo.passLen)
                {
                    if (IsFileModified(e))
                    {
                        if (ResourceMessageBox(hWnd, IDS_SAVE_CHANGES_BEFORE_REKEY_MSG, IDS_APPNAME, MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) == IDYES)
                        {
                            if (!DbSave(e->db))
                            {
                                errMsgId = IDS_SAVEFILEFAILED;
                            }
                            else if (!DbRekey(e->db, &props->keyInfo))
                            {
                                errMsgId = IDS_SETKEYFAILED;
                            }
                        }
                    }
                    else if (!DbRekey(e->db, &props->keyInfo))
                    {
                        errMsgId = IDS_SETKEYFAILED;
                    }
                }
            }
        }

        DestroyFilePropInfo(props);
    }
    else
    {
        errMsgId = IDS_UNEXPECTEDERROR;
    }
    
    if (errMsgId)
    {
        ResourceMessageBox(hWnd, errMsgId, IDS_APPNAME, MB_OK | MB_ICONERROR);
    }
}

static BOOL WINAPI ProcessMenuCommandForEdit(HWND hWndEdit, DWORD idm)
{
    UINT msg;
    LPARAM lparam = 0;

    switch (idm)
    {
        case IDM_EDIT_UNDO:
            msg = EM_UNDO;
            break;
        case IDM_EDIT_CUT:
            msg = WM_CUT;
            break;
        case IDM_EDIT_COPY:
            msg = WM_COPY;
            break;
        case IDM_EDIT_PASTE:
            msg = WM_PASTE;
            break;
        case IDM_EDIT_DELETE:
            msg = WM_CLEAR;
            break;
        case IDM_EDIT_SELECTALL:
            msg = EM_SETSEL;
            lparam = -1;
            break;
        default:
            return FALSE;
    }

    SendMessage(hWndEdit, msg, 0, lparam);

    return TRUE;
}

static void WINAPI ProcessMenuCommandForTreeView(APPWINDOWINFO *e, DWORD idm)
{
    switch (idm)
    {
        case IDM_EDIT_NEWITEM:
			AppTreeViewNewItem(e, TRUE);
            break;
		case IDM_EDIT_NEWSUBITEM:
			AppTreeViewNewItem(e, FALSE);
            break;
		case IDM_EDIT_RENAME:
			AppTreeViewEditItem(e, FALSE);
            break;
		case IDM_EDIT_DELETE:

            if (ResourceMessageBox(e->hWndMain, IDS_REMOVEITEMS, IDS_APPNAME, MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) == IDYES)
            {
			    AppTreeViewEditItem(e, TRUE);
            }

            break;
        case IDM_EDIT_READONLY:
            AppTreeViewSetReadOnlyItem(e);
            break;
    }
}

static LRESULT WINAPI AppEditCommand(HWND hWnd, WPARAM wParam)
{
    APPWINDOWINFO* e = (APPWINDOWINFO*)GetWindowLongPtr(hWnd, 0);

    HWND hWndFocus = GetFocus();
    DWORD idm = LOWORD(wParam);

    if (hWndFocus == e->edit)
    {
        if (!ProcessMenuCommandForEdit(hWndFocus, idm))
        {
            if (idm == IDM_EDIT_READONLY)
            {
                AppTreeViewSetReadOnlyItem(e);
            }
        }
    }
    else if (hWndFocus == e->treeview)
    {
        ProcessMenuCommandForTreeView(e, idm);
    }
    else if (IsEditWindow(hWndFocus))
    {
        ProcessMenuCommandForEdit(hWndFocus, idm);
    }

    return 0;
}

struct MENUSTATE
{
    UINT id;
    UINT fState;
};

typedef struct MENUSTATE MENUSTATE;

struct EDITMENUSTATE
{
    MENUSTATE undo;
    MENUSTATE cut;
    MENUSTATE copy;
    MENUSTATE paste;
    MENUSTATE del;
    MENUSTATE selAll;
    MENUSTATE newItem;
    MENUSTATE newSubitem;
    MENUSTATE rename;
    MENUSTATE readonly;
};

typedef struct EDITMENUSTATE EDITMENUSTATE;

static const EDITMENUSTATE EditMenuInit =
{
    IDM_EDIT_UNDO, MFS_GRAYED,
    IDM_EDIT_CUT, MFS_GRAYED,
    IDM_EDIT_COPY, MFS_GRAYED,
    IDM_EDIT_PASTE, MFS_GRAYED,
    IDM_EDIT_DELETE, MFS_GRAYED,
    IDM_EDIT_SELECTALL, MFS_GRAYED,
    IDM_EDIT_NEWITEM, MFS_GRAYED,
    IDM_EDIT_NEWSUBITEM, MFS_GRAYED,
    IDM_EDIT_RENAME, MFS_GRAYED,
    IDM_EDIT_READONLY, MFS_GRAYED
};

struct FILEMENUSTATE
{
    MENUSTATE fileNew;
    MENUSTATE fileOpen;
    MENUSTATE fileSave;
    MENUSTATE props;
    MENUSTATE close;
};

typedef struct FILEMENUSTATE FILEMENUSTATE;

static const FILEMENUSTATE FileMenuInit =
{
    IDM_FILE_NEW, MFS_GRAYED,
    IDM_FILE_OPEN, MFS_GRAYED,
    IDM_FILE_SAVE, MFS_GRAYED,
    IDM_FILE_PROPERTIES, MFS_GRAYED,
    IDM_FILE_CLOSE, MFS_GRAYED
};

struct TREEVIEWMENUSTATE
{
    MENUSTATE newItem;
    MENUSTATE newSubitem;
    MENUSTATE del;
    MENUSTATE rename;
    MENUSTATE readonly;
};

typedef struct TREEVIEWMENUSTATE TREEVIEWMENUSTATE;

static const TREEVIEWMENUSTATE TreeViewMenuInit =
{
    IDM_EDIT_NEWITEM, MFS_GRAYED,
    IDM_EDIT_NEWSUBITEM, MFS_GRAYED,
    IDM_EDIT_DELETE, MFS_GRAYED,
    IDM_EDIT_RENAME, MFS_GRAYED,
    IDM_EDIT_READONLY, MFS_GRAYED
};

static BOOL WINAPI UpdateMenu(HMENU hMenu, MENUSTATE *items, UINT count)
{
    MENUITEMINFO itm;

    itm.cbSize = sizeof(MENUITEMINFO);
    itm.fMask = MIIM_STATE;
    
    for (UINT i = 0; i < count; i++)
    {
        itm.fState = items[i].fState;

        if (!SetMenuItemInfo(hMenu, items[i].id, FALSE, &itm))
        {
            return FALSE;
        }
    }

    return TRUE;
}

static void WINAPI InitMenuStateForEdit(HWND hWndEdit, EDITMENUSTATE *editMenu)
{
    LONG editStyle = GetWindowLong(hWndEdit, GWL_STYLE);                    

    if ((editStyle & (WS_VISIBLE | WS_DISABLED)) == WS_VISIBLE)
    {
        DWORD selLength = GetEditControlSelectionLength(hWndEdit);

        if (selLength)
        {
            editMenu->copy.fState = MFS_ENABLED;
        }

        if (selLength != (DWORD)SendMessage(hWndEdit, WM_GETTEXTLENGTH, 0, 0))
        {
            editMenu->selAll.fState = MFS_ENABLED;
        }

        if (!(editStyle & ES_READONLY))
        {
            if (SendMessage(hWndEdit, EM_CANUNDO, 0, 0))
            {
                editMenu->undo.fState = MFS_ENABLED;
            }

            if (selLength)
            {
                editMenu->cut.fState = MFS_ENABLED;
                editMenu->del.fState = MFS_ENABLED;
            }

            if (IsClipboardFormatAvailable(CF_UNICODETEXT))
            {
                editMenu->paste.fState = MFS_ENABLED;
            }
        }
    }
}

// TODO: break up into smaller funcs
static LRESULT CALLBACK MainWmInitMenuPopup(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (!HIWORD(lParam)) // HIWORD(lParam) is TRUE if it's the system menu; otherwise, FALSE
    {
        APPWINDOWINFO* e = (APPWINDOWINFO*)GetWindowLongPtr(hWnd, 0);
                
        switch (LOWORD(lParam)) // The zero-based relative position of the menu item that opens the drop-down menu or submenu.
        {
            case 0: // File
            {
                FILEMENUSTATE fileMenu = FileMenuInit;
                
                fileMenu.fileNew.fState = MFS_ENABLED;
                fileMenu.fileOpen.fState = MFS_ENABLED;

                if (e->db)
                {
                    if (IsFileModified(e))
                    {
                        fileMenu.fileSave.fState = MFS_ENABLED;
                    }

                    fileMenu.props.fState = MFS_ENABLED;
                    fileMenu.close.fState = MFS_ENABLED;
                }

                UpdateMenu((HMENU)wParam, (MENUSTATE*)&fileMenu, sizeof(FILEMENUSTATE) / sizeof(MENUSTATE));

                return 0;
            }
            case 1: // Edit
            {
                HWND focus = GetFocus();
                
                EDITMENUSTATE editMenu = EditMenuInit;

                if (focus == e->treeview || focus == e->edit)
                {
                    if (e->db)
                    {
                        UINT32 count;
                        BOOL rwFile = DbIsReadWrite(e->db);
                        BOOL canAddItem = rwFile && DbGetItemCount(e->db, &count) && (count < MAX_RECORD_COUNT);
                        
                        if (focus == e->edit)
                        {
                            InitMenuStateForEdit(e->edit, &editMenu);
                        }
                        else if (canAddItem)
                        {
                            editMenu.newItem.fState = MFS_ENABLED;
                        }

                        TVITEM tvi;
                        tvi.hItem = (HTREEITEM)SendMessage(e->treeview, TVM_GETNEXTITEM, TVGN_CARET, 0);

	                    if (tvi.hItem)
	                    {
                            tvi.mask = TVIF_PARAM;

                            if (SendMessage(e->treeview, TVM_GETITEM, 0, (LPARAM)&tvi))
                            {
                                if (focus == e->treeview)
                                {
                                    if (canAddItem)
                                    {
                                        UINT32 level;

                                        if (DbGetRecordLevel(e->db, (UINT32)tvi.lParam, &level) && level && (level < MAX_RECORD_LEVEL_DEPTH))
                                        {
                                            editMenu.newSubitem.fState = MFS_ENABLED;
                                        }
                                    }
                               
                                    if (rwFile)
                                    {
                                        editMenu.del.fState = MFS_ENABLED;
                                    }
                                }

                                BOOL readonly;

                                if (DbIsReadOnlyItem(e->db, (UINT32)tvi.lParam, &readonly))
                                {
                                    if (rwFile)
                                    {
                                        editMenu.readonly.fState = MFS_ENABLED;
                                    }

                                    if (readonly)
                                    {
                                        editMenu.readonly.fState |= MFS_CHECKED;
                                    }
                                    else if (rwFile && focus == e->treeview)
                                    {
                                        editMenu.rename.fState = MFS_ENABLED;
                                    }
                                }
                            }
                        }
                    }
                }
                else if (IsEditWindow(focus))
                {
                    InitMenuStateForEdit(focus, &editMenu);
                }

                UpdateMenu((HMENU)wParam, (MENUSTATE*)&editMenu, sizeof(EDITMENUSTATE) / sizeof(MENUSTATE));

                return 0;
            }
        }
    }

    return DefWindowProc(hWnd, msg, wParam, lParam);
}

static LRESULT CALLBACK MainWmContextMenuTreeView(HWND hWnd, HWND wParam, LPARAM lParam)
{
    HMENU menu = LoadMenu(GetCurrentModuleHandle(), MAKEINTRESOURCE(IDR_TREEMENU));

     // TODO: add more safety

	if (menu)
	{
		POINT pos;

		pos.x = GET_X_LPARAM(lParam);
		pos.y = GET_Y_LPARAM(lParam);

		if ((pos.x == -1) && (pos.y == -1))
		{
			pos.x = 0;
			pos.y = 0;

			ClientToScreen(wParam, &pos);
		}

		HMENU popup = GetSubMenu(menu, 0);

        APPWINDOWINFO* e = (APPWINDOWINFO*)GetWindowLongPtr(hWnd, 0);
        TREEVIEWMENUSTATE treeMenu = TreeViewMenuInit;

        if (e->db)
        {
            UINT32 count;
            BOOL rwFile = DbIsReadWrite(e->db);
            BOOL canAddItem = rwFile && DbGetItemCount(e->db, &count) && (count < MAX_RECORD_COUNT);

            if (canAddItem)
            {
                treeMenu.newItem.fState = MFS_ENABLED;
            }

            TVITEM tvi;
            tvi.hItem = (HTREEITEM)SendMessage(e->treeview, TVM_GETNEXTITEM, TVGN_CARET, 0);

            if (tvi.hItem)
		    {
                tvi.mask = TVIF_PARAM;

                if (SendMessage(e->treeview, TVM_GETITEM, 0, (LPARAM)&tvi))
                {
                    if (canAddItem)
                    {
                        UINT32 level;

                        if (DbGetRecordLevel(e->db, (UINT32)tvi.lParam, &level) && level && (level < MAX_RECORD_LEVEL_DEPTH))
                        {
                            treeMenu.newSubitem.fState = MFS_ENABLED;
                        }
                    }

                    if (rwFile)
                    {
                        treeMenu.del.fState = MFS_ENABLED;
                    }

                    BOOL readonly;

                    if (DbIsReadOnlyItem(e->db, (UINT32)tvi.lParam, &readonly))
                    {
                        if (rwFile)
                        {
                            treeMenu.readonly.fState = MFS_ENABLED;
                        }

                        if (readonly)
                        {
                            treeMenu.readonly.fState |= MFS_CHECKED;
                        }
                        else if (rwFile)
                        {
                            treeMenu.rename.fState = MFS_ENABLED;
                        }
                    }
                }
            }
        }

        UpdateMenu(popup, (MENUSTATE*)&treeMenu, sizeof(TREEVIEWMENUSTATE) / sizeof(MENUSTATE));

        // TODO: get menu drop alignment from the window
        // http://msdn.microsoft.com/en-us/library/windows/desktop/ms648003(v=vs.85).aspx
        // http://blogs.msdn.com/b/larryosterman/archive/2008/10/07/what-s-wrong-with-this-code-part-24-the-answer.aspx
        // http://blogs.msdn.com/b/michkap/archive/2007/03/11/1857043.aspx

		TrackPopupMenuEx(popup, GetSystemMetrics(SM_MENUDROPALIGNMENT) ? (TPM_RIGHTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON | TPM_NONOTIFY) : (TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON | TPM_NONOTIFY), pos.x, pos.y, hWnd, NULL);

		DestroyMenu(menu);
	}

    return TRUE;
}

static LRESULT CALLBACK MainWmContextMenu(HWND hWnd, HWND wParam, LPARAM lParam)
{
    switch (GetDlgCtrlID(wParam))
	{
        case  IDC_APP_TREEVIEW:
		    return MainWmContextMenuTreeView(hWnd, wParam, lParam);
	}

    return FALSE;
}

static LRESULT CALLBACK MainWmCtlColorStatic(HWND hWnd, UINT msg, HDC wParam, HWND lParam)
{
    switch (GetDlgCtrlID(lParam))
	{
        case  IDC_APP_EDIT:
            LONG editStyle = GetWindowLong(lParam, GWL_STYLE);

            if ((editStyle & (WS_VISIBLE | WS_DISABLED | ES_READONLY)) == (ES_READONLY | WS_VISIBLE))
            {
                SetBkMode(wParam, TRANSPARENT);

		        return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
            }

            break;
	}

    return DefWindowProc(hWnd, msg, (WPARAM)wParam, (LPARAM)lParam);
}

static void WINAPI MainWmGetMinMaxInfo(HWND hWnd, LPMINMAXINFO lParam)
{
    APPWINDOWINFO* e = (APPWINDOWINFO*)GetWindowLongPtr(hWnd, 0);

    /*
    // WARNING!
    //
    // WM_GETMINMAXINFO is the first message for a top-level window,
    // followed by WM_NCCREATE, WM_NCCALCSIZE, and finally WM_CREATE
    // before CreateWindowEx() has even returned the handle
    // to the window being created.
    */
    if (e)
    {
        lParam->ptMinTrackSize = e->ptMinTrackSize;
    }
}

static void WINAPI SetActiveAccel(HWND hWnd, BOOL isTreeView)
{
    APPWINDOWINFO* e = (APPWINDOWINFO*)GetWindowLongPtr(hWnd, 0);
    
    UpdateMsgLoopAccel(e->hMsgLoop, hWnd, isTreeView ? e->hAccelTreeView : e->hAccel);
}

static LRESULT CALLBACK MainWindowProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
		case WM_NOTIFY:
			switch (((LPNMHDR)lParam)->idFrom)
			{
				case IDC_APP_TREEVIEW:
					switch (((LPNMHDR)lParam)->code)
					{
						case TVN_GETDISPINFO:
							return MainWmNotifyTvnGetDispInfo(hWnd, (LPNMTVDISPINFO)lParam);

						case TVN_ITEMEXPANDING:
							return MainWmNotifyTvnItemExpanding(hWnd, (LPNMTREEVIEW)lParam);

						case TVN_SELCHANGED:
							return MainWmNotifyTvnSelChanged(hWnd, (LPNMTREEVIEW)lParam);

						case NM_RCLICK:
							return MainWmNotifyTvnNmRClick((LPNMHDR)lParam);

						case TVN_ENDLABELEDIT:
							return MainWmNotifyTvnEndLabelEdit(hWnd, (LPNMTVDISPINFO)lParam);

						case TVN_BEGINLABELEDIT:
							return MainWmNotifyTvnBeginLabelEdit(hWnd, (LPNMTVDISPINFO)lParam);

		/*				case TVN_DELETEITEM:
							AppTreeViewDeleteItem(hWnd, (LPNMTREEVIEW)lParam);

							return 0;
						
						case TVN_SETDISPINFO:
							AppTreeViewOnSetDispInfo((LPNMTVDISPINFO)lParam);

							return 0; */

                        case NM_SETFOCUS:
                        case NM_KILLFOCUS:
                            SetActiveAccel(hWnd, ((LPNMHDR)lParam)->code == NM_SETFOCUS);
                            return 0;
					}

					break;
				case IDC_APP_HSPLITTER:
				case IDC_APP_VSPLITTER:
					MainWmNotifySplitter(hWnd, (NMSPLITTER*)lParam);

                    return 0;
			}

			break;
	    case WM_GETMINMAXINFO:
            MainWmGetMinMaxInfo(hWnd, (LPMINMAXINFO)lParam);

			return 0;
		case WM_WINDOWPOSCHANGED:
			if (!(((PWINDOWPOS)lParam)->flags & SWP_NOSIZE)) // fire only on size change
			{
				 MainWmWindowPosChanged(hWnd);
			}

			return 0;
		case WM_CONTEXTMENU:
            if (MainWmContextMenu(hWnd, (HWND)wParam, lParam))
            {
                return 0;
            }

			break;
        case WM_INITMENUPOPUP:
            return MainWmInitMenuPopup(hWnd, msg, wParam, lParam);

		case WM_COMMAND:
        /*    if (lParam)
            {
                // message from control
            }
            else
            { */
			    switch (LOWORD(wParam))
			    {
				    case IDM_FILE_PROPERTIES:
                        FilePropertiesMsg(hWnd);
					    
                        return 0;
				    case IDM_FILE_OPEN:
					    AppOpenFile(hWnd);

					    return 0;
                    case IDM_FILE_SAVE:
					    AppSaveFile(hWnd);

					    return 0;
				    case IDM_FILE_NEW:
					    AppNewFile(hWnd);

					    return 0;
                    case IDM_FILE_CLOSE:
                        AppCloseFile(hWnd);

                        return 0;
				    case IDM_EXIT_APP:
					    SendMessage(hWnd, WM_CLOSE, NULL, NULL);

					    return 0;

			    /*	case IDC_APP_EDIT:
					    if (HIWORD(wParam) == EN_CHANGE)
					    {
						    MessageBox(hWnd, _T("edit text changed"), _T("app"), MB_OK);
					    }

					    return 0;*/

                    case IDM_EDIT_UNDO:
                    case IDM_EDIT_CUT:                    
                    case IDM_EDIT_COPY:
                    case IDM_EDIT_PASTE:
                    case IDM_EDIT_DELETE:
                    case IDM_EDIT_SELECTALL:
                    case IDM_EDIT_NEWITEM:
                    case IDM_EDIT_NEWSUBITEM:
                    case IDM_EDIT_RENAME:
                    case IDM_EDIT_READONLY:
                        return AppEditCommand(hWnd, wParam);

                    case IDM_ABOUT_APP:
                        MainWmCommandAbout(hWnd);

                        return 0;
			    }
          //  }

			break;
		case WM_ACTIVATE: //  WM_NCACTIVATE: // WM_ACTIVATEAPP:
			MainWmActivate(hWnd, wParam);

            return 0;
        case WM_SYSCOMMAND:
            MainWmSysCommand(hWnd, wParam);

            break;
        case WM_APP_WINDOWCREATED:
            MainWmAppWindowCreated(hWnd, wParam, (HWND)lParam);

            return 0;
        case WM_CLOSE:
            MainWmClose(hWnd);

            return 0;
        case WM_NCDESTROY:
			MainWmNcDestroy(hWnd);

			break;
        case WM_DESTROY:
            MainWmDestroy(hWnd);

            break;
		case WM_CREATE:
			return MainWmCreate(hWnd, (LPCREATESTRUCT)lParam);

        case WM_SYSCOLORCHANGE:
            MainWmSysColorChange(hWnd);

            break;
        case WM_SETTINGCHANGE:
            MainWmSettingChange(hWnd, wParam, lParam);

            break;
        case WM_CTLCOLORSTATIC:
            return MainWmCtlColorStatic(hWnd, msg, (HDC)wParam, (HWND)lParam);

		//case WM_NCCREATE:
			//return AppNcCreate(hWnd, msg, wParam, lParam);
	}

    return DefWindowProc(hWnd, msg, wParam, lParam);
}


ATOM WINAPI RegisterMainWindowClass()
{
    WNDCLASSEX wc;
    ZeroMemory(&wc, sizeof(wc));

    wc.cbSize		  = sizeof(WNDCLASSEX);
	// wc.style          = 0; // CS_HREDRAW | CS_VREDRAW;
	wc.lpfnWndProc    = MainWindowProc;
	// wc.cbClsExtra     = 0;
	wc.cbWndExtra     = sizeof(APPWINDOWINFO*);
    wc.hInstance      = GetCurrentModuleHandle();
    wc.hIcon          = LoadIcon(wc.hInstance, IDI_APPLICATION);
	wc.hCursor        = LoadCursor(NULL, IDC_ARROW);
	// wc.hbrBackground  = (HBRUSH)(COLOR_BTNFACE + 1);
	wc.lpszMenuName   = MAKEINTRESOURCE(IDR_MAINMENU);
	wc.lpszClassName  = MAINWNDCLASSNAME;
	// wc.hIconSm        = NULL; // (HICON)LoadImage(wc.hInstance, IDI_APPLICATION, IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);

    if (wc.hCursor)
    {
        return RegisterClassEx(&wc);
    }

    return 0;
}

HWND WINAPI CreateMainWindow(HMSGLOOP hMsgLoop)
{
    LPCWSTR lpWindowName;

    if (GetString(IDS_APPNAME, &lpWindowName))
    {
        return CreateWindowEx(WS_EX_APPWINDOW,
            MAINWNDCLASSNAME,
            lpWindowName,
            WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            DpiScaleX(APP_DEFAULT_LOGICAL_WIDTH),
            DpiScaleY(APP_DEFAULT_LOGICAL_HEIGHT),
            NULL,
            NULL,
            GetCurrentModuleHandle(),
            (LPVOID)hMsgLoop);
    }

    return NULL;
}