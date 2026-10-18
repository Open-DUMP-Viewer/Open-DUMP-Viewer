/*****************************************************************************
    Open DUMP Viewer for Oracle database

    odv_types.h
    Internal type definitions for the Open DUMP Parser DLL

    Copyright (C) 2026 YANAI Taketo
 *****************************************************************************/

#ifndef ODV_TYPES_H
#define ODV_TYPES_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>

#ifdef WINDOWS
#include <windows.h>
#ifdef _MSC_VER
#pragma warning(disable:4996)
#endif
#endif

/* Calling convention: __stdcall on Windows, default on POSIX */
#ifndef ODV_CALL
  #ifdef WINDOWS
    #define ODV_CALL __stdcall
  #else
    #define ODV_CALL
  #endif
#endif

/*---------------------------------------------------------------------------
    Constants
 ---------------------------------------------------------------------------*/

/* Return codes */
#define ODV_OK                  0
#define ODV_ERROR              -1
#define ODV_ERROR_MALLOC       -2
#define ODV_ERROR_INVALID_ARG  -3
#define ODV_ERROR_FORMAT       -4
#define ODV_ERROR_BUFFER_OVER  -9
#define ODV_ERROR_FOPEN       -101
#define ODV_ERROR_FREAD       -102
#define ODV_ERROR_FWRITE      -103
#define ODV_ERROR_FSEEK       -104
#define ODV_ERROR_CANCELLED   -200
#define ODV_ERROR_UNSUPPORTED -300

/* Success with a warning (M-21): the operation completed and everything that
   could be read was delivered, but the dump ends early. Positive, so a caller
   that only accepts ODV_OK does not mistake a partial result for a whole one.
   Same values as in odv_api.h. */
#define ODV_WARN_TRUNCATED       1   /* the dump stops before its own end */
#define ODV_WARN_VOLUME_MISSING  2   /* a later file of a split set is missing */

/* Boolean */
#define ODV_TRUE   1
#define ODV_FALSE  0

/* Dump file types */
#define DUMP_UNKNOWN          -1
#define DUMP_EXPDP             0
#define DUMP_EXPDP_COMPRESS    1
#define DUMP_EXP              10
#define DUMP_EXP_DIRECT       11

/* Character sets */
#define CHARSET_UNKNOWN       -1
#define CHARSET_SJIS           1
#define CHARSET_EUC            2
#define CHARSET_UTF8           3
#define CHARSET_UTF16LE        4
#define CHARSET_UTF16BE        5
#define CHARSET_US7           10
#define CHARSET_US8           11  /* ISO-8859-1 (WE8ISO8859P1) */
#define CHARSET_WIN1252       12  /* Windows-1252 (WE8MSWIN1252) */
#define CHARSET_ISO8859P15    13  /* ISO-8859-15 (WE8ISO8859P15) */
#define CHARSET_GBK           14  /* GBK (ZHS16GBK) */
/* Japanese Oracle charsets.  CHARSET_SJIS / CHARSET_EUC are JA16SJIS /
   JA16EUC; they and the TILDE / YEN variants are decoded with Oracle's own
   mapping (odv_ja16.c), not a Windows code page.  Other charsets whose name
   or exp id merely looks like SJIS / EUC keep the Windows code page
   conversion (CP932 / CP20932). */
#define CHARSET_SJIS_TILDE    15  /* JA16SJISTILDE */
#define CHARSET_EUC_TILDE     16  /* JA16EUCTILDE */
#define CHARSET_SJIS_CP932    17  /* other "SJIS" charsets: Windows CP932 */
#define CHARSET_EUC_CP20932   18  /* other "EUC" charsets: Windows CP20932 */
#define CHARSET_SJIS_YEN      19  /* JA16SJISYEN */
#define CHARSET_EUC_YEN       20  /* JA16EUCYEN */

/* Column data types */
#define COL_NULL               0
#define COL_CHAR               1
#define COL_NCHAR              2
#define COL_VARCHAR            3
#define COL_NVARCHAR           4
#define COL_NUMBER             5
#define COL_FLOAT              6
#define COL_LONG               7
#define COL_RAW                8
#define COL_DATE              10
#define COL_TIMESTAMP         12
#define COL_TIMESTAMP_TZ      13
#define COL_TIMESTAMP_LTZ     14
#define COL_INTERVAL_YM       15
#define COL_INTERVAL_DS       16
#define COL_BLOB              20
#define COL_CLOB              21
#define COL_NCLOB             22
#define COL_LONG_RAW          23
#define COL_BIN_FLOAT         24
#define COL_BIN_DOUBLE        25
#define COL_BFILE             30
#define COL_XMLTYPE           40
#define COL_ROWID             80
#define COL_USER_DEFINE       90

/* EXP export modes */
#define EXP_MODE_TABLE         0
#define EXP_MODE_USER          1
#define EXP_MODE_DATABASE      2

