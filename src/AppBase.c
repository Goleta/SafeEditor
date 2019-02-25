//
// This is free and unencumbered software released into the public domain.
//

#include "AppBase.h"
#include "UICommon.h"
#include "SplitterWindow.h"
#include "MainWindow.h"
#include "Resources.h"
#include <Ole2.h>
#include <DelayImp.h>
#include "AppCrypto.h"
#include "LibraryLoader.h"

#define EXIT_CODE_OS_NOT_SUPPORTED 1
#define EXIT_CODE_OS_CRYPTO_NOT_SUPPORTED 2
#define EXIT_CODE_CONTROLS_INIT_FAILED 3
#define EXIT_CODE_FAILED -1

const TCHAR EMPTYSTRING[] = _T("");


HMODULE	__Module;	// Module handle of the calling process.
HANDLE	__Heap;		// Handle to the heap of the calling process.
PVOID __CryptoCtx;


__declspec(noalias) __declspec(restrict) LPWSTR WINAPI AllocString(SIZE_T count)
{
    return (LPWSTR)HeapAlloc(__Heap, 0, count * sizeof(WCHAR));
}

__declspec(noalias) __declspec(restrict) LPVOID WINAPI AllocBlock(SIZE_T dwBytes)
{
    return HeapAlloc(__Heap, 0, dwBytes);
}

__declspec(noalias) __declspec(restrict) LPVOID WINAPI AllocClearBlock(SIZE_T dwBytes)
{
    return HeapAlloc(__Heap, HEAP_ZERO_MEMORY, dwBytes);
}

__declspec(noalias) BOOL WINAPI FreeBlock(LPVOID lpMem)
{
    return lpMem ? HeapFree(__Heap, 0, lpMem) : TRUE;
}

__declspec(noalias) __declspec(restrict) void* __cdecl app_realloc(void *memblock, size_t size)
{
    if (!memblock)
    {
        return HeapAlloc(__Heap, 0, size);
    }
    else if (!size)
    {
        HeapFree(__Heap, 0, memblock);

        return NULL;
    }

    return HeapReAlloc(__Heap, 0, memblock, size);
}

__declspec(noalias) __declspec(restrict) void* __cdecl app_malloc(size_t size)
{
    return HeapAlloc(__Heap, 0, size);
}

__declspec(noalias) void __cdecl app_free(void *memblock)
{
    if (memblock)
    {
        HeapFree(__Heap, 0, memblock);
    }
}

int WINAPI GetString(UINT uID, LPCWSTR *str)
{
    return LoadStringW(__Module, uID, (LPWSTR)str, 0);
}

HMODULE WINAPI GetCurrentModuleHandle()
{
    return __Module;
}


struct MSGLOOP
{
    HWND hWnd;
    HACCEL hAccel;
    HWND hWndPrev;
    HACCEL hAccelPrev;
};

BOOL WINAPI SetMsgLoop(HMSGLOOP hMsgLoop, HWND hWnd, HACCEL hAccel)
{
    if (!hMsgLoop || !hWnd)
    {
        return FALSE;
    }

    hMsgLoop->hWndPrev = hMsgLoop->hWnd;
    hMsgLoop->hAccelPrev = hMsgLoop->hAccel;
    hMsgLoop->hWnd = hWnd;
    hMsgLoop->hAccel = hAccel;

    return TRUE;
}

BOOL WINAPI UpdateMsgLoopAccel(HMSGLOOP hMsgLoop, HWND hWnd, HACCEL hAccel)
{
    if (!hMsgLoop || !hWnd || (hMsgLoop->hWnd != hWnd))
    {
        return FALSE;
    }

    hMsgLoop->hAccel = hAccel;

    return TRUE;
}

BOOL WINAPI RestoreMsgLoop(HMSGLOOP hMsgLoop)
{
    if (!hMsgLoop || !hMsgLoop->hWndPrev)
    {
        return FALSE;
    }

    hMsgLoop->hWnd = hMsgLoop->hWndPrev;
    hMsgLoop->hAccel = hMsgLoop->hAccelPrev;
    hMsgLoop->hWndPrev = NULL;
    hMsgLoop->hAccelPrev = NULL;

    return TRUE;
}

static BOOL WINAPI IsOsSupported()
{
    // MAGOLE TODO: VersionHelpers.h -> VerifyVersionInfo 

    OSVERSIONINFO version;
    version.dwOSVersionInfoSize = sizeof(version);

    return GetVersionEx(&version) &&
        (version.dwPlatformId == VER_PLATFORM_WIN32_NT) &&
        (version.dwMajorVersion >= 5);
}

