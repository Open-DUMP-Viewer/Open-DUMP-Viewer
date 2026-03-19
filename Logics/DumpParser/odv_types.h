/*****************************************************************************
    OraDB DUMP Viewer

    odv_types.h
    Internal type definitions for the OraDB DUMP Parser DLL

    Copyright (C) 2026 YANAI Taketo
 *****************************************************************************/

#ifndef ODV_TYPES_H
#define ODV_TYPES_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#ifdef WINDOWS
#include <windows.h>
#pragma warning(disable:4996)
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
#define CHARSET_US8           11

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
#define ODV_PATH_LEN           260
#define ODV_OBJNAME_LEN        128
#define ODV_MSG_LEN           1024
#define ODV_WORD_LEN          6000
#define ODV_VARCHAR_LEN      98301   /* UTF-8 max VARCHAR2 */
#define ODV_FILE_BUF_LEN     32768
#define ODV_DUMP_BLOCK_LEN    4096   /* EXPDP read block size */
#define ODV_EXP_READ_BUF_LEN 65536
#define ODV_EXP_RECORD_LEN  6144000
#define ODV_DDL_BUF_LEN    1048576   /* 1MB for DDL */
#define ODV_LOB_CHUNK_LEN   131072   /* 128KB LOB chunk */
#define ODV_MAX_TABLES        1000
#define ODV_MAX_COLUMNS       1000
#define ODV_MAX_CONSTRAINTS     50
#define ODV_MAX_CONSTRAINT_COLS 16

/* Table/Partition types (for ODV_TABLE_ENTRY.type) */
#define TABLE_TYPE_TABLE              0
#define TABLE_TYPE_PARTITION_TABLE    1   /* Parent of partitioned table */
#define TABLE_TYPE_PARTITION          2
#define TABLE_TYPE_SUBPARTITION       3

/* Constraint types */
#define CONSTRAINT_PK          0   /* PRIMARY KEY */
#define CONSTRAINT_UNIQUE      1   /* UNIQUE */
#define CONSTRAINT_FK          2   /* FOREIGN KEY */

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
    char   type_str[64];         /* "VARCHAR2(100)" etc. */
    int    not_null;             /* 1=NOT NULL constraint */
    char   default_val[256];     /* DEFAULT value expression */
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
} ODV_CONSTRAINT;

