/*****************************************************************************
    Open DUMP Viewer for Oracle database

    odv_csv.c
    CSV file output

    Exports parsed dump data to CSV format.
    - Header row with column names
    - RFC 4180 compliant escaping (double-quote enclosure for commas,
      newlines, and double-quotes within values)

    Copyright (C) 2026 YANAI Taketo
 *****************************************************************************/

#include "odv_types.h"
#include <stdio.h>

/*---------------------------------------------------------------------------
    csv_needs_escape

    Returns 1 if the value (len bytes) contains characters requiring CSV
    escaping. Values are read by their length: a value may contain 0x00
    bytes, which used to end it (M-22).
 ---------------------------------------------------------------------------*/
static int csv_needs_escape(const char *val, size_t len, char delimiter)
{
    size_t i;
    for (i = 0; i < len; i++) {
        if (val[i] == delimiter || val[i] == '"' || val[i] == '\n' || val[i] == '\r')
            return 1;
    }
    return 0;
}

/*---------------------------------------------------------------------------
    csv_write_escaped

    Writes a value (len bytes) to the file, wrapping in double-quotes and
    escaping embedded double-quotes (by doubling them) when necessary.
 ---------------------------------------------------------------------------*/
static void csv_write_escaped(FILE *fp, const char *val, size_t len, char delimiter)
{
    size_t i, run = 0;

    if (!csv_needs_escape(val, len, delimiter)) {
        fwrite(val, 1, len, fp);
        return;
    }

    fputc('"', fp);
    for (i = 0; i < len; i++) {
        if (val[i] == '"') {
            /* the run up to and including this quote, then the second quote */
            fwrite(val + i - run, 1, run + 1, fp);
            fputc('"', fp);
            run = 0;
        } else {
            run++;
        }
    }
    if (run) fwrite(val + len - run, 1, run, fp);
    fputc('"', fp);
}

static void csv_write_name(FILE *fp, const char *name, char delimiter)
{
    csv_write_escaped(fp, name, strlen(name), delimiter);
}

/*---------------------------------------------------------------------------
    CSV export context (used as row callback user_data)
 ---------------------------------------------------------------------------*/
typedef struct {
    FILE *fp;
    int64_t row_count;
    const char *target_table;   /* NULL = export all tables */
    const char *target_schema;
    int header_written;
    ODV_SESSION *session;       /* For accessing column type info */
    int write_header;           /* 1=output column name header row */
    int write_types;            /* 1=output column type row after header */
    char delimiter;             /* Field delimiter character (default ',') */
} CSV_CONTEXT;

/*---------------------------------------------------------------------------
    csv_row_callback

    Called for each row during dump parsing.
    Writes the row to the CSV file.
 ---------------------------------------------------------------------------*/
static void ODV_CALL csv_row_callback(
    const char *schema, const char *table,
    int col_count, const char **col_names, const char **col_values,
    const int *col_lens, void *user_data)
{
    CSV_CONTEXT *ctx = (CSV_CONTEXT *)user_data;
    int i;
    (void)schema;

    if (!ctx || !ctx->fp) return;

    /* Filter by table name if specified */
    if (ctx->target_table && ctx->target_table[0] != '\0') {
        if (strcmp(table, ctx->target_table) != 0) return;
    }

    /* Write header row on first data row */
    if (!ctx->header_written) {
        ctx->header_written = 1;

        if (ctx->write_header) {
            for (i = 0; i < col_count; i++) {
                if (i > 0) fputc(ctx->delimiter, ctx->fp);
                csv_write_name(ctx->fp, col_names[i], ctx->delimiter);
            }
            fputc('\n', ctx->fp);
        }

        /* Write column type row if requested */
        if (ctx->write_types && ctx->session) {
            for (i = 0; i < col_count; i++) {
                if (i > 0) fputc(ctx->delimiter, ctx->fp);
                if (i < ctx->session->table.col_count &&
                    ctx->session->table.columns[i].type_str[0]) {
                    csv_write_name(ctx->fp, ctx->session->table.columns[i].type_str,
                                   ctx->delimiter);
                }
            }
            fputc('\n', ctx->fp);
        }
    }

    /* Write data row. NULL and an empty value are both an empty field; a
       value is written whole by its length, 0x00 bytes included (M-22). */
    for (i = 0; i < col_count; i++) {
        if (i > 0) fputc(ctx->delimiter, ctx->fp);
        if (col_values[i] && col_lens[i] > 0) {
            csv_write_escaped(ctx->fp, col_values[i], (size_t)col_lens[i], ctx->delimiter);
        }
    }
    fputc('\n', ctx->fp);

    ctx->row_count++;

    /* Report progress periodically (every 100 rows) */
    if (ctx->session && ctx->session->progress_cb && (ctx->row_count % 100) == 0) {
        ctx->session->progress_cb(ctx->row_count, table, ctx->session->progress_ud);
    }
}

