//
// This is free and unencumbered software released into the public domain.
//

#include "LibraryLoader.h"
#include <tchar.h>


typedef BOOL(WINAPI* SetDefaultDllDirectories)(DWORD DirectoryFlags);
#define LOAD_LIBRARY_SEARCH_SYSTEM32        0x00000800


BOOL WINAPI RestrictLibrarySearchPaths()
{
    HMODULE hKernel = GetModuleHandle(_T("KERNEL32.dll"));

    decltype(SetDllDirectoryW)* setDllDirectory = (decltype(SetDllDirectoryW)*)GetProcAddress(hKernel, "SetDllDirectoryW");

    if (setDllDirectory)
    {
        if (!setDllDirectory(_T("")))
        {
            return FALSE;
        }
    }

	//
    // SetSearchPathMode: KB959426 on Windows XP with SP2 and later
    // and Windows Server 2003 with SP1 and later
    //
    decltype(SetSearchPathMode)* setSearchPathMode = (decltype(SetSearchPathMode)*)GetProcAddress(hKernel, "SetSearchPathMode");

    if (setSearchPathMode)
    {
        if (!setSearchPathMode(BASE_SEARCH_PATH_ENABLE_SAFE_SEARCHMODE | BASE_SEARCH_PATH_PERMANENT))
        {
            return FALSE;
        }
    }

	//
    // LOAD_LIBRARY_SEARCH_SYSTEM32 flag for LoadLibraryExW is only supported
    // if the DLL-preload fixes are installed.
    //
    // SetDefaultDllDirectories is available with KB2533623 on Vista/Win7 or Win8 and above.
    //
    SetDefaultDllDirectories setDefaultDllDirectories = (SetDefaultDllDirectories)GetProcAddress(hKernel, "SetDefaultDllDirectories");

    if (setDefaultDllDirectories)
    {
        if (!setDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32))
        {            
            return FALSE;
        }
    }
}

HMODULE WINAPI LoadSystemLibrary(LPCTSTR lpFileName)
{
    if (lpFileName == NULL)
    {
        return NULL;
    }

    int dllNameLen = lstrlen(lpFileName); // _tcslen

    if (dllNameLen == 0)
    {
        return NULL;
    }

    TCHAR sysDir[MAX_PATH + 1];

	//
    // The length, in TCHARs, of the string copied to the buffer,
    // not including the terminating null character.
    //
    UINT sysDirLen = GetSystemDirectory(sysDir, MAX_PATH);

    if (!sysDirLen || sysDirLen > MAX_PATH)
    {
        return FALSE;
    }

	//
    // The path does not end with a backslash unless
    // the system directory is the root directory.
    //
    if (sysDir[sysDirLen - 1] != _T('\\'))
    {
        sysDir[sysDirLen] = _T('\\');

        sysDirLen++;
    }

    size_t pathLen = sysDirLen + dllNameLen;

    if (pathLen > MAX_PATH)
    {
        return FALSE;
    }

    // _tcsncpy(sysDir + sysDirLen, lpFileName, dllNameLen);

    CopyMemory(sysDir + sysDirLen, lpFileName, dllNameLen * sizeof(TCHAR));


    sysDir[pathLen] = _T('\0');
	
    //
    // LOAD_WITH_ALTERED_SEARCH_PATH makes a DLL look
    // in its own directory for dependencies.
    //
    return LoadLibraryEx(sysDir, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
}