/*****************************************************************************
    OraDB DUMP Viewer

    odv_sql.c
    SQL INSERT statement output

    Generates INSERT INTO statements for various DBMS targets:
    - Oracle:     Standard Oracle SQL syntax
    - PostgreSQL: Follows PG quoting conventions
    - MySQL:      Backtick identifiers
    - SQL Server: Bracket identifiers

    Copyright (C) 2026 YANAI Taketo
 *****************************************************************************/

#include "odv_types.h"
#include <stdio.h>
#include <string.h>

/*---------------------------------------------------------------------------
    SQL export context
 ---------------------------------------------------------------------------*/
typedef struct {
    FILE       *fp;
    int64_t     row_count;
    const char *target_table;
    int         dbms_type;
    int         header_written;
    char        insert_prefix[4096];  /* Cached INSERT INTO ... VALUES ( */
    ODV_SESSION *session;             /* For accessing column type info */
    int         create_table;         /* 1=output CREATE TABLE DDL */
} SQL_CONTEXT;

/*---------------------------------------------------------------------------
    Helper: write SQL-escaped string value
    Escapes single quotes by doubling them.
 ---------------------------------------------------------------------------*/
static void sql_write_string(FILE *fp, const char *val)
{
    fputc('\'', fp);
    while (*val) {
        if (*val == '\'') {
            fputc('\'', fp);
            fputc('\'', fp);
        } else if (*val == '\\' && 0) {
            /* MySQL needs backslash escaping, but we use standard SQL mode */
            fputc('\\', fp);
            fputc('\\', fp);
        } else {
            fputc(*val, fp);
        }
        val++;
    }
    fputc('\'', fp);
}

/*---------------------------------------------------------------------------
    Helper: quote an identifier for the target DBMS
 ---------------------------------------------------------------------------*/
static void sql_write_identifier(FILE *fp, const char *name, int dbms)
{
    switch (dbms) {
    case DBMS_MYSQL:
        fprintf(fp, "`%s`", name);
        break;
    case DBMS_SQLSERVER:
        fprintf(fp, "[%s]", name);
        break;
    case DBMS_ORACLE:
    case DBMS_POSTGRES:
    default:
        fprintf(fp, "\"%s\"", name);
        break;
    }
}

/*---------------------------------------------------------------------------
    Helper: check if a value looks numeric (no quoting needed)
 ---------------------------------------------------------------------------*/
static int is_numeric_value(const char *val)
{
    if (!val || !*val) return 0;
    if (*val == '-' || *val == '+') val++;
    if (!*val) return 0;

    while (*val) {
        if (*val == '.' || (*val >= '0' && *val <= '9')) {
            val++;
        } else {
            return 0;
        }
    }
    return 1;
}

/*---------------------------------------------------------------------------
    map_oracle_to_target_type

    Maps an Oracle type string to the equivalent type for the target DBMS.
 ---------------------------------------------------------------------------*/
