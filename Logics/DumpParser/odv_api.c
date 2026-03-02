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
#define ODV_VERSION_STRING "1.0.0.0"

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
