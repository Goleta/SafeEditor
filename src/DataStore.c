//
// 2013 Jan 14
//
// This is free and unencumbered software released into the public domain.
//

#include "AppBase.h"
#include "DataStore.h"
#include "sqlite3.h"
#include "AppDebug.h"


struct DBCONTEXT
{
    sqlite3 *sqliteObj;
    DbChangedCallback changed;
    void *userData;
    int lastSavedTotalChanges;
    BOOL isDirty;
    UINT32 lastState;
};

typedef struct DBCONTEXT DBCONTEXT;


static __forceinline BOOL WINAPI DbIsInTransaction(DBHANDLE db)
{
    return sqlite3_get_autocommit(db->sqliteObj) == 0;
}

static __forceinline void WINAPI FireCallback(DBHANDLE db, UINT32 state)
{
    if (db->changed)
    {
        db->changed(db->userData, state);
    }
}

static UINT32 WINAPI SetSavedState(DBHANDLE db)
{
    int totalChanges = sqlite3_total_changes(db->sqliteObj);

    if (db->isDirty || (db->lastSavedTotalChanges != totalChanges))
    {
        db->isDirty = FALSE;
        db->lastSavedTotalChanges = totalChanges;
        db->lastState = DBSTATESAVED;

        return DBSTATESAVED;
    }

    return DBSTATENONE;
}

BOOL WINAPI DbIsDirty(DBHANDLE db)
{
    if (!db)
    {
        return FALSE;
    }
    
    return db->isDirty || (db->lastSavedTotalChanges != sqlite3_total_changes(db->sqliteObj));
}

static void WINAPI OnFileChanged(DBHANDLE db)
{
    if ((db->lastState != DBSTATEDIRTY) && DbIsDirty(db))
    {
        db->lastState = DBSTATEDIRTY;

        FireCallback(db, DBSTATEDIRTY);
    }
}

static int WINAPI sqlite_exec_nonquery(sqlite3 *db, LPCWSTR sql)
{
	int err = 0;
	const void* sql_text = sql;
	sqlite3_stmt* prep_st = NULL;
	const void* st_tail = NULL;

	for(;;)
	{
		err = sqlite3_prepare16_v2(db, sql_text, -1, &prep_st, &st_tail);

		if (err)
		{
			return err;
		}

		if (prep_st)
		{
			err = sqlite3_step(prep_st);

			sqlite3_finalize(prep_st);

			if (err != SQLITE_DONE)
			{
				return err;
			}
		}

		if (!st_tail || !((WCHAR*)st_tail)[0])
		{
			break;
		}

		sql_text = st_tail;
	}

	return SQLITE_OK;
}

static int WINAPI DbBeginTransaction(sqlite3 *db)
{
    //  _T("PRAGMA locking_mode = EXCLUSIVE;") \
	
    WCHAR st_text[] = _T("BEGIN EXCLUSIVE TRANSACTION;");

    return sqlite_exec_nonquery(db, st_text);
}

static int WINAPI DbCommitTransaction(sqlite3 *db)
{
    WCHAR st_text[] = _T("COMMIT TRANSACTION;");

    return sqlite_exec_nonquery(db, st_text);
}

/*
static int WINAPI DbRollbackTransaction(sqlite3 *db)
{
    WCHAR st_text[] = _T("ROLLBACK TRANSACTION;");

    return sqlite_exec_nonquery(db, st_text);
}
*/

DBHANDLE WINAPI DbOpen(LPCWSTR filePath, UINT32 flags)
{
    if (!filePath ||
        !filePath[0] ||
        (flags & ~(DB_OPEN_READONLY | DB_OPEN_READWRITE | DB_OPEN_CREATE)) != 0 ||
        !flags)
    {
        return NULL;
    }

    DBHANDLE db = (DBHANDLE)AllocClearBlock(sizeof(DBCONTEXT));

    if (!db)
    {
        return NULL;
    }

    if (sqlite3_open16(filePath, &db->sqliteObj, flags) == SQLITE_OK)
    {
        sqlite3_busy_timeout(db->sqliteObj, 0); // sqlite3_busy_handler()

        return db;
    }

    if (db->sqliteObj)
    {
        sqlite3_close(db->sqliteObj);
    }

    FreeBlock(db);

    return NULL;
}