static const char *map_oracle_to_target_type(const char *oracle_type, int dbms)
{
    /* Extract base name (before parenthesis) */
    static char result[256];
    char base[64];
    const char *paren;
    int i;

    if (!oracle_type || !oracle_type[0]) return "VARCHAR(255)";

    /* Copy and uppercase the base name */
    paren = strchr(oracle_type, '(');
    if (paren) {
        int len = (int)(paren - oracle_type);
        if (len > 63) len = 63;
        for (i = 0; i < len; i++) {
            base[i] = (oracle_type[i] >= 'a' && oracle_type[i] <= 'z')
                     ? oracle_type[i] - 32 : oracle_type[i];
        }
        base[len] = '\0';
        /* Trim trailing spaces */
        while (len > 0 && base[len-1] == ' ') base[--len] = '\0';
    } else {
        int len = (int)strlen(oracle_type);
        if (len > 63) len = 63;
        for (i = 0; i < len; i++) {
            base[i] = (oracle_type[i] >= 'a' && oracle_type[i] <= 'z')
                     ? oracle_type[i] - 32 : oracle_type[i];
        }
        base[len] = '\0';
        while (len > 0 && base[len-1] == ' ') base[--len] = '\0';
    }

    if (dbms == DBMS_ORACLE) {
        /* Oracle: return as-is */
        snprintf(result, sizeof(result), "%s", oracle_type);
        return result;
    }

    if (dbms == DBMS_POSTGRES) {
        if (strcmp(base, "VARCHAR2") == 0 || strcmp(base, "NVARCHAR2") == 0) {
            if (paren) { snprintf(result, sizeof(result), "VARCHAR%s", paren); return result; }
            return "VARCHAR(255)";
        }
        if (strcmp(base, "NUMBER") == 0) {
            if (paren) { snprintf(result, sizeof(result), "NUMERIC%s", paren); return result; }
            return "NUMERIC";
        }
        if (strcmp(base, "DATE") == 0 || strcmp(base, "TIMESTAMP") == 0) return "TIMESTAMP";
        if (strcmp(base, "CLOB") == 0 || strcmp(base, "NCLOB") == 0 || strcmp(base, "LONG") == 0) return "TEXT";
        if (strcmp(base, "BLOB") == 0 || strcmp(base, "RAW") == 0) return "BYTEA";
        if (strcmp(base, "BINARY_FLOAT") == 0) return "REAL";
        if (strcmp(base, "BINARY_DOUBLE") == 0) return "DOUBLE PRECISION";
        if (strcmp(base, "CHAR") == 0 || strcmp(base, "NCHAR") == 0) {
            if (paren) { snprintf(result, sizeof(result), "CHAR%s", paren); return result; }
            return "CHAR(1)";
        }
        return "TEXT";
    }

    if (dbms == DBMS_MYSQL) {
        if (strcmp(base, "VARCHAR2") == 0 || strcmp(base, "NVARCHAR2") == 0) {
            if (paren) { snprintf(result, sizeof(result), "VARCHAR%s", paren); return result; }
            return "VARCHAR(255)";
        }
        if (strcmp(base, "NUMBER") == 0) {
            if (paren) { snprintf(result, sizeof(result), "DECIMAL%s", paren); return result; }
            return "DECIMAL(38,10)";
        }
        if (strcmp(base, "DATE") == 0 || strcmp(base, "TIMESTAMP") == 0) return "DATETIME";
        if (strcmp(base, "CLOB") == 0 || strcmp(base, "NCLOB") == 0 || strcmp(base, "LONG") == 0) return "LONGTEXT";
        if (strcmp(base, "BLOB") == 0 || strcmp(base, "RAW") == 0) return "LONGBLOB";
        if (strcmp(base, "BINARY_FLOAT") == 0) return "FLOAT";
        if (strcmp(base, "BINARY_DOUBLE") == 0) return "DOUBLE";
        if (strcmp(base, "CHAR") == 0 || strcmp(base, "NCHAR") == 0) {
            if (paren) { snprintf(result, sizeof(result), "CHAR%s", paren); return result; }
            return "CHAR(1)";
        }
        return "TEXT";
    }

    if (dbms == DBMS_SQLSERVER) {
        if (strcmp(base, "VARCHAR2") == 0 || strcmp(base, "NVARCHAR2") == 0) {
            if (paren) { snprintf(result, sizeof(result), "NVARCHAR%s", paren); return result; }
            return "NVARCHAR(255)";
        }
        if (strcmp(base, "NUMBER") == 0) {
            if (paren) { snprintf(result, sizeof(result), "DECIMAL%s", paren); return result; }
            return "DECIMAL(38,10)";
        }
        if (strcmp(base, "DATE") == 0 || strcmp(base, "TIMESTAMP") == 0) return "DATETIME2";
        if (strcmp(base, "CLOB") == 0 || strcmp(base, "NCLOB") == 0 || strcmp(base, "LONG") == 0) return "NVARCHAR(MAX)";
        if (strcmp(base, "BLOB") == 0 || strcmp(base, "RAW") == 0) return "VARBINARY(MAX)";
        if (strcmp(base, "BINARY_FLOAT") == 0) return "REAL";
        if (strcmp(base, "BINARY_DOUBLE") == 0) return "FLOAT";
        if (strcmp(base, "CHAR") == 0) {
            if (paren) { snprintf(result, sizeof(result), "NCHAR%s", paren); return result; }
            return "NCHAR(1)";
        }
        if (strcmp(base, "NCHAR") == 0) {
            snprintf(result, sizeof(result), "%s", oracle_type);
            return result;
        }
        return "NVARCHAR(MAX)";
    }

    /* Unknown DBMS: return as-is */
    snprintf(result, sizeof(result), "%s", oracle_type);
    return result;
}

/*---------------------------------------------------------------------------
    write_create_table

    Outputs CREATE TABLE DDL for the target DBMS.
 ---------------------------------------------------------------------------*/
static void write_create_table(SQL_CONTEXT *ctx, const char *schema,
                                const char *table, int col_count,
                                const char **col_names, int dbms)
{
    FILE *fp = ctx->fp;
    int i;

    if (!ctx->session) return;

    fprintf(fp, "CREATE TABLE ");

    if (schema && schema[0] != '\0') {
        sql_write_identifier(fp, schema, dbms);
        fputc('.', fp);
    }
    sql_write_identifier(fp, table, dbms);
    fprintf(fp, " (\n");

    for (i = 0; i < col_count; i++) {
        if (i > 0) fprintf(fp, ",\n");
        fprintf(fp, "    ");
        sql_write_identifier(fp, col_names[i], dbms);
        fputc(' ', fp);

        if (i < ctx->session->table.col_count &&
            ctx->session->table.columns[i].type_str[0]) {
            fputs(map_oracle_to_target_type(ctx->session->table.columns[i].type_str, dbms), fp);
        } else {
            fputs("VARCHAR(255)", fp);
        }
    }

    fprintf(fp, "\n);\n\n");
}

/*---------------------------------------------------------------------------
    build_insert_prefix

    Builds the "INSERT INTO schema.table (col1, col2, ...) VALUES (" prefix
    and caches it for reuse across rows.
 ---------------------------------------------------------------------------*/
