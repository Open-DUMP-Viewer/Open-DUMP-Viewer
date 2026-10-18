/*****************************************************************************
    Open DUMP Viewer for Oracle database

    odv_record.c
    Record buffer management and row delivery

    Copyright (C) 2026 YANAI Taketo
 *****************************************************************************/

#include "odv_types.h"

/*---------------------------------------------------------------------------
    Record buffer management
 ---------------------------------------------------------------------------*/

int init_record(ODV_RECORD *rec, int max_cols)
{
    if (!rec) return ODV_ERROR_INVALID_ARG;
    if (max_cols <= 0) max_cols = 256;

    rec->values = (ODV_VALUE *)calloc(max_cols, sizeof(ODV_VALUE));
    if (!rec->values) return ODV_ERROR_MALLOC;

    rec->max_columns = max_cols;
    rec->col_count = 0;
    return ODV_OK;
}

void free_record(ODV_RECORD *rec)
{
    int i;
    if (!rec || !rec->values) return;

    for (i = 0; i < rec->max_columns; i++) {
        if (rec->values[i].data) {
            free(rec->values[i].data);
            rec->values[i].data = NULL;
        }
    }
    free(rec->values);
    rec->values = NULL;
    rec->max_columns = 0;
    rec->col_count = 0;
}

void reset_record(ODV_RECORD *rec)
{
    int i;
    if (!rec || !rec->values) return;

    for (i = 0; i < rec->col_count; i++) {
        rec->values[i].is_null = 1;
        rec->values[i].data_len = 0;
        /* Keep buffer allocated for reuse */
    }
    rec->col_count = 0;
}

/*---------------------------------------------------------------------------
    Value helpers
 ---------------------------------------------------------------------------*/

int ensure_value_buf(ODV_VALUE *v, int needed)
{
    if (!v) return ODV_ERROR_INVALID_ARG;

    if (v->buf_size >= needed) return ODV_OK;

    /* Round up to next power-of-2-ish size for reuse efficiency */
    int new_size = (needed < 256) ? 256 : (needed + 255) & ~255;

    if (v->data) {
        unsigned char *p = (unsigned char *)realloc(v->data, new_size);
        if (!p) return ODV_ERROR_MALLOC;
        v->data = p;
    } else {
        v->data = (unsigned char *)malloc(new_size);
        if (!v->data) return ODV_ERROR_MALLOC;
    }
    v->buf_size = new_size;
    return ODV_OK;
}

int set_value_null(ODV_VALUE *v)
{
    if (!v) return ODV_ERROR_INVALID_ARG;
    v->is_null = 1;
    v->data_len = 0;
    v->type = COL_NULL;
    return ODV_OK;
}

int set_value_string(ODV_VALUE *v, const char *str, int len)
{
    int rc;
    if (!v) return ODV_ERROR_INVALID_ARG;

    if (!str || len <= 0) {
        v->is_null = 1;
        v->data_len = 0;
        return ODV_OK;
    }

    rc = ensure_value_buf(v, len + 1);
    if (rc != ODV_OK) return rc;

    memcpy(v->data, str, len);
    v->data[len] = '\0';
    v->data_len = len;
    v->is_null = 0;
    return ODV_OK;
}

/*---------------------------------------------------------------------------
    Charset-converted metadata cache
    Converted once per table (when table name changes), reused for all rows.

    Kept in the session (s->meta), not in a static: one static cache shared
    by every session let a parse running in another thread rename the rows
    of this one, so rows reached the callbacks under another dump's or
    another table's names (M-25). Two slots are filled in turn, and only
    for a delivered row, so the schema / table / column-name pointers the row
    callback receives change whenever the table of the row differs from the
    previous row's (callers may cache the names by pointer). The progress
    callback never moves the slots: its name for a table no row has been
    delivered for yet is converted into a buffer of its own.
 ---------------------------------------------------------------------------*/

static void convert_meta_string(const char *src, int src_cs, int dst_cs,
                                char *dst, int dst_size)
{
    if (src_cs != dst_cs && src_cs != CHARSET_UNKNOWN) {
        int out_len = 0;
        if (convert_charset(src, (int)strlen(src), src_cs,
                            dst, dst_size, dst_cs, &out_len) == ODV_OK) {
            dst[out_len] = '\0';
            return;
        }
    }
    /* Fallback: copy as-is */
    odv_strcpy(dst, src, dst_size - 1);
}

static ODV_META_CACHE *meta_get(ODV_SESSION *s)
{
    if (!s->meta)
        s->meta = (ODV_META_CACHE *)calloc(1, sizeof(ODV_META_CACHE));
    return s->meta;
}

/* 1 if the slot in use holds the current table's names */
static int meta_is_current(const ODV_META_CACHE *mc, const ODV_SESSION *s)
{
    return mc->valid &&
           mc->col_count == s->table.col_count &&
           strcmp(mc->src_schema, s->table.schema) == 0 &&
           strcmp(mc->src_name, s->table.name) == 0;
}

/* The names of the current table in the output charset for a row, or NULL
   when the cache cannot be allocated. */