/*
static BOOL WINAPI IsOsSupported()
{
    OSVERSIONINFO ver;

    ver.dwOSVersionInfoSize = sizeof(ver);

    return GetVersionEx(&ver) &&
                              (_OsVersion.dwPlatformId == SUPPORTED_0S_PLATFORM) &&
                              ((_OsVersion.dwMajorVersion > SUPPORTED_OS_MAJOR) ||
                              ((_OsVersion.dwMajorVersion == SUPPORTED_OS_MAJOR) &&
                              (_OsVersion.dwMinorVersion >= SUPPORTED_0S_MINOR)));
}
*/

ICryptoProv* WINAPI GetCryptoCtx()
{
    return (ICryptoProv*)DecodePointer(__CryptoCtx);
}

static BOOL WINAPI CryptRelease(PVOID encCtx)
{
    ICryptoProv *ctx = (ICryptoProv*)DecodePointer(encCtx);

    if (ctx)
    {
        return ctx->lpVtbl->Release(ctx);
    }

    return TRUE;
}

static BOOL WINAPI CryptInit(PVOID *encCtx)
{
    ICryptoProv *ctx = InitCryptoProv(NULL);

    *encCtx = EncodePointer(ctx);
    
    return ctx != NULL;
}


static BOOL WINAPI InitWindowsControls()
{
    INITCOMMONCONTROLSEX commCtrl;
    ZeroMemory(&commCtrl, sizeof(commCtrl));

    commCtrl.dwSize = sizeof(INITCOMMONCONTROLSEX);
    commCtrl.dwICC = ICC_WIN95_CLASSES | ICC_STANDARD_CLASSES /* |  ICC_NATIVEFNTCTL_CLASS */; // http://msdn.microsoft.com/en-us/library/windows/desktop/bb775454(v=vs.85).aspx

    return InitCommonControlsEx(&commCtrl);
}

#define DEFAULT_SCREEN_DPI 96

typedef struct SCREENDPIINFO
{
    int x;
    int y;
} SCREENDPIINFO;

SCREENDPIINFO __dpi;


static BOOL WINAPI InitDpiInfo()
{
    HDC hdcScreen = GetDC(NULL);

    if (hdcScreen)
    {
        __dpi.x = GetDeviceCaps(hdcScreen, LOGPIXELSX);
        __dpi.y = GetDeviceCaps(hdcScreen, LOGPIXELSY);

        ReleaseDC(NULL, hdcScreen);

        return TRUE;
    }

    __dpi.x = DEFAULT_SCREEN_DPI;
    __dpi.y = DEFAULT_SCREEN_DPI;

    return FALSE;
}

static FORCEINLINE int WINAPI _MulDiv(int nNumber, int nNumerator, int nDenominator)
{
    return (int)(((LONG64)nNumber * nNumerator + (nDenominator >> 1)) / nDenominator);
}


int DpiScaleX(int value)
{
   return _MulDiv(value, __dpi.x, DEFAULT_SCREEN_DPI);
}

int DpiScaleY(int value)
{
   return _MulDiv(value, __dpi.y, DEFAULT_SCREEN_DPI);
}


// https://github.com/numpy/numpy/wiki/windows-dll-notes
// https://bugzilla.mozilla.org/attachment.cgi?id=8551924&action=diff