// Must be called only after DB key is set; otherwise, will fail.
// File must be read-write.
BOOL WINAPI DbAcquireExclusiveAccess(DBHANDLE db)
{
    if (!DbIsInTransaction(db))
    {
        PrintDebugMsg(_T("BEGIN EXCLUSIVE TRANSACTION"));

        int err = DbBeginTransaction(db->sqliteObj);

        if (err == SQLITE_OK)
        {
            return TRUE;
        }

#if PRINTDEBUG
        else
        {
            PrintDebugMsg((LPCTSTR)sqlite3_errmsg16(db->sqliteObj));
        }
#endif
    }

    return FALSE;
}

BOOL WINAPI DbClose(DBHANDLE db, BOOL saveChanges)
{
    if (!db)
    {
        return TRUE;
    }

    UINT32 state = DBSTATENONE;

    if (saveChanges && DbIsInTransaction(db) && DbIsDirty(db))
    {
        if (DbCommitTransaction(db->sqliteObj) == SQLITE_OK)
        {
            state = SetSavedState(db);
        }

#if PRINTDEBUG
        else
        {
            PrintDebugMsg((LPCTSTR)sqlite3_errmsg16(db->sqliteObj));
        }
#endif
    }

    /*
    // If an sqlite3 object is destroyed while a transaction is open,
    // the transaction is automatically rolled back.
    */
    if (sqlite3_close(db->sqliteObj) == SQLITE_OK)
    {
        db->sqliteObj = NULL;

        FireCallback(db, state | DBSTATECLOSED);
        FreeBlock(db);

        return TRUE;
    }
    else if (state)
    {
        FireCallback(db, state);
    }

    PrintDebugMsg(_T("sqlite3_close Failed!"));
    
    return FALSE;
}

BOOL WINAPI DbSave(DBHANDLE db)
{
    if (DbIsReadWrite(db))
    {
        if (DbIsInTransaction(db) && DbIsDirty(db))
        {
            PrintDebugMsg(_T("in trans"));

            if (DbCommitTransaction(db->sqliteObj) != SQLITE_OK)
            {
#if PRINTDEBUG
                PrintDebugMsg((LPCTSTR)sqlite3_errmsg16(db->sqliteObj));
#endif

                return FALSE;
            }

            UINT32 state = SetSavedState(db);

            if (state)
            {
                FireCallback(db, state);
            }
        }

        DbAcquireExclusiveAccess(db);
    }

    return TRUE;
}

BOOL WINAPI DbIsReadWrite(DBHANDLE db)
{
    return sqlite3_db_readonly(db->sqliteObj, NULL) == 0;
}

BOOL WINAPI DbKey(DBHANDLE db, const KEYINFO *keyInfo)
{
    if (!db || !keyInfo || !keyInfo->passLen || !keyInfo->pass)
    {
        return FALSE;
    }

    return sqlite3_key(db->sqliteObj, keyInfo, 0) == SQLITE_OK;
}

// this method commits any already running transaction
BOOL WINAPI DbRekey(DBHANDLE db, const KEYINFO *keyInfo)
{
    if (!db || !keyInfo || !keyInfo->passLen || !keyInfo->pass)
    {
        return FALSE;
    }

    BOOL result = sqlite3_rekey(db->sqliteObj, keyInfo, 0) == SQLITE_OK;

   /* if (result)
    {
        db->isDirty = TRUE;
        OnFileChanged(db);
    } */

    return result;
}

BOOL WINAPI DbSetChangedCallback(DBHANDLE db, DbChangedCallback callback, void *userData)
{
    db->changed = callback;
    db->userData = userData;

    FireCallback(db, db->lastState | DBSTATEINIT);

    return TRUE;
}

