//
// 2013 May 3
//
// This is free and unencumbered software released into the public domain.
//

#include "AppCom.h"

struct IEditDropTarget
{
    IDropTarget idp;
    ULONG refCount;
    BOOL allowDrop;
    HWND hWnd;
    UINT32 formatIndex;
};

typedef struct IEditDropTarget IEditDropTarget;


static void STDMETHODCALLTYPE PositionEditWndCursor(HWND hWndEdit, POINTL pt)
{
	ScreenToClient(hWndEdit, (POINT*)&pt); // should use MapWindowPoints() ?

	LRESULT curPos = SendMessage(hWndEdit, EM_CHARFROMPOS, 0, MAKELPARAM(pt.x, pt.y));

	SendMessage(hWndEdit, EM_SETSEL, LOWORD(curPos), LOWORD(curPos));
}

static void STDMETHODCALLTYPE EditWndUpdateDropEffect(DWORD *pdwEffect, DWORD grfKeyState)
{
	DWORD dwEffect;
	
    switch (grfKeyState & (MK_CONTROL | MK_SHIFT))
    {
        case MK_CONTROL:
        case MK_CONTROL | MK_SHIFT:
            dwEffect = DROPEFFECT_COPY;
            break;
        default:
            dwEffect = (*pdwEffect & DROPEFFECT_MOVE) ? DROPEFFECT_MOVE : DROPEFFECT_COPY;
            break;
    }

    *pdwEffect &= dwEffect;
}