// http://www.flounder.com/loadlibrary_explorer.htm

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow)
{
	UNREFERENCED_PARAMETER(hPrevInstance);
	UNREFERENCED_PARAMETER(pCmdLine);
	UNREFERENCED_PARAMETER(nCmdShow);

    SetErrorMode(SEM_FAILCRITICALERRORS);

    RestrictLibrarySearchPaths();
    
    BOOL oleInit = FALSE;
    int exitCode = EXIT_CODE_FAILED;
    ATOM appClass = NULL;
    ATOM splitterClass = NULL;
    MSGLOOP loop;
    MSG msg;
    BOOL status;

    ZeroMemory(&loop, sizeof(MSGLOOP));

    __Module = hInstance;
    __Heap = GetProcessHeap();
    __CryptoCtx = EncodePointer(NULL);

    if (!IsOsSupported())
    {
        ResourceMessageBox(NULL, IDS_OS_NOT_SUPPORTED, IDS_APPNAME, MB_OK | MB_ICONERROR);

        exitCode = EXIT_CODE_OS_NOT_SUPPORTED;
        goto cleanup;
    }
    
    if (!CryptInit(&__CryptoCtx))
    {
        ResourceMessageBox(NULL, IDS_OS_CRYPTO_NOT_SUPPORTED, IDS_APPNAME, MB_OK | MB_ICONERROR);

        exitCode = EXIT_CODE_OS_CRYPTO_NOT_SUPPORTED;
        goto cleanup;
    }

    if (!InitWindowsControls())
    {
        ResourceMessageBox(NULL, IDS_CONTROLS_INIT_FAILED, IDS_APPNAME, MB_OK | MB_ICONERROR);

        exitCode = EXIT_CODE_CONTROLS_INIT_FAILED;
        goto cleanup;
    }

    InitDpiInfo();
    
    if (!(oleInit = SUCCEEDED(OleInitialize(NULL))))
    {
        ResourceMessageBox(NULL, IDS_OLE_INIT_FAILED, IDS_APPNAME, MB_OK | MB_ICONERROR);

        exitCode = EXIT_CODE_CONTROLS_INIT_FAILED;
        goto cleanup;
    }

    if (!(appClass = RegisterMainWindowClass()) || !(splitterClass = RegisterSplitterClass()))
    {
        ResourceMessageBox(NULL, IDS_REGISTER_WINDOW_CLASS_FAILED, IDS_APPNAME, MB_OK | MB_ICONERROR);

        exitCode = EXIT_CODE_CONTROLS_INIT_FAILED;
        goto cleanup;
    }
  
    if ((loop.hWnd = CreateMainWindow(&loop)))
    {
        ShowWindow(loop.hWnd, SW_SHOWDEFAULT);
        UpdateWindow(loop.hWnd);

        if (__argc == 2)
        {
            OpenDbFile(loop.hWnd, __wargv[1]);
        }

        while ((status = GetMessage(&msg, NULL, 0, 0)) != 0)
        {
            if (status == -1)
            {
                // loop failed
                goto cleanup;
            }

            if (!loop.hAccel || !TranslateAccelerator(loop.hWnd, loop.hAccel, &msg))
            {
                if (!IsDialogMessage(loop.hWnd, &msg))
                {
                    TranslateMessage(&msg);
                    DispatchMessage(&msg);
                }
            }
        }

        exitCode = (int)msg.wParam;
    }
    else
    {
        ResourceMessageBox(NULL, IDS_MAIN_WND_CREATE_FAILED, IDS_APPNAME, MB_OK | MB_ICONERROR);
    }
    
    cleanup:

    if (appClass)
    {
        UnregisterClass(MAKEINTATOM(appClass), __Module);
    }

    if (splitterClass)
    {
        UnregisterClass(MAKEINTATOM(splitterClass), __Module);
    }

    CryptRelease(__CryptoCtx);

    if (oleInit)
    {
        OleUninitialize();
    }

    return exitCode; // // ExitProcess((UINT)exitCode);
}



// Number of 100ns ticks per time unit
#define TICKS_PER_MILLISECOND 10000ULL
#define TICKS_PER_SECOND (TICKS_PER_MILLISECOND * 1000)
#define TICKS_PER_MINUTE (TICKS_PER_SECOND * 60)
#define TICKS_PER_HOUR (TICKS_PER_MINUTE * 60)
#define TICKS_PER_DAY (TICKS_PER_HOUR * 24)

// Number of days in a non-leap year
#define DAYS_PER_YEAR 365
// Number of days in 4 years
#define DAYS_PER_4_YEARS (DAYS_PER_YEAR * 4 + 1)
// Number of days in 100 years
#define DAYS_PER_100_YEARS (DAYS_PER_4_YEARS * 25 - 1)
// Number of days in 400 years
#define DAYS_PER_400_YEARS (DAYS_PER_100_YEARS * 4 + 1)

#define DAYS_TO_10000 (DAYS_PER_400_YEARS * 25 - 366)

// Number of days from 1/1/0001 to 12/31/1600
#define DAYS_TO_1601 (DAYS_PER_400_YEARS * 4)

#define DATETIME_MAX_TICKS (DAYS_TO_10000 * TICKS_PER_DAY - 1)
#define FILETIME_OFFSET (DAYS_TO_1601 * TICKS_PER_DAY)


union DATETIME
{
    UINT64 ticks;
    FILETIME ft;
};

typedef union DATETIME DATETIME;


UINT64 WINAPI GetDateTimeNow()
{
    DATETIME dt;

    GetSystemTimeAsFileTime(&dt.ft);

    return (dt.ticks + FILETIME_OFFSET);
}

FILETIME WINAPI DateTimeToFileTime(UINT64 ticks)
{
    DATETIME dt;

    dt.ticks = ticks - FILETIME_OFFSET;

    return dt.ft;
}