BOOL WINAPI DbAddNewItem(DBHANDLE db, RECORD* record)
{
    // TODO: add nesting level limit using DbGetRecordLevel

    UINT64 token;

	const void* sql_text = _T("INSERT INTO Records(ParentRecordId, Title, DateCreated, DateModified, Token) VALUES(?1, ?2, ?3, ?3, ?4);");
	sqlite3_stmt* stmt = NULL;
	const void* sql_tail = NULL;

	int err = sqlite3_prepare16_v2(db->sqliteObj, sql_text, -1, &stmt, &sql_tail);

	if (!err && stmt)
	{
        record->dateCreated = record->dateModified = GetDateTimeNow();

        ICryptoProv* prov = GetCryptoCtx();
        CryptoProvRandom(prov, &token, sizeof(UINT64));
        
		sqlite3_bind_int64(stmt, 1, record->parentRecordId);
		sqlite3_bind_text16(stmt, 2, (const void*)record->title, -1, SQLITE_STATIC);
		sqlite3_bind_int64(stmt, 3, record->dateCreated);
        sqlite3_bind_int64(stmt, 4, token);

		err = sqlite3_step(stmt);

		if (err == SQLITE_DONE)
		{
			// MAGOLE BUG: possible overflow
			record->recordId = (UINT32)sqlite3_last_insert_rowid(db->sqliteObj);
		}
	}

    sqlite3_finalize(stmt);

    OnFileChanged(db);

	return err == SQLITE_DONE;
}

BOOL WINAPI DbItemExists(DBHANDLE db, UINT32 recordId, BOOL *itemExists)
{
	const void* sql_text = _T("SELECT EXISTS (SELECT NULL FROM Records WHERE RecordId=?1);");

    *itemExists = FALSE;
	sqlite3_stmt* stmt = NULL;
	const void* sql_tail = NULL;

	int err = sqlite3_prepare16_v2(db->sqliteObj, sql_text, -1, &stmt, &sql_tail);

	if (!err && stmt)
	{
		sqlite3_bind_int64(stmt, 1, recordId);

		err = sqlite3_step(stmt);

		*itemExists = (err == SQLITE_ROW) &&
                        (sqlite3_column_count(stmt) == 1) &&
                        (sqlite3_column_int(stmt, 0) == 1);
	}

    sqlite3_finalize(stmt);

	return err == SQLITE_ROW;
}

BOOL WINAPI DbUpdateItemNotes(DBHANDLE db, const RECORD *record)
{
	const void* sql_text = _T("UPDATE Records SET Notes=?2, DateModified=?3 WHERE RecordId=?1 AND Notes COLLATE BINARY != IFNULL(?2,'');");

	sqlite3_stmt* stmt = NULL;
	const void* sql_tail = NULL;

	int err = sqlite3_prepare16_v2(db->sqliteObj, sql_text, -1, &stmt, &sql_tail);

	if (!err && stmt)
	{
		sqlite3_bind_int64(stmt, 1, record->recordId);
		sqlite3_bind_text16(stmt, 2, (const void*)record->notes, -1, SQLITE_STATIC);
		sqlite3_bind_int64(stmt, 3, record->dateModified);

		err = sqlite3_step(stmt);
	}

    sqlite3_finalize(stmt);

    OnFileChanged(db);

	return err == SQLITE_DONE;
}

/*
// titleMaxCount - max number of chars to copy to "title" param including NULL terminator
*/
BOOL WINAPI DbGetItemTitle(DBHANDLE db, UINT32 recordId, LPWSTR title, UINT32 titleMaxCount)
{
	int err;
	const void* sql_text = _T("SELECT Title FROM Records WHERE RecordId=?1;");

	sqlite3_stmt* stmt = NULL;
	const void* sql_tail = NULL;

    title[0] = NULL;

	err = sqlite3_prepare16_v2(db->sqliteObj, sql_text, -1, &stmt, &sql_tail);

	if (!err && stmt)
	{
		sqlite3_bind_int64(stmt, 1, recordId);

		err = sqlite3_step(stmt);

		if (err == SQLITE_ROW)
		{
            if (sqlite3_column_count(stmt) == 1)
            {
			    LPWSTR dbTitle = (LPWSTR)sqlite3_column_text16(stmt, 0);
			    UINT32 dbTitleCount = sqlite3_column_bytes16(stmt, 0) / sizeof(WCHAR); // must be called right after sqlite3_column_text16

			    size_t titleCount = min(dbTitleCount /*_tcslen(title)*/, titleMaxCount - 1); // not including null terminator

			    memcpy(title, dbTitle, titleCount * sizeof(WCHAR));
			    title[titleCount] = 0; // null terminate the string

			    // todo: remove
		        //	swprintf(dispInfo->item.pszText, _T("%i - %s"), dispInfo->item.lParam, title);

                err = SQLITE_OK;
            }
		}
	}

    sqlite3_finalize(stmt);

	return err == SQLITE_OK;
}