/* EXPDP record header bytes */
#define EXPDP_REC_NORMAL_01    0x01
#define EXPDP_REC_NORMAL_04    0x04
#define EXPDP_REC_LOB_08       0x08
#define EXPDP_REC_LOB_09       0x09
#define EXPDP_REC_SINGLE_LOB   0x0c
#define EXPDP_REC_OVER255_18   0x18
#define EXPDP_REC_OVER255_19   0x19
#define EXPDP_REC_END          0xff

/* Buffer sizes */
/* Paths are UTF-8 (M-26). A MAX_PATH (260 UTF-16 units) Windows path takes
   up to 780 bytes in UTF-8 (3 per kanji), so 260 bytes cut a short Japanese
   path. A longer path is refused, never silently truncated. */
#define ODV_PATH_LEN          1024
#define ODV_OBJNAME_LEN        128
#define ODV_MSG_LEN           2048   /* holds a message with a whole path */
#define ODV_WORD_LEN          6000
#define ODV_VARCHAR_LEN      98301   /* UTF-8 max VARCHAR2 */
#define ODV_FILE_BUF_LEN     32768
#define ODV_DUMP_BLOCK_LEN    4096   /* EXPDP read block size */
#define ODV_EXP_READ_BUF_LEN 65536
#define ODV_EXP_RECORD_LEN  6144000
#define ODV_DDL_BUF_LEN    1048576   /* 1MB for DDL */
#define ODV_LOB_CHUNK_LEN   131072   /* 128KB LOB chunk */
/* Hard cap on table_list entries (tables + partitions + subpartitions).
   table_list grows on demand (odv_table_list_reserve); the cap only bounds a
   pathological dump. It used to be a fixed 1000-entry array, so a dump whose
   tables and partitions exceeded 1000 in total listed the rest as unnamed
   plain tables (M-29). Oracle allows ~1M partitions per table. */
#define ODV_MAX_TABLES     1048576
#define ODV_MAX_COLUMNS       1000
/* ODV_TABLE.constraints grows on demand; this only bounds a pathological
   dump. It was a fixed array of 50, and the 51st constraint or index of a
   table was dropped without notice. */
#define ODV_MAX_CONSTRAINTS  65536
/* Key columns per constraint / index: Oracle's own limit is 32 (it was 16
   here, so a wider key lost its trailing columns). */
#define ODV_MAX_CONSTRAINT_COLS 32
#define ODV_MAX_SPLIT         2048   /* Max files in a FILESIZE/%U split set */

/* Table/Partition types (for ODV_TABLE_ENTRY.type) */
#define TABLE_TYPE_TABLE              0
#define TABLE_TYPE_PARTITION_TABLE    1   /* Parent of partitioned table */
#define TABLE_TYPE_PARTITION          2
#define TABLE_TYPE_SUBPARTITION       3

/* Constraint types */
#define CONSTRAINT_PK          0   /* PRIMARY KEY */
#define CONSTRAINT_UNIQUE      1   /* UNIQUE */
#define CONSTRAINT_FK          2   /* FOREIGN KEY */
#define CONSTRAINT_CHECK       3   /* CHECK */
#define CONSTRAINT_INDEX       4   /* INDEX (non-unique, non-PK) */

/* Date format options */
#define DATE_FMT_SLASH         0     /* YYYY/MM/DD HH:MI:SS */
#define DATE_FMT_COMPACT       1     /* YYYYMMDD */
#define DATE_FMT_FULL          2     /* YYYYMMDDHH24MISS */
#define DATE_FMT_CUSTOM        3     /* Custom format string */

/* Output CSV escaping */
#define CSV_ESCAPE_COMMA     0x04
#define CSV_ESCAPE_NEWLINE   0x08
#define CSV_ESCAPE_DQUOTE    0x10

/* DBMS types for SQL output */
#define DBMS_ORACLE            0
#define DBMS_POSTGRES          4
#define DBMS_MYSQL             5
#define DBMS_SQLSERVER         6

/*---------------------------------------------------------------------------
    Data Structures
 ---------------------------------------------------------------------------*/

/* DDL text fields of any length.

   A column's DEFAULT (DATA_DEFAULT is LONG) and comment (up to 4000 bytes), a
   table's comment, a CHECK condition (SEARCH_CONDITION is LONG) and an index
   expression are kept in a fixed array when they fit, and otherwise in full
   in the paired heap pointer (<field>_long), with the fixed array left "".
   The fixed sizes used to be hard limits: longer comments were cut at 511
   bytes and longer CHECK conditions / DEFAULTs were dropped.

   Read such a field only through ODV_TEXT(fixed, long). Code that reads the
   fixed array alone still sees a valid (empty) string for a long value, never
   a cut-off expression. Write through odv_text_set(). The heap copies of
   s->table are released by odv_table_free_text(). */
