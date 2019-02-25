//
// This is free and unencumbered software released into the public domain.
//

#include "SplitterWindow.h"
#include "UICommon.h"
#include <Windowsx.h>

static_assert((sizeof(NMSPLITTER) % 4) == 0, "sizeof(NMSPLITTER) must be multiple of 4.");

#define SPLITTER_MIN_LOGICAL_SPACE 64 // default splitter min width or height in pixels
#define SPLITTER_LOGICAL_HALF_THICKNESS 2 // default half thickness of the splitter bar

#define SplitterMakeParams(isVSplitter, position) ((LPVOID)((((ULONG_PTR)(isVSplitter)) & 0x1) | (((ULONG_PTR)(position)) << 1)))
#define IsVSplitter(params) ((BOOL)(((ULONG_PTR)(params)) & 0x1));
#define GetSplitterPosition(params) ((DWORD)(((ULONG_PTR)(params)) >> 1))


typedef struct SPLITTERCTX
{
    NMSPLITTER nm;
    BOOL isDragMode;
    int cursorOffset;
    int minSpace;
    HCURSOR sizeCursor;
} SPLITTERCTX;


static const TCHAR SPLITTERCLASSNAME[] = _T("Splitter");


static BOOL WINAPI SplitterIsOnDivider(SPLITTERCTX *ctx)
{
    RECT rect; // splitter divider rectangle
    POINT pt; // client point
	DWORD mp; // message point
    
	int pos = ctx->nm.position;

	if (pos < ctx->minSpace)
	{
		pos = ctx->minSpace;
	}
	else
	{
		int space = ((ctx->nm.hdr.code) ? ctx->nm.width : ctx->nm.height) - ctx->minSpace;

		if (pos > space)
		{
			pos = space;
		}
	}

	if (ctx->nm.hdr.code) // vertical
	{
		rect.left = pos - ctx->nm.barHalfThickness;
		rect.right = pos + ctx->nm.barHalfThickness;
        rect.top = 0;
        rect.bottom = ctx->nm.height;
	}
	else
	{
        rect.left = 0;
        rect.right = ctx->nm.width;
		rect.top = pos - ctx->nm.barHalfThickness;
		rect.bottom = pos + ctx->nm.barHalfThickness;
	}

    mp = GetMessagePos();

	pt.x = GET_X_LPARAM(mp);
    pt.y = GET_Y_LPARAM(mp);

	ScreenToClient(ctx->nm.hdr.hwndFrom, &pt);

	// return PtInRect(&rect, pt);
    return pt.x >= rect.left && pt.x < rect.right && pt.y >= rect.top && pt.y < rect.bottom;
}


/*static LRESULT CALLBACK SplitterWmPaint(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	 PAINTSTRUCT ps;
	HDC dc = BeginPaint(hWnd, &ps);

	if (dc && ps.fErase)
	{
		FillRect(dc, &ps.rcPaint, (HBRUSH)(COLOR_BTNFACE + 1));
	}

    EndPaint(hWnd, &ps);

    return 0;
} */

static BOOL WINAPI SplitterWmSetCursor(HWND hWnd)
{
    SPLITTERCTX* ctx = (SPLITTERCTX*)GetWindowLongPtr(hWnd, 0);
    BOOL isOnDivider = SplitterIsOnDivider(ctx);

    if (isOnDivider)
    {
        SetCursor(ctx->sizeCursor);
    }

    return isOnDivider;
}

static void WINAPI SplitterWmWindowPosChanged(HWND hWnd, const WINDOWPOS *lParam)
{
    NMSPLITTER nm;
    SPLITTERCTX* ctx = (SPLITTERCTX*)GetWindowLongPtr(hWnd, 0);

    ctx->nm.height = lParam->cy;
    ctx->nm.width = lParam->cx;

    Movs32(&nm, &ctx->nm, sizeof(nm) / sizeof(UINT32));

	if (nm.position < ctx->minSpace)
	{
		nm.position = ctx->minSpace;
	}
	else
	{
		int space = ((nm.hdr.code) ? nm.width : nm.height) - ctx->minSpace;

		if (nm.position > space)
		{
			nm.position = space;
		}
	}

	SendMessage(GetParent(nm.hdr.hwndFrom), WM_NOTIFY, nm.hdr.idFrom, (LPARAM)&nm);
}

