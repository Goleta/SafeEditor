//
// 2013 Jan 14
//
// This is free and unencumbered software released into the public domain.
//

#ifndef __DATASTORE_H__
#define __DATASTORE_H__

#include "AppBase.h"
#include "DataStoreShared.h"

#ifdef __cplusplus
extern "C" {
#endif


#define DBSCHEMAVERSION 1

#define MAX_RECORD_LEVEL_DEPTH 4 // how deep we can nest items
#define MAX_RECORD_COUNT 32000 // max number of records supported by DB or UI, don't add more than that!

#define RECORD_MAX_TITLE 127    // Max number of characters allowed in title excluding null terminator.
#define RECORD_MAX_NOTES 2047   // Max number of characters in notes excluding null terminator.

#define FILE_MAX_PASSWORD 127       // Max password length excluding null terminator.
#define FILE_MAX_TITLE 127          // Max file title length excluding null terminator.
#define FILE_MAX_DESCRIPTION 255    // Max file description excluding null terminator.

#define INVALID_RECORD_ID ((UINT32)-1)


#define DB_OPEN_READONLY    0x00000001
#define DB_OPEN_READWRITE   0x00000002
#define DB_OPEN_CREATE      0x00000004


//DECLARE_HANDLE(DBHANDLE);
typedef struct DBCONTEXT *DBHANDLE;


struct DBVERSIONINFO
{
    BOOL busy;
    UINT32 schemaVersion;
    BOOL isSupported;
    BOOL isUpgradeNeeded;
};

typedef struct DBVERSIONINFO DBVERSIONINFO;

struct RECORD
{
	UINT32 recordId;
	UINT32 parentRecordId;
	UINT64 dateCreated;
	UINT64 dateModified;
    UINT64 token;
	LPWSTR title;
	LPWSTR notes;
    BOOL readonly;
};

typedef struct RECORD RECORD;

struct CHILDRECORDID
{
    UINT32 recordId; // child id
    UINT32 parentRecordId;
};

typedef struct CHILDRECORDID CHILDRECORDID;

typedef void (CALLBACK* RecordCallback)(const RECORD *item);
typedef void (CALLBACK* ChildIdCallback)(const CHILDRECORDID *child);

#define DBSTATENONE     0x0
#define DBSTATEINIT     0x1
#define DBSTATEDIRTY    0x2
#define DBSTATESAVED    0x4
#define DBSTATECLOSED   0x8

typedef void (CALLBACK* DbChangedCallback)(void *userData, UINT32 state);


BOOL WINAPI DbAddNewItem(DBHANDLE db, RECORD* record);
BOOL WINAPI DbItemExists(DBHANDLE db, UINT32 recordId, BOOL *itemExists);
BOOL WINAPI DbUpdateItemNotes(DBHANDLE db, const RECORD *record);
BOOL WINAPI DbGetItemTitle(DBHANDLE db, UINT32 recordId, LPWSTR title, UINT32 titleMaxCount);
BOOL WINAPI DbItemHasChildren(DBHANDLE db, UINT32 recordId, BOOL *hasChildren);
BOOL WINAPI DbGetItemChildrenIds(DBHANDLE db, CHILDRECORDID *child, ChildIdCallback callback);
BOOL WINAPI DbGetVersionInfo(DBHANDLE db, DBVERSIONINFO *version);
BOOL WINAPI DbUpdateItemTitle(DBHANDLE db, RECORD* record);
BOOL WINAPI DbRemoveItem(DBHANDLE db, UINT32 recordId);
BOOL WINAPI DbGetItem(DBHANDLE db, RECORD *record, RecordCallback callback);
BOOL WINAPI DbUpgradeSchema(DBHANDLE db);
BOOL WINAPI DbIsReadOnlyItem(DBHANDLE db, UINT32 recordId, BOOL *isReadOnly);
BOOL WINAPI DbSetReadOnlyItem(DBHANDLE db, RECORD *record);
BOOL WINAPI DbGetFileDetails(DBHANDLE db, RECORD *record);
BOOL WINAPI DbUpdateFileDetails(DBHANDLE db, LPCWSTR title, LPCWSTR notes);
BOOL WINAPI DbGetRecordLevel(DBHANDLE db, UINT32 recordId, UINT32 *level);
BOOL WINAPI DbGetItemCount(DBHANDLE db, UINT32 *count);
BOOL WINAPI DbIsReadWrite(DBHANDLE db);

BOOL WINAPI DbKey(DBHANDLE db, const KEYINFO *keyInfo);
BOOL WINAPI DbRekey(DBHANDLE db, const KEYINFO *keyInfo);

DBHANDLE WINAPI DbOpen(LPCWSTR filePath, UINT32 flags);
BOOL WINAPI DbClose(DBHANDLE db, BOOL saveChanges);
BOOL WINAPI DbSave(DBHANDLE db);
BOOL WINAPI DbAcquireExclusiveAccess(DBHANDLE db);

BOOL WINAPI DbSetChangedCallback(DBHANDLE db, DbChangedCallback callback, void *userData);
BOOL WINAPI DbIsDirty(DBHANDLE db);


#ifdef __cplusplus
}
#endif

#endif // __DATASTORE_H__