#define ODV_TEXT(fixed, longp) \
    ((longp) ? (const char *)(longp) : (const char *)(fixed))

/* Column definition */
typedef struct {
    char   name[ODV_OBJNAME_LEN + 1];
    int    type;                 /* COL_* constant */
    int    length;
    int    precision;
    int    scale;
    int    charset;              /* For NCHAR/NVARCHAR */
    int    flags;
    int    property;
    int    col_num;              /* EXPDP COL_NUM (user column id; 0 = hidden) */
    int    seg_col;              /* EXPDP SEGCOL_NUM (storage slot; 0 = not stored) */
    char   type_str[64];         /* "VARCHAR2(100)" etc. */
    int    not_null;             /* 1=NOT NULL constraint */
    char   default_val[256];     /* DEFAULT value expression (see ODV_TEXT) */
    char   comment[512];         /* Column comment (COMMENT ON COLUMN) (see ODV_TEXT) */
    char  *default_long;         /* full DEFAULT when longer than default_val[], else NULL */
    char  *comment_long;         /* full comment when longer than comment[], else NULL */
} ODV_COLUMN;

/* Constraint definition */
typedef struct {
    int    type;                                                    /* CONSTRAINT_PK/UNIQUE/FK */
    char   name[ODV_OBJNAME_LEN + 1];                              /* Constraint name */
    char   columns[ODV_MAX_CONSTRAINT_COLS][ODV_OBJNAME_LEN + 1];  /* Column names */
    int    col_count;
    /* FK only */
    char   ref_schema[ODV_OBJNAME_LEN + 1];
    char   ref_table[ODV_OBJNAME_LEN + 1];
    char   ref_columns[ODV_MAX_CONSTRAINT_COLS][ODV_OBJNAME_LEN + 1];
    int    ref_col_count;
    /* CHECK only */
    char   condition[1024];                                             /* CHECK condition expression (see ODV_TEXT) */
    /* INDEX only */
    char   index_expr[1024];                                            /* Function-based index expression (full column list text) (see ODV_TEXT) */
    char  *condition_long;       /* full condition when longer than condition[], else NULL */
    char  *index_expr_long;      /* full expression when longer than index_expr[], else NULL */
    /* Constraint state from the EXP DDL (M-33). Zero / "" = the default
       (NO ACTION, NOT DEFERRABLE, INITIALLY IMMEDIATE, ENABLED). */
    char   delete_rule[16];      /* FK: "CASCADE" or "SET NULL" */
    int    deferrable;           /* 1 = DEFERRABLE */
    int    initially_deferred;   /* 1 = INITIALLY DEFERRED */
    int    disabled;             /* 1 = created DISABLE */
} ODV_CONSTRAINT;

/* Table definition */
typedef struct {
    char        schema[ODV_OBJNAME_LEN + 1];
    char        name[ODV_OBJNAME_LEN + 1];
    char        partition[ODV_OBJNAME_LEN + 1];
    ODV_COLUMN  columns[ODV_MAX_COLUMNS];
    int         col_count;
    int         lob_col_count;
    /* Row-data column layout (H-18): one entry per column actually present in
     * the row data (SEGCOL_NUM >= 1), in SEGCOL order.  data_disp[i] indexes
     * columns[] for a displayed column, or is -1 to read and discard a hidden
     * in-data column (object table OID, ADT attribute, nested-table SETID,
     * invisible column).  data_type[i] is that column's COL_* type, used to
     * split the stream into the non-LOB inline prefix and the LOB-ish suffix.
     * Virtual / not-stored columns (SEGCOL_NUM == 0) are absent here. */
    int         data_disp[ODV_MAX_COLUMNS];
    int         data_type[ODV_MAX_COLUMNS];
    int         data_count;
    int         dump_charset;
    int         os_charset;
    int         nls_charset;
    int         endian;          /* 0=little, 1=big */
    int64_t     record_count;
    int64_t     ddl_offset;      /* File position of CREATE TABLE DDL (for fast seek) */
    int         is_partition;
    ODV_CONSTRAINT *constraints; /* heap array of constraint_cap entries
                                    (EXP; NULL for EXPDP), constraint_count used */
    int         constraint_count;
    int         constraint_cap;
    char        comment[512];    /* Table comment (COMMENT ON TABLE) (see ODV_TEXT) */
    char       *comment_long;    /* full table comment when longer than comment[] */
    char       *notes;           /* DDL of this table that could not be kept whole,
                                    one '\n'-terminated line each (DDL charset), or
                                    NULL; reaches the caller as the type=6 entry of
                                    constraints_json (EXP) */
} ODV_TABLE;

