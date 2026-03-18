/*****************************************************************************
    OraDB DUMP Viewer

    odv_api.c
    DLL exported functions - session management and operation dispatch

    Copyright (C) 2026 YANAI Taketo
 *****************************************************************************/

#include "odv_types.h"
#include "odv_api.h"

/*---------------------------------------------------------------------------
    Version
 ---------------------------------------------------------------------------*/
#define ODV_VERSION_STRING "2.3.0"

/*---------------------------------------------------------------------------
    Internal helpers
 ---------------------------------------------------------------------------*/

static void set_error(ODV_SESSION *s, const char *msg)
{
    if (s && msg) {
        odv_strcpy(s->last_error, msg, ODV_MSG_LEN);
    }
}

static void clear_session(ODV_SESSION *s)
{
    memset(s, 0, sizeof(ODV_SESSION));
    s->dump_type = DUMP_UNKNOWN;
    s->dump_charset = CHARSET_UTF8;
    s->out_charset = CHARSET_UTF8;
    s->last_progress_pct = -1;

    /* Export option defaults */
    s->date_format = DATE_FMT_SLASH;
    s->custom_date_format[0] = '\0';
    s->csv_write_header = 1;
    s->csv_write_types = 0;
    s->sql_create_table = 0;
}

/*---------------------------------------------------------------------------
    Session Lifecycle
 ---------------------------------------------------------------------------*/

ODV_API int __stdcall odv_create_session(ODV_SESSION **session)
{
    ODV_SESSION *s;

    if (!session) return ODV_ERROR_INVALID_ARG;

    s = (ODV_SESSION *)calloc(1, sizeof(ODV_SESSION));
    if (!s) return ODV_ERROR_MALLOC;

    clear_session(s);

    /* Pre-allocate record buffer */
    if (init_record(&s->record, 256) != ODV_OK) {
        free(s);
        return ODV_ERROR_MALLOC;
    }

    *session = s;
    return ODV_OK;
}

ODV_API int __stdcall odv_destroy_session(ODV_SESSION *session)
{
    if (!session) return ODV_ERROR_INVALID_ARG;

    free_record(&session->record);

    /* Free LOB buffer if allocated */
    if (session->state.lob_buf) {
        free(session->state.lob_buf);
        session->state.lob_buf = NULL;
    }

    free(session);
    return ODV_OK;
}

/*---------------------------------------------------------------------------
    Configuration
 ---------------------------------------------------------------------------*/

ODV_API int __stdcall odv_set_dump_file(ODV_SESSION *s, const char *path)
{
    FILE *fp;

    if (!s || !path) return ODV_ERROR_INVALID_ARG;

    odv_strcpy(s->dump_path, path, ODV_PATH_LEN);

    /* Verify file is accessible and get size */
    fp = fopen(path, "rb");
    if (!fp) {
        set_error(s, "Cannot open dump file");
        return ODV_ERROR_FOPEN;
    }
    odv_fseek(fp, 0, SEEK_END);
    s->dump_size = odv_ftell(fp);
    fclose(fp);

    /* Reset state for new file */
    s->dump_type = DUMP_UNKNOWN;
    s->table_count = 0;
    s->total_rows = 0;
    s->cancelled = 0;

    return ODV_OK;
}

ODV_API int __stdcall odv_set_row_callback(ODV_SESSION *s, ODV_ROW_CALLBACK cb, void *user_data)
{
    if (!s) return ODV_ERROR_INVALID_ARG;
    s->row_cb = cb;
    s->row_ud = user_data;
    return ODV_OK;
}

ODV_API int __stdcall odv_set_progress_callback(ODV_SESSION *s, ODV_PROGRESS_CALLBACK cb, void *user_data)
{
    if (!s) return ODV_ERROR_INVALID_ARG;
    s->progress_cb = cb;
    s->progress_ud = user_data;
    return ODV_OK;
}

ODV_API int __stdcall odv_set_table_callback(ODV_SESSION *s, ODV_TABLE_CALLBACK cb, void *user_data)
{
    if (!s) return ODV_ERROR_INVALID_ARG;
    s->table_cb = cb;
    s->table_ud = user_data;
    return ODV_OK;
}

ODV_API int __stdcall odv_set_table_filter(ODV_SESSION *s, const char *schema, const char *table)
{
    if (!s) return ODV_ERROR_INVALID_ARG;

    if (!table || table[0] == '\0') {
        /* Clear filter */
        s->filter_active = 0;
        s->filter_schema[0] = '\0';
        s->filter_table[0] = '\0';
        return ODV_OK;
    }

    /* Store filter names in UTF-8 (will be reverse-converted after charset detection) */
    if (schema && schema[0]) {
        odv_strcpy(s->filter_schema, schema, ODV_OBJNAME_LEN);
    } else {
        s->filter_schema[0] = '\0';
    }
    odv_strcpy(s->filter_table, table, ODV_OBJNAME_LEN);
    s->filter_active = 1;
    s->pass_flg = 0;

    return ODV_OK;
}

