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
#include "odv_api.h"
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
    int         create_table;         /* 1=output DROP TABLE + CREATE TABLE DDL */
    int         create_index;         /* 1=output CREATE INDEX DDL */
    int         write_comments;       /* 1=output COMMENT ON DDL */
    int         write_inserts;        /* 1=output INSERT INTO statements */
    int         all_tables;           /* 1=export all tables (no filter) */
    int         indexes_written;      /* 1=CREATE INDEX already output for current table */
    int         comments_written;     /* 1=COMMENT ON already output for current table */
    char        last_schema[129];     /* Schema name from last row (for post-parse index output) */
    char        last_table[129];      /* Table name from last row */
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
    Helper: parse precision and scale from parenthesized portion
    e.g. "(10,2)" → prec=10, scale=2;  "(126)" → prec=126, scale=0
    Returns 1 if parsed successfully, 0 otherwise.
 ---------------------------------------------------------------------------*/
static int parse_number_prec_scale(const char *paren, int *prec, int *scale)
{
    *prec = 0;
    *scale = 0;
    if (!paren || *paren != '(') return 0;

    /* Skip spaces after '(' and parse first number with optional sign */
    const char *p = paren + 1;
    while (*p == ' ') p++;
    *prec = atoi(p);

    /* Look for comma (scale) */
    const char *comma = strchr(p, ',');
    if (comma) {
        const char *q = comma + 1;
        while (*q == ' ') q++;
        *scale = atoi(q);
    }
    return 1;
}

/*---------------------------------------------------------------------------
    map_oracle_to_target_type

    Maps an Oracle type string to the equivalent type for the target DBMS.
    Handles:
    - NUMBER(p>38) → FLOAT equivalent (EXPDP encodes FLOAT as NUMBER)
    - NUMBER(p, negative_scale) → adjusted NUMERIC for non-Oracle targets
    - FLOAT(n) → binary precision to IEEE float mapping
    - INTERVAL, LONG RAW, XMLTYPE, ROWID, BFILE, etc.
 ---------------------------------------------------------------------------*/