/* Partition list entry (built from EXPDP dictionary table) */
typedef struct {
    char    schema[ODV_OBJNAME_LEN + 1];
    char    table[ODV_OBJNAME_LEN + 1];
    char    partition[ODV_OBJNAME_LEN + 1];
    char    subpartition[ODV_OBJNAME_LEN + 1];
    int     partition_no;        /* PROCESS_ORDER from dictionary */
    int64_t row_count;           /* COMPLETED_ROWS from dictionary */
} ODV_PARTITION_ENTRY;

/* Lightweight constraint entry for EXPDP metadata (name-only, no column info) */
typedef struct {
    char    name[ODV_OBJNAME_LEN + 1];
    int     type;                /* CONSTRAINT_PK/UNIQUE/FK/INDEX etc. */
} ODV_CONSTRAINT_NAME;

#define ODV_MAX_META_CONSTRAINTS 20  /* Max constraints per table in table_list */

/* Table list entry (for list_tables) */
typedef struct {
    char    schema[ODV_OBJNAME_LEN + 1];
    char    name[ODV_OBJNAME_LEN + 1];
    char    partition[ODV_OBJNAME_LEN + 1];      /* Partition name (if partition/subpartition) */
    char    parent_partition[ODV_OBJNAME_LEN + 1]; /* Parent partition (if subpartition) */
    int     type;                /* TABLE_TYPE_* constant */
    int     col_count;
    int64_t row_count;
    /* EXPDP metadata (populated by master table scan) */
    ODV_CONSTRAINT_NAME meta_constraints[ODV_MAX_META_CONSTRAINTS];
    int     meta_constraint_count;
} ODV_TABLE_ENTRY;

/* Column value (decoded, ready for output) */
typedef struct {
    int             type;
    int             is_null;
    unsigned char  *data;        /* Decoded string/binary data */
    int             data_len;
    int             buf_size;    /* Allocated buffer size */
} ODV_VALUE;

/* Record (one row of data) */
typedef struct {
    ODV_VALUE  *values;
    int         col_count;
    int         max_columns;
} ODV_RECORD;

/*---------------------------------------------------------------------------
    Virtual multi-file read stream (H-28)

    Presents an ordered set of split dump files (exp FILESIZE= / expdp
    dumpfile=%U, named <stem>_01, _02, ...) as one continuous logical byte
    stream, so a row / LOB / XML / master table that straddles a file boundary
    is read without interruption.  Each physical file after the first carries
    its own header block that is NOT part of the logical stream; `skip` is how
    many leading bytes of that physical file are dropped (0 for the first file,
    the block size for exp, one 4KB block for expdp).  For a single, non-split
    dump the stream has exactly one segment with skip 0, so read/seek/tell are
    byte-for-byte identical to operating on the one FILE directly.
 ---------------------------------------------------------------------------*/
typedef struct {
    char   *path;        /* physical file path (heap) */
    int64_t skip;        /* leading bytes of this file not in the logical stream */
    int64_t avail;       /* logical bytes this file contributes (size - skip) */
    int64_t logstart;    /* logical offset of this file's first contributed byte */
} ODV_VSEG;

typedef struct {
    ODV_VSEG *seg;       /* heap array of nfiles segments */
    int       nfiles;
    int       cap;
    int64_t   logsize;   /* total logical size (sum of seg[].avail) */
    int64_t   logpos;    /* current logical read position */
    FILE     *fp;        /* lazily-opened handle for segment open_idx */
    int       open_idx;  /* segment fp refers to (-1 = none open) */
    int64_t   phys_pos;  /* physical offset fp is positioned at (avoids re-seek) */
    int       tail;      /* how the stream ends: ODV_TAIL_* (M-21) */
    struct _odv_session *owner; /* told when a read reaches an early end */
} ODV_VFILE;

/* How a dump's logical stream ends (ODV_VFILE.tail, M-21). Decided in
   odv_vopen from the last file of the set: an exp dump ends with its EXIT
   records, an expdp file holds the master table where its header (or, from
   23ai, its trailer block) says. */
#define ODV_TAIL_COMPLETE  0   /* ends where the dump says it ends */
#define ODV_TAIL_CUT       1   /* ends early: copy cut short, export failed */
#define ODV_TAIL_VOLMISS   2   /* this file is complete, but the dump goes on
                                  in a split volume that was not found */

/*---------------------------------------------------------------------------
    EXPDP record parsing state machine (ARK-style)

    data_step values:
      Positive (normal column reading):
        DS_COL_LENGTH (0)  = Read column length marker byte
        DS_COL_LEN_HI (1)  = First byte of 2-byte column length
        DS_COL_LEN_LO (2)  = Second byte of 2-byte column length
        DS_COL_DATA   (3)  = Reading column data bytes
        DS_LOB_CHUNK  (4)  = Reading LOB chunk data (NEXT_CHUNK)

      Negative (LOB state machine):
        DS_LOB_LEN_LO  (-8)  = LOB 2-byte length: low byte
        DS_LOB_LEN_HI  (-9)  = LOB 2-byte length: high byte
        DS_LOB_FE_NEXT (-10) = After 0xFE in LOB context
        DS_LOB_POST    (-11) = After LOB chunk end check
        DS_LOB_MARKER  (-12) = LOB column marker byte
        DS_LOB_PADDING (-14) = NULL column padding before LOB
 ---------------------------------------------------------------------------*/