BOOL WINAPI DbItemHasChildren(DBHANDLE db, UINT32 recordId, BOOL *hasChildren)
{
	const void* sql_text = _T("SELECT EXISTS (SELECT NULL FROM Records WHERE ParentRecordId=?1 LIMIT 1);");

	sqlite3_stmt* stmt = NULL;
	const void* sql_tail = NULL;
    *hasChildren = FALSE;

	int err = sqlite3_prepare16_v2(db->sqliteObj, sql_text, -1, &stmt, &sql_tail);

	if (!err && stmt)
	{
		sqlite3_bind_int64(stmt, 1, recordId);

		err = sqlite3_step(stmt);

		*hasChildren = (err == SQLITE_ROW) &&
                        (sqlite3_column_count(stmt) == 1) &&
                        (sqlite3_column_int(stmt, 0) == 1);
	}

    sqlite3_finalize(stmt);

	return err == SQLITE_ROW;
}

BOOL WINAPI DbGetItemChildrenIds(DBHANDLE db, CHILDRECORDID *child, ChildIdCallback callback)
{
	const void* sql_text = _T("SELECT RecordId FROM Records WHERE ParentRecordId=?1;");

	sqlite3_stmt* stmt = NULL;
	const void* sql_tail = NULL;

	int err = sqlite3_prepare16_v2(db->sqliteObj, sql_text, -1, &stmt, &sql_tail);

	if (!err && stmt)
	{
		sqlite3_bind_int64(stmt, 1, child->parentRecordId);

		while((err = sqlite3_step(stmt)) == SQLITE_ROW)
		{
			child->recordId = (UINT32)sqlite3_column_int64(stmt, 0);

			callback(child);
		}
	}

    sqlite3_finalize(stmt);

	return err == SQLITE_DONE || err == SQLITE_OK;
}

BOOL WINAPI DbGetItemCount(DBHANDLE db, UINT32 *count)
{
	*count = 0;
	const void* sql_text = _T("SELECT COUNT(1) FROM Records WHERE RecordId>0;");

	sqlite3_stmt* stmt = NULL;
	const void* sql_tail = NULL;

	int err = sqlite3_prepare16_v2(db->sqliteObj, sql_text, -1, &stmt, &sql_tail);

	if (!err && stmt)
	{
		if ((err = sqlite3_step(stmt)) == SQLITE_ROW)
		{
			*count = (UINT32)sqlite3_column_int64(stmt, 0);
		}
	}

    sqlite3_finalize(stmt);

	return err == SQLITE_ROW;
}