static int WINAPI SplitterPositionFromLParam(const SPLITTERCTX *ctx, LPARAM lParam, int offset)
{
    int pos;
    int size;

    if (ctx->nm.hdr.code)
	{
        pos = GET_X_LPARAM(lParam);
        size = ctx->nm.width;
    }
    else
    {
        pos = GET_Y_LPARAM(lParam);
        size = ctx->nm.height;
    }

    pos += offset;

	if (pos < ctx->minSpace)
	{
		return ctx->minSpace;
	}
	else if (pos > size - ctx->minSpace)
	{
		return size - ctx->minSpace;
	}

    return pos;
}

static void WINAPI SplitterWmLButtonDown(HWND hWnd, LPARAM lParam)
{
    SPLITTERCTX* ctx = (SPLITTERCTX*)GetWindowLongPtr(hWnd, 0);

	if (SplitterIsOnDivider(ctx))
	{    
	    SetCapture(hWnd);

        ctx->isDragMode = TRUE;
        ctx->cursorOffset = ctx->nm.position - SplitterPositionFromLParam(ctx, lParam, 0);

        if (abs(ctx->cursorOffset) > ctx->nm.barHalfThickness)
        {
            ctx->nm.position = SplitterPositionFromLParam(ctx, 0, ctx->nm.position);
            ctx->cursorOffset = ctx->nm.position - (ctx->nm.hdr.code ? GET_X_LPARAM(lParam) : GET_Y_LPARAM(lParam)); 
        }
    }
}

static void WINAPI SplitterWmMouseMove(HWND hWnd, LPARAM lParam)
{
	SPLITTERCTX* ctx = (SPLITTERCTX*)GetWindowLongPtr(hWnd, 0);
    
    if (ctx->isDragMode)
	{
		int pos = SplitterPositionFromLParam(ctx, lParam, ctx->cursorOffset);

		if (pos != ctx->nm.position)
		{
            ctx->nm.position = pos;

            NMSPLITTER nm;

            Movs32(&nm, &ctx->nm, sizeof(nm) / sizeof(UINT32));

            SendMessage(GetParent(nm.hdr.hwndFrom), WM_NOTIFY, nm.hdr.idFrom, (LPARAM)&nm);
		}
	}
}

static void WINAPI SplitterWmLButtonUp(HWND hWnd)
{
    SPLITTERCTX* ctx = (SPLITTERCTX*)GetWindowLongPtr(hWnd, 0);

	if (ctx->isDragMode)
	{
		ReleaseCapture();
	}
}

static void WINAPI SplitterWmCaptureChanged(HWND hWnd)
{
    SPLITTERCTX* ctx = (SPLITTERCTX*)GetWindowLongPtr(hWnd, 0);

	if (ctx->isDragMode)
	{
        ctx->isDragMode = FALSE;
	}
}

static int SplitterDpiScale(BOOL isVSplitter, int value)
{
    return isVSplitter ? DpiScaleX(value) : DpiScaleY(value);
}

static LRESULT CALLBACK SplitterWmCreate(HWND hWnd, const CREATESTRUCT *lParam)
{
    SPLITTERCTX* __restrict ctx = (SPLITTERCTX*)AllocBlock(sizeof(SPLITTERCTX));

    if (ctx)
    {
        ctx->nm.hdr.hwndFrom = hWnd;
        ctx->nm.hdr.idFrom = (UINT_PTR)lParam->hMenu;
        ctx->nm.hdr.code = IsVSplitter(lParam->lpCreateParams);
        ctx->nm.barHalfThickness = SplitterDpiScale(ctx->nm.hdr.code, SPLITTER_LOGICAL_HALF_THICKNESS);
        ctx->nm.position = GetSplitterPosition(lParam->lpCreateParams);
        ctx->nm.height = lParam->cy;
        ctx->nm.width = lParam->cx;
        ctx->isDragMode = FALSE;
        ctx->cursorOffset = 0;
        ctx->minSpace = SplitterDpiScale(ctx->nm.hdr.code, SPLITTER_MIN_LOGICAL_SPACE);

        ctx->sizeCursor = LoadCursor(NULL, ctx->nm.hdr.code ? IDC_SIZEWE : IDC_SIZENS);

        if (ctx->sizeCursor)
        {
            SetWindowLongPtr(hWnd, 0, (LONG_PTR)ctx);

            return 0;
        }

        FreeBlock(ctx);
    }

    return -1; // failed window creation
}