/* data_step constants */
#define DS_COL_LENGTH       0
#define DS_COL_LEN_HI       1
#define DS_COL_LEN_LO       2
#define DS_COL_DATA         3
#define DS_LOB_CHUNK        4
#define DS_LOB_LEN_LO     (-8)
#define DS_LOB_LEN_HI     (-9)
#define DS_LOB_FE_NEXT   (-10)
#define DS_LOB_POST      (-11)
#define DS_LOB_MARKER    (-12)
#define DS_LOB_PADDING   (-14)

/* LOB preview max size */
#define ODV_LOB_PREVIEW_LEN   4096

/* EXPDP parse state */
typedef struct {
    /* Main state */
    int     step;                /* 1=expect header, 2=reading record data */
    int     data_step;           /* Sub-state: DS_* constants */

    /* Column tracking */
    int     col_idx;             /* Current non-LOB column counter (0..non_lob_cols-1) */
    int     lob_col_idx;         /* Current LOB column index (0-based among LOBs) */
    int     col_len;             /* Current column data length */
    int     col_remaining;       /* Bytes remaining in current column */

    /* Non-LOB column → absolute column index mapping.
     * In EXPDP LOB records, column data is packed without LOB columns.
     * non_lob_map[i] gives the absolute column index for the i-th
     * non-LOB column in the binary stream. */
    int     non_lob_map[ODV_MAX_COLUMNS];
    int     non_lob_count;       /* Number of genuine non-LOB columns (prefix) */
    int     map_count;           /* Full map length (non-LOB prefix + LOB-ish
                                    columns appended in table order), used for
                                    the external-table inline format (H-21) */
    int     frag_lob_count;      /* Number of columns the direct-path LOB chunk
                                    machine processes = BLOB/CLOB/NCLOB/LONG/
                                    LONG RAW (= map_count - non_lob_count).
                                    Unlike table.lob_col_count it counts plain
                                    LONG (a fragment when the table also has a
                                    real LOB) and excludes BFILE/object types
                                    (which are inline), so the "all LOBs read"
                                    test fires at the right column (H-06). */

    /* Record header */
    int     record_header;       /* Last record header byte (0x01/0x04/0x08/0x09/0x0c) */
    int     is_lob_record;       /* 1=current record is LOB (0x08/0x09/0x0c) */
    int     is_between_record;   /* 1=at least one record header seen */

    /* LOB state */
    int     is_last_chunk;       /* 1=current LOB chunk is the last */
    int     is_end_lob;          /* 1=all LOB columns processed */
    int     lob_length;          /* Cumulative LOB chunk bytes for current column */
    int     lob_preview_len;     /* Bytes accumulated in LOB preview */
    /* CLOB/NCLOB preview conversion carries the trailing incomplete UTF-16BE
     * unit (odd byte) or an unpaired high surrogate from one 4KB read block to
     * the next, so a character split across the block boundary is not lost or
     * turned into U+FFFD.  lob_pv_fresh marks the first block of a LOB column's
     * preview so the (reused) value buffer is cleared before appending. */
    unsigned char lob_pv_carry[4];
    int     lob_pv_carry_len;
    int     lob_pv_fresh;
    unsigned char *lob_buf;
    int     lob_buf_len;
    int     lob_buf_alloc;

    /* >255 columns */
    int     is_over255;          /* 1=record has >255 column format */
    int     over255_count;       /* Count of 255-boundary crossings */
    int     filler_length;       /* Filler bytes at 255 boundary (2 or 4) */

    /* Segment (3c wrapper) tracking */
    int     seg_remaining;       /* Bytes left in current 3c segment (-1=no limit) */

    /* External-table format: set within the parse of ONE table once a real LOB
     * column (CLOB/NCLOB/BLOB/LONG RAW) has been read inline, which only
     * happens in the external-table / QUERY= format (direct path sends real
     * LOBs as 08/09 chunk fragments, never inline).  It is PER-TABLE — reset at
     * the start of every table — so a direct-path table that stores a small LOB
     * inline (ENABLE STORAGE IN ROW, or the 23ai inline form) does not cause a
     * later plain-LONG table in the same pass to be misread.  Used to decode an
     * inline LONG as UTF-16BE instead of the DB charset (H-21). */
    int     expdp_external;

    /* 2-byte length buffer (reused for column and LOB lengths) */
    unsigned char len_buf[2];
} ODV_PARSE_STATE;