static const char *map_oracle_to_target_type(const char *oracle_type, int dbms)
{
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

    /* --- Oracle: return as-is, but fix NUMBER(>38) back to FLOAT --- */
    if (dbms == DBMS_ORACLE) {
        if (strcmp(base, "NUMBER") == 0 && paren) {
            int prec, scale;
            parse_number_prec_scale(paren, &prec, &scale);
            if (prec > 38 && scale == 0) {
                /* This was originally FLOAT(n) encoded as NUMBER(n) by EXPDP */
                snprintf(result, sizeof(result), "FLOAT(%d)", prec);
                return result;
            }
        }
        snprintf(result, sizeof(result), "%s", oracle_type);
        return result;
    }

    /* --- Helper variables for NUMBER precision/scale --- */
    int prec = 0, scale = 0;
    int has_prec = 0;
    if (strcmp(base, "NUMBER") == 0 && paren) {
        has_prec = parse_number_prec_scale(paren, &prec, &scale);
    }

    /* =================================================================
       PostgreSQL
       ================================================================= */
    if (dbms == DBMS_POSTGRES) {
        /* VARCHAR2 / NVARCHAR2 → VARCHAR */
        if (strcmp(base, "VARCHAR2") == 0 || strcmp(base, "NVARCHAR2") == 0) {
            if (paren) { snprintf(result, sizeof(result), "VARCHAR%s", paren); return result; }
            return "VARCHAR(255)";
        }
        /* NUMBER */
        if (strcmp(base, "NUMBER") == 0) {
            if (has_prec && prec > 38 && scale == 0) return "DOUBLE PRECISION"; /* FLOAT */
            if (has_prec && scale < 0) {
                snprintf(result, sizeof(result), "NUMERIC(%d,0)", prec + (-scale));
                return result;
            }
            if (paren) { snprintf(result, sizeof(result), "NUMERIC%s", paren); return result; }
            return "NUMERIC";
        }
        /* FLOAT (from EXP parser) — binary precision */
        if (strcmp(base, "FLOAT") == 0) {
            if (paren) {
                int bp = atoi(paren + 1);
                return (bp <= 24) ? "REAL" : "DOUBLE PRECISION";
            }
            return "DOUBLE PRECISION";
        }
        /* DATE / TIMESTAMP */
        if (strcmp(base, "DATE") == 0) return "TIMESTAMP";
        if (strcmp(base, "TIMESTAMP") == 0) {
            /* Extract precision from paren, e.g. "(6)" → 6 */
            int ts_prec = paren ? atoi(paren + 1) : 6;
            if (strstr(oracle_type, "WITH TIME ZONE") ||
                strstr(oracle_type, "WITH LOCAL TIME ZONE")) {
                /* PG: WITH LOCAL TIME ZONE → WITH TIME ZONE */
                snprintf(result, sizeof(result), "TIMESTAMP(%d) WITH TIME ZONE", ts_prec);
            } else {
                snprintf(result, sizeof(result), "TIMESTAMP(%d)", ts_prec);
            }
            return result;
        }
        /* LOB / LONG */
        if (strcmp(base, "CLOB") == 0 || strcmp(base, "NCLOB") == 0 || strcmp(base, "LONG") == 0) return "TEXT";
        if (strcmp(base, "BLOB") == 0) return "BYTEA";
        if (strcmp(base, "RAW") == 0 || strcmp(base, "LONG RAW") == 0) return "BYTEA";
        /* BINARY_FLOAT / BINARY_DOUBLE */
        if (strcmp(base, "BINARY_FLOAT") == 0) return "REAL";
        if (strcmp(base, "BINARY_DOUBLE") == 0) return "DOUBLE PRECISION";
        /* CHAR / NCHAR */
        if (strcmp(base, "CHAR") == 0 || strcmp(base, "NCHAR") == 0) {
            if (paren) { snprintf(result, sizeof(result), "CHAR%s", paren); return result; }
            return "CHAR(1)";
        }
        /* INTERVAL */
        if (strcmp(base, "INTERVAL YEAR TO MONTH") == 0 ||
            strcmp(base, "INTERVAL") == 0) return "INTERVAL";
        if (strcmp(base, "INTERVAL DAY TO SECOND") == 0) return "INTERVAL";
        /* XMLTYPE / ROWID / BFILE / USER_DEFINED */
        if (strcmp(base, "XMLTYPE") == 0) return "XML";
        if (strcmp(base, "ROWID") == 0) return "VARCHAR(18)";
        if (strcmp(base, "BFILE") == 0) return "VARCHAR(530)";
        if (strcmp(base, "USER_DEFINED") == 0) return "TEXT";
        return "TEXT";
    }

    /* =================================================================
       MySQL
       ================================================================= */
    if (dbms == DBMS_MYSQL) {
        if (strcmp(base, "VARCHAR2") == 0 || strcmp(base, "NVARCHAR2") == 0) {
            if (paren) { snprintf(result, sizeof(result), "VARCHAR%s", paren); return result; }
            return "VARCHAR(255)";
        }
        if (strcmp(base, "NUMBER") == 0) {
            if (has_prec && prec > 38 && scale == 0) return "DOUBLE";
            if (has_prec && scale < 0) {
                snprintf(result, sizeof(result), "DECIMAL(%d,0)", prec + (-scale));
                return result;
            }
            if (paren) { snprintf(result, sizeof(result), "DECIMAL%s", paren); return result; }
            return "DECIMAL(38,10)";
        }
        if (strcmp(base, "FLOAT") == 0) {
            if (paren) {
                int bp = atoi(paren + 1);
                return (bp <= 24) ? "FLOAT" : "DOUBLE";
            }
            return "DOUBLE";
        }
        if (strcmp(base, "DATE") == 0) return "DATETIME";
        if (strcmp(base, "TIMESTAMP") == 0) {
            /* MySQL DATETIME supports (0-6) fractional seconds, max 6 */
            int ts_prec = paren ? atoi(paren + 1) : 0;
            if (ts_prec > 6) ts_prec = 6;
            if (ts_prec > 0) {
                snprintf(result, sizeof(result), "DATETIME(%d)", ts_prec);
            } else {
                snprintf(result, sizeof(result), "DATETIME");
            }
            return result;
        }
        if (strcmp(base, "CLOB") == 0 || strcmp(base, "NCLOB") == 0 || strcmp(base, "LONG") == 0) return "LONGTEXT";
        if (strcmp(base, "BLOB") == 0 || strcmp(base, "LONG RAW") == 0) return "LONGBLOB";
        if (strcmp(base, "RAW") == 0) return "VARBINARY(2000)";
        if (strcmp(base, "BINARY_FLOAT") == 0) return "FLOAT";
        if (strcmp(base, "BINARY_DOUBLE") == 0) return "DOUBLE";
        if (strcmp(base, "CHAR") == 0 || strcmp(base, "NCHAR") == 0) {
            if (paren) { snprintf(result, sizeof(result), "CHAR%s", paren); return result; }
            return "CHAR(1)";
        }
        if (strcmp(base, "INTERVAL YEAR TO MONTH") == 0 ||
            strcmp(base, "INTERVAL DAY TO SECOND") == 0 ||
            strcmp(base, "INTERVAL") == 0) return "VARCHAR(64)";
        if (strcmp(base, "XMLTYPE") == 0) return "LONGTEXT";
        if (strcmp(base, "ROWID") == 0) return "VARCHAR(18)";
        if (strcmp(base, "BFILE") == 0) return "VARCHAR(530)";
        if (strcmp(base, "USER_DEFINED") == 0) return "LONGTEXT";
        return "TEXT";
    }

    /* =================================================================
       SQL Server
       ================================================================= */
    if (dbms == DBMS_SQLSERVER) {
        if (strcmp(base, "VARCHAR2") == 0 || strcmp(base, "NVARCHAR2") == 0) {
            if (paren) { snprintf(result, sizeof(result), "NVARCHAR%s", paren); return result; }
            return "NVARCHAR(255)";
        }
        if (strcmp(base, "NUMBER") == 0) {
            if (has_prec && prec > 38 && scale == 0) return "FLOAT";  /* FLOAT(53) */
            if (has_prec && scale < 0) {
                snprintf(result, sizeof(result), "DECIMAL(%d,0)", prec + (-scale));
                return result;
            }
            if (paren) { snprintf(result, sizeof(result), "DECIMAL%s", paren); return result; }
            return "DECIMAL(38,10)";
        }
        if (strcmp(base, "FLOAT") == 0) {
            if (paren) {
                int bp = atoi(paren + 1);
                snprintf(result, sizeof(result), "FLOAT(%d)", bp <= 24 ? 24 : 53);
                return result;
            }
            return "FLOAT";
        }
        if (strcmp(base, "DATE") == 0) return "DATETIME2";
        if (strcmp(base, "TIMESTAMP") == 0) {
            int ts_prec = paren ? atoi(paren + 1) : 7;
            if (ts_prec > 7) ts_prec = 7; /* SQL Server max = 7 */
            if (strstr(oracle_type, "WITH TIME ZONE")) {
                snprintf(result, sizeof(result), "DATETIMEOFFSET(%d)", ts_prec);
            } else {
                snprintf(result, sizeof(result), "DATETIME2(%d)", ts_prec);
            }
            return result;
        }
        if (strcmp(base, "CLOB") == 0 || strcmp(base, "NCLOB") == 0 || strcmp(base, "LONG") == 0) return "NVARCHAR(MAX)";
        if (strcmp(base, "BLOB") == 0 || strcmp(base, "LONG RAW") == 0) return "VARBINARY(MAX)";
        if (strcmp(base, "RAW") == 0) {
            if (paren) { snprintf(result, sizeof(result), "VARBINARY%s", paren); return result; }
            return "VARBINARY(MAX)";
        }
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
        if (strcmp(base, "INTERVAL YEAR TO MONTH") == 0 ||
            strcmp(base, "INTERVAL DAY TO SECOND") == 0 ||
            strcmp(base, "INTERVAL") == 0) return "NVARCHAR(64)";
        if (strcmp(base, "XMLTYPE") == 0) return "XML";
        if (strcmp(base, "ROWID") == 0) return "NVARCHAR(18)";
        if (strcmp(base, "BFILE") == 0) return "NVARCHAR(530)";
        if (strcmp(base, "USER_DEFINED") == 0) return "NVARCHAR(MAX)";
        return "NVARCHAR(MAX)";
    }

    /* Unknown DBMS: return as-is */
    snprintf(result, sizeof(result), "%s", oracle_type);
    return result;
}