BOOL WINAPI DbGetRecordLevel(DBHANDLE db, UINT32 recordId, UINT32 *level)
{
	*level = 0;    
	const void* sql_text = _T("SELECT ParentRecordId FROM Records WHERE RecordId=?1;");

	sqlite3_stmt* stmt = NULL;
	const void* sql_tail = NULL;

	int err = sqlite3_prepare16_v2(db->sqliteObj, sql_text, -1, &stmt, &sql_tail);

	if (!err && stmt)
	{
		sqlite3_bind_int64(stmt, 1, recordId);

		while((err = sqlite3_step(stmt)) == SQLITE_ROW)
		{
			sqlite3_int64 parentId = sqlite3_column_int64(stmt, 0);
            
            if (parentId >= 0)
            {
                (*level)++;
            }

			if (parentId <= 0 || sqlite3_reset(stmt) != SQLITE_OK)
            {
                break;
            }
            
            sqlite3_bind_int64(stmt, 1, parentId);
		}
	}

    sqlite3_finalize(stmt);

	return err == SQLITE_DONE || err == SQLITE_ROW || err == SQLITE_OK;
}

 /*
static BOOL WINAPI DbIsValid(sqlite3* db)
{
	const void* sql_text = _T("SELECT COUNT(1) FROM sqlite_master;");

	sqlite3_stmt* stmt = NULL;
	const void* sql_tail = NULL;

	int err = sqlite3_prepare16_v2(db, sql_text, -1, &stmt, &sql_tail);

	if (!err && stmt)
	{
		err = sqlite3_step(stmt);

		BOOL valid = ((err == SQLITE_ROW) && (sqlite3_column_count(stmt) == 1) && (sqlite3_column_int(stmt, 0) >= 0));

		sqlite3_finalize(stmt);

		return valid;
	}

	return FALSE;
}
*/

BOOL WINAPI DbGetVersionInfo(DBHANDLE db, DBVERSIONINFO *version)
{
	ZeroMemory(version, sizeof(DBVERSIONINFO));

	const void* sql_text = _T("PRAGMA user_version;");
    BOOL result = FALSE;
	sqlite3_stmt* stmt = NULL;
	const void* sql_tail = NULL;

	int err = sqlite3_prepare16_v2(db->sqliteObj, sql_text, -1, &stmt, &sql_tail);

	if (!err && stmt)
	{
		err = sqlite3_step(stmt);
        version->busy = err == SQLITE_BUSY;
        
        if ((err == SQLITE_ROW) && (sqlite3_column_count(stmt) == 1))
        {
            result = TRUE;
		    version->schemaVersion = (UINT32)sqlite3_column_int(stmt, 0);

            // TODO: lookup table for supported schema versions and upgrade caps
            version->isSupported = version->schemaVersion >= 0 && version->schemaVersion <= DBSCHEMAVERSION;
            version->isUpgradeNeeded = version->schemaVersion < DBSCHEMAVERSION;
        }
	}
    
    sqlite3_finalize(stmt);

	return result;
}

static int WINAPI DbSetVersion(sqlite3* db, UINT32 schemaVersion)
{
    TCHAR sql_text[64];
    int err = SQLITE_ERROR;

    if (_sntprintf_s(sql_text,
            sizeof(sql_text) / sizeof(sql_text[0]),
            sizeof(sql_text) / sizeof(sql_text[0]),
            _T("PRAGMA user_version=%i;"),
            schemaVersion) != -1)
    {
	    sqlite3_stmt* stmt = NULL;
	    const void* sql_tail = NULL;

	    err = sqlite3_prepare16_v2(db, sql_text, -1, &stmt, &sql_tail);

	    if (!err && stmt)
	    {
		    err = sqlite3_step(stmt);

            if (err == SQLITE_DONE)
            {
                err = SQLITE_OK;
            }		    
	    }

        sqlite3_finalize(stmt);
    }

	return err;
}

BOOL WINAPI DbUpdateItemTitle(DBHANDLE db, RECORD* record)
{
	int err;
	const void* sql_text = _T("UPDATE Records SET Title=?2, DateModified=?3 WHERE RecordId=?1 AND Title COLLATE BINARY != ?2;");

	sqlite3_stmt* stmt = NULL;
	const void* sql_tail = NULL;
    BOOL updated = FALSE;

	err = sqlite3_prepare16_v2(db->sqliteObj, sql_text, -1, &stmt, &sql_tail);
    
	if (!err && stmt)
	{
		sqlite3_bind_int64(stmt, 1, record->recordId);
		sqlite3_bind_text16(stmt, 2, (const void*)record->title, -1, SQLITE_STATIC);

        record->dateModified = GetDateTimeNow();
		sqlite3_bind_int64(stmt, 3, record->dateModified);

		err = sqlite3_step(stmt);

        updated = (err == SQLITE_DONE) && sqlite3_changes(db->sqliteObj) != 0;
	}

    sqlite3_finalize(stmt);

    OnFileChanged(db);

	return updated;
}