static void WINAPI SplitterWmNcDestroy(HWND hWnd)
{
    SPLITTERCTX* ctx = (SPLITTERCTX*)SetWindowLongPtr(hWnd, 0, NULL);

    FreeBlock(ctx);
}

static LRESULT CALLBACK SplitterWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg)
	{
   //     case WM_CTLCOLOREDIT:
   //         SetBkColor((HDC)wParam, GetSysColor(COLOR_ACTIVECAPTION));
   //
    //        return (LRESULT)GetSysColorBrush(COLOR_ACTIVECAPTION);

        case WM_CTLCOLORSTATIC:
		case WM_NOTIFY:
		case WM_COMMAND:
		case WM_CONTEXTMENU:
            return SendMessage(GetParent(hWnd), msg, wParam, lParam);

		case WM_MOUSEMOVE:
			if (wParam & MK_LBUTTON)
			{
				SplitterWmMouseMove(hWnd, lParam);
			}

			return 0;
		case WM_SETCURSOR:
			if ((hWnd == (HWND)wParam) &&
                (LOWORD(lParam) == HTCLIENT) &&
                SplitterWmSetCursor(hWnd))
			{
				return TRUE;
			}

			break;
	//	case WM_PAINT:
	//		return SplitterWmPaint(hWnd, msg, wParam, lParam);

       /* case WM_ERASEBKGND:

            return TRUE;*/

      /*  case WM_PAINT:

            return 0; */
        // case WM_WINDOWPOSCHANGING:
		case WM_WINDOWPOSCHANGED:
			if (!(((PWINDOWPOS)lParam)->flags & SWP_NOSIZE)) // fire only on size change
			{
				 SplitterWmWindowPosChanged(hWnd, (PWINDOWPOS)lParam);
			}

			return 0;
		case WM_LBUTTONUP:
			SplitterWmLButtonUp(hWnd);

            return 0;
		case WM_LBUTTONDOWN:
			SplitterWmLButtonDown(hWnd, lParam);

            return 0;

    /*    case WM_CANCELMODE:
            break;
*/
        case WM_CAPTURECHANGED:
            SplitterWmCaptureChanged(hWnd);
    
            return 0;
		case WM_CREATE:
			return SplitterWmCreate(hWnd, (LPCREATESTRUCT)lParam);

        case WM_NCDESTROY:
            SplitterWmNcDestroy(hWnd);
            break;
	}

    return DefWindowProc(hWnd, msg, wParam, lParam);
}

ATOM WINAPI RegisterSplitterClass()
{
    WNDCLASSEX wc;
    ZeroMemory(&wc, sizeof(wc));

    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = SplitterWndProc;
    // cbClsExtra
    wc.cbWndExtra    = sizeof(SPLITTERCTX*);
    wc.hInstance    = GetCurrentModuleHandle();
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW); 
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = SPLITTERCLASSNAME;

    if (wc.hCursor)
    {
        return RegisterClassEx(&wc);
    }

    return 0;
}

HWND WINAPI CreateSplitterWindow(HWND hWndParent, DWORD wndId, BOOL isVSplitter, DWORD position)
{
    return CreateWindowEx(WS_EX_NOPARENTNOTIFY | WS_EX_CONTROLPARENT, SPLITTERCLASSNAME, NULL, /* WS_DISABLED | */ WS_VISIBLE | WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN, 0, 0, 0, 0, hWndParent, (HMENU)wndId, GetCurrentModuleHandle(), SplitterMakeParams(isVSplitter, position));
}