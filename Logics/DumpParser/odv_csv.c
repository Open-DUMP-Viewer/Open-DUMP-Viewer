/*****************************************************************************
    OraDB DUMP Viewer

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

    Returns 1 if the value contains characters requiring CSV escaping.
 ---------------------------------------------------------------------------*/
static int csv_needs_escape(const char *val)
{
    while (*val) {
        if (*val == ',' || *val == '"' || *val == '\n' || *val == '\r')
            return 1;
        val++;
    }
    return 0;
}

/*---------------------------------------------------------------------------
    csv_write_escaped

    Writes a value to the file, wrapping in double-quotes and escaping
    embedded double-quotes (by doubling them) when necessary.
 ---------------------------------------------------------------------------*/
static void csv_write_escaped(FILE *fp, const char *val)
{
    if (!csv_needs_escape(val)) {
        fputs(val, fp);
        return;
    }

    fputc('"', fp);
    while (*val) {
        if (*val == '"') {
            fputc('"', fp);
            fputc('"', fp);
        } else {
            fputc(*val, fp);
        }
        val++;
    }
    fputc('"', fp);
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
} CSV_CONTEXT;

/*---------------------------------------------------------------------------
    csv_row_callback

    Called for each row during dump parsing.
    Writes the row to the CSV file.
 ---------------------------------------------------------------------------*/
static void __stdcall csv_row_callback(
    const char *schema, const char *table,
    int col_count, const char **col_names, const char **col_values,
    void *user_data)
{
    CSV_CONTEXT *ctx = (CSV_CONTEXT *)user_data;
    int i;

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
                if (i > 0) fputc(',', ctx->fp);
                csv_write_escaped(ctx->fp, col_names[i]);
            }
            fputc('\n', ctx->fp);
        }

        /* Write column type row if requested */
        if (ctx->write_types && ctx->session) {
            for (i = 0; i < col_count; i++) {
                if (i > 0) fputc(',', ctx->fp);
                if (i < ctx->session->table.col_count &&
                    ctx->session->table.columns[i].type_str[0]) {
                    csv_write_escaped(ctx->fp, ctx->session->table.columns[i].type_str);
                }
            }
            fputc('\n', ctx->fp);
        }
    }

    /* Write data row */
    for (i = 0; i < col_count; i++) {
        if (i > 0) fputc(',', ctx->fp);
        if (col_values[i] && col_values[i][0] != '\0') {
            csv_write_escaped(ctx->fp, col_values[i]);
        }
    }
    fputc('\n', ctx->fp);

    ctx->row_count++;
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
    void *saved_ud;
    int rc;

    if (!s || !output_path) return ODV_ERROR_INVALID_ARG;

    /* Open output file (UTF-8, no BOM) */
    ctx.fp = fopen(output_path, "wb");
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

    /* Save and replace row callback */
    saved_cb = s->row_cb;
    saved_ud = s->row_ud;
    s->row_cb = csv_row_callback;
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
            s->row_ud = saved_ud;
            return rc;
        }
    }

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
        rc = ODV_ERROR_FORMAT;
        break;
    }

    fclose(ctx.fp);

    /* Restore original callback */
    s->row_cb = saved_cb;
    s->row_ud = saved_ud;

    return rc;
}