ODV_API int __stdcall odv_set_data_offset(ODV_SESSION *s, int64_t offset)
{
    if (!s) return ODV_ERROR_INVALID_ARG;
    s->seek_offset = offset;
    return ODV_OK;
}

ODV_API int __stdcall odv_set_date_format(ODV_SESSION *s, int fmt, const char *custom_fmt)
{
    if (!s) return ODV_ERROR_INVALID_ARG;
    s->date_format = fmt;
    if (fmt == DATE_FMT_CUSTOM && custom_fmt) {
        odv_strcpy(s->custom_date_format, custom_fmt, sizeof(s->custom_date_format) - 1);
    } else {
        s->custom_date_format[0] = '\0';
    }
    return ODV_OK;
}

ODV_API int __stdcall odv_set_csv_options(ODV_SESSION *s, int write_header, int write_types)
{
    if (!s) return ODV_ERROR_INVALID_ARG;
    s->csv_write_header = write_header;
    s->csv_write_types = write_types;
    return ODV_OK;
}

ODV_API int __stdcall odv_set_sql_options(ODV_SESSION *s, int create_table)
{
    if (!s) return ODV_ERROR_INVALID_ARG;
    s->sql_create_table = create_table;
    return ODV_OK;
}

ODV_API int __stdcall odv_set_app_version(ODV_SESSION *s, const char *ver)
{
    if (!s) return ODV_ERROR_INVALID_ARG;
    if (ver) {
        odv_strcpy(s->app_version, ver, sizeof(s->app_version) - 1);
    } else {
        s->app_version[0] = '\0';
    }
    return ODV_OK;
}

/*---------------------------------------------------------------------------
    Operations
 ---------------------------------------------------------------------------*/

ODV_API int __stdcall odv_check_dump_kind(ODV_SESSION *s, int *dump_type)
{
    int rc;

    if (!s || !dump_type) return ODV_ERROR_INVALID_ARG;
    if (s->dump_path[0] == '\0') {
        set_error(s, "Dump file path not set");
        return ODV_ERROR_INVALID_ARG;
    }

    rc = detect_dump_kind(s);
    if (rc == ODV_OK) {
        *dump_type = s->dump_type;
    }
    return rc;
}

ODV_API int __stdcall odv_list_tables(ODV_SESSION *s)
{
    int rc;

    if (!s) return ODV_ERROR_INVALID_ARG;

    /* Auto-detect dump kind if not done */
    if (s->dump_type == DUMP_UNKNOWN) {
        rc = detect_dump_kind(s);
        if (rc != ODV_OK) return rc;
    }

    s->cancelled = 0;
    s->table_count = 0;

    switch (s->dump_type) {
    case DUMP_EXPDP:
    case DUMP_EXPDP_COMPRESS:
        return parse_expdp_dump(s, 1 /* list_only */);
    case DUMP_EXP:
    case DUMP_EXP_DIRECT:
        return parse_exp_dump(s, 1 /* list_only */);
    default:
        set_error(s, "Unknown or unsupported dump format");
        return ODV_ERROR_FORMAT;
    }
}

ODV_API int __stdcall odv_parse_dump(ODV_SESSION *s)
{
    int rc;

    if (!s) return ODV_ERROR_INVALID_ARG;

    /* Auto-detect dump kind if not done */
    if (s->dump_type == DUMP_UNKNOWN) {
        rc = detect_dump_kind(s);
        if (rc != ODV_OK) return rc;
    }

    s->cancelled = 0;
    s->total_rows = 0;

    switch (s->dump_type) {
    case DUMP_EXPDP:
    case DUMP_EXPDP_COMPRESS:
        return parse_expdp_dump(s, 0 /* full parse */);
    case DUMP_EXP:
    case DUMP_EXP_DIRECT:
        return parse_exp_dump(s, 0 /* full parse */);
    default:
        set_error(s, "Unknown or unsupported dump format");
        return ODV_ERROR_FORMAT;
    }
}

ODV_API int __stdcall odv_export_csv(ODV_SESSION *s, const char *table_name, const char *output_path)
{
    if (!s || !output_path) return ODV_ERROR_INVALID_ARG;
    return write_csv_file(s, table_name, output_path);
}

