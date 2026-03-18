/*****************************************************************************
    OraDB DUMP Viewer

    odv_api.h
    Public DLL API for the OraDB DUMP Parser

    Copyright (C) 2026 YANAI Taketo
 *****************************************************************************/

#ifndef ODV_API_H
#define ODV_API_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*---------------------------------------------------------------------------
    DLL Export Macro
 ---------------------------------------------------------------------------*/
#ifdef ODV_DLL_MODE
  #define ODV_API __declspec(dllexport)
#else
  #define ODV_API __declspec(dllimport)
#endif

/*---------------------------------------------------------------------------
    Opaque Handle
 ---------------------------------------------------------------------------*/
typedef struct _odv_session ODV_SESSION;

/*---------------------------------------------------------------------------
    Dump Type Constants (returned by odv_check_dump_kind)
 ---------------------------------------------------------------------------*/
#define ODV_DUMP_UNKNOWN          -1
#define ODV_DUMP_EXPDP             0
#define ODV_DUMP_EXPDP_COMPRESS    1
#define ODV_DUMP_EXP              10
#define ODV_DUMP_EXP_DIRECT       11

/*---------------------------------------------------------------------------
    Return Codes
 ---------------------------------------------------------------------------*/
#define ODV_OK                     0
#define ODV_ERR                   -1
#define ODV_ERR_CANCELLED       -200

/*---------------------------------------------------------------------------
    Callback Types
 ---------------------------------------------------------------------------*/

/* Row data delivery callback (called per row during parse) */
typedef void (__stdcall *ODV_ROW_CALLBACK)(
    const char *schema,
    const char *table,
    int col_count,
    const char **col_names,
    const char **col_values,
    void *user_data
);

/* Progress notification callback
   rows_processed: total rows processed so far
   current_table:  name of the table currently being parsed
   user_data:      user context pointer
   Note: Called when file-position percentage changes (0-100), at most 101 times.
         Use odv_get_progress_pct() after callback to get the current percentage. */
typedef void (__stdcall *ODV_PROGRESS_CALLBACK)(
    int64_t rows_processed,
    const char *current_table,
    void *user_data
);

/* Table discovery callback (called per table during list_tables)
   data_offset: file position of the table DDL, usable with odv_set_data_offset
   for fast seeking on subsequent parse_dump calls. */
typedef void (__stdcall *ODV_TABLE_CALLBACK)(
    const char *schema,
    const char *table,
    int col_count,
    const char **col_names,
    const char **col_types,
    const int *col_not_nulls,
    const char **col_defaults,
    int64_t row_count,
    int64_t data_offset,
    void *user_data
);

/*---------------------------------------------------------------------------
    Session Lifecycle
 ---------------------------------------------------------------------------*/

ODV_API int __stdcall odv_create_session(ODV_SESSION **session);
ODV_API int __stdcall odv_destroy_session(ODV_SESSION *session);

/*---------------------------------------------------------------------------
    Configuration
 ---------------------------------------------------------------------------*/

ODV_API int __stdcall odv_set_dump_file(ODV_SESSION *s, const char *path);
ODV_API int __stdcall odv_set_row_callback(ODV_SESSION *s, ODV_ROW_CALLBACK cb, void *user_data);
ODV_API int __stdcall odv_set_progress_callback(ODV_SESSION *s, ODV_PROGRESS_CALLBACK cb, void *user_data);
ODV_API int __stdcall odv_set_table_callback(ODV_SESSION *s, ODV_TABLE_CALLBACK cb, void *user_data);

/* Set table filter for selective parsing.
   schema/table names in UTF-8. DLL reverse-converts to dump charset for comparison.
   Pass NULL to clear filter and parse all tables. */
ODV_API int __stdcall odv_set_table_filter(ODV_SESSION *s, const char *schema, const char *table);

/* Set file seek offset for fast table access.
   Use the data_offset value from the table callback to skip DDL scan.
   Set to 0 to disable (default: scan from beginning). */
ODV_API int __stdcall odv_set_data_offset(ODV_SESSION *s, int64_t offset);

/* Set date format for export output.
   fmt: 0=YYYY/MM/DD HH:MI:SS, 1=YYYYMMDD, 2=YYYYMMDDHHMMSS, 3=Custom
   custom_fmt: format string for fmt=3 (tokens: YYYY,MM,DD,HH24,MI,SS)
               Pass NULL for non-custom formats. */
ODV_API int __stdcall odv_set_date_format(ODV_SESSION *s, int fmt, const char *custom_fmt);

/* Set CSV export options.
   write_header: 1=output column name header row (default), 0=skip
   write_types:  1=output column type row after header, 0=skip (default) */
ODV_API int __stdcall odv_set_csv_options(ODV_SESSION *s, int write_header, int write_types);

/* Set SQL export options.
   create_table: 1=output CREATE TABLE DDL before INSERTs, 0=skip (default) */
ODV_API int __stdcall odv_set_sql_options(ODV_SESSION *s, int create_table);

/* Set application version string (displayed in export comments).
   ver: UTF-8 version string e.g. "1.1.0". Pass NULL to clear. */
ODV_API int __stdcall odv_set_app_version(ODV_SESSION *s, const char *ver);

/*---------------------------------------------------------------------------
    Operations
 ---------------------------------------------------------------------------*/

/* Detect dump file format (EXP / EXPDP / compressed) */
ODV_API int __stdcall odv_check_dump_kind(ODV_SESSION *s, int *dump_type);

/* List all tables in the dump (fires table_callback per table) */
ODV_API int __stdcall odv_list_tables(ODV_SESSION *s);

/* Parse all data (fires row_callback per row, progress_callback periodically) */
ODV_API int __stdcall odv_parse_dump(ODV_SESSION *s);

/* Export a specific table to CSV file */
ODV_API int __stdcall odv_export_csv(ODV_SESSION *s, const char *table_name, const char *output_path);

/* Export a specific table to SQL INSERT statements
   dbms_type: 0=Oracle, 4=PostgreSQL, 5=MySQL, 6=SQL Server */
ODV_API int __stdcall odv_export_sql(ODV_SESSION *s, const char *table_name, const char *output_path, int dbms_type);

/* Extract LOB column data to individual files.
   schema/table: target table (UTF-8)
   lob_column:   name of the BLOB/CLOB/NCLOB column to extract
   output_dir:   directory to write LOB files to
   filename_col: column whose value is used as filename (NULL=sequential numbering)
   extension:    file extension (NULL="lob")
   data_offset:  DDL offset for fast seek (0=scan from beginning) */
ODV_API int __stdcall odv_extract_lob(
    ODV_SESSION *s,
    const char *schema, const char *table,
    const char *lob_column,
    const char *output_dir,
    const char *filename_col,
    const char *extension,
    int64_t data_offset);

/* Get number of LOB files written by the last odv_extract_lob call */
ODV_API int64_t __stdcall odv_get_lob_files_written(ODV_SESSION *s);

/* Request cancellation of a running operation */
ODV_API int __stdcall odv_cancel(ODV_SESSION *s);

/*---------------------------------------------------------------------------
    Utilities
 ---------------------------------------------------------------------------*/

/* Get DLL version string */
ODV_API const char * __stdcall odv_get_version(void);

/* Get last error message for the session */
ODV_API const char * __stdcall odv_get_last_error(ODV_SESSION *s);

/* Get current progress percentage (0-100), call from progress callback */
ODV_API int __stdcall odv_get_progress_pct(ODV_SESSION *s);

#ifdef __cplusplus
}
#endif

#endif /* ODV_API_H */