// TODO: add support for CF_TEXT and CF_LOCALE
static const FORMATETC UnicodeTextFormatEtc = { CF_UNICODETEXT, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
static const FORMATETC TextFormatEtc = { CF_TEXT, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
// static const FORMATETC LocaleFormatEtc = { CF_LOCALE, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };

/* formats in the preferred order */
static const FORMATETC* SupportedFormats[] =
{
    &UnicodeTextFormatEtc,
    &TextFormatEtc
};

static BOOL SetSupportedDropFormat(IEditDropTarget *This, IDataObject *pDataObj)
{
    for (UINT32 i = 0; i < (sizeof(SupportedFormats) / sizeof(SupportedFormats[0])); i++)
    {
        if (pDataObj->lpVtbl->QueryGetData(pDataObj, (LPFORMATETC)SupportedFormats[i]) == S_OK)
        {
            This->formatIndex = i;
            return TRUE;
        }
    }

    return FALSE;
}

//  DWORD codepage;
//  int cch = GetLocaleInfo(LOCALE_USER_DEFAULT, LOCALE_RETURN_NUMBER | LOCALE_IDEFAULTANSICODEPAGE, (LPTSTR)&codepage, sizeof(DWORD) / sizeof(TCHAR));


static void STDMETHODCALLTYPE DropData(IEditDropTarget *This, IDataObject *pDataObject)
{
    // TODO: if func fails, notify drop source and don't remove content from source

	STGMEDIUM stgmed;
    LPFORMATETC hFmt = (LPFORMATETC)SupportedFormats[This->formatIndex];

	if(pDataObject->lpVtbl->GetData(pDataObject, hFmt, &stgmed) == S_OK)
	{
		LPVOID data = GlobalLock(stgmed.hGlobal);

        if (data)
        {
            switch (hFmt->cfFormat)
            {
                case CF_UNICODETEXT:
                    SendMessage(This->hWnd, EM_REPLACESEL, TRUE, (LPARAM)data);

                    break;
                case CF_TEXT:
                    {
                        int len = MultiByteToWideChar(CP_ACP, 0, (LPCSTR)data, -1, NULL, 0);

                        if (len)
                        {
                            LPWSTR lpText = AllocString(len);

                            if (lpText)
                            {
                                if (MultiByteToWideChar(CP_ACP, 0, (LPCSTR)data, -1, lpText, len) == len)
                                {
                                    SendMessage(This->hWnd, EM_REPLACESEL, TRUE, (LPARAM)lpText);
                                }

                                FreeBlock(lpText);
                            }
                        }
                    }

                    break;
            }

		    GlobalUnlock(stgmed.hGlobal);
        }

		ReleaseStgMedium(&stgmed);
	}
}

static ULONG STDMETHODCALLTYPE IEditDropTarget_AddRef(IDropTarget *This)
{
    return ++((IEditDropTarget*)This)->refCount;
}

static HRESULT STDMETHODCALLTYPE IEditDropTarget_QueryInterface(IDropTarget *This, REFIID riid, void **ppvObject)
{
    if (!ppvObject)
    {
        return E_POINTER;
    }

    if (IsEqualIID(riid, IID_IUnknown) ||  IsEqualIID(riid, IID_IDropTarget))
    {
        IEditDropTarget_AddRef(This);
        *ppvObject = This;

        return S_OK;
    }

    *ppvObject = NULL;

    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE IEditDropTarget_Release(IDropTarget *This)
{
    ULONG refCount = --((IEditDropTarget*)This)->refCount;
    
    if (refCount == 0)
    {
        // CoTaskMemFree(This);
        FreeBlock(This);
    }

    return refCount;
}

static BOOL STDMETHODCALLTYPE IsEditSpaceAvailable(HWND hEditWnd)
{
     SIZE_T length = (SIZE_T)SendMessage(hEditWnd, WM_GETTEXTLENGTH, 0, 0);
     SIZE_T maxLength = (SIZE_T)SendMessage(hEditWnd, EM_GETLIMITTEXT, 0, 0);

     return length < maxLength;
}

static HRESULT STDMETHODCALLTYPE IEditDropTarget_DragEnter(IDropTarget *This, IDataObject *pDataObj, DWORD grfKeyState, POINTL pt, DWORD *pdwEffect)
{
    IEditDropTarget *thisObj = (IEditDropTarget*)This;

    LONG editWndStyle = GetWindowLong(thisObj->hWnd, GWL_STYLE);

	thisObj->allowDrop = ((editWndStyle & (WS_VISIBLE | WS_DISABLED | ES_READONLY)) == WS_VISIBLE) &&
                         IsEditSpaceAvailable(thisObj->hWnd) &&
                         SetSupportedDropFormat(thisObj, pDataObj);

	if (thisObj->allowDrop)
	{
        EditWndUpdateDropEffect(pdwEffect, grfKeyState);

		SetFocus(thisObj->hWnd);
		PositionEditWndCursor(thisObj->hWnd, pt);
	}
	else
	{
		*pdwEffect = DROPEFFECT_NONE;
	}

	return S_OK;
}

static HRESULT STDMETHODCALLTYPE IEditDropTarget_DragOver(IDropTarget *This, DWORD grfKeyState, POINTL pt, DWORD *pdwEffect)
{
    IEditDropTarget *thisObj = (IEditDropTarget*)This;

    if (thisObj->allowDrop)
	{
		EditWndUpdateDropEffect(pdwEffect, grfKeyState);
		PositionEditWndCursor(thisObj->hWnd, pt);
	}
	else
	{
		*pdwEffect = DROPEFFECT_NONE;
	}

	return S_OK;
}

static HRESULT STDMETHODCALLTYPE IEditDropTarget_DragLeave(IDropTarget *This)
{
    UNREFERENCED_PARAMETER(This);
    // thisObj->allowDrop = FALSE;

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE IEditDropTarget_Drop(IDropTarget *This, IDataObject *pDataObj, DWORD grfKeyState, POINTL pt, DWORD *pdwEffect)
{
    IEditDropTarget *thisObj = (IEditDropTarget*)This;

	if (thisObj->allowDrop)
	{
        EditWndUpdateDropEffect(pdwEffect, grfKeyState);
        PositionEditWndCursor(thisObj->hWnd, pt);
		DropData(thisObj, pDataObj);

        // thisObj->allowDrop = FALSE;
	}
	else
	{
		*pdwEffect = DROPEFFECT_NONE;
	}

	return S_OK;
}

static const IDropTargetVtbl IEditDropTargetVtbl = {
    IEditDropTarget_QueryInterface,
    IEditDropTarget_AddRef,
    IEditDropTarget_Release,
    IEditDropTarget_DragEnter,
    IEditDropTarget_DragOver,
    IEditDropTarget_DragLeave,
    IEditDropTarget_Drop
};

HRESULT STDAPICALLTYPE RegisterDropWindow(HWND hWnd, IDropTarget **ppDropTarget)
{
    if (!hWnd)
    {
        return E_INVALIDARG;
    }

    if (!ppDropTarget)
    {
        return E_POINTER;
    }

    *ppDropTarget = NULL;
	IEditDropTarget *pDropTarget = (IEditDropTarget*)AllocBlock(sizeof(IEditDropTarget)); // CoTaskMemAlloc(sizeof(IEditDropTarget));

    if (!pDropTarget)
    {
        return E_OUTOFMEMORY;
    }

    pDropTarget->idp.lpVtbl = &IEditDropTargetVtbl;
    pDropTarget->refCount = 1;    
    pDropTarget->allowDrop = FALSE;
    pDropTarget->hWnd = hWnd;
    pDropTarget->formatIndex = 0;

	HRESULT hResult = RegisterDragDrop(hWnd, &pDropTarget->idp);

    if (FAILED(hResult))
    {
        // CoTaskMemFree(pDropTarget);
        FreeBlock(pDropTarget);
    }
    else
    {
	    *ppDropTarget = &pDropTarget->idp;
    }

    return hResult;
}

HRESULT STDAPICALLTYPE UnregisterDropWindow(HWND hWnd, IDropTarget *pDropTarget)
{
    if (!hWnd || !pDropTarget)
    {
        return E_INVALIDARG;
    }

	HRESULT hResult = RevokeDragDrop(hWnd);
    
    if (SUCCEEDED(hResult))
    {
	    pDropTarget->lpVtbl->Release(pDropTarget);
    }

    return hResult;
}