/* EXP parse state */
typedef struct {
    int     step;                /* 0=header,1=mode,2=DDL,3=meta,5=data,6=skip,7=skip */
    int     data_step;
    int     header_size;
    int     oracle_version;
    int     exp_mode;            /* EXP_MODE_TABLE etc. */
    int     db_charset;          /* Header record 7 DB charset (row-value decode).
                                    s->dump_charset holds the client charset used
                                    for DDL text (names/comments/CHECK/DEFAULT). */
    char    export_user[ODV_OBJNAME_LEN + 1]; /* Header record 1: export user/schema */
} ODV_EXP_STATE;

/* Forward declaration */
typedef struct _odv_session ODV_SESSION;

/* Callback types */
typedef void (ODV_CALL *ODV_ROW_CALLBACK)(
    const char *schema,
    const char *table,
    int col_count,
    const char **col_names,
    const char **col_values,
    void *user_data
);

typedef void (ODV_CALL *ODV_PROGRESS_CALLBACK)(
    int64_t rows_processed,
    const char *current_table,
    void *user_data
);

typedef void (ODV_CALL *ODV_TABLE_CALLBACK)(
    const char *schema,
    const char *table,
    int col_count,
    const char **col_names,
    const char **col_types,
    const int *col_not_nulls,    /* 1=NOT NULL, 0=nullable (per column) */
    const char **col_defaults,   /* DEFAULT value expression (per column, "" if none) */
    int constraint_count,        /* Number of constraints (PK/UNIQUE/FK) */
    const char *constraints_json,/* JSON-encoded constraint array (see ODV_CONSTRAINT) */
    int64_t row_count,
    int64_t data_offset,         /* File position of table DDL (for fast seek) */
    void *user_data
);

/* Main session structure */
struct _odv_session {
    /* Dump file info */
    char            dump_path[ODV_PATH_LEN + 1];
    int64_t         dump_size;
    int             dump_type;       /* DUMP_EXPDP, DUMP_EXP etc. */
    int             dump_charset;
    int             out_charset;
    int             expdp_xml_prefix; /* EXPDP: 2-byte prefix before "<?xml" in
                                         a table-definition block (the DB charset
                                         id). Captured from the first such block;
                                         -1 until captured. Used to tell a real
                                         table definition from a row value that
                                         merely contains "<?xml" (H-23). */

    /* Current table being parsed */
    ODV_TABLE       table;

    /* Table list: heap array of table_cap entries, table_count in use.
       Grown by odv_table_list_reserve(); freed by odv_destroy_session(). */
    ODV_TABLE_ENTRY *table_list;
    int             table_count;
    int             table_cap;

    /* Hash index over table_list by schema + name, holding the first entry
       of each name (see odv_table_list_find); kept in step lazily. */
    int            *tl_bucket;      /* chain heads, tl_nbucket (power of 2) */
    int            *tl_next;        /* chain link per entry, tl_next_cap */
    int             tl_nbucket;
    int             tl_next_cap;
    int             tl_count;       /* entries [0, tl_count) are indexed */

    /* Partition list (EXPDP dictionary). Not populated by any parser at
       present: partition_count stays 0 and partition_list NULL. */
    ODV_PARTITION_ENTRY *partition_list;
    int             partition_count;

    /* Parse state */
    ODV_PARSE_STATE state;
    ODV_EXP_STATE   exp_state;

    /* Record buffer (reused per row) */
    ODV_RECORD      record;
    unsigned char   read_buf[ODV_EXP_READ_BUF_LEN];

    /* Callbacks */
    ODV_ROW_CALLBACK        row_cb;
    void                   *row_ud;
    ODV_PROGRESS_CALLBACK   progress_cb;
    void                   *progress_ud;
    ODV_TABLE_CALLBACK      table_cb;
    void                   *table_ud;

    /* Table filter for selective parsing */
    char            filter_schema[ODV_OBJNAME_LEN + 1];
    char            filter_table[ODV_OBJNAME_LEN + 1];
    char            filter_partition[ODV_OBJNAME_LEN + 1]; /* Partition name filter (empty=all) */
    int             filter_active;   /* 0=no filter, 1=filter active */
    int             pass_flg;        /* 1=skip current table's records */
    int64_t         seek_offset;     /* If >0, seek here after header to skip DDL scan */

    /* Control */
    int             cancelled;
    char            last_error[ODV_MSG_LEN + 1];
    int             stream_cut;      /* ODV_TAIL_* of a stream whose early end
                                        the current operation read up to (M-21);
                                        reset at the start of each operation */

    /* Statistics */
    int64_t         total_rows;

    /* Progress tracking (file-position-based percentage with hysteresis) */
    int             last_progress_pct;  /* Last reported percentage (0-100) */

