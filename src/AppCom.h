//
// 2013 May 3
//
// This is free and unencumbered software released into the public domain.
//

#ifndef __APPCOM_2A9C25B1865444C9AB3E209A25B0964E_H__
#define __APPCOM_2A9C25B1865444C9AB3E209A25B0964E_H__

#define CINTERFACE 1 // use C style COM interfaces
#define CONST_VTABLE 1 // use const Vtbl

#include "AppBase.h"
#include <Ole2.h>

#ifdef __cplusplus
extern "C" {
#endif


HRESULT STDAPICALLTYPE RegisterDropWindow(HWND hWnd, IDropTarget **ppDropTarget);
HRESULT STDAPICALLTYPE UnregisterDropWindow(HWND hWnd, IDropTarget *pDropTarget);


#ifdef __cplusplus
}
#endif

#endif // __APPCOM_2A9C25B1865444C9AB3E209A25B0964E_H__