BOOL WINAPI DbUpdateFileDetails(DBHANDLE db, LPCWSTR title, LPCWSTR notes)
{
	int err;
	const void* sql_text = _T("UPDATE Records SET Title=IFNULL(?1,Title), Notes=IFNULL(?2,Notes), DateModified=?3 WHERE RecordId=0 AND ((Title COLLATE BINARY != IFNULL(?1,'')) OR (Notes COLLATE BINARY != IFNULL(?2,'')));");

	sqlite3_stmt* stmt = NULL;
	const void* sql_tail = NULL;

	err = sqlite3_prepare16_v2(db->sqliteObj, sql_text, -1, &stmt, &sql_tail);
    
	if (!err && stmt)
	{
		sqlite3_bind_text16(stmt, 1, (const void*)title, -1, SQLITE_STATIC);
		sqlite3_bind_text16(stmt, 2, (const void*)notes, -1, SQLITE_STATIC);
		sqlite3_bind_int64(stmt, 3, GetDateTimeNow());

		err = sqlite3_step(stmt);
	}

    sqlite3_finalize(stmt);

    OnFileChanged(db);

	return err == SQLITE_DONE;
}

BOOL WINAPI DbGetFileDetails(DBHANDLE db, RECORD *record)
{
	int err;
    const void* sql_text = _T("SELECT Title, Notes, DateCreated, DateModified FROM Records WHERE RecordId=0;");

	sqlite3_stmt* stmt = NULL;
	const void* sql_tail = NULL;
    
	err = sqlite3_prepare16_v2(db->sqliteObj, sql_text, -1, &stmt, &sql_tail);

	if (!err && stmt)
	{
		err = sqlite3_step(stmt);

		if (err == SQLITE_ROW)
		{
            if (sqlite3_column_count(stmt) == 4) // check number of columns
            {
                LPWSTR title = (LPWSTR)sqlite3_column_text16(stmt, 0);
                UINT32 titleCount = min(sqlite3_column_bytes16(stmt, 0) / sizeof(WCHAR), FILE_MAX_TITLE);

                memcpy(record->title, title, titleCount * sizeof(WCHAR));
			    record->title[titleCount] = 0; // null terminate the string

                LPWSTR notes = (LPWSTR)sqlite3_column_text16(stmt, 1);
                UINT32 notesCount = min(sqlite3_column_bytes16(stmt,1) / sizeof(WCHAR), FILE_MAX_DESCRIPTION);

                memcpy(record->notes, notes, notesCount * sizeof(WCHAR));
			    record->notes[notesCount] = 0; // null terminate the string

                record->dateCreated = sqlite3_column_int64(stmt, 2);
                record->dateModified = sqlite3_column_int64(stmt, 3);

                err = SQLITE_OK;
            }
		}
	}

    sqlite3_finalize(stmt);

    return err == SQLITE_OK;
}

BOOL WINAPI DbRemoveItem(DBHANDLE db, UINT32 recordId)
{
	int err;
    const void* sql_text = _T("DELETE FROM Records WHERE RecordId=?1;");

	sqlite3_stmt* stmt = NULL;
	const void* sql_tail = NULL;

	err = sqlite3_prepare16_v2(db->sqliteObj, sql_text, -1, &stmt, &sql_tail);

	if (!err && stmt)
	{
		sqlite3_bind_int64(stmt, 1, recordId);

		err = sqlite3_step(stmt);
    }

    sqlite3_finalize(stmt);

    OnFileChanged(db);

    return err == SQLITE_DONE;
}