    /* Application version (set by caller via odv_set_app_version) */
    char            app_version[64];

    /* Export options */
    int             date_format;           /* 0=SLASH, 1=COMPACT, 2=FULL, 3=CUSTOM */
    char            custom_date_format[256]; /* Format string for DATE_FMT_CUSTOM */
    int             csv_write_header;      /* 1=write column header row (default:1) */
    int             csv_write_types;       /* 1=write column type row (default:0) */
    char            csv_delimiter;         /* CSV field delimiter (default ',') */
    uint32_t        sql_flags;             /* Bitwise OR of ODV_SQL_* flags (default: ODV_SQL_ALL) */

    /* TIMESTAMP WITH (LOCAL) TIME ZONE (odv_tz.c) */
    char            db_timezone[64];       /* Source DBTIMEZONE from the dump ("" = not found) */
    int             tz_raw_values;         /* Internal: 1 while write_sql_file parses; TSTZ/TSLTZ
                                              values are then delivered as "#<hex bytes>" */

    /* LOB extraction options */
    int             lob_extract_mode;      /* 1=extracting LOB files */
    char            lob_column[ODV_OBJNAME_LEN + 1];   /* Target LOB column name */
    int             lob_column_index;      /* LOB-only index of target column (-1=unset) */
    char            lob_output_dir[ODV_PATH_LEN + 1];  /* Output directory */
    char            lob_filename_col[ODV_OBJNAME_LEN + 1]; /* Column for filename (empty=sequential) */
    char            lob_extension[32];     /* File extension (default "lob") */
    int64_t         lob_files_written;     /* Count of files written */
    int64_t         lob_files_failed;      /* Count of LOB rows that could not be written (H-07) */
    int             lob_column_missing;    /* 1 = the table had no such LOB column (M-27) */
};

/*---------------------------------------------------------------------------
    Utility macros
 ---------------------------------------------------------------------------*/

/* Case-insensitive string comparison */
#ifdef WINDOWS
  #define odv_stricmp(a, b)       _stricmp((a), (b))
  #define odv_strnicmp(a, b, n)   _strnicmp((a), (b), (n))
#else
  #include <strings.h>
  #define odv_stricmp(a, b)       strcasecmp((a), (b))
  #define odv_strnicmp(a, b, n)   strncasecmp((a), (b), (n))
#endif

/* 64-bit file I/O */
#ifdef WINDOWS
  #define odv_fseek(fp, off, whence) _fseeki64((fp), (off), (whence))
  #define odv_ftell(fp)              _ftelli64((fp))
#else
  #define odv_fseek(fp, off, whence) fseeko((fp), (off), (whence))
  #define odv_ftell(fp)              ftello((fp))
#endif

/* Safe string copy (null-terminated, no truncation warnings) */
#define odv_strcpy(dst, src, maxlen) \
    snprintf((dst), (size_t)(maxlen) + 1, "%s", (src))

/* Min/Max */
#define ODV_MIN(a, b) ((a) < (b) ? (a) : (b))
#define ODV_MAX(a, b) ((a) > (b) ? (a) : (b))

/*---------------------------------------------------------------------------
    Internal function prototypes (cross-module)
 ---------------------------------------------------------------------------*/

/* odv_detect.c */
int detect_dump_kind(ODV_SESSION *s);

/* odv_expdp.c */
int parse_expdp_dump(ODV_SESSION *s, int list_only);

/* odv_exp.c */
int parse_exp_dump(ODV_SESSION *s, int list_only);

/* odv_record.c */
int  init_record(ODV_RECORD *rec, int max_cols);
void free_record(ODV_RECORD *rec);
void reset_record(ODV_RECORD *rec);
int  set_value_null(ODV_VALUE *v);
int  set_value_string(ODV_VALUE *v, const char *str, int len);
int  ensure_value_buf(ODV_VALUE *v, int needed);
int  deliver_row(ODV_SESSION *s);
void odv_report_progress(ODV_SESSION *s, ODV_VFILE *fp);
void invalidate_meta_cache(void);
void update_meta_cache(ODV_SESSION *s);

/* table_list growth (odv_api.c). Makes room for one more entry at
 * s->table_list[s->table_count] (zero-filled when first allocated, but a slot
 * reused after table_count was reset is not); the caller fills every field
 * and increments table_count. Returns 0 when the cap is reached or memory runs
 * out (a warning is left in last_error); the caller must then not report
 * the table through table_cb either, so callback i and entry i stay paired. */
int     odv_table_list_reserve(ODV_SESSION *s);

/* The first entry k in [0, table_count) whose schema and name equal the given
 * ones (as stored in the entry), or -1. Same result as scanning the list from
 * the start, through a hash index (odv_api.c), so listing a dump with tens of
 * thousands of tables and partitions stays linear. */
int     odv_table_list_find(ODV_SESSION *s, const char *schema, const char *name);

