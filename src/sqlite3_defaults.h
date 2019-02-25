//
// This is free and unencumbered software released into the public domain.
//

// Streaming blob data http://www.sqlite.org/capi3ref.html#sqlite3_blob_open

#ifndef __SQLITE3_DEFAULTS_H__
#define __SQLITE3_DEFAULTS_H__

#include "AppBase.h"
#include "DataStoreShared.h"
#include "AppDebug.h"

#define MY_SQLITE_OMIT_ALL_DATETIME_FUNCS 1
#define MY_SQLITE_FORCE_WIN_NT 1

/* my extensions */

// #define MY_SQLITE_SIMULATE_ENCRYPTION // when defined, page encryption is not performed

#define MY_SQLITE_RESERVED_IV 16
#define MY_SQLITE_RESERVED_HMAC 32
#define MY_SQLITE_RESERVED_IV_HMAC ((MY_SQLITE_RESERVED_IV) + (MY_SQLITE_RESERVED_HMAC)) // IV + HMAC

#define MY_SQLITE_PBKDF2_DEFAULT_ROUNDS 100000
#define MY_SQLITE_PBKDF2_DEFAULT_ROUNDS_DRIFT 10000

#define MY_SQLITE_HEADER_RESERVED_OFFSET 76


// SEHEADER struct size must be a multiple of 16
// because AES256 block size is 16 bytes
// and all crypto operations are done on whole blocks.
__declspec(align(4)) struct SEHEADER
{
    UINT8 salt[28];
    UINT8 iter[4]; // KDF iterations counter
};

typedef struct SEHEADER SEHEADER;

/* end of my extensions */



// http://www.sqlite.org/compile.html

#define SQLITE_POWERSAFE_OVERWRITE 0

#define SQLITE_DEFAULT_PAGE_SIZE 4096

#define SQLITE_SECURE_DELETE   

#define SQLITE_THREADSAFE 0

#define SQLITE_DEFAULT_FILE_FORMAT 4

#define SQLITE_DEFAULT_FOREIGN_KEYS 1 // Needed to recursively remove record children
// SQLITE_DEFAULT_RECURSIVE_TRIGGERS

/* 3 = in memory */
#define SQLITE_TEMP_STORE 3

/* enable encryption codec */
#define SQLITE_HAS_CODEC

/*
If set to 1, then the default locking_mode is set to EXCLUSIVE.
If omitted or set to 0 then the default locking_mode is NORMAL.
*/
#define SQLITE_DEFAULT_LOCKING_MODE 1

// http://www.sqlite.org/c3ref/c_config_getmalloc.html#sqliteconfigmemstatus
#define SQLITE_DEFAULT_MEMSTATUS 0

// #define SQLITE_WIN32_MALLOC


#define SQLITE_MUTEX_NOOP


#define SQLITE_OMIT_AUTOVACUUM
#define SQLITE_OMIT_LOAD_EXTENSION
#define SQLITE_OMIT_AUTHORIZATION
#define SQLITE_OMIT_BUILTIN_TEST
#define SQLITE_OMIT_COMPLETE
#define SQLITE_OMIT_DATETIME_FUNCS
#define SQLITE_OMIT_DEPRECATED
#define SQLITE_OMIT_GET_TABLE
#define SQLITE_OMIT_LOCALTIME
#define SQLITE_OMIT_EXPLAIN
#define SQLITE_OMIT_SHARED_CACHE

// #define SQLITE_OMIT_FLOATING_POINT

#define SQLITE_OMIT_COMPILEOPTION_DIAGS

/*
#define SQLITE_OMIT_WAL



#define SQLITE_OMIT_BLOB_LITERAL

#define SQLITE_OMIT_COMPILEOPTION_DIAGS

#define SQLITE_OMIT_COMPOUND_SELECT


#define SQLITE_OMIT_FLAG_PRAGMAS

#define SQLITE_OMIT_PAGER_PRAGMAS
#define SQLITE_OMIT_PRAGMA
#define SQLITE_OMIT_PROGRESS_CALLBACK
#define SQLITE_OMIT_SCHEMA_PRAGMAS
#define SQLITE_OMIT_SCHEMA_VERSION_PRAGMAS

#define SQLITE_OMIT_TCL_VARIABLE
#define SQLITE_OMIT_TRIGGER
#define SQLITE_OMIT_TRACE
#define SQLITE_OMIT_VIEW
#define SQLITE_OMIT_VIRTUALTABLE
*/


#endif // __SQLITE3_DEFAULTS_H__