/*---------------------------------------------------------------------------
    has_column_comments

    Check if any column has a comment.
 ---------------------------------------------------------------------------*/
static int has_column_comments(ODV_SESSION *s)
{
    int i;
    if (!s) return 0;
    for (i = 0; i < s->table.col_count; i++) {
        if (s->table.columns[i].comment[0]) return 1;
    }
    return 0;
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

    /* DROP TABLE IF EXISTS */
    switch (dbms) {
    case DBMS_ORACLE:
        /* Oracle: PL/SQL anonymous block (IF EXISTS not supported before 23c) */
        fprintf(fp, "BEGIN EXECUTE IMMEDIATE 'DROP TABLE ");
        if (schema && schema[0] != '\0') fprintf(fp, "\"%s\".", schema);
        fprintf(fp, "\"%s\" CASCADE CONSTRAINTS PURGE'", table);
        fprintf(fp, "; EXCEPTION WHEN OTHERS THEN NULL; END;\n/\n\n");
        break;
    case DBMS_MYSQL:
        fprintf(fp, "DROP TABLE IF EXISTS ");
        if (schema && schema[0] != '\0') {
            sql_write_identifier(fp, schema, dbms);
            fputc('.', fp);
        }
        sql_write_identifier(fp, table, dbms);
        fprintf(fp, ";\n\n");
        break;
    case DBMS_SQLSERVER:
        fprintf(fp, "IF OBJECT_ID('");
        if (schema && schema[0] != '\0') fprintf(fp, "%s.", schema);
        fprintf(fp, "%s', 'U') IS NOT NULL DROP TABLE ", table);
        if (schema && schema[0] != '\0') {
            sql_write_identifier(fp, schema, dbms);
            fputc('.', fp);
        }
        sql_write_identifier(fp, table, dbms);
        fprintf(fp, ";\n\n");
        break;
    default: /* PostgreSQL */
        fprintf(fp, "DROP TABLE IF EXISTS ");
        if (schema && schema[0] != '\0') {
            sql_write_identifier(fp, schema, dbms);
            fputc('.', fp);
        }
        sql_write_identifier(fp, table, dbms);
        fprintf(fp, " CASCADE;\n\n");
        break;
    }

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
    write_indexes

    Outputs CREATE INDEX DDL for any CONSTRAINT_INDEX entries.
    Called after write_create_table in the SQL export flow.
 ---------------------------------------------------------------------------*/
static void write_indexes(SQL_CONTEXT *ctx, const char *schema,
                          const char *table, int dbms)
{
    FILE *fp = ctx->fp;
    int i, j;

    if (!ctx->session) return;

    for (i = 0; i < ctx->session->table.constraint_count; i++) {
        ODV_CONSTRAINT *c = &ctx->session->table.constraints[i];
        if (c->type != CONSTRAINT_INDEX) continue;

        fprintf(fp, "CREATE INDEX ");
        if (c->name[0]) {
            sql_write_identifier(fp, c->name, dbms);
            fputc(' ', fp);
        }
        fprintf(fp, "ON ");
        if (schema && schema[0] != '\0') {
            sql_write_identifier(fp, schema, dbms);
            fputc('.', fp);
        }
        sql_write_identifier(fp, table, dbms);
        fputc(' ', fp);

        /* Use index_expr if available (preserves function-based expressions),
           otherwise build from columns[] */
        if (c->index_expr[0] && dbms == DBMS_ORACLE) {
            /* Oracle: use raw expression as-is (preserves function-based indexes) */
            fputs(c->index_expr, fp);
        } else if (c->col_count > 0) {
            fputc('(', fp);
            for (j = 0; j < c->col_count; j++) {
                if (j > 0) fputs(", ", fp);
                sql_write_identifier(fp, c->columns[j], dbms);
            }
            fputc(')', fp);
        } else {
            /* Fallback: empty index (should not happen) */
            fputs("()", fp);
        }

        fprintf(fp, ";\n");
    }

    /* Add blank line after indexes if any were written */
    {
        int has_index = 0;
        for (i = 0; i < ctx->session->table.constraint_count; i++) {
            if (ctx->session->table.constraints[i].type == CONSTRAINT_INDEX) {
                has_index = 1;
                break;
            }
        }
        if (has_index) fputc('\n', fp);
    }
}

/*---------------------------------------------------------------------------
    write_comments

    Outputs COMMENT ON TABLE/COLUMN statements.
    Called after parse completes (comments may appear after data in EXP format).
    Oracle/PostgreSQL: COMMENT ON TABLE/COLUMN ... IS '...'
    MySQL: ALTER TABLE ... COMMENT = '...' (table), not standard for columns
    SQL Server: sp_addextendedproperty (non-standard, skip for now)
 ---------------------------------------------------------------------------*/
static void write_comments(SQL_CONTEXT *ctx, const char *schema,
                           const char *table, int dbms)
{
    FILE *fp = ctx->fp;
    int i;
    int has_any = 0;

    if (!ctx->session) return;

    /* Check if any comments exist */
    if (ctx->session->table.comment[0]) has_any = 1;
    if (!has_any) {
        for (i = 0; i < ctx->session->table.col_count; i++) {
            if (ctx->session->table.columns[i].comment[0]) { has_any = 1; break; }
        }
    }
    if (!has_any) return;

    /* Table comment */
    if (ctx->session->table.comment[0]) {
        switch (dbms) {
        case DBMS_MYSQL:
            fprintf(fp, "ALTER TABLE ");
            if (schema && schema[0]) { sql_write_identifier(fp, schema, dbms); fputc('.', fp); }
            sql_write_identifier(fp, table, dbms);
            fprintf(fp, " COMMENT = ");
            sql_write_string(fp, ctx->session->table.comment);
            fprintf(fp, ";\n");
            break;
        case DBMS_SQLSERVER:
            /* SQL Server uses sp_addextendedproperty — output as comment */
            fprintf(fp, "-- COMMENT ON TABLE %s: ", table);
            sql_write_string(fp, ctx->session->table.comment);
            fputc('\n', fp);
            break;
        default: /* Oracle, PostgreSQL */
            fprintf(fp, "COMMENT ON TABLE ");
            if (schema && schema[0]) { sql_write_identifier(fp, schema, dbms); fputc('.', fp); }
            sql_write_identifier(fp, table, dbms);
            fprintf(fp, " IS ");
            sql_write_string(fp, ctx->session->table.comment);
            fprintf(fp, ";\n");
            break;
        }
    }

    /* Column comments */
    for (i = 0; i < ctx->session->table.col_count; i++) {
        if (!ctx->session->table.columns[i].comment[0]) continue;

        switch (dbms) {
        case DBMS_MYSQL:
            /* MySQL: column comments set via ALTER TABLE MODIFY COLUMN ... COMMENT '...'
               This requires full column definition — too complex. Output as SQL comment. */
            fprintf(fp, "-- COMMENT ON COLUMN %s.", table);
            fprintf(fp, "%s: ", ctx->session->table.columns[i].name);
            sql_write_string(fp, ctx->session->table.columns[i].comment);
            fputc('\n', fp);
            break;
        case DBMS_SQLSERVER:
            fprintf(fp, "-- COMMENT ON COLUMN %s.", table);
            fprintf(fp, "%s: ", ctx->session->table.columns[i].name);
            sql_write_string(fp, ctx->session->table.columns[i].comment);
            fputc('\n', fp);
            break;
        default: /* Oracle, PostgreSQL */
            fprintf(fp, "COMMENT ON COLUMN ");
            if (schema && schema[0]) { sql_write_identifier(fp, schema, dbms); fputc('.', fp); }
            sql_write_identifier(fp, table, dbms);
            fputc('.', fp);
            sql_write_identifier(fp, ctx->session->table.columns[i].name, dbms);
            fprintf(fp, " IS ");
            sql_write_string(fp, ctx->session->table.columns[i].comment);
            fprintf(fp, ";\n");
            break;
        }
    }

    fputc('\n', fp);
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
    if (ctx->session && ctx->session->app_version[0]) {
        fprintf(fp, "-- Generated by OraDB DUMP Viewer v%s\n\n", ctx->session->app_version);
    } else {
        fprintf(fp, "-- Generated by OraDB DUMP Viewer\n\n");
    }

    /* Output CREATE TABLE DDL if requested.
       Note: write_indexes is called after parse completes (in write_sql_file)
       because EXP format has INDEX DDL after data records. */
    if (ctx->create_table) {
        write_create_table(ctx, schema, table, col_count, col_names, dbms);
    }

    /* Write INDEX/COMMENT eagerly if data is already available (EXPDP).
       For EXP, constraints arrive after data records — handled by table_cb. */
    ctx->indexes_written = 0;
    ctx->comments_written = 0;
    if (ctx->create_index && ctx->session->table.constraint_count > 0) {
        write_indexes(ctx, schema, table, dbms);
        ctx->indexes_written = 1;
    }
    if (ctx->write_comments && (ctx->session->table.comment[0] ||
        has_column_comments(ctx->session))) {
        write_comments(ctx, schema, table, dbms);
        ctx->comments_written = 1;
    }

    /* DDL-only mode: skip building INSERT prefix */
    if (!ctx->write_inserts) {
        ctx->insert_prefix[0] = '\0';
        return;
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
static void ODV_CALL sql_row_callback(
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

    /* Detect table change in all-table mode */
    if (ctx->all_tables && ctx->header_written && ctx->last_table[0] &&
        strcmp(table, ctx->last_table) != 0) {
        fprintf(ctx->fp, "\n");
        ctx->header_written = 0;
        ctx->insert_prefix[0] = '\0';
    }

    /* Build INSERT prefix / DDL header on first row of each table */
    if (!ctx->header_written) {
        build_insert_prefix(ctx, schema, table, col_count, col_names, ctx->dbms_type);
    }

    /* Remember schema/table for post-parse index output */
    if (schema) odv_strcpy(ctx->last_schema, schema, 128);
    if (table) odv_strcpy(ctx->last_table, table, 128);

    /* DDL-only mode: skip INSERT output */
    if (!ctx->write_inserts) return;

    /* Write INSERT statement */
    fputs(ctx->insert_prefix, ctx->fp);

    for (i = 0; i < col_count; i++) {
        if (i > 0) fputs(", ", ctx->fp);

        if (!col_values[i] || col_values[i][0] == '\0') {
            fputs("NULL", ctx->fp);
        } else if (ctx->session && i < ctx->session->table.col_count &&
                   (ctx->session->table.columns[i].type == COL_BIN_FLOAT ||
                    ctx->session->table.columns[i].type == COL_BIN_DOUBLE) &&
                   (strcmp(col_values[i], "NaN") == 0 ||
                    strcmp(col_values[i], "Inf") == 0 ||
                    strcmp(col_values[i], "-Inf") == 0)) {
            /* Special IEEE 754 values: NaN, Inf, -Inf */
            int is_float = (ctx->session->table.columns[i].type == COL_BIN_FLOAT);
            const char *val = col_values[i];
            switch (ctx->dbms_type) {
            case DBMS_ORACLE:
                if (strcmp(val, "NaN") == 0)
                    fputs(is_float ? "BINARY_FLOAT_NAN" : "BINARY_DOUBLE_NAN", ctx->fp);
                else if (strcmp(val, "Inf") == 0)
                    fputs(is_float ? "BINARY_FLOAT_INFINITY" : "BINARY_DOUBLE_INFINITY", ctx->fp);
                else
                    fprintf(ctx->fp, "-%s", is_float ? "BINARY_FLOAT_INFINITY" : "BINARY_DOUBLE_INFINITY");
                break;
            case DBMS_POSTGRES:
                fprintf(ctx->fp, "'%s'::%s",
                    strcmp(val, "Inf") == 0 ? "Infinity" :
                    strcmp(val, "-Inf") == 0 ? "-Infinity" : val,
                    is_float ? "REAL" : "DOUBLE PRECISION");
                break;
            default:
                /* MySQL / SQL Server: no NaN/Inf support → NULL */
                fputs("NULL", ctx->fp);
                break;
            }
        } else if (ctx->session && i < ctx->session->table.col_count &&
                   (ctx->session->table.columns[i].type == COL_DATE ||
                    ctx->session->table.columns[i].type == COL_TIMESTAMP ||
                    ctx->session->table.columns[i].type == COL_TIMESTAMP_TZ ||
                    ctx->session->table.columns[i].type == COL_TIMESTAMP_LTZ)) {
            /* Date/Timestamp: DBMS-specific formatting */
            switch (ctx->dbms_type) {
            case DBMS_ORACLE: {
                int is_ts = (ctx->session->table.columns[i].type != COL_DATE);
                if (is_ts) {
                    /* TIMESTAMP: use TO_TIMESTAMP with fractional seconds */
                    fprintf(ctx->fp, "TO_TIMESTAMP(");
                    sql_write_string(ctx->fp, col_values[i]);
                    fprintf(ctx->fp, ", 'YYYY/MM/DD HH24:MI:SS.FF')");
                } else {
                    /* DATE: use TO_DATE */
                    const char *ora_fmt;
                    switch (ctx->session->date_format) {
                    case DATE_FMT_COMPACT: ora_fmt = "YYYYMMDD"; break;
                    case DATE_FMT_FULL:    ora_fmt = "YYYYMMDDHH24MISS"; break;
                    default:               ora_fmt = "YYYY/MM/DD HH24:MI:SS"; break;
                    }
                    fprintf(ctx->fp, "TO_DATE(");
                    sql_write_string(ctx->fp, col_values[i]);
                    fprintf(ctx->fp, ", '%s')", ora_fmt);
                }
                break;
            }
            default:
                sql_write_string(ctx->fp, col_values[i]);
                break;
            }
        } else if (ctx->session && i < ctx->session->table.col_count &&
                   (ctx->session->table.columns[i].type == COL_RAW ||
                    ctx->session->table.columns[i].type == COL_LONG_RAW)) {
            /* RAW: DBMS-specific hex format */
            const char *val = col_values[i];
            /* Skip "0x" prefix if present */
            if (val[0] == '0' && (val[1] == 'x' || val[1] == 'X')) val += 2;
            switch (ctx->dbms_type) {
            case DBMS_ORACLE:
                fprintf(ctx->fp, "HEXTORAW('%s')", val);
                break;
            case DBMS_POSTGRES:
                fprintf(ctx->fp, "'\\x%s'::BYTEA", val);
                break;
            case DBMS_MYSQL:
                fprintf(ctx->fp, "X'%s'", val);
                break;
            default: /* SQL Server */
                fprintf(ctx->fp, "0x%s", val);
                break;
            }
        } else if (is_numeric_value(col_values[i])) {
            fputs(col_values[i], ctx->fp);
        } else {
            sql_write_string(ctx->fp, col_values[i]);
        }
    }

    fputs(");\n", ctx->fp);
    ctx->row_count++;

    /* Report progress periodically (every 100 rows) */
    if (ctx->session && ctx->session->progress_cb && (ctx->row_count % 100) == 0) {
        ctx->session->progress_cb(ctx->row_count, table, ctx->session->progress_ud);
    }
}

/*---------------------------------------------------------------------------
    sql_table_callback

    Called once per table (including zero-row tables) during all-table export.
    Ensures DDL is emitted for tables that had no data rows, and flushes
    INDEX/COMMENT for the just-completed table.
 ---------------------------------------------------------------------------*/
static void ODV_CALL sql_table_callback(
    const char *schema, const char *table,
    int col_count, const char **col_names, const char **col_types,
    const int *col_not_nulls, const char **col_defaults,
    int constraint_count, const char *constraints_json,
    int64_t row_count, int64_t data_offset, void *user_data)
{
    SQL_CONTEXT *ctx = (SQL_CONTEXT *)user_data;
    if (!ctx || !ctx->fp) return;

    (void)col_types; (void)col_not_nulls; (void)col_defaults;
    (void)constraint_count; (void)constraints_json;
    (void)row_count; (void)data_offset;

    /* Filter by table name if specified */
    if (ctx->target_table && ctx->target_table[0] != '\0') {
        if (strcmp(table, ctx->target_table) != 0) return;
    }

    /* Table boundary separator */
    if (ctx->header_written && ctx->last_table[0] &&
        strcmp(table, ctx->last_table) != 0) {
        fprintf(ctx->fp, "\n");
        ctx->header_written = 0;
        ctx->insert_prefix[0] = '\0';
    }

    /* Write DDL header for zero-row tables (row_callback never fires for them). */
    if (!ctx->header_written) {
        build_insert_prefix(ctx, schema, table, col_count, col_names, ctx->dbms_type);
    }

    /* For EXP format: INDEX/COMMENT DDL arrives after data records.
       At table_cb time, s->table still holds THIS table's data, so
       flush any INDEX/COMMENT that wasn't output during build_insert_prefix. */
    if (ctx->header_written) {
        if (ctx->create_index && !ctx->indexes_written &&
            ctx->session->table.constraint_count > 0) {
            write_indexes(ctx, schema, table, ctx->dbms_type);
            ctx->indexes_written = 1;
        }
        if (ctx->write_comments && !ctx->comments_written &&
            (ctx->session->table.comment[0] || has_column_comments(ctx->session))) {
            write_comments(ctx, schema, table, ctx->dbms_type);
            ctx->comments_written = 1;
        }
    }

    if (schema) odv_strcpy(ctx->last_schema, schema, 128);
    if (table) odv_strcpy(ctx->last_table, table, 128);
}

/*---------------------------------------------------------------------------
    write_sql_file

    Exports a table to SQL INSERT statements.
 ---------------------------------------------------------------------------*/
int write_sql_file(ODV_SESSION *s, const char *table_name,
                   const char *output_path, int dbms_type)
{
    SQL_CONTEXT ctx;
    ODV_ROW_CALLBACK saved_row_cb;
    void *saved_row_ud;
    ODV_TABLE_CALLBACK saved_table_cb;
    void *saved_table_ud;
    int saved_filter_active;
    int64_t saved_seek_offset;
    int rc;
    int all_tables;

    if (!s || !output_path) return ODV_ERROR_INVALID_ARG;

    all_tables = (!table_name || table_name[0] == '\0');

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
    ctx.create_table   = (s->sql_flags & ODV_SQL_CREATE_TABLE) ? 1 : 0;
    ctx.create_index   = (s->sql_flags & ODV_SQL_CREATE_INDEX) ? 1 : 0;
    ctx.write_comments = (s->sql_flags & ODV_SQL_WRITE_COMMENTS) ? 1 : 0;
    ctx.write_inserts  = (s->sql_flags & ODV_SQL_WRITE_INSERTS) ? 1 : 0;
    ctx.all_tables = all_tables;
    ctx.last_schema[0] = '\0';
    ctx.last_table[0] = '\0';

    /* Save and replace callbacks */
    saved_row_cb = s->row_cb;
    saved_row_ud = s->row_ud;
    saved_table_cb = s->table_cb;
    saved_table_ud = s->table_ud;

    s->row_cb = sql_row_callback;
    s->row_ud = &ctx;

    /* Hook table_cb for zero-row table DDL and per-table INDEX/COMMENT flush */
    s->table_cb = sql_table_callback;
    s->table_ud = &ctx;

    if (all_tables) {
        saved_filter_active = s->filter_active;
        saved_seek_offset = s->seek_offset;
        s->filter_active = 0;
        s->seek_offset = 0;
    }

    /* Re-parse dump */
    s->cancelled = 0;
    s->total_rows = 0;

    /* Auto-detect dump kind if not done */
    if (s->dump_type == DUMP_UNKNOWN) {
        rc = detect_dump_kind(s);
        if (rc != ODV_OK) {
            fclose(ctx.fp);
            s->row_cb = saved_row_cb;
            s->row_ud = saved_row_ud;
            s->table_cb = saved_table_cb;
            s->table_ud = saved_table_ud;
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

    /* Restore original state */
    s->row_cb = saved_row_cb;
    s->row_ud = saved_row_ud;
    s->table_cb = saved_table_cb;
    s->table_ud = saved_table_ud;
    if (all_tables) {
        s->filter_active = saved_filter_active;
        s->seek_offset = saved_seek_offset;
    }

    return rc;
}