/* DDL text fields (odv_api.c; see ODV_TEXT). odv_text_set stores src[0..len)
 * in fixed[cap] when it fits, else in full in *longp (fixed left ""); it
 * returns 0 only when memory runs out (nothing is stored then).
 * odv_table_free_text releases every *_long of a table and its constraint
 * array (counts reset); call it before reusing or zeroing the table. */
int     odv_text_set(char *fixed, size_t cap, char **longp, const char *src, size_t len);
void    odv_table_free_text(ODV_TABLE *t);

/* Virtual multi-file read stream (odv_api.c; see ODV_VFILE). For a single,
 * non-split dump these behave exactly like fopen/fread/fseek/ftell/fclose on
 * the one file. */
int     odv_vopen(ODV_VFILE *vf, ODV_SESSION *s);
size_t  odv_vread(void *ptr, size_t size, size_t nmemb, ODV_VFILE *vf);
int     odv_vseek(ODV_VFILE *vf, int64_t offset, int whence);
int64_t odv_vtell(ODV_VFILE *vf);
void    odv_vclose(ODV_VFILE *vf);

/* fopen for a path in UTF-8 (odv_api.c, M-26). On Windows a valid UTF-8 path
 * is opened through its UTF-16 form (_wfopen), so it may hold characters
 * outside the ANSI code page; bytes that are not UTF-8 are taken as ANSI, as
 * plain fopen does. Elsewhere it is fopen. */
FILE   *odv_fopen_path(const char *path, const char *mode);

/* 1 if the expdp file at s->dump_path ends before the master table its own
 * header (or trailer) points to: a dump cut short, not a compressed one,
 * when no table definition is found in it (odv_api.c, M-21). */
int     odv_expdp_cut_short(ODV_SESSION *s);

/* odv_number.c */
int decode_oracle_number(const unsigned char *buf, int len, char *out, int out_size);
int decode_oracle_rowid(const unsigned char *buf, int len, int is_urowid, char *out, int out_size);

/* odv_datetime.c */
int decode_oracle_date(const unsigned char *buf, int len, char *out, int out_size, int fmt, const char *custom_fmt);
int decode_oracle_timestamp(const unsigned char *buf, int len, char *out, int out_size, int fmt, const char *custom_fmt, int ts_precision);
int decode_binary_float(const unsigned char *buf, char *out, int out_size);
int decode_binary_double(const unsigned char *buf, char *out, int out_size);
int decode_interval_ym(const unsigned char *buf, int len, char *out, int out_size);
int decode_interval_ds(const unsigned char *buf, int len, char *out, int out_size);

/* odv_tz.c */
int  odv_decode_timestamp_value(ODV_SESSION *s, int col_type,
                                const unsigned char *buf, int len,
                                char *out, int out_size, int ts_precision);
int  odv_tz_sql_value(ODV_SESSION *s, int col_type, int ts_precision,
                      const char *value, int dbms_type,
                      char *out, int out_size, int *is_expr);
const char *odv_tz_region_name(int region_id);
int  odv_tz_region_offset(int region_id, int64_t utc_secs, int32_t *off_secs);
const char *odv_tz_data_version(void);
void odv_tz_scan_exp_dbtz(ODV_SESSION *s, FILE *fp);
void odv_tz_capture_dbtz_xml(ODV_SESSION *s, const char *xml, int len);

/* odv_charset.c */
int convert_charset(const char *src, int src_len, int src_cs,
                    char *dst, int dst_size, int dst_cs, int *out_len);

/* odv_ja16.c (Oracle JA16SJIS / JA16EUC and their TILDE / YEN variants) */
int odv_ja16_is_table_charset(int cs);
int odv_ja16_to_utf8(const unsigned char *src, int src_len, int cs,
                     char *dst, int dst_size, int *out_len);
int odv_utf8_to_ja16(const unsigned char *src, int src_len, int cs,
                     char *dst, int dst_size, int *out_len);

/* odv_xml.c */
typedef void (*xml_tag_callback)(const char *tag, const char *value, int depth, void *ctx);
int parse_xml_ddl(const char *xml, int xml_len, xml_tag_callback cb, void *ctx);

/* odv_csv.c */
int write_csv_file(ODV_SESSION *s, const char *table_name, const char *output_path);

/* odv_sql.c */
int write_sql_file(ODV_SESSION *s, const char *table_name, const char *output_path, int dbms_type);

/* LOB helpers (odv_api.c) */
int  odv_lob_check_column(ODV_SESSION *s);
int  odv_lob_accumulate(ODV_SESSION *s, int lob_col_idx, const unsigned char *data, int len);
int  odv_lob_write_file(ODV_SESSION *s);
void odv_lob_reset_buffer(ODV_SESSION *s);

#endif /* ODV_TYPES_H */
