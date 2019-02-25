//
// This is free and unencumbered software released into the public domain.
//

//
// http://msdn.microsoft.com/en-us/library/ms681385(VS.85).aspx
// Header Versions: http://msdn.microsoft.com/en-us/library/aa383745.aspx
// Naming a file: http://msdn.microsoft.com/en-us/library/aa365247.aspx
//

#ifndef __APPBASE_3AB45C5EC1854C7FAD9455C0EB4CD959_H__
#define __APPBASE_3AB45C5EC1854C7FAD9455C0EB4CD959_H__

//
// C Preprocessor directives: http://en.wikipedia.org/wiki/C_preprocessor
// Predefined macros: http://msdn.microsoft.com/en-us/library/b0084kay(v=VS.100).aspx
//


#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOCOMM
#define NOCOMM
#endif

#ifndef NTDDI_VERSION
#define NTDDI_VERSION 0x06000000 // NTDDI_VISTA
#endif

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600 // _WIN32_WINNT_VISTA 
#endif

#ifndef _WIN32_WINDOWS
#define _WIN32_WINDOWS 0x0600
#endif


/*
** http://msdn.microsoft.com/en-us/library/windows/desktop/hh298349(v=vs.85).aspx
** http://msdn.microsoft.com/en-us/library/aa383745(v=vs.85).aspx
*/

#ifndef _WIN32_IE
#define _WIN32_IE 0x0600 // _WIN32_IE_IE60
#endif


// #define ISOLATION_AWARE_ENABLED 1

#include <intrin.h>
#include <windows.h>
#include <math.h>
#include <tchar.h>
#include "AppCrypto.h"


extern const TCHAR EMPTYSTRING[];



// #include <stdlib.h>
// #include <string.h>
// #include "stdio.h"

// Enabling Visual Styles: http://msdn.microsoft.com/en-us/library/bb773175%28VS.85%29.aspx

/*
#if defined _M_IX86
#pragma comment(linker,"/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='x86' publicKeyToken='6595b64144ccf1df' language='*'\"")
#elif defined _M_IA64
#pragma comment(linker,"/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='ia64' publicKeyToken='6595b64144ccf1df' language='*'\"")
#elif defined _M_X64
#pragma comment(linker,"/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='amd64' publicKeyToken='6595b64144ccf1df' language='*'\"")
#else */
/*#pragma comment(linker,"/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#endif
*/

// #pragma comment(linker,"/MERGE:.rdata=.text")

/*
#define __STR2__(x) #x
#define __STR1__(x) __STR2__(x)
#define __LOC__ __FILE__ "("__STR1__(__LINE__)") : Warning Msg: "
*/


#if _MSC_VER >= 1300

#pragma intrinsic( memset , memcpy, /* memmove ,*/ strlen, memcmp)
#pragma intrinsic(_rotr64, _byteswap_uint64, _byteswap_ulong, __stosd, __movsd, __stosb, __movsb, abs)

#ifndef RotateRight64
#define RotateRight64 _rotr64
#endif

#ifndef Swap64
#define Swap64 _byteswap_uint64
#endif

#ifndef Swap32
#define Swap32 _byteswap_ulong
#endif

#ifndef Stos32
#define Stos32(dest, data, count) __stosd(((unsigned long*)(dest)), ((unsigned long)(data)), (count))
#endif

#ifndef Movs32
#define Movs32(dest, source, count) __movsd(((unsigned long*)(dest)), ((const unsigned long*)(source)), (count))
#endif

#ifndef Stos8
#define Stos8(dest, data, count) __stosb(((unsigned char*)(dest)), ((unsigned char)(data)), (count))
#endif

#ifndef Movs8
#define Movs8(dest, source, count) __movsb(((unsigned char*)(dest)), ((const unsigned char*)(source)), (count))
#endif

#endif



#ifndef RotateRight64
#define RotateRight64(x, n) (((x) >> (n)) | ((x) << (64 - (n))))
#endif

#ifndef Swap64
#error Swap64 implementation is not found.
#endif

#ifndef Swap32
#error Swap32 implementation is not found.
#endif

#ifndef Stos32
#error Stos32 implementation is not found.
#endif

#ifndef Movs32
#error Movs32 implementation is not found.
#endif

#ifndef Stos8
#error Stos8 implementation is not found.
#endif

#ifndef Movs8
#error Movs8 implementation is not found.
#endif


/*
#ifndef ABS
#define ABS(x) ((x) > 0 ? (x) : -(x))
#endif
*/



// #if !__STDC__

// #endif


#ifdef __cplusplus
extern "C" {
#endif


HMODULE WINAPI GetCurrentModuleHandle();


ICryptoProv* WINAPI GetCryptoCtx();


__declspec(noalias) __declspec(restrict) LPWSTR WINAPI AllocString(SIZE_T count);
__declspec(noalias) __declspec(restrict) LPVOID WINAPI AllocBlock(SIZE_T dwBytes);
__declspec(noalias) __declspec(restrict) LPVOID WINAPI AllocClearBlock(SIZE_T dwBytes);
__declspec(noalias) BOOL WINAPI FreeBlock(LPVOID lpMem);

__declspec(noalias) __declspec(restrict) void* __cdecl app_realloc(void *memblock, size_t size);
__declspec(noalias) __declspec(restrict) void* __cdecl app_malloc(size_t size);
__declspec(noalias) void __cdecl app_free(void *memblock);


#define CACHE_LINE		64
#define CACHE_ALIGN		__declspec(align(CACHE_LINE))


#define AlignPtr(ptr, align)	((void*)((((ULONG_PTR)(ptr)) + ((ULONG_PTR)(align)) - 1) & (~(((ULONG_PTR)(align)) - 1))))
#define AlignSize(size, align)	((SIZE_T)AlignPtr(size, align))

#define BytesNotAligned(data, align)	(((ULONG_PTR)(data)) & (((ULONG_PTR)(align)) - 1))

// Gets number of array elements
#define COUNT_OF(x) ((sizeof(x) / sizeof(0[x])) / ((size_t)(!(sizeof(x) % sizeof(0[x])))))



UINT64 WINAPI GetDateTimeNow();
FILETIME WINAPI DateTimeToFileTime(UINT64 ticks);


//int AppMain();


#ifdef __cplusplus
}
#endif


#endif // __APPBASE_3AB45C5EC1854C7FAD9455C0EB4CD959_H__
