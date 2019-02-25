//
// This is free and unencumbered software released into the public domain.
//

#ifndef __SPLITTERCONTROL_H__
#define __SPLITTERCONTROL_H__

#include "AppBase.h"

#ifdef __cplusplus
extern "C" {
#endif


typedef struct NMSPLITTER
{
	NMHDR hdr;
    int barHalfThickness;
    int position;
    int height;
	int width;
} NMSPLITTER;


ATOM WINAPI RegisterSplitterClass();

HWND WINAPI CreateSplitterWindow(HWND hWndParent, DWORD wndId, BOOL isVSplitter, DWORD position);


#ifdef __cplusplus
}
#endif

#endif // __SPLITTERCONTROL_H__