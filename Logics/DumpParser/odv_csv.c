/*****************************************************************************
    Open DUMP Viewer for Oracle database

    odv_csv.c
    CSV file output

    Exports parsed dump data to CSV format.
    - Header row with column names
    - RFC 4180 compliant escaping (double-quote enclosure for commas,
      newlines, and double-quotes within values)
    - Records end with CRLF (RFC 4180), as do the lines of the notes file;
      a line break inside a value is written as it is (within the quotes)
    - The header (and type) row also for a table that has no rows (none in
      the dump, or none in the partition / rows asked for)
    - LOB / LONG values whole (not the 4096-byte preview the viewer shows)
    - A value the parser could not decode (a marker such as %VARRAY%) is
      written as an empty field, never as the value; each such field is listed
      in "<output>.notes.txt" next to the CSV file

    Copyright (C) 2026 YANAI Taketo
 *****************************************************************************/

#include "odv_types.h"
#include <stdio.h>
#include <string.h>

/*---------------------------------------------------------------------------
    csv_needs_escape

    Returns 1 if the value contains characters requiring CSV escaping.
 ---------------------------------------------------------------------------*/
static int csv_needs_escape(const char *val, size_t n, char delimiter)
{
    size_t i;
    for (i = 0; i < n; i++) {
        char c = val[i];
        if (c == delimiter || c == '"' || c == '\n' || c == '\r')
            return 1;
    }
    return 0;
}

/*---------------------------------------------------------------------------
    csv_write_escaped

    Writes a value of n bytes to the file, wrapping in double-quotes and
    escaping embedded double-quotes (by doubling them) when necessary.
 ---------------------------------------------------------------------------*/
static void csv_write_escaped(FILE *fp, const char *val, size_t n, char delimiter)
{
    size_t i, st = 0;

    if (!csv_needs_escape(val, n, delimiter)) {
        fwrite(val, 1, n, fp);
        return;
    }
    fputc('"', fp);
    for (i = 0; i < n; i++) {
        if (val[i] == '"') {
            fwrite(val + st, 1, i - st + 1, fp);   /* up to and including the quote */
            fputc('"', fp);
            st = i + 1;
        }
    }
    fwrite(val + st, 1, n - st, fp);
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
    char delimiter;             /* Field delimiter character (default ',') */
    char *notes;                /* fields written empty because the value is not whole */
    size_t notes_len, notes_cap;
    int64_t notes_count;
} CSV_CONTEXT;

/* The line end of the CSV records and of the notes file (RFC 4180) */
#define CSV_EOL "\r\n"

static void csv_note(CSV_CONTEXT *ctx, const char *table, const char *col, const char *why)
{
    char line[1024];
    int n = snprintf(line, sizeof(line) - 2, "row %lld of %s, column %s: %s; written as an empty field",
                     (long long)(ctx->row_count + 1), table, col, why);
    if (n <= 0) return;
    if ((size_t)n >= sizeof(line) - 2) n = (int)sizeof(line) - 3;   /* cut, still one line */
    memcpy(line + n, CSV_EOL, 2);
    n += 2;
    ctx->notes_count++;
    if (ctx->notes_count > 100000) return;          /* the count still says how many */
    if (ctx->notes_len + (size_t)n + 1 > ctx->notes_cap) {
        size_t nc = ctx->notes_cap ? ctx->notes_cap * 2 : 4096;
        char *np;
        while (nc < ctx->notes_len + (size_t)n + 1) nc *= 2;
        np = (char *)realloc(ctx->notes, nc);
        if (!np) return;
        ctx->notes = np;
        ctx->notes_cap = nc;
    }
    memcpy(ctx->notes + ctx->notes_len, line, (size_t)n);
    ctx->notes_len += (size_t)n;
    ctx->notes[ctx->notes_len] = '\0';
}

/* The header row (column names) and the type row, as set, once per file */
static void csv_write_header(CSV_CONTEXT *ctx, int col_count, const char **col_names)
{
    ODV_SESSION *s = ctx->session;
    int i;

    if (ctx->header_written) return;
    ctx->header_written = 1;

    if (ctx->write_header) {
        for (i = 0; i < col_count; i++) {
            if (i > 0) fputc(ctx->delimiter, ctx->fp);
            csv_write_escaped(ctx->fp, col_names[i], strlen(col_names[i]), ctx->delimiter);
        }
        fputs(CSV_EOL, ctx->fp);
    }

    /* Write column type row if requested */
    if (ctx->write_types && s) {
        for (i = 0; i < col_count; i++) {
            if (i > 0) fputc(ctx->delimiter, ctx->fp);
            if (i < s->table.col_count && s->table.columns[i].type_str[0]) {
                const char *t = s->table.columns[i].type_str;
                csv_write_escaped(ctx->fp, t, strlen(t), ctx->delimiter);
            }
        }
        fputs(CSV_EOL, ctx->fp);
    }
}

/*---------------------------------------------------------------------------
    csv_write_row

    One row. vals[i] / lens[i]: the value and its length in bytes (it may
    hold 0x00), lens[i] < 0 = NULL. NULL and an empty value are both an empty
    field.
 ---------------------------------------------------------------------------*/
static void csv_write_row(CSV_CONTEXT *ctx, const char *table, int col_count,
                          const char **col_names, const char **vals, const int *lens)
{
    ODV_SESSION *s = ctx->session;
    int i;

    /* Write header row on first data row */
    csv_write_header(ctx, col_count, col_names);

    /* Write data row */
    for (i = 0; i < col_count; i++) {
        int fl = 0;
        if (i > 0) fputc(ctx->delimiter, ctx->fp);
        if (s && i < s->record.col_count && i < s->record.max_columns)
            fl = s->record.values[i].flags;
        if (fl & ODV_VFLAG_MARKER) {
            char why[96];
            snprintf(why, sizeof(why), "the value could not be decoded (%.40s)",
                     vals[i] ? vals[i] : "");
            csv_note(ctx, table, col_names[i], why);
            continue;
        }
        if (fl & ODV_VFLAG_CUT) {
            csv_note(ctx, table, col_names[i], "the value is longer than the export keeps whole (512 MiB)");
            continue;
        }
        if (vals[i] && lens[i] > 0)
            csv_write_escaped(ctx->fp, vals[i], (size_t)lens[i], ctx->delimiter);
    }
    fputs(CSV_EOL, ctx->fp);

    ctx->row_count++;

    /* Report progress periodically (every 100 rows) */
    if (s && s->progress_cb && (ctx->row_count % 100) == 0) {
        s->progress_cb(ctx->row_count, table, s->progress_ud);
    }
}

/*---------------------------------------------------------------------------
    csv_row_callback

    Called for each row during dump parsing, with the values' lengths
    (ODV_ROW_CALLBACK_EX: a value may hold 0x00, NULL is a length of -1).
 ---------------------------------------------------------------------------*/
static void ODV_CALL csv_row_callback(
    const char *schema, const char *table,
    int col_count, const char **col_names, const char **col_values,
    const int *col_lens, void *user_data)
{
    CSV_CONTEXT *ctx = (CSV_CONTEXT *)user_data;
    (void)schema;

    if (!ctx || !ctx->fp) return;

    /* Filter by table name if specified */
    if (ctx->target_table && ctx->target_table[0] != '\0') {
        if (strcmp(table, ctx->target_table) != 0) return;
    }
    csv_write_row(ctx, table, col_count, col_names, col_values, col_lens);
}

/*---------------------------------------------------------------------------
    csv_table_callback

    Called once per table (and partition) the parse reads: a table of one
    table's export that has no rows still gets its header (and type) row.
    The parse also reports tables it reads past; only the target counts.
 ---------------------------------------------------------------------------*/
static void ODV_CALL csv_table_callback(
    const char *schema, const char *table,
    int col_count, const char **col_names, const char **col_types,
    const int *col_not_nulls, const char **col_defaults,
    int constraint_count, const char *constraints_json,
    int64_t row_count, int64_t data_offset, void *user_data)
{
    CSV_CONTEXT *ctx = (CSV_CONTEXT *)user_data;
    const ODV_SESSION *s;
    (void)col_types; (void)col_not_nulls; (void)col_defaults;
    (void)constraint_count; (void)constraints_json; (void)row_count; (void)data_offset;

    if (!ctx || !ctx->fp || ctx->header_written) return;
    if (!ctx->target_table || !ctx->target_table[0] || !table) return;
    if (strcmp(table, ctx->target_table) != 0) return;
    s = ctx->session;
    if (s && s->filter_active && s->filter_schema[0] && schema &&
        odv_stricmp(schema, s->filter_schema) != 0)
        return;
    if (col_count <= 0 || !col_names) return;
    csv_write_header(ctx, col_count, col_names);
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
    ODV_TABLE_CALLBACK saved_table_cb;
    void *saved_ud, *saved_table_ud;
    int rc;

    if (!s || !output_path) return ODV_ERROR_INVALID_ARG;

    memset(&ctx, 0, sizeof(ctx));
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
    saved_table_cb = s->table_cb;
    saved_table_ud = s->table_ud;
    s->row_cb = NULL;
    s->row_cb_ex = csv_row_callback;
    s->row_ud = &ctx;
    s->table_cb = csv_table_callback;   /* a table with no rows: its header */
    s->table_ud = &ctx;

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
            s->table_cb = saved_table_cb;
            s->table_ud = saved_table_ud;
            return rc;
        }
    }

    /* LOB / LONG values whole (M-12) */
    s->full_values = 1;

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

    s->full_values = 0;
    if (fclose(ctx.fp) != 0 && rc >= ODV_OK) {
        odv_strcpy(s->last_error, "Writing the CSV output file failed", ODV_MSG_LEN);
        rc = ODV_ERROR_FWRITE;
    }

    /* Fields written empty because their value is not whole: listed next to
       the CSV (not silently: the field alone cannot tell them from NULL) */
    if (ctx.notes_count > 0) {
        char npath[ODV_PATH_LEN + 16];
        FILE *nf;
        snprintf(npath, sizeof(npath), "%s.notes.txt", output_path);
        nf = odv_fopen_path(npath, "wb");
        if (nf) {
            if (ctx.notes) fwrite(ctx.notes, 1, ctx.notes_len, nf);
            if (ctx.notes_count > 100000)
                fprintf(nf, "... %lld fields in all" CSV_EOL, (long long)ctx.notes_count);
            fclose(nf);
        }
        if (rc == ODV_OK)
            snprintf(s->last_error, ODV_MSG_LEN,
                     "%lld field(s) could not be exported and were left empty (see %s)",
                     (long long)ctx.notes_count, npath);
    }
    free(ctx.notes);

    /* Restore original callback */
    s->row_cb = saved_cb;
    s->row_cb_ex = saved_cb_ex;
    s->row_ud = saved_ud;
    s->table_cb = saved_table_cb;
    s->table_ud = saved_table_ud;

    return rc;
}