ODV_API int __stdcall odv_export_sql(ODV_SESSION *s, const char *table_name, const char *output_path, int dbms_type)
{
    if (!s || !output_path) return ODV_ERROR_INVALID_ARG;
    return write_sql_file(s, table_name, output_path, dbms_type);
}

/*---------------------------------------------------------------------------
    LOB Extraction Helpers
 ---------------------------------------------------------------------------*/

/* Check if the target LOB column exists in the current table.
   Sets s->lob_column_index to the LOB-only index (0-based). */
int odv_lob_check_column(ODV_SESSION *s)
{
    int i, lob_idx = 0;
    s->lob_column_index = -1;

    if (!s->lob_extract_mode || s->lob_column[0] == '\0')
        return ODV_OK;

    for (i = 0; i < s->table.col_count; i++) {
        int t = s->table.columns[i].type;
        if (t == COL_BLOB || t == COL_CLOB || t == COL_NCLOB) {
#ifdef WINDOWS
            if (_stricmp(s->table.columns[i].name, s->lob_column) == 0) {
#else
            if (strcasecmp(s->table.columns[i].name, s->lob_column) == 0) {
#endif
                s->lob_column_index = lob_idx;
                odv_lob_reset_buffer(s);
                return ODV_OK;
            }
            lob_idx++;
        }
    }

    set_error(s, "LOB column not found in table");
    return ODV_ERROR;
}

/* Accumulate LOB chunk data into the session buffer.
   Only accumulates if lob_col_idx matches the target column. */
int odv_lob_accumulate(ODV_SESSION *s, int lob_col_idx, const unsigned char *data, int len)
{
    if (!s->lob_extract_mode || s->lob_column_index < 0)
        return ODV_OK;
    if (lob_col_idx != s->lob_column_index)
        return ODV_OK;
    if (!data || len <= 0)
        return ODV_OK;

    /* Ensure buffer has space */
    if (s->state.lob_buf_len + len > s->state.lob_buf_alloc) {
        int new_alloc = s->state.lob_buf_len + len + 0x1000; /* +4KB headroom */
        unsigned char *new_buf = (unsigned char *)realloc(s->state.lob_buf, new_alloc);
        if (!new_buf) return ODV_ERROR_MALLOC;
        s->state.lob_buf = new_buf;
        s->state.lob_buf_alloc = new_alloc;
    }

    memcpy(s->state.lob_buf + s->state.lob_buf_len, data, len);
    s->state.lob_buf_len += len;
    return ODV_OK;
}

/* Write accumulated LOB buffer to a file and reset the buffer.
   Filename is based on lob_filename_col or sequential number. */
int odv_lob_write_file(ODV_SESSION *s)
{
    char path[ODV_PATH_LEN * 2 + 1];
    char fname[256];
    FILE *fpw;

    if (s->lob_column_index < 0)
        return ODV_OK;
    if (!s->state.lob_buf || s->state.lob_buf_len <= 0) {
        odv_lob_reset_buffer(s);
        return ODV_OK; /* NULL LOB — skip */
    }

    /* Build output path */
    if (s->lob_output_dir[0])
        snprintf(path, sizeof(path), "%s", s->lob_output_dir);
    else
        snprintf(path, sizeof(path), ".");

    /* Determine filename */
    fname[0] = '\0';
    if (s->lob_filename_col[0] != '\0') {
        /* Find the column value for filename */
        int i;
        for (i = 0; i < s->table.col_count; i++) {
#ifdef WINDOWS
            if (_stricmp(s->table.columns[i].name, s->lob_filename_col) == 0) {
#else
            if (strcasecmp(s->table.columns[i].name, s->lob_filename_col) == 0) {
#endif
                if (i < s->record.col_count && !s->record.values[i].is_null &&
                    s->record.values[i].data_len > 0 && s->record.values[i].data_len < (int)sizeof(fname) - 1) {
                    memcpy(fname, s->record.values[i].data, s->record.values[i].data_len);
                    fname[s->record.values[i].data_len] = '\0';
                }
                break;
            }
        }
    }
    if (fname[0] == '\0') {
        /* Sequential numbering */
        snprintf(fname, sizeof(fname), "%lld", (long long)(s->lob_files_written + 1));
    }

    /* Append path separator + filename + extension (safe construction) */
    {
        int plen = (int)strlen(path);
        int remain = (int)sizeof(path) - plen;
        const char *ext = s->lob_extension[0] ? s->lob_extension : "lob";
        const char *sep = "";

        if (plen > 0 && path[plen - 1] != '\\' && path[plen - 1] != '/')
#ifdef WINDOWS
            sep = "\\";
#else
            sep = "/";
#endif

        if (remain < (int)(strlen(sep) + strlen(fname) + 1 + strlen(ext) + 1)) {
            set_error(s, "LOB output path too long");
            odv_lob_reset_buffer(s);
            return ODV_ERROR_BUFFER_OVER;
        }
        snprintf(path + plen, remain, "%s%s.%s", sep, fname, ext);
    }

    /* Write binary */
    fpw = fopen(path, "wb");
    if (!fpw) {
        set_error(s, "Cannot create LOB output file");
        odv_lob_reset_buffer(s);
        return ODV_ERROR_FOPEN;
    }
    if ((int)fwrite(s->state.lob_buf, 1, s->state.lob_buf_len, fpw) != s->state.lob_buf_len) {
        fclose(fpw);
        set_error(s, "LOB file write error");
        odv_lob_reset_buffer(s);
        return ODV_ERROR_FWRITE;
    }
    fclose(fpw);

    s->lob_files_written++;
    odv_lob_reset_buffer(s);
    return ODV_OK;
}

void odv_lob_reset_buffer(ODV_SESSION *s)
{
    if (s->state.lob_buf) {
        free(s->state.lob_buf);
        s->state.lob_buf = NULL;
    }
    s->state.lob_buf_len = 0;
    s->state.lob_buf_alloc = 0;
}

/*---------------------------------------------------------------------------
    LOB Extraction API
 ---------------------------------------------------------------------------*/

ODV_API int __stdcall odv_extract_lob(
    ODV_SESSION *s,
    const char *schema, const char *table,
    const char *lob_column,
    const char *output_dir,
    const char *filename_col,
    const char *extension,
    int64_t data_offset)
{
    int rc;

    if (!s || !table || !lob_column || !output_dir)
        return ODV_ERROR_INVALID_ARG;

    /* Configure LOB extraction */
    s->lob_extract_mode = 1;
    odv_strcpy(s->lob_column, lob_column, ODV_OBJNAME_LEN);
    odv_strcpy(s->lob_output_dir, output_dir, ODV_PATH_LEN);
    s->lob_column_index = -1;
    s->lob_files_written = 0;

    if (filename_col && filename_col[0])
        odv_strcpy(s->lob_filename_col, filename_col, ODV_OBJNAME_LEN);
    else
        s->lob_filename_col[0] = '\0';

    if (extension && extension[0])
        odv_strcpy(s->lob_extension, extension, sizeof(s->lob_extension) - 1);
    else
        odv_strcpy(s->lob_extension, "lob", sizeof(s->lob_extension) - 1);

    /* Auto-detect dump kind if not done */
    if (s->dump_type == DUMP_UNKNOWN) {
        rc = detect_dump_kind(s);
        if (rc != ODV_OK) goto cleanup;
    }

    /* Set table filter and offset */
    odv_set_table_filter(s, schema, table);
    odv_set_data_offset(s, data_offset);

    /* Reset state */
    s->cancelled = 0;
    s->total_rows = 0;
    odv_lob_reset_buffer(s);

    /* Run the parse (LOB accumulation happens inside parse_*_dump) */
    switch (s->dump_type) {
    case DUMP_EXPDP:
    case DUMP_EXPDP_COMPRESS:
        rc = parse_expdp_dump(s, 0);
        break;
    case DUMP_EXP:
    case DUMP_EXP_DIRECT:
        rc = parse_exp_dump(s, 0);
        break;
    default:
        set_error(s, "Unknown or unsupported dump format");
        rc = ODV_ERROR_FORMAT;
        break;
    }

cleanup:
    /* Always clean up LOB state regardless of success/failure */
    s->lob_extract_mode = 0;
    s->lob_column_index = -1;
    odv_lob_reset_buffer(s);

    return rc;
}

ODV_API int64_t __stdcall odv_get_lob_files_written(ODV_SESSION *s)
{
    if (!s) return 0;
    return s->lob_files_written;
}

ODV_API int __stdcall odv_cancel(ODV_SESSION *s)
{
    if (!s) return ODV_ERROR_INVALID_ARG;
    s->cancelled = 1;
    return ODV_OK;
}

/*---------------------------------------------------------------------------
    Utilities
 ---------------------------------------------------------------------------*/

ODV_API const char * __stdcall odv_get_version(void)
{
    return ODV_VERSION_STRING;
}

ODV_API const char * __stdcall odv_get_last_error(ODV_SESSION *s)
{
    if (!s) return "Invalid session";
    return s->last_error;
}

ODV_API int __stdcall odv_get_progress_pct(ODV_SESSION *s)
{
    if (!s) return 0;
    return s->last_progress_pct;
}