/* Table definition */
typedef struct {
    char        schema[ODV_OBJNAME_LEN + 1];
    char        name[ODV_OBJNAME_LEN + 1];
    char        partition[ODV_OBJNAME_LEN + 1];
    ODV_COLUMN  columns[ODV_MAX_COLUMNS];
    int         col_count;
    int         lob_col_count;
    int         dump_charset;
    int         os_charset;
    int         nls_charset;
    int         endian;          /* 0=little, 1=big */
    int64_t     record_count;
    int64_t     ddl_offset;      /* File position of CREATE TABLE DDL (for fast seek) */
    int         is_partition;
    ODV_CONSTRAINT constraints[ODV_MAX_CONSTRAINTS];
    int         constraint_count;
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

/* Table list entry (for list_tables) */
typedef struct {
    char    schema[ODV_OBJNAME_LEN + 1];
    char    name[ODV_OBJNAME_LEN + 1];
    char    partition[ODV_OBJNAME_LEN + 1];      /* Partition name (if partition/subpartition) */
    char    parent_partition[ODV_OBJNAME_LEN + 1]; /* Parent partition (if subpartition) */
    int     type;                /* TABLE_TYPE_* constant */
    int     col_count;
    int64_t row_count;
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

/* EXPDP parse state */
typedef struct {
    int     step;                /* Main state: 1=HEADER, 2=DATA, 3=TABLE_DATA */
    int     data_step;           /* Sub-state for column parsing */
    int     col_idx;             /* Current column index */
    int     lob_col_idx;         /* Current LOB column index */
    int     record_len;          /* Bytes remaining in current record */
    int     col_len;             /* Current column data length */
    int     is_between_record;   /* Between records flag */
    int     is_end_record;       /* End of record flag */
    int     is_over255;          /* >255 columns flag */
    int     is_in_filler;        /* In filler region flag */
    int     over255_count;       /* 255-column boundary counter */

    /* LOB state */
    unsigned char *lob_buf;
    int     lob_buf_len;
    int     lob_buf_alloc;
    int     is_last_chunk;
} ODV_PARSE_STATE;

/* EXP parse state */
typedef struct {
    int     step;                /* 0=header,1=mode,2=DDL,3=meta,5=data,6=skip,7=skip */
    int     data_step;
    int     header_size;
    int     oracle_version;
    int     exp_mode;            /* EXP_MODE_TABLE etc. */
    char    export_user[ODV_OBJNAME_LEN + 1]; /* Header record 1: export user/schema */
} ODV_EXP_STATE;

/* Forward declaration */
typedef struct _odv_session ODV_SESSION;

/* Callback types */
typedef void (__stdcall *ODV_ROW_CALLBACK)(
    const char *schema,
    const char *table,
    int col_count,
    const char **col_names,
    const char **col_values,
    void *user_data
);

typedef void (__stdcall *ODV_PROGRESS_CALLBACK)(
    int64_t rows_processed,
    const char *current_table,
    void *user_data
);

typedef void (__stdcall *ODV_TABLE_CALLBACK)(
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

    /* Current table being parsed */
    ODV_TABLE       table;

    /* Table list */
    ODV_TABLE_ENTRY table_list[ODV_MAX_TABLES];
    int             table_count;

    /* Partition list (built from EXPDP dictionary) */
    ODV_PARTITION_ENTRY partition_list[ODV_MAX_TABLES];
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
    int             filter_active;   /* 0=no filter, 1=filter active */
    int             pass_flg;        /* 1=skip current table's records */
    int64_t         seek_offset;     /* If >0, seek here after header to skip DDL scan */

    /* Control */
    int             cancelled;
    char            last_error[ODV_MSG_LEN + 1];

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
    int             sql_create_table;      /* 1=output DROP+CREATE TABLE DDL (default:1) */

    /* LOB extraction options */
    int             lob_extract_mode;      /* 1=extracting LOB files */
    char            lob_column[ODV_OBJNAME_LEN + 1];   /* Target LOB column name */
    int             lob_column_index;      /* LOB-only index of target column (-1=unset) */
    char            lob_output_dir[ODV_PATH_LEN + 1];  /* Output directory */
    char            lob_filename_col[ODV_OBJNAME_LEN + 1]; /* Column for filename (empty=sequential) */
    char            lob_extension[32];     /* File extension (default "lob") */
    int64_t         lob_files_written;     /* Count of files written */
};

/*---------------------------------------------------------------------------
    Utility macros
 ---------------------------------------------------------------------------*/

/* 64-bit file I/O */
#ifdef WINDOWS
  #define odv_fseek(fp, off, whence) _fseeki64((fp), (off), (whence))
  #define odv_ftell(fp)              _ftelli64((fp))
#else
  #define odv_fseek(fp, off, whence) fseeko((fp), (off), (whence))
  #define odv_ftell(fp)              ftello((fp))
#endif

/* Safe string copy */
#define odv_strcpy(dst, src, maxlen) do { \
    strncpy((dst), (src), (maxlen)); \
    (dst)[(maxlen)] = '\0'; \
} while(0)

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
void odv_report_progress(ODV_SESSION *s, FILE *fp);
void invalidate_meta_cache(void);
void update_meta_cache(ODV_SESSION *s);

/* odv_number.c */
int decode_oracle_number(const unsigned char *buf, int len, char *out, int out_size);

/* odv_datetime.c */
int decode_oracle_date(const unsigned char *buf, int len, char *out, int out_size, int fmt, const char *custom_fmt);
int decode_oracle_timestamp(const unsigned char *buf, int len, char *out, int out_size, int fmt, const char *custom_fmt, int ts_precision);
int decode_binary_float(const unsigned char *buf, char *out, int out_size);
int decode_binary_double(const unsigned char *buf, char *out, int out_size);
int decode_interval_ym(const unsigned char *buf, int len, char *out, int out_size);
int decode_interval_ds(const unsigned char *buf, int len, char *out, int out_size);

/* odv_charset.c */
int convert_charset(const char *src, int src_len, int src_cs,
                    char *dst, int dst_size, int dst_cs, int *out_len);

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