static void build_insert_prefix(SQL_CONTEXT *ctx, const char *schema,
                                const char *table, int col_count,
                                const char **col_names, int dbms)
{
    FILE *fp = ctx->fp;
    int i;

    ctx->header_written = 1;

    /* Write CREATE TABLE comment at the top */
    fprintf(fp, "-- Table: ");
    if (schema && schema[0] != '\0') {
        fprintf(fp, "%s.", schema);
    }
    fprintf(fp, "%s\n", table);
    fprintf(fp, "-- Generated by OraDB DUMP Viewer\n\n");

    /* Output CREATE TABLE DDL if requested */
    if (ctx->create_table) {
        write_create_table(ctx, schema, table, col_count, col_names, dbms);
    }

    /* Build cached prefix string (safe position tracking) */
    {
        int pos = 0;
        int remain = (int)sizeof(ctx->insert_prefix);
        int n;

#define SAFE_APPEND(fmt, ...) do { \
    n = snprintf(ctx->insert_prefix + pos, remain, fmt, ##__VA_ARGS__); \
    if (n > 0 && n < remain) { pos += n; remain -= n; } \
    else { remain = 0; } \
} while(0)

        SAFE_APPEND("INSERT INTO ");

        if (schema && schema[0] != '\0' && remain > 0) {
            switch (dbms) {
            case DBMS_MYSQL:    SAFE_APPEND("`%s`.", schema); break;
            case DBMS_SQLSERVER: SAFE_APPEND("[%s].", schema); break;
            default:            SAFE_APPEND("\"%s\".", schema); break;
            }
        }

        if (remain > 0) {
            switch (dbms) {
            case DBMS_MYSQL:    SAFE_APPEND("`%s`", table); break;
            case DBMS_SQLSERVER: SAFE_APPEND("[%s]", table); break;
            default:            SAFE_APPEND("\"%s\"", table); break;
            }
        }

        SAFE_APPEND(" (");

        for (i = 0; i < col_count && remain > 0; i++) {
            if (i > 0) SAFE_APPEND(", ");
            switch (dbms) {
            case DBMS_MYSQL:    SAFE_APPEND("`%s`", col_names[i]); break;
            case DBMS_SQLSERVER: SAFE_APPEND("[%s]", col_names[i]); break;
            default:            SAFE_APPEND("\"%s\"", col_names[i]); break;
            }
        }

        SAFE_APPEND(") VALUES (");

#undef SAFE_APPEND

        ctx->insert_prefix[sizeof(ctx->insert_prefix) - 1] = '\0';
    }
}

/*---------------------------------------------------------------------------
    sql_row_callback
 ---------------------------------------------------------------------------*/
static void __stdcall sql_row_callback(
    const char *schema, const char *table,
    int col_count, const char **col_names, const char **col_values,
    void *user_data)
{
    SQL_CONTEXT *ctx = (SQL_CONTEXT *)user_data;
    int i;

    if (!ctx || !ctx->fp) return;

    /* Filter by table name if specified */
    if (ctx->target_table && ctx->target_table[0] != '\0') {
        if (strcmp(table, ctx->target_table) != 0) return;
    }

    /* Build INSERT prefix on first row */
    if (!ctx->header_written) {
        build_insert_prefix(ctx, schema, table, col_count, col_names, ctx->dbms_type);
    }

    /* Write INSERT statement */
    fputs(ctx->insert_prefix, ctx->fp);

    for (i = 0; i < col_count; i++) {
        if (i > 0) fputs(", ", ctx->fp);

        if (!col_values[i] || col_values[i][0] == '\0') {
            fputs("NULL", ctx->fp);
        } else if (is_numeric_value(col_values[i])) {
            fputs(col_values[i], ctx->fp);
        } else {
            sql_write_string(ctx->fp, col_values[i]);
        }
    }

    fputs(");\n", ctx->fp);
    ctx->row_count++;
}

/*---------------------------------------------------------------------------
    write_sql_file

    Exports a table to SQL INSERT statements.
 ---------------------------------------------------------------------------*/
int write_sql_file(ODV_SESSION *s, const char *table_name,
                   const char *output_path, int dbms_type)
{
    SQL_CONTEXT ctx;
    ODV_ROW_CALLBACK saved_cb;
    void *saved_ud;
    int rc;

    if (!s || !output_path) return ODV_ERROR_INVALID_ARG;

    ctx.fp = fopen(output_path, "wb");
    if (!ctx.fp) {
        odv_strcpy(s->last_error, "Cannot create SQL output file", ODV_MSG_LEN);
        return ODV_ERROR_FOPEN;
    }

    ctx.row_count = 0;
    ctx.target_table = table_name;
    ctx.dbms_type = dbms_type;
    ctx.header_written = 0;
    ctx.insert_prefix[0] = '\0';
    ctx.session = s;
    ctx.create_table = s->sql_create_table;

    /* Save and replace row callback */
    saved_cb = s->row_cb;
    saved_ud = s->row_ud;
    s->row_cb = sql_row_callback;
    s->row_ud = &ctx;

    /* Re-parse dump */
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