/*---------------------------------------------------------------------------
    write_csv_file

    Exports a table (or all tables) from the dump to a CSV file.
    Re-parses the dump using the row callback to stream data to the file.
 ---------------------------------------------------------------------------*/
int write_csv_file(ODV_SESSION *s, const char *table_name, const char *output_path)
{
    CSV_CONTEXT ctx;
    ODV_ROW_CALLBACK saved_cb;
    ODV_ROW_CALLBACK_EX saved_cb_ex;
    void *saved_ud;
    int rc;

    if (!s || !output_path) return ODV_ERROR_INVALID_ARG;

    /* Open output file (UTF-8, no BOM) */
    ctx.fp = odv_fopen_path(output_path, "wb");   /* UTF-8 path (M-26) */
    if (!ctx.fp) {
        odv_strcpy(s->last_error, "Cannot create CSV output file", ODV_MSG_LEN);
        return ODV_ERROR_FOPEN;
    }

    ctx.row_count = 0;
    ctx.target_table = table_name;
    ctx.target_schema = NULL;
    ctx.header_written = 0;
    ctx.session = s;
    ctx.write_header = s->csv_write_header;
    ctx.write_types = s->csv_write_types;
    ctx.delimiter = s->csv_delimiter ? s->csv_delimiter : ',';

    /* Save and replace row callback */
    saved_cb = s->row_cb;
    saved_cb_ex = s->row_cb_ex;
    saved_ud = s->row_ud;
    s->row_cb = NULL;
    s->row_cb_ex = csv_row_callback;
    s->row_ud = &ctx;

    /* Re-parse dump to stream rows */
    s->cancelled = 0;
    s->total_rows = 0;

    /* Auto-detect dump kind if not done */
    if (s->dump_type == DUMP_UNKNOWN) {
        rc = detect_dump_kind(s);
        if (rc != ODV_OK) {
            fclose(ctx.fp);
            s->row_cb = saved_cb;
            s->row_cb_ex = saved_cb_ex;
            s->row_ud = saved_ud;
            return rc;
        }
    }

    switch (s->dump_type) {
    case DUMP_EXPDP:
        rc = parse_expdp_dump(s, 0);
        break;
    case DUMP_EXPDP_COMPRESS:
        odv_strcpy(s->last_error, "Compressed EXPDP dumps are not supported", ODV_MSG_LEN);
        rc = ODV_ERROR_UNSUPPORTED;
        break;
    case DUMP_EXP:
    case DUMP_EXP_DIRECT:
        rc = parse_exp_dump(s, 0);
        break;
    default:
        rc = ODV_ERROR_FORMAT;
        break;
    }

    fclose(ctx.fp);

    /* Restore original callback */
    s->row_cb = saved_cb;
    s->row_cb_ex = saved_cb_ex;
    s->row_ud = saved_ud;

    return rc;
}