static const ODV_META_SLOT *update_meta_cache(ODV_SESSION *s)
{
    ODV_META_CACHE *mc = meta_get(s);
    ODV_META_SLOT *sl;
    int i;

    if (!mc) return NULL;

    /* Check if cache is already valid for this table */
    if (meta_is_current(mc, s))
        return &mc->slot[mc->cur];  /* Already cached */

    /* A new table (or a new definition): fill the other slot */
    mc->cur ^= 1;
    sl = &mc->slot[mc->cur];

    /* Convert schema and table name */
    convert_meta_string(s->table.schema, s->dump_charset, s->out_charset,
                        sl->schema, sizeof(sl->schema));
    convert_meta_string(s->table.name, s->dump_charset, s->out_charset,
                        sl->name, sizeof(sl->name));

    /* Convert column names */
    for (i = 0; i < s->table.col_count && i < ODV_MAX_COLUMNS; i++) {
        convert_meta_string(s->table.columns[i].name, s->dump_charset, s->out_charset,
                            sl->col_names[i], sizeof(sl->col_names[i]));
    }

    /* Remember source for change detection */
    odv_strcpy(mc->src_schema, s->table.schema, ODV_OBJNAME_LEN);
    odv_strcpy(mc->src_name, s->table.name, ODV_OBJNAME_LEN);
    mc->col_count = s->table.col_count;
    mc->valid = 1;
    return sl;
}

/* The current table's name for the progress callback (the slots are left
   as they are, see above). */
static const char *progress_table_name(ODV_SESSION *s)
{
    ODV_META_CACHE *mc = meta_get(s);
    if (!mc) return s->table.name;
    if (meta_is_current(mc, s)) return mc->slot[mc->cur].name;
    convert_meta_string(s->table.name, s->dump_charset, s->out_charset,
                        mc->progress_name, sizeof(mc->progress_name));
    return mc->progress_name;
}

void invalidate_meta_cache(ODV_SESSION *s)
{
    if (s && s->meta) s->meta->valid = 0;
}

void free_meta_cache(ODV_SESSION *s)
{
    if (!s) return;
    free(s->meta);
    s->meta = NULL;
}

/*---------------------------------------------------------------------------
    Row delivery to VB.NET callback

    A value is handed over with its length (ODV_ROW_CALLBACK_EX): the decoded
    value is data[0..data_len), which may contain 0x00 bytes, and NULL is a
    length of -1. The old callback (ODV_ROW_CALLBACK) only has NUL-terminated
    strings, so it still gets "" for NULL and sees a value up to its first
    0x00; its readers took the first 0x00 as the end of the value and an
    empty string as NULL, so 'a'||chr(0)||'b' became 'a' and chr(0) NULL in
    the preview, CSV and SQL (M-22).
 ---------------------------------------------------------------------------*/

int deliver_row(ODV_SESSION *s)
{
    const char *col_names[ODV_MAX_COLUMNS];
    const char *col_values[ODV_MAX_COLUMNS];
    int col_lens[ODV_MAX_COLUMNS];
    const ODV_META_SLOT *meta;
    const char *schema, *table;
    int i, n;
    static const char empty_str[] = "";

    if (!s || (!s->row_cb && !s->row_cb_ex)) return ODV_OK;

    /* Ensure metadata is charset-converted for this table */
    meta = update_meta_cache(s);
    schema = meta ? meta->schema : s->table.schema;
    table  = meta ? meta->name : s->table.name;

    n = s->table.col_count;
    if (n > ODV_MAX_COLUMNS) n = ODV_MAX_COLUMNS;
    if (n > s->record.max_columns) n = s->record.max_columns;
    if (n < 0) n = 0;

    for (i = 0; i < n; i++) {
        const ODV_VALUE *v = &s->record.values[i];
        col_names[i] = meta ? meta->col_names[i] : s->table.columns[i].name;

        /* A value never set has no buffer (a record is calloc'ed, so is_null
           is 0 there): NULL, as before. Oracle omits trailing NULL columns of
           a row, and those slots may never have been written. */
        if (v->is_null || !v->data || v->data_len < 0) {
            col_values[i] = s->row_cb_ex ? NULL : empty_str;    /* NULL */
            col_lens[i] = -1;
        } else if (v->data_len == 0) {
            col_values[i] = empty_str;                          /* empty, not NULL */
            col_lens[i] = 0;
        } else {
            col_values[i] = (const char *)v->data;
            col_lens[i] = v->data_len;
        }
    }

    if (s->row_cb_ex)
        s->row_cb_ex(schema, table, n, col_names, col_values, col_lens, s->row_ud);
    else
        s->row_cb(schema, table, n, col_names, col_values, s->row_ud);

    s->total_rows++;

    return ODV_OK;
}

/*---------------------------------------------------------------------------
    File-position-based progress reporting with hysteresis.
    Only fires the callback when the percentage changes (0-100),
    reducing UI thread overhead from per-row to at most 101 calls.
 ---------------------------------------------------------------------------*/
void odv_report_progress(ODV_SESSION *s, ODV_VFILE *fp)
{
    int pct;

    if (!s || !s->progress_cb || s->dump_size <= 0) return;

    pct = (int)(odv_vtell(fp) * 100 / s->dump_size);
    if (pct > 100) pct = 100;

    /* Hysteresis: only fire when percentage actually changes */
    if (pct != s->last_progress_pct) {
        s->last_progress_pct = pct;
        /* Charset-converted table name */
        s->progress_cb(s->total_rows, progress_table_name(s), s->progress_ud);
    }
}