BOOL WINAPI DbGetItem(DBHANDLE db, RECORD *record, RecordCallback callback)
{
	int err;
    const void* sql_text = _T("SELECT Title, Notes, DateCreated, DateModified, IsReadOnly FROM Records WHERE RecordId=?1;");

	sqlite3_stmt* stmt = NULL;
	const void* sql_tail = NULL;

	err = sqlite3_prepare16_v2(db->sqliteObj, sql_text, -1, &stmt, &sql_tail);

	if (!err && stmt)
	{
		sqlite3_bind_int64(stmt, 1, record->recordId);

		err = sqlite3_step(stmt);

		if (err == SQLITE_ROW)
		{
            if (sqlite3_column_count(stmt) == 5) // check number of columns
            {
                record->title = (LPWSTR)sqlite3_column_text16(stmt, 0);
                record->notes = (LPWSTR)sqlite3_column_text16(stmt, 1);

                record->dateCreated = sqlite3_column_int64(stmt, 2);
                record->dateModified = sqlite3_column_int64(stmt, 3);
                record->readonly = sqlite3_column_int(stmt, 4) != FALSE;

                callback(record);

                err = SQLITE_OK;
            }
		}
	}

    sqlite3_finalize(stmt);

    return err == SQLITE_OK;
}

BOOL WINAPI DbIsReadOnlyItem(DBHANDLE db, UINT32 recordId, BOOL *isReadOnly)
{
	int err;
	const void* sql_text = _T("SELECT IsReadOnly FROM Records WHERE RecordId=?1;");

	sqlite3_stmt* stmt = NULL;
	const void* sql_tail = NULL;
    *isReadOnly = TRUE;

	err = sqlite3_prepare16_v2(db->sqliteObj, sql_text, -1, &stmt, &sql_tail);

	if (!err && stmt)
	{
		sqlite3_bind_int64(stmt, 1, recordId);

		err = sqlite3_step(stmt);

        if (err == SQLITE_ROW)
        {
            if (sqlite3_column_count(stmt) == 1)
            {
		        *isReadOnly = sqlite3_column_int(stmt, 0) != FALSE;

                err = SQLITE_OK;
            }
        }
	}

    sqlite3_finalize(stmt);

	return err == SQLITE_OK;
}

BOOL WINAPI DbSetReadOnlyItem(DBHANDLE db, RECORD *record)
{
	int err;
	const void* sql_text = _T("UPDATE Records SET IsReadOnly=?2, DateModified=?3 WHERE RecordId=?1;");

	sqlite3_stmt* stmt = NULL;
	const void* sql_tail = NULL;

	err = sqlite3_prepare16_v2(db->sqliteObj, sql_text, -1, &stmt, &sql_tail);
    
	if (!err && stmt)
	{
		sqlite3_bind_int64(stmt, 1, record->recordId);
        sqlite3_bind_int(stmt, 2, record->readonly != FALSE);

        record->dateModified = GetDateTimeNow();
		sqlite3_bind_int64(stmt, 3, record->dateModified);

		err = sqlite3_step(stmt);
	}

    sqlite3_finalize(stmt);

    OnFileChanged(db);

	return err == SQLITE_DONE;
}

static int WINAPI DbInsertRootRecord(sqlite3* db, INT64 recordId, INT64 parentRecordId, UINT64 datetime, UINT64 token)
{
    int err;
	const void* sql_text = _T("INSERT INTO Records (RecordId, ParentRecordId, DateCreated, DateModified, Token) VALUES (?1, ?2, ?3, ?3, ?4);");

	sqlite3_stmt* stmt = NULL;
	const void* sql_tail = NULL;

	err = sqlite3_prepare16_v2(db, sql_text, -1, &stmt, &sql_tail);
    
	if (!err && stmt)
	{
		sqlite3_bind_int64(stmt, 1, recordId);
        sqlite3_bind_int64(stmt, 2, parentRecordId);
        sqlite3_bind_int64(stmt, 3, datetime);
        sqlite3_bind_int64(stmt, 4, token);

		err = sqlite3_step(stmt);

		sqlite3_finalize(stmt);

		if (err == SQLITE_DONE)
		{
            if (sqlite3_changes(db) != 0)
            {
			    return SQLITE_OK;
            }
		}
	}

	return err;
}

static int WINAPI DbCreateSchema(sqlite3 *db)
{
    int err;

    /*
    // error codes
    // http://www.sqlite.org/c3ref/c_abort.html
    */

    WCHAR st_text[] = 
    _T("CREATE TABLE Records(RecordId INTEGER PRIMARY KEY AUTOINCREMENT, ParentRecordId INTEGER NOT NULL REFERENCES Records(RecordId) ON DELETE CASCADE, ") \
	_T("DateCreated INTEGER NOT NULL, DateModified INTEGER NOT NULL, ") \
    _T("Token INTEGER NOT NULL, IsReadOnly INTEGER NOT NULL DEFAULT 0, ") \
    _T("Title TEXT NOT NULL DEFAULT '', Notes TEXT NOT NULL DEFAULT '');") \
	_T("CREATE INDEX ParentRecordId_IDX on Records(ParentRecordId);"); // \
  //  _T("INSERT INTO Records(RecordId, ParentRecordId, DateCreated, DateModified, Token) VALUES(-1, -1, 0, 0, -1);") \
//	_T("INSERT INTO Records(RecordId, ParentRecordId, DateCreated, DateModified, Token) VALUES(0, -1, 0, 0, -1);");

    err = sqlite_exec_nonquery(db, st_text);

    if (err == SQLITE_OK)
    {
        UINT64 datetime = GetDateTimeNow();
        UINT64 token;

        ICryptoProv* prov = GetCryptoCtx();

        CryptoProvRandom(prov, &token, sizeof(UINT64));
        
        err = DbInsertRootRecord(db, (INT64)-1, (INT64)-1, datetime, token);

        if (err == SQLITE_OK)
        {
            CryptoProvRandom(prov, &token, sizeof(UINT64));

            return DbInsertRootRecord(db, 0, (INT64)-1, datetime, token);
        }
    }

    return err;
}

BOOL WINAPI DbUpgradeSchema(DBHANDLE db)
{
    DBVERSIONINFO info;
    int err = SQLITE_ERROR;
    
    if (DbGetVersionInfo(db, &info))
    {
        if (info.schemaVersion == 0)
        {
            if ((err = sqlite_exec_nonquery(db->sqliteObj, _T("SAVEPOINT InitDB;"))) == SQLITE_OK)
            {            
                if (((err = DbCreateSchema(db->sqliteObj)) == SQLITE_OK) &&
                    ((err = DbSetVersion(db->sqliteObj, DBSCHEMAVERSION)) == SQLITE_OK))
                {
                    if ((err = sqlite_exec_nonquery(db->sqliteObj, _T("RELEASE SAVEPOINT InitDB;"))) == SQLITE_OK)
                    {
                        db->isDirty = TRUE;
                        OnFileChanged(db);
                    }
                }
                else
                {
                    sqlite_exec_nonquery(db->sqliteObj, _T("ROLLBACK TO SAVEPOINT InitDB;"));
                }
            }
        }
    }

    return (err == SQLITE_OK);
}




#if SHOWDEBUGMSG
static void ShowLastError()
{
   LPTSTR message;

   if (FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, GetLastError(), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPTSTR)&message, 0, NULL))
   {
      MessageBox(NULL, message, _T("app"), MB_OK);
      LocalFree(message);
   }
}
#else
#define ShowLastError()
#endif

#if SHOWDEBUGMSG
static void show_sqlite_error(DBHANDLE db, LPCTSTR extra)
{
	const void* errmsg = sqlite3_errmsg16((sqlite3*)db);

	TCHAR str[1000];
	_sntprintf_s(str, sizeof(str) / sizeof(str[0]), sizeof(str) / sizeof(str[0]), _T("code: %i, msg: %s"), sqlite3_errcode((sqlite3*)db), errmsg);

	MessageBox(NULL, str, extra, MB_OK);
}
#else
#define show_sqlite_error(db, extra)
#endif

#if SHOWDEBUGMSG
static void show_sqlite_error2(int err, LPCTSTR extra)
{
	TCHAR str[1000];
	_sntprintf_s(str, sizeof(str) / sizeof(str[0]), sizeof(str) / sizeof(str[0]), _T("code: %i"), err);

	MessageBox(NULL, str, extra, MB_OK);

    // OutputDebugString(str);
}
#else
#define show_sqlite_error2(err, extra)
#endif