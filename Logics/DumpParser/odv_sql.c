/*****************************************************************************
    Open DUMP Viewer for Oracle database

    odv_sql.c
    SQL export: DDL and INSERT statements for four target DBMS
    - Oracle:     SQL*Plus script
    - PostgreSQL: psql script
    - MySQL:      mysql client script
    - SQL Server: sqlcmd / SSMS script

    The column types and value literals follow one mapping shared with the VB
    export path (SqlExportLogic / ExportHelper): see the rules in the comments
    of map_type() and put_value(). In short:
      - every value is written whole (LOBs, LONG, long strings), split to fit
        Oracle's 4000-byte literal limit and SQL*Plus's line length;
      - a value the parser could not decode (a marker such as %VARRAY%, flagged
        ODV_VFLAG_MARKER) is never written as the value: NULL plus a
        "-- NOTE:" line saying why;
      - what a target type cannot hold (precision, range, time zone) is said
        in a "-- NOTE:" line instead of being lost silently;
      - dates are written in a fixed form whatever the display date format.

    Copyright (C) 2026 YANAI Taketo
 *****************************************************************************/

#include "odv_types.h"
#include "odv_api.h"
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>

/* Oracle output targets SQL*Plus, which rejects a line longer than 2,498
   bytes in 11.2 (4,998 in 21c / 23ai) with SP2-0027; lines are kept to
   SQL_LINE_MAX. A long literal is split into pieces of about SQL_PIECE
   rendered bytes, each on a line of its own. */
#define SQL_LINE_MAX      2400
#define SQL_PIECE         1000
#define ORA_STR_MAX       4000    /* text literal / VARCHAR2 in SQL (MAX_STRING_SIZE=STANDARD) */
#define ORA_RAW_MAX       2000    /* RAW in SQL */
#define ORA_PLSQL_LONG    32760   /* LONG / LONG RAW variable in PL/SQL */

/* A foreign key statement in a table's fk text starts with
   FK_MARK_START <sort key> FK_MARK_END (taken out in write_tail) */
#define FK_MARK_START     '\x01'
#define FK_MARK_END       '\x02'

/*---------------------------------------------------------------------------
    Growable text
 ---------------------------------------------------------------------------*/
typedef struct {
    char   *p;
    size_t  len, cap;
    size_t  line;       /* offset of the start of the current line */
    int     err;
} SQL_SB;

static void sb_putn(SQL_SB *b, const char *s, size_t n)
{
    size_t i;
    if (b->err || n == 0) return;
    if (b->len + n + 1 > b->cap) {
        size_t nc = b->cap ? b->cap : 256;
        char *np;
        while (nc < b->len + n + 1) {
            if (nc > ((size_t)-1) / 2) { b->err = 1; return; }
            nc *= 2;
        }
        np = (char *)realloc(b->p, nc);
        if (!np) { b->err = 1; return; }
        b->p = np;
        b->cap = nc;
    }
    memcpy(b->p + b->len, s, n);
    for (i = n; i > 0; i--) {
        if (s[i - 1] == '\n') { b->line = b->len + i; break; }
    }
    b->len += n;
    b->p[b->len] = '\0';
}

static void sb_puts(SQL_SB *b, const char *s) { sb_putn(b, s, strlen(s)); }
static void sb_putc(SQL_SB *b, char c)        { sb_putn(b, &c, 1); }

static void sb_printf(SQL_SB *b, const char *fmt, ...)
{
    char tmp[512];
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if ((size_t)n < sizeof(tmp)) { sb_putn(b, tmp, (size_t)n); return; }
    {
        char *big = (char *)malloc((size_t)n + 1);
        if (!big) { b->err = 1; return; }
        va_start(ap, fmt);
        vsnprintf(big, (size_t)n + 1, fmt, ap);
        va_end(ap);
        sb_putn(b, big, (size_t)n);
        free(big);
    }
}

static size_t sb_col(const SQL_SB *b) { return b->len - b->line; }
static void sb_clear(SQL_SB *b) { b->len = 0; b->line = 0; b->err = 0; if (b->p) b->p[0] = '\0'; }
static void sb_free(SQL_SB *b) { free(b->p); memset(b, 0, sizeof(*b)); }
static const char *sb_str(const SQL_SB *b) { return b->p ? b->p : ""; }

/*---------------------------------------------------------------------------
    Per-table bookkeeping: the DDL that follows all INSERTs (constraints,
    indexes, comments) is collected per table and written at the end of the
    file, then the foreign keys, so that a table's partitions written apart
    (expdp writes them in size order, other tables in between) do not get a
    second CREATE TABLE and every referenced table exists before its FKs.
 ---------------------------------------------------------------------------*/
/* One constraint / index of a table as written, with its place in the
   order (con_rank, then by name; see piece_cmp): the ones exp writes after
   the last table arrive apart and are put in the same order (write_tail) */
typedef struct {
    int     rank, gen, seq;
    char    name[ODV_OBJNAME_LEN + 1];
    char   *ddl;          /* the statement(s), "" when only a note */
    char   *note;         /* "-- NOTE:" lines, "" when none */
} SQL_CPIECE;

typedef struct {
    char   *key;          /* "schema\x1ftable" (output charset) */
    int     begun;        /* CREATE TABLE / INSERT header written */
    SQL_CPIECE *pc;       /* constraints / indexes of the table's DDL */
    int     npc, cpc;
    SQL_CPIECE *lpc;      /* those exp writes after the last table (M-17) */
    int     nlpc, clpc;
    SQL_SB  cm;           /* comments */
    SQL_SB  notes;        /* notes of the comments and of the table */
    SQL_SB  fk;           /* foreign keys */
    SQL_SB  lfk;          /* foreign keys exp writes after the last table */
    int     next;         /* hash chain */
} SQL_TAB;

/* Column kinds (value writing) */
enum {
    K_TEXT, K_NTEXT, K_NUM, K_FLOAT, K_BINF, K_BIND, K_DATE, K_TS, K_TSTZ,
    K_TSLTZ, K_IYM, K_IDS, K_RAW, K_LONGRAW, K_BLOB, K_CLOB, K_NCLOB, K_LONG,
    K_ROWID, K_BFILE, K_XML, K_JSON, K_VECTOR, K_BOOL, K_UDT
};

typedef struct {
    int kind;
    int ts_frac;        /* TIMESTAMP / INTERVAL DS fractional digits of the column */
    int tgt_frac;       /* fractional-second digits the target type keeps */
    int dec_p, dec_s;   /* non-Oracle DECIMAL(p,s) target; dec_p 0 = no limit */
    int tgt_double;     /* non-Oracle target is an IEEE double */
} SQL_COLINFO;

typedef struct {
    FILE       *fp;
    int64_t     row_count;
    const char *target_table;
    int         dbms;
    ODV_SESSION *session;
    int         create_table, create_index, write_comments, write_inserts;
    int         all_tables;
    int         header_written;      /* current table's header written */
    int         cur_tab;             /* index in tabs of the current table, -1 */
    char       *last_key;            /* key of the current table */
    SQL_SB      prefix;              /* "INSERT INTO ... VALUES (" (any length, M-28) */
    SQL_COLINFO *ci;                 /* per column of the current table */
    int         ci_count;
    /* one row */
    SQL_SB      vals, decl, pre, post, rnotes, tmp, tmp2;
    int         plsql;
    /* tables */
    SQL_TAB    *tabs;
    int         ntabs, ctabs;
    int        *bucket;
    int         nbucket;
    /* charset conversion scratch */
    char       *cvt;
    size_t      cvt_cap;
    int         failed;              /* a write to the file failed */
} SQL_CONTEXT;

/*---------------------------------------------------------------------------
    Small helpers
 ---------------------------------------------------------------------------*/

/* Text from the session's tables (dump / client charset) in the output
   charset. The result is valid until the next call. */
static const char *cvt_text(SQL_CONTEXT *ctx, const char *src)
{
    ODV_SESSION *s = ctx->session;
    size_t n, need;
    int out_len = 0;
    if (!src) return "";
    if (!s || s->dump_charset == s->out_charset || s->dump_charset == CHARSET_UNKNOWN)
        return src;
    n = strlen(src);
    if (n == 0) return src;
    need = n * 4 + 16;
    if (need > ctx->cvt_cap) {
        char *np = (char *)realloc(ctx->cvt, need);
        if (!np) return src;
        ctx->cvt = np;
        ctx->cvt_cap = need;
    }
    if (convert_charset(src, (int)n, s->dump_charset, ctx->cvt, (int)ctx->cvt_cap,
                        s->out_charset, &out_len) != ODV_OK || out_len < 0 ||
        (size_t)out_len >= ctx->cvt_cap)
        return src;
    ctx->cvt[out_len] = '\0';
    return ctx->cvt;
}

/* Quoted identifier for the dialect; the quote character inside the name is
   doubled (", `, ]). */
static void put_ident(SQL_SB *b, const char *name, int dbms)
{
    char open = '"', close = '"';
    const char *p;
    if (dbms == DBMS_MYSQL) open = close = '`';
    else if (dbms == DBMS_SQLSERVER) { open = '['; close = ']'; }
    sb_putc(b, open);
    for (p = name; *p; p++) {
        if (*p == close) sb_putc(b, close);
        sb_putc(b, *p);
    }
    sb_putc(b, close);
}

/* "schema"."table" (schema omitted when empty) */
static void put_qname(SQL_SB *b, const char *schema, const char *table, int dbms)
{
    if (schema && schema[0]) { put_ident(b, schema, dbms); sb_putc(b, '.'); }
    put_ident(b, table, dbms);
}

/* Length of the UTF-8 sequence starting at p (1 for a stray byte) */
static size_t utf8_len_at(const unsigned char *p, size_t avail)
{
    size_t k = 1;
    if (p[0] >= 0xF0) k = 4;
    else if (p[0] >= 0xE0) k = 3;
    else if (p[0] >= 0xC0) k = 2;
    if (k > avail) k = avail;
    return k;
}

/*---------------------------------------------------------------------------
    String literals

    Oracle      '...'  '' for ', and CR / LF / NUL as '...'||CHR(n)||'...' so a
                literal stays on one physical line for SQL*Plus (H-20, M-22)
    PostgreSQL  '...'  '' for '; backslashes as they are
                (standard_conforming_strings = on, set at the top of the file)
    MySQL       '...'  '' for ', \ doubled (M-16) and NUL as \0
    SQL Server  N'...' '' for ' (Unicode literal; the columns are N types)
 ---------------------------------------------------------------------------*/

/* Rendered bytes of one character in an Oracle literal */
static size_t ora_char_cost(unsigned char c)
{
    if (c == '\'') return 2;
    if (c == '\n' || c == '\r' || c == '\0') return 12;   /* '||CHR(13)||' */
    return 1;
}

/* One Oracle literal piece: '...' with CHR() for CR / LF / NUL */
static void ora_piece(SQL_SB *b, const char *p, size_t n)
{
    size_t i;
    sb_putc(b, '\'');
    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char)p[i];
        if (c == '\'') sb_puts(b, "''");
        else if (c == '\n' || c == '\r' || c == '\0') sb_printf(b, "'||CHR(%d)||'", (int)c);
        else sb_putc(b, (char)c);
    }
    sb_putc(b, '\'');
}

/* Next piece of [p, p+n) starting at *pos: at most `cap` rendered bytes
   (and `srcmax` source bytes), ending on a UTF-8 character boundary.
   Returns the piece length (0 at the end). */
static size_t next_piece(const char *p, size_t n, size_t pos, size_t cap, size_t srcmax)
{
    size_t i = pos, cost = 0;
    while (i < n) {
        size_t k = utf8_len_at((const unsigned char *)p + i, n - i);
        size_t c = (k == 1) ? ora_char_cost((unsigned char)p[i]) : k;
        if (i > pos && (cost + c > cap || i - pos + k > srcmax)) break;
        cost += c;
        i += k;
    }
    return i - pos;
}

static size_t ora_rendered_len(const char *p, size_t n)
{
    size_t i, r = 2;
    for (i = 0; i < n; i++) r += ora_char_cost((unsigned char)p[i]);
    return r;
}

/* Oracle text value of at most ORA_STR_MAX bytes: one literal, or pieces
   joined by || on lines of their own when it would not fit on a line. */
static void ora_text(SQL_SB *b, const char *p, size_t n)
{
    size_t pos = 0;
    if (ora_rendered_len(p, n) <= SQL_PIECE) { ora_piece(b, p, n); return; }
    while (pos < n) {
        size_t k = next_piece(p, n, pos, SQL_PIECE, n);
        if (pos > 0) sb_puts(b, "||\n");
        ora_piece(b, p + pos, k);
        pos += k;
    }
}

/* Oracle text value of any length as TO_CLOB('..')||TO_CLOB('..') (fn =
   TO_CLOB / TO_NCLOB): the pieces are concatenated as CLOB, so the 4000-byte
   limit of a VARCHAR2 concatenation does not apply (measured: an NVARCHAR2
   of 2000 full-width characters, 6000 bytes, goes in this way). */
static void ora_clob_concat(SQL_SB *b, const char *p, size_t n, const char *fn)
{
    size_t pos = 0;
    while (pos < n) {
        size_t k = next_piece(p, n, pos, SQL_PIECE, ORA_STR_MAX);
        if (pos > 0) sb_puts(b, "||\n");
        sb_printf(b, "%s(", fn);
        ora_piece(b, p + pos, k);
        sb_putc(b, ')');
        pos += k;
    }
}

static int has_byte(const char *p, size_t n, char c)
{
    return memchr(p, c, n) != NULL;
}

/* Non-Oracle string literal */
static void other_text(SQL_SB *b, const char *p, size_t n, int dbms)
{
    size_t i;
    if (dbms == DBMS_SQLSERVER && has_byte(p, n, '\0')) {
        /* NUL: CONCAT(CAST(N'..' AS NVARCHAR(MAX)), NCHAR(0), N'..') — '+' of
           two N'' literals is cut at 4000 characters */
        size_t st = 0;
        sb_puts(b, "CONCAT(CAST(N'");
        for (i = 0; i <= n; i++) {
            if (i == n || p[i] == '\0') {
                size_t j;
                for (j = st; j < i; j++) {
                    if (p[j] == '\'') sb_putc(b, '\'');
                    sb_putc(b, p[j]);
                }
                if (st == 0) sb_puts(b, "' AS NVARCHAR(MAX))");
                else sb_putc(b, '\'');
                if (i < n) sb_puts(b, ", NCHAR(0), N'");
                st = i + 1;
            }
        }
        sb_putc(b, ')');
        return;
    }
    if (dbms == DBMS_SQLSERVER) sb_putc(b, 'N');
    sb_putc(b, '\'');
    for (i = 0; i < n; i++) {
        char c = p[i];
        if (c == '\'') sb_puts(b, "''");
        else if (dbms == DBMS_MYSQL && c == '\\') sb_puts(b, "\\\\");
        else if (dbms == DBMS_MYSQL && c == '\0') sb_puts(b, "\\0");
        else sb_putc(b, c);
    }
    sb_putc(b, '\'');
}

/*---------------------------------------------------------------------------
    Oracle type strings

    exp and expdp give the same forms (VARCHAR2(10 CHAR), NUMBER(*,0),
    FLOAT(126), TIMESTAMP(9) WITH TIME ZONE, INTERVAL DAY(2) TO SECOND(6), the
    exp spelling "TIMESTAMP (6)" / "INTERVAL DAY (9) TO SECOND (9)" ...).
    Read case-insensitively, ignoring blanks around and inside parentheses.
 ---------------------------------------------------------------------------*/
typedef struct {
    char base[64];      /* upper case, blanks squeezed, up to the first '(' */
    int  nargs;
    char a1[32], a2[32];/* first parenthesised group, trimmed, upper case */
    int  char_sem;      /* a1 ends in " CHAR" */
    int  byte_sem;      /* a1 ends in " BYTE" */
    char rest[96];      /* upper case text after the first group */
    int  rest_arg;      /* number in the parentheses of rest (TO SECOND(9)), -1 */
} ORA_TYPE;

static void copy_trim_upper(char *dst, size_t cap, const char *s, size_t n)
{
    size_t i, o = 0;
    while (n > 0 && isspace((unsigned char)*s)) { s++; n--; }
    while (n > 0 && isspace((unsigned char)s[n - 1])) n--;
    for (i = 0; i < n && o + 1 < cap; i++) {
        char c = (char)toupper((unsigned char)s[i]);
        if (isspace((unsigned char)c)) {
            if (o > 0 && dst[o - 1] == ' ') continue;
            c = ' ';
        }
        dst[o++] = c;
    }
    dst[o] = '\0';
}

static void parse_ora_type(const char *ts, ORA_TYPE *t)
{
    const char *lp, *rp;
    memset(t, 0, sizeof(*t));
    t->rest_arg = -1;
    if (!ts) ts = "";
    lp = strchr(ts, '(');
    if (!lp) {
        copy_trim_upper(t->base, sizeof(t->base), ts, strlen(ts));
        return;
    }
    copy_trim_upper(t->base, sizeof(t->base), ts, (size_t)(lp - ts));
    rp = strchr(lp, ')');
    if (!rp) rp = lp + strlen(lp);
    {
        const char *comma = memchr(lp + 1, ',', (size_t)(rp - lp - 1));
        if (comma) {
            copy_trim_upper(t->a1, sizeof(t->a1), lp + 1, (size_t)(comma - lp - 1));
            copy_trim_upper(t->a2, sizeof(t->a2), comma + 1, (size_t)(rp - comma - 1));
            t->nargs = 2;
        } else {
            copy_trim_upper(t->a1, sizeof(t->a1), lp + 1, (size_t)(rp - lp - 1));
            t->nargs = t->a1[0] ? 1 : 0;
        }
    }
    {
        size_t l = strlen(t->a1);
        if (l > 5 && strcmp(t->a1 + l - 5, " CHAR") == 0) { t->char_sem = 1; t->a1[l - 5] = '\0'; }
        else if (l > 5 && strcmp(t->a1 + l - 5, " BYTE") == 0) { t->byte_sem = 1; t->a1[l - 5] = '\0'; }
    }
    if (*rp == ')') {
        const char *q;
        copy_trim_upper(t->rest, sizeof(t->rest), rp + 1, strlen(rp + 1));
        q = strchr(t->rest, '(');
        if (q) t->rest_arg = atoi(q + 1);
    }
}

static int is_base(const ORA_TYPE *t, const char *name) { return strcmp(t->base, name) == 0; }
static int starts(const char *s, const char *p) { return strncmp(s, p, strlen(p)) == 0; }

/* Oracle's type in one spelling for exp and expdp alike: exp writes
   NUMBER(10, 0) / TIMESTAMP (6) / INTERVAL DAY (9) TO SECOND (9), expdp
   NUMBER(10) / TIMESTAMP(6) / INTERVAL DAY(9) TO SECOND(9). Types written
   with a quoted name (object types) are left as they are. */
static void ora_type_canon(const char *ts, SQL_SB *out)
{
    ORA_TYPE t;
    const char *r;
    if (!ts || !ts[0]) { sb_puts(out, "VARCHAR2(4000)"); return; }
    if (strchr(ts, '"')) { sb_puts(out, ts); return; }
    parse_ora_type(ts, &t);
    if (!(is_base(&t, "VARCHAR2") || is_base(&t, "NVARCHAR2") || is_base(&t, "CHAR") ||
          is_base(&t, "NCHAR") || is_base(&t, "VARCHAR") || is_base(&t, "RAW") ||
          is_base(&t, "NUMBER") || is_base(&t, "FLOAT") || is_base(&t, "UROWID") ||
          starts(t.base, "TIMESTAMP") || starts(t.base, "INTERVAL"))) {
        sb_puts(out, ts);
        return;
    }
    sb_puts(out, t.base);
    if (t.nargs >= 1) {
        sb_putc(out, '(');
        sb_puts(out, t.a1);
        if (t.char_sem) sb_puts(out, " CHAR");
        else if (t.byte_sem) sb_puts(out, " BYTE");
        /* NUMBER(p,0) is NUMBER(p), but NUMBER(*,0) (INTEGER) is not NUMBER(*) */
        if (t.nargs == 2 && !(is_base(&t, "NUMBER") && strcmp(t.a2, "0") == 0 && strcmp(t.a1, "*") != 0)) {
            sb_putc(out, ',');
            sb_puts(out, t.a2);
        }
        sb_putc(out, ')');
    }
    if (t.rest[0]) {
        sb_putc(out, ' ');
        for (r = t.rest; *r; r++) {
            if (*r == ' ' && r[1] == '(') continue;   /* "TO SECOND (9)" */
            sb_putc(out, *r);
        }
    }
}

/* Integer argument; '*' = 38 (Oracle's default precision); -999 when absent */
static int targ(const char *a)
{
    if (!a[0]) return -999;
    if (a[0] == '*') return 38;
    return atoi(a);
}

/* Column kind from the parser's type and the type string */
static int col_kind(const ODV_COLUMN *c, const ORA_TYPE *t)
{
    if (starts(t->base, "XMLTYPE") || strstr(c->type_str, "XMLTYPE")) return K_XML;
    /* "OWNER"."TYPE": a user-defined type (exp keeps it as a VARCHAR column) */
    if (c->type_str[0] == '"') return K_UDT;
    switch (c->type) {
    case COL_CHAR: case COL_VARCHAR:        return K_TEXT;
    case COL_NCHAR: case COL_NVARCHAR:      return K_NTEXT;
    case COL_NUMBER:
    case COL_FLOAT:                         return is_base(t, "FLOAT") ? K_FLOAT : K_NUM;
    case COL_BIN_FLOAT:                     return K_BINF;
    case COL_BIN_DOUBLE:                    return K_BIND;
    case COL_DATE:                          return K_DATE;
    case COL_TIMESTAMP:                     return K_TS;
    case COL_TIMESTAMP_TZ:                  return K_TSTZ;
    case COL_TIMESTAMP_LTZ:                 return K_TSLTZ;
    case COL_INTERVAL_YM:                   return K_IYM;
    case COL_INTERVAL_DS:                   return K_IDS;
    case COL_RAW:                           return K_RAW;
    case COL_LONG_RAW:                      return K_LONGRAW;
    case COL_BLOB:                          return K_BLOB;
    case COL_CLOB:                          return K_CLOB;
    case COL_NCLOB:                         return K_NCLOB;
    case COL_LONG:                          return K_LONG;
    case COL_ROWID:                         return K_ROWID;
    case COL_BFILE:                         return K_BFILE;
    case COL_XMLTYPE:                       return K_XML;
    case COL_JSON:                          return K_JSON;
    case COL_VECTOR:                        return K_VECTOR;
    case COL_BOOLEAN:                       return K_BOOL;
    case COL_USER_DEFINE: case COL_VARRAY:  return K_UDT;
    default:                                return K_TEXT;
    }
}

/*---------------------------------------------------------------------------
    map_type — the column type for the target DBMS (CREATE TABLE)

    Oracle: the type as the dump gives it. Others (TYPE_MAPPING_SPEC 2):
      VARCHAR2(n [BYTE])      PG VARCHAR(n)   MySQL VARCHAR(n)   MSSQL NVARCHAR(n|MAX)
      VARCHAR2(n CHAR)        PG VARCHAR(n)   MySQL VARCHAR(n)   MSSQL NVARCHAR(2n|MAX)
      NVARCHAR2(n)            PG VARCHAR(n)   MySQL VARCHAR(n)   MSSQL NVARCHAR(n|MAX)
      CHAR / NCHAR(n)         PG CHAR(n)      MySQL CHAR(n) / VARCHAR(n) if n>255   MSSQL NCHAR(n)
      CLOB / NCLOB / LONG     PG TEXT         MySQL LONGTEXT     MSSQL NVARCHAR(MAX)
      NUMBER                  PG NUMERIC      MySQL DECIMAL(65,30)  MSSQL DECIMAL(38,10)
      NUMBER(p[,s])           NUMERIC/DECIMAL(p,s); s<0: (p-s,0); s>p: (s,s); clamped
                              to MySQL 65/30 and SQL Server 38 with a note
      FLOAT(p)                PG NUMERIC      MySQL DOUBLE       MSSQL FLOAT(53)
      BINARY_FLOAT / DOUBLE   REAL / DOUBLE PRECISION | FLOAT / DOUBLE | REAL / FLOAT(53)
      DATE                    PG TIMESTAMP(0) MySQL DATETIME     MSSQL DATETIME2(0)
      TIMESTAMP(p)            p capped at 6 (PG, MySQL) / 7 (SQL Server)
      TIMESTAMP WITH [LOCAL] TIME ZONE  PG TIMESTAMPTZ  MySQL DATETIME (UTC)  MSSQL DATETIMEOFFSET
      INTERVAL                PG INTERVAL ... MySQL VARCHAR(64)  MSSQL NVARCHAR(64)
      RAW(n)                  PG BYTEA        MySQL VARBINARY(n) MSSQL VARBINARY(n|MAX)
      BLOB / LONG RAW         PG BYTEA        MySQL LONGBLOB     MSSQL VARBINARY(MAX)
      JSON                    PG JSON         MySQL JSON         MSSQL NVARCHAR(MAX)
      XMLTYPE                 PG XML          MySQL LONGTEXT     MSSQL XML
      BOOLEAN                 BOOLEAN         BOOLEAN            BIT
      ROWID / UROWID          VARCHAR(18) / TEXT ...
      VECTOR / object types   text types (with a note)
    The table notes (tn) say what a type cannot hold.
 ---------------------------------------------------------------------------*/
#define MYSQL_ROW_MAX 65535

/* A "-- NOTE:" line: head, then the message whole (a DEFAULT expression can
 * be thousands of bytes); a line break in it goes on as a comment line
 * ("--   "), so the text after it is not read as SQL */
static void put_note(SQL_SB *b, const char *head, const char *col, const char *mid,
                     const char *fmt, va_list ap)
{
    char tmp[512], *msg = tmp;
    const char *p;
    va_list aq;
    int n;
    va_copy(aq, ap);
    n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
    if (n < 0) { va_end(aq); return; }
    if ((size_t)n >= sizeof(tmp)) {
        msg = (char *)malloc((size_t)n + 1);
        if (!msg) { va_end(aq); b->err = 1; return; }
        vsnprintf(msg, (size_t)n + 1, fmt, aq);
    }
    va_end(aq);
    sb_puts(b, head);
    sb_puts(b, col);
    sb_puts(b, mid);
    for (p = msg; *p; ) {
        const char *nl = strpbrk(p, "\r\n");
        if (!nl) { sb_puts(b, p); break; }
        sb_putn(b, p, (size_t)(nl - p));
        if (nl[0] == '\r' && nl[1] == '\n') nl++;
        sb_puts(b, "\n--   ");
        p = nl + 1;
    }
    sb_putc(b, '\n');
    if (msg != tmp) free(msg);
}

static void note_col(SQL_SB *tn, const char *col, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    put_note(tn, "-- NOTE: column \"", col, "\": ", fmt, ap);
    va_end(ap);
}

/* Fit DECIMAL(p,s) (p>=1) within a DBMS's maximum precision / scale */
static void fit_decimal(int *p, int *s, int maxp, int maxs, int *clamped)
{
    *clamped = 0;
    if (*s > maxs) { *s = maxs; *clamped = 1; }
    if (*p > maxp) { *p = maxp; *clamped = 1; }
    if (*s > *p) { *s = *p; *clamped = 1; }
}

static void map_type(SQL_CONTEXT *ctx, int i, const char *colname, SQL_SB *out,
                     SQL_SB *tn, int mysql_text)
{
    ODV_SESSION *s = ctx->session;
    const ODV_COLUMN *c = &s->table.columns[i];
    SQL_COLINFO *ci = &ctx->ci[i];
    int dbms = ctx->dbms;
    ORA_TYPE t;
    int n;

    parse_ora_type(c->type_str, &t);
    ci->kind = col_kind(c, &t);
    ci->ts_frac = 6;
    ci->tgt_frac = 9;
    ci->dec_p = ci->dec_s = 0;
    ci->tgt_double = 0;
    if (ci->kind == K_TS || ci->kind == K_TSTZ || ci->kind == K_TSLTZ)
        ci->ts_frac = (t.nargs >= 1) ? targ(t.a1) : 6;
    else if (ci->kind == K_IDS)
        ci->ts_frac = (t.rest_arg >= 0) ? t.rest_arg : 6;
    if (ci->ts_frac < 0 || ci->ts_frac > 9) ci->ts_frac = 6;

    if (dbms == DBMS_ORACLE) {
        ora_type_canon(c->type_str, out);
        if (ci->kind == K_UDT)
            note_col(tn, colname, "type %s must exist (CREATE TYPE) before this table is created",
                     c->type_str);
        return;
    }

    n = (t.nargs >= 1) ? targ(t.a1) : -999;
    switch (ci->kind) {
    case K_TEXT:
    case K_NTEXT: {
        int is_char = starts(t.base, "CHAR") || starts(t.base, "NCHAR");
        if (n < 1) n = is_char ? 1 : 4000;
        if (is_char) {
            if (dbms == DBMS_SQLSERVER) {
                sb_printf(out, "NCHAR(%d)", n > 4000 ? 4000 : n);
                if (t.char_sem)
                    note_col(tn, colname, "%s -> NCHAR(%d): a value with characters outside the BMP "
                             "needs more than %d UTF-16 units and does not fit", c->type_str, n, n);
            } else if (dbms == DBMS_MYSQL && n > 255) {
                sb_printf(out, "VARCHAR(%d)", n);
            } else {
                sb_printf(out, "CHAR(%d)", n);
            }
        } else {
            if (dbms == DBMS_SQLSERVER) {
                int units = t.char_sem ? 2 * n : n;
                if (units > 4000) sb_puts(out, "NVARCHAR(MAX)");
                else sb_printf(out, "NVARCHAR(%d)", units);
            } else if (dbms == DBMS_MYSQL && mysql_text && n > 255) {
                sb_puts(out, "TEXT");
            } else {
                sb_printf(out, "VARCHAR(%d)", n);
            }
        }
        break;
    }
    case K_CLOB: case K_NCLOB: case K_LONG:
        sb_puts(out, dbms == DBMS_POSTGRES ? "TEXT" : dbms == DBMS_MYSQL ? "LONGTEXT" : "NVARCHAR(MAX)");
        break;
    case K_NUM: {
        int p = n, sc = (t.nargs >= 2) ? targ(t.a2) : 0, P, S, cl = 0;
        if (t.nargs == 0) {
            /* NUMBER without precision: 38 decimal digits at any scale */
            if (dbms == DBMS_POSTGRES) { sb_puts(out, "NUMERIC"); break; }
            if (dbms == DBMS_MYSQL) { ci->dec_p = 65; ci->dec_s = 30; }
            else { ci->dec_p = 38; ci->dec_s = 10; }
            sb_printf(out, "DECIMAL(%d,%d)", ci->dec_p, ci->dec_s);
            note_col(tn, colname, "NUMBER without precision -> DECIMAL(%d,%d): values with more than "
                     "%d integer digits do not fit, decimals beyond %d places are rounded",
                     ci->dec_p, ci->dec_s, ci->dec_p - ci->dec_s, ci->dec_s);
            break;
        }
        if (p < 1) p = 38;
        if (sc == -999) sc = 0;
        if (sc < 0) { P = p - sc; S = 0; }
        else if (sc > p) { P = sc; S = sc; }
        else { P = p; S = sc; }
        if (dbms == DBMS_MYSQL) fit_decimal(&P, &S, 65, 30, &cl);
        else if (dbms == DBMS_SQLSERVER) fit_decimal(&P, &S, 38, 38, &cl);
        ci->dec_p = P;
        ci->dec_s = S;
        sb_printf(out, "%s(%d,%d)", dbms == DBMS_POSTGRES ? "NUMERIC" : "DECIMAL", P, S);
        if (cl)
            note_col(tn, colname, "%s cannot be represented exactly; DECIMAL(%d,%d) is used "
                     "(values that do not fit are noted per row)", c->type_str, P, S);
        break;
    }
    case K_FLOAT: {
        int bp = (n >= 1) ? n : 126;
        if (dbms == DBMS_POSTGRES) { sb_puts(out, "NUMERIC"); break; }
        ci->tgt_double = 1;
        sb_puts(out, dbms == DBMS_MYSQL ? "DOUBLE" : "FLOAT(53)");
        if (bp > 49)
            note_col(tn, colname, "%s holds up to %d significant digits; %s keeps 15 "
                     "(values with more are noted per row)", c->type_str,
                     (int)(bp * 0.30103) + 1, dbms == DBMS_MYSQL ? "DOUBLE" : "FLOAT(53)");
        break;
    }
    case K_BINF:
        sb_puts(out, dbms == DBMS_POSTGRES ? "REAL" : dbms == DBMS_MYSQL ? "FLOAT" : "REAL");
        break;
    case K_BIND:
        sb_puts(out, dbms == DBMS_POSTGRES ? "DOUBLE PRECISION" : dbms == DBMS_MYSQL ? "DOUBLE" : "FLOAT(53)");
        break;
    case K_DATE:
        ci->tgt_frac = 0;
        sb_puts(out, dbms == DBMS_POSTGRES ? "TIMESTAMP(0)" : dbms == DBMS_MYSQL ? "DATETIME" : "DATETIME2(0)");
        break;
    case K_TS: case K_TSTZ: case K_TSLTZ: {
        int maxf = (dbms == DBMS_SQLSERVER) ? 7 : 6;
        int f = ci->ts_frac > maxf ? maxf : ci->ts_frac;
        ci->tgt_frac = f;
        if (ci->kind == K_TS)
            sb_printf(out, "%s(%d)", dbms == DBMS_POSTGRES ? "TIMESTAMP" :
                      dbms == DBMS_MYSQL ? "DATETIME" : "DATETIME2", f);
        else if (dbms == DBMS_POSTGRES)
            sb_printf(out, "TIMESTAMPTZ(%d)", f);
        else if (dbms == DBMS_MYSQL)
            sb_printf(out, "DATETIME(%d)", f);
        else
            sb_printf(out, "DATETIMEOFFSET(%d)", f);
        if (ci->ts_frac > maxf)
            note_col(tn, colname, "%s: %s keeps %d fractional digits of a second; digits %d-%d are dropped",
                     c->type_str, dbms == DBMS_POSTGRES ? "PostgreSQL" : dbms == DBMS_MYSQL ? "MySQL" : "SQL Server",
                     maxf, maxf + 1, ci->ts_frac);
        if (ci->kind == K_TSTZ) {
            if (dbms == DBMS_POSTGRES)
                note_col(tn, colname, "TIMESTAMP WITH TIME ZONE -> TIMESTAMPTZ keeps the instant; the original offset / region is not kept");
            else if (dbms == DBMS_MYSQL)
                note_col(tn, colname, "TIMESTAMP WITH TIME ZONE -> DATETIME holding UTC; the offset / region is not kept");
            else
                note_col(tn, colname, "TIMESTAMP WITH TIME ZONE -> DATETIMEOFFSET keeps the offset; a region name is written as its offset at that instant");
        } else if (ci->kind == K_TSLTZ) {
            if (dbms == DBMS_MYSQL)
                note_col(tn, colname, "TIMESTAMP WITH LOCAL TIME ZONE -> DATETIME holding UTC");
            if (!s->db_timezone[0])
                note_col(tn, colname, "the dump does not give DBTIMEZONE; values are the database's wall clock without an offset");
        }
        break;
    }
    case K_IYM:
        if (dbms == DBMS_POSTGRES) sb_puts(out, "INTERVAL YEAR TO MONTH");
        else {
            sb_puts(out, dbms == DBMS_MYSQL ? "VARCHAR(64)" : "NVARCHAR(64)");
            note_col(tn, colname, "%s has no %s type; the value is kept as Oracle's text (+Y-M)", c->type_str,
                     dbms == DBMS_MYSQL ? "MySQL" : "SQL Server");
        }
        break;
    case K_IDS: {
        int f = ci->ts_frac > 6 ? 6 : ci->ts_frac;
        if (dbms == DBMS_POSTGRES) {
            ci->tgt_frac = f;
            sb_printf(out, "INTERVAL DAY TO SECOND(%d)", f);
            if (ci->ts_frac > 6)
                note_col(tn, colname, "%s: PostgreSQL keeps 6 fractional digits of a second; digits 7-%d are dropped",
                         c->type_str, ci->ts_frac);
        } else {
            sb_puts(out, dbms == DBMS_MYSQL ? "VARCHAR(64)" : "NVARCHAR(64)");
            note_col(tn, colname, "%s has no %s type; the value is kept as Oracle's text (+D HH:MI:SS.F)", c->type_str,
                     dbms == DBMS_MYSQL ? "MySQL" : "SQL Server");
        }
        break;
    }
    case K_RAW:
        if (n < 1) n = 2000;
        if (dbms == DBMS_POSTGRES) sb_puts(out, "BYTEA");
        else if (dbms == DBMS_MYSQL) sb_printf(out, "VARBINARY(%d)", n);
        else if (n > 8000) sb_puts(out, "VARBINARY(MAX)");
        else sb_printf(out, "VARBINARY(%d)", n);
        break;
    case K_LONGRAW: case K_BLOB:
        sb_puts(out, dbms == DBMS_POSTGRES ? "BYTEA" : dbms == DBMS_MYSQL ? "LONGBLOB" : "VARBINARY(MAX)");
        break;
    case K_ROWID:
        if (starts(t.base, "UROWID"))
            sb_puts(out, dbms == DBMS_SQLSERVER ? "NVARCHAR(MAX)" : "TEXT");
        else
            sb_puts(out, dbms == DBMS_SQLSERVER ? "NVARCHAR(18)" : "VARCHAR(18)");
        break;
    case K_BFILE:
        sb_puts(out, dbms == DBMS_SQLSERVER ? "NVARCHAR(530)" : "VARCHAR(530)");
        note_col(tn, colname, "BFILE has no counterpart; the locator is kept as text BFILENAME('dir', 'file')");
        break;
    case K_XML:
        sb_puts(out, dbms == DBMS_MYSQL ? "LONGTEXT" : "XML");
        break;
    case K_JSON:
        sb_puts(out, dbms == DBMS_POSTGRES ? "JSON" : dbms == DBMS_MYSQL ? "JSON" : "NVARCHAR(MAX)");
        if (dbms == DBMS_MYSQL)
            note_col(tn, colname, "MySQL's JSON type normalizes the document (key order, duplicate keys)");
        break;
    case K_VECTOR:
        sb_puts(out, dbms == DBMS_POSTGRES ? "TEXT" : dbms == DBMS_MYSQL ? "LONGTEXT" : "NVARCHAR(MAX)");
        note_col(tn, colname, "%s has no standard counterpart; kept as its text form [x,y,...]", c->type_str);
        break;
    case K_BOOL:
        sb_puts(out, dbms == DBMS_SQLSERVER ? "BIT" : "BOOLEAN");
        break;
    case K_UDT:
    default:
        sb_puts(out, dbms == DBMS_POSTGRES ? "TEXT" : dbms == DBMS_MYSQL ? "LONGTEXT" : "NVARCHAR(MAX)");
        if (ci->kind == K_UDT)
            note_col(tn, colname, "Oracle type %s has no counterpart; values are written as NULL", c->type_str);
        break;
    }
}

/* MySQL row size estimate of one column type text (bytes) */
static long mysql_col_bytes(const char *ty)
{
    int n;
    if (starts(ty, "VARCHAR(")) { n = atoi(ty + 8); return 4L * n + 2; }
    if (starts(ty, "CHAR(")) { n = atoi(ty + 5); return 4L * n; }
    if (starts(ty, "VARBINARY(")) { n = atoi(ty + 10); return (long)n + 2; }
    if (starts(ty, "DECIMAL(")) { n = atoi(ty + 8); return n / 2 + 2; }
    if (starts(ty, "DATETIME")) return 8;
    if (strstr(ty, "TEXT") || strstr(ty, "BLOB") || strcmp(ty, "JSON") == 0) return 12;
    return 8;
}

/*---------------------------------------------------------------------------
    DEFAULT expressions for the other dialects: only a number, a string
    literal or NULL is portable; any other Oracle expression is noted.
 ---------------------------------------------------------------------------*/
static int default_portable(const char *d, SQL_SB *lit, int dbms, int kind)
{
    const char *p = d;
    size_t n = strlen(d);
    while (n > 0 && isspace((unsigned char)p[0])) { p++; n--; }
    while (n > 0 && (isspace((unsigned char)p[n - 1]) || p[n - 1] == ';')) n--;
    if (n == 0) return 0;
    /* strip one pair of outer parentheses (exp writes "(0)" for some) */
    while (n >= 2 && p[0] == '(' && p[n - 1] == ')') {
        p++; n -= 2;
        while (n > 0 && isspace((unsigned char)p[0])) { p++; n--; }
        while (n > 0 && isspace((unsigned char)p[n - 1])) n--;
    }
    if (n == 4 && odv_strnicmp(p, "NULL", 4) == 0) { sb_puts(lit, "NULL"); return 1; }
    if (p[0] == '\'' && n >= 2 && p[n - 1] == '\'') {
        /* one literal: inner quotes come doubled */
        size_t i;
        SQL_SB raw;
        memset(&raw, 0, sizeof(raw));
        for (i = 1; i + 1 < n; i++) {
            if (p[i] == '\'') {
                if (i + 2 < n && p[i + 1] == '\'') { sb_putc(&raw, '\''); i++; continue; }
                sb_free(&raw);
                return 0;          /* 'a' || 'b' and the like */
            }
            sb_putc(&raw, p[i]);
        }
        if (raw.err) { sb_free(&raw); return 0; }
        if (dbms == DBMS_MYSQL && (kind == K_CLOB || kind == K_NCLOB || kind == K_LONG ||
                                   kind == K_JSON || kind == K_BLOB || kind == K_VECTOR))
            sb_putc(lit, '(');
        other_text(lit, sb_str(&raw), raw.len, dbms);
        if (dbms == DBMS_MYSQL && (kind == K_CLOB || kind == K_NCLOB || kind == K_LONG ||
                                   kind == K_JSON || kind == K_BLOB || kind == K_VECTOR))
            sb_putc(lit, ')');
        sb_free(&raw);
        return 1;
    }
    {
        size_t i = 0;
        int digits = 0;
        if (p[i] == '-' || p[i] == '+') i++;
        for (; i < n; i++) {
            if (isdigit((unsigned char)p[i])) digits++;
            else if (p[i] != '.' && p[i] != 'E' && p[i] != 'e' && p[i] != '+' && p[i] != '-') return 0;
        }
        if (!digits) return 0;
        sb_putn(lit, p, n);
        return 1;
    }
}

/*---------------------------------------------------------------------------
    CHECK conditions / index expressions for the other dialects

    Oracle's expression is rewritten only when it is made of identifiers,
    numbers, string literals, comparison / logical / arithmetic operators and
    parentheses; an unquoted identifier is Oracle upper case and is quoted
    as such. Anything else (a function call, ||, a DATE '...' literal) cannot
    be carried over reliably: returns 0 and the caller writes a note.
 ---------------------------------------------------------------------------*/
static int is_cond_keyword(const char *w)
{
    static const char *kw[] = { "AND", "OR", "NOT", "IN", "BETWEEN", "LIKE", "IS", "NULL",
                                "ESCAPE", "ASC", "DESC", NULL };
    int i;
    for (i = 0; kw[i]; i++) if (strcmp(w, kw[i]) == 0) return 1;
    return 0;
}

static int translate_expr(const char *e, SQL_SB *out, int dbms)
{
    const char *p = e;
    int after_is = 0;      /* 1 = after IS, 2 = after IS NOT: only NULL may follow
                              (IS JSON, IS A SET, IS OF ... are Oracle's) */
    while (*p) {
        unsigned char c = (unsigned char)*p;
        if (isspace(c)) { sb_putc(out, ' '); while (isspace((unsigned char)*p)) p++; continue; }
        if (after_is && !(isalpha(c) || c == '_')) return 0;
        if (c == '"') {
            const char *q = strchr(p + 1, '"');
            char name[ODV_OBJNAME_LEN * 4 + 1];
            size_t n;
            if (!q) return 0;
            n = (size_t)(q - p - 1);
            if (n >= sizeof(name)) return 0;
            memcpy(name, p + 1, n);
            name[n] = '\0';
            put_ident(out, name, dbms);
            p = q + 1;
            continue;
        }
        if (c == '\'') {
            SQL_SB raw;
            memset(&raw, 0, sizeof(raw));
            p++;
            for (;;) {
                if (!*p) { sb_free(&raw); return 0; }
                if (*p == '\'') {
                    if (p[1] == '\'') { sb_putc(&raw, '\''); p += 2; continue; }
                    p++;
                    break;
                }
                sb_putc(&raw, *p++);
            }
            other_text(out, sb_str(&raw), raw.len, dbms == DBMS_SQLSERVER ? DBMS_POSTGRES : dbms);
            sb_free(&raw);
            continue;
        }
        if (isdigit(c) || (c == '.' && isdigit((unsigned char)p[1]))) {
            while (isalnum((unsigned char)*p) || *p == '.' ||
                   ((*p == '+' || *p == '-') && (p[-1] == 'E' || p[-1] == 'e')))
                sb_putc(out, *p++);
            continue;
        }
        if (isalpha(c) || c == '_' || c >= 0x80) {
            char w[ODV_OBJNAME_LEN * 4 + 1];
            size_t n = 0;
            const char *q;
            while ((isalnum((unsigned char)*p) || *p == '_' || *p == '$' || *p == '#' ||
                    (unsigned char)*p >= 0x80) && n + 1 < sizeof(w))
                w[n++] = (char)toupper((unsigned char)*p++);
            w[n] = '\0';
            q = p;
            while (isspace((unsigned char)*q)) q++;
            if (*q == '(' && !is_cond_keyword(w)) return 0;      /* function call */
            if (*q == '\'') return 0;                            /* DATE '...' etc. */
            if (after_is) {
                if (after_is == 1 && strcmp(w, "NOT") == 0) after_is = 2;
                else if (strcmp(w, "NULL") == 0) after_is = 0;
                else return 0;
            } else if (strcmp(w, "IS") == 0) {
                after_is = 1;
            }
            if (is_cond_keyword(w)) sb_puts(out, w);
            else put_ident(out, w, dbms);
            continue;
        }
        if (c == '|' && p[1] == '|') return 0;
        if (strchr("()=<>!+-*/,", (int)c)) {
            if (c == '!' && p[1] == '=') { sb_puts(out, "<>"); p += 2; continue; }
            sb_putc(out, (char)c);
            p++;
            continue;
        }
        return 0;
    }
    return after_is ? 0 : 1;
}

/*---------------------------------------------------------------------------
    Oracle DDL text on lines of at most SQL_LINE_MAX bytes: a long condition
    or default is broken at blanks outside literals and quoted names.
 ---------------------------------------------------------------------------*/
static void put_wrapped(SQL_SB *b, const char *text)
{
    const char *p = text;
    while (*p) {
        if (*p == '\'') {
            /* A string literal. One longer than SQL_PIECE bytes is written as
               '...'||'...' pieces on lines of their own (the same value; a
               3000-byte DEFAULT literal on one line is SP2-0341 in SQL*Plus).
               An N'...' literal keeps N on every piece. */
            const char *e = p + 1, *s;
            int nlit = (p > text && (p[-1] == 'N' || p[-1] == 'n') &&
                        (p - 1 == text || !(isalnum((unsigned char)p[-2]) || p[-2] == '_' ||
                                            p[-2] == '$' || p[-2] == '#' || p[-2] == '"')));
            size_t cnt = 0;
            while (*e) {
                if (*e == '\'') {
                    if (e[1] == '\'') { e += 2; continue; }
                    break;
                }
                e++;
            }
            if (sb_col(b) > 0 && sb_col(b) + (size_t)(e - p) + 2 > SQL_LINE_MAX) {
                /* the literal would not fit on this line: start a new one
                   (taking the N of N'...' along) */
                if (nlit && b->len > 0 && !b->err) {
                    b->len--;
                    b->p[b->len] = '\0';
                    sb_putc(b, '\n');
                    sb_putc(b, p[-1]);
                } else {
                    sb_putc(b, '\n');
                }
            }
            if ((size_t)(e - p) <= SQL_PIECE) {
                sb_putn(b, p, (size_t)(e - p) + (*e ? 1 : 0));
                p = *e ? e + 1 : e;
                continue;
            }
            sb_putc(b, '\'');
            for (s = p + 1; s < e; ) {
                size_t k = (*s == '\'') ? 2 : utf8_len_at((const unsigned char *)s, (size_t)(e - s));
                if (cnt > 0 && cnt + k > SQL_PIECE) {
                    sb_puts(b, "'||\n");
                    if (nlit) sb_putc(b, 'N');
                    sb_putc(b, '\'');
                    cnt = 0;
                }
                sb_putn(b, s, k);
                cnt = (*s == '\n') ? 0 : cnt + k;
                s += k;
            }
            if (*e) { sb_putc(b, '\''); p = e + 1; }
            else p = e;
            continue;
        }
        if (*p == '"') {
            /* a quoted name: as it is */
            const char *e = strchr(p + 1, '"');
            size_t n = e ? (size_t)(e - p) + 1 : strlen(p);
            sb_putn(b, p, n);
            p += n;
            continue;
        }
        if (*p == ' ' && sb_col(b) > SQL_LINE_MAX - 200) {
            sb_putc(b, '\n');
            p++;
            continue;
        }
        if (*p == '\n' || *p == '\r') {
            /* a line break outside literals is only white space */
            sb_putc(b, '\n');
            p++;
            continue;
        }
        sb_putc(b, *p++);
    }
}

/* Copy a CHECK condition without one pair of parentheses that encloses all
   of it ("(A) OR (B)" keeps its own), and without blanks at the ends. */
static void strip_outer_parens(const char *text, SQL_SB *out)
{
    const char *b = text, *e;
    while (*b == ' ' || *b == '\t' || *b == '\n' || *b == '\r') b++;
    e = b + strlen(b);
    while (e > b && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\n' || e[-1] == '\r')) e--;
    if (e - b >= 2 && *b == '(' && e[-1] == ')') {
        const char *p;
        int depth = 0;
        char q = 0;
        for (p = b; p < e; p++) {
            if (q) { if (*p == q) q = 0; continue; }
            if (*p == '\'' || *p == '"') { q = *p; continue; }
            if (*p == '(') depth++;
            else if (*p == ')' && --depth == 0) break;
        }
        if (p == e - 1) { b++; e--; }
    }
    sb_putn(out, b, (size_t)(e - b));
}

/* Copy an index's column text without the blanks just inside parentheses
   and before commas (outside literals and quoted names): exp writes
   ("A" , "B" ), Data Pump ("A", "B"), and the same table should give the
   same SQL from either. */
static void trim_paren_ws(const char *text, SQL_SB *out)
{
    const char *p = text;
    char q = 0, last = 0;
    while (*p) {
        if (q) {
            if (*p == q) q = 0;
        } else if (*p == '\'' || *p == '"') {
            q = *p;
        } else if (*p == ' ' || *p == '\t') {
            const char *r = p;
            while (*r == ' ' || *r == '\t') r++;
            if (last == '(' || *r == ')' || *r == ',') { p = r; continue; }
        }
        last = *p;
        sb_putc(out, *p++);
    }
}

/*---------------------------------------------------------------------------
    Tables (hash by key)
 ---------------------------------------------------------------------------*/
static unsigned int key_hash(const char *k)
{
    unsigned int h = 2166136261u;
    while (*k) { h ^= (unsigned char)*k++; h *= 16777619u; }
    return h;
}

static void make_key(char *out, size_t cap, const char *schema, const char *table)
{
    snprintf(out, cap, "%s\x1f%s", schema ? schema : "", table ? table : "");
}

static int tab_find(SQL_CONTEXT *ctx, const char *key, int create)
{
    unsigned int h;
    int i;
    if (ctx->nbucket == 0) {
        ctx->bucket = (int *)malloc(1024 * sizeof(int));
        if (!ctx->bucket) return -1;
        ctx->nbucket = 1024;
        for (i = 0; i < 1024; i++) ctx->bucket[i] = -1;
    }
    h = key_hash(key) & (unsigned int)(ctx->nbucket - 1);
    for (i = ctx->bucket[h]; i >= 0; i = ctx->tabs[i].next)
        if (strcmp(ctx->tabs[i].key, key) == 0) return i;
    if (!create) return -1;
    if (ctx->ntabs >= ctx->ctabs) {
        int nc = ctx->ctabs ? ctx->ctabs * 2 : 64;
        SQL_TAB *nt = (SQL_TAB *)realloc(ctx->tabs, (size_t)nc * sizeof(SQL_TAB));
        if (!nt) return -1;
        ctx->tabs = nt;
        ctx->ctabs = nc;
    }
    if (ctx->ntabs >= ctx->nbucket * 2) {
        int nb = ctx->nbucket * 4, j;
        int *nbk = (int *)malloc((size_t)nb * sizeof(int));
        if (nbk) {
            for (j = 0; j < nb; j++) nbk[j] = -1;
            for (j = 0; j < ctx->ntabs; j++) {
                unsigned int hh = key_hash(ctx->tabs[j].key) & (unsigned int)(nb - 1);
                ctx->tabs[j].next = nbk[hh];
                nbk[hh] = j;
            }
            free(ctx->bucket);
            ctx->bucket = nbk;
            ctx->nbucket = nb;
            h = key_hash(key) & (unsigned int)(ctx->nbucket - 1);
        }
    }
    i = ctx->ntabs;
    memset(&ctx->tabs[i], 0, sizeof(SQL_TAB));
    ctx->tabs[i].key = (char *)malloc(strlen(key) + 1);
    if (!ctx->tabs[i].key) return -1;
    strcpy(ctx->tabs[i].key, key);
    ctx->tabs[i].next = ctx->bucket[h];
    ctx->bucket[h] = i;
    ctx->ntabs++;
    return i;
}

/*---------------------------------------------------------------------------
    Constraints, indexes and comments of the current table (s->table) as DDL
    text, collected per table and written at the end of the file.
 ---------------------------------------------------------------------------*/
static const char *tbl_comment(const ODV_SESSION *s)
{
    return ODV_TEXT(s->table.comment, s->table.comment_long);
}

static const char *col_comment(const ODV_SESSION *s, int i)
{
    return ODV_TEXT(s->table.columns[i].comment, s->table.columns[i].comment_long);
}

/* "name" inside a PL/SQL string literal: ' doubled */
static void put_plsql_ident(SQL_SB *b, const char *name)
{
    sb_putc(b, '"');
    for (; *name; name++) {
        if (*name == '\'') sb_putc(b, '\'');
        if (*name == '"') sb_putc(b, '"');
        sb_putc(b, *name);
    }
    sb_putc(b, '"');
}

/* Oracle COMMENT ON TABLE / COLUMN. COMMENT ON ... IS takes a literal only,
   so a comment with a line break, or too long for one line, is issued
   through EXECUTE IMMEDIATE with the literal built in PL/SQL: '' for a quote
   (doubled again inside the PL/SQL literal), CHR() for a line break, and
   pieces on lines of their own. */
static void ora_comment(SQL_SB *b, const char *schema, const char *table,
                        const char *column, const char *text)
{
    size_t n = strlen(text), pos = 0;
    size_t head = 40 + strlen(schema ? schema : "") + strlen(table) + strlen(column ? column : "");
    if (!strpbrk(text, "\r\n") && head + ora_rendered_len(text, n) <= SQL_LINE_MAX) {
        sb_puts(b, column ? "COMMENT ON COLUMN " : "COMMENT ON TABLE ");
        put_qname(b, schema, table, DBMS_ORACLE);
        if (column) { sb_putc(b, '.'); put_ident(b, column, DBMS_ORACLE); }
        sb_puts(b, " IS ");
        ora_piece(b, text, n);
        sb_puts(b, ";\n");
        return;
    }
    sb_puts(b, "BEGIN\n    EXECUTE IMMEDIATE 'COMMENT ON ");
    sb_puts(b, column ? "COLUMN " : "TABLE ");
    if (schema && schema[0]) { put_plsql_ident(b, schema); sb_putc(b, '.'); }
    put_plsql_ident(b, table);
    if (column) { sb_putc(b, '.'); put_plsql_ident(b, column); }
    sb_puts(b, " IS '''");
    while (pos < n) {
        size_t k = next_piece(text, n, pos, SQL_PIECE / 2, ORA_STR_MAX), i;
        sb_puts(b, "\n    || '");
        for (i = pos; i < pos + k; i++) {
            if (text[i] == '\'') sb_puts(b, "''''");
            else if (text[i] == '\n' || text[i] == '\r') sb_printf(b, "' || CHR(%d) || '", (int)(unsigned char)text[i]);
            else sb_putc(b, text[i]);
        }
        sb_putc(b, '\'');
        pos += k;
    }
    sb_puts(b, "\n    || '''';\nEND;\n/\n");
}

/* The column definition used again by MySQL's MODIFY COLUMN ... COMMENT */
static void col_definition(SQL_CONTEXT *ctx, int i, const char *colname, SQL_SB *b,
                           SQL_SB *tn, int mysql_text, int with_default);

static void render_comments(SQL_CONTEXT *ctx, const char *schema, const char *table,
                            SQL_SB *b, SQL_SB *nb)
{
    ODV_SESSION *s = ctx->session;
    int dbms = ctx->dbms, i;
    const char *tc;

    tc = tbl_comment(s);
    if (tc[0]) {
        const char *t = cvt_text(ctx, tc);
        SQL_SB tmp;
        memset(&tmp, 0, sizeof(tmp));
        sb_puts(&tmp, t);
        switch (dbms) {
        case DBMS_ORACLE:
            ora_comment(b, schema, table, NULL, sb_str(&tmp));
            break;
        case DBMS_POSTGRES:
            sb_puts(b, "COMMENT ON TABLE ");
            put_qname(b, schema, table, dbms);
            sb_puts(b, " IS ");
            other_text(b, tmp.p, tmp.len, dbms);
            sb_puts(b, ";\n");
            break;
        case DBMS_MYSQL:
            if (tmp.len > 2048 * 4) {
                sb_puts(nb, "-- NOTE: the table comment is longer than MySQL allows (2048 characters) and was not written\n");
                break;
            }
            sb_puts(b, "ALTER TABLE ");
            put_qname(b, schema, table, dbms);
            sb_puts(b, " COMMENT = ");
            other_text(b, tmp.p, tmp.len, dbms);
            sb_puts(b, ";\n");
            break;
        default:
            sb_puts(b, "EXEC sp_addextendedproperty N'MS_Description', ");
            other_text(b, tmp.p, tmp.len, dbms);
            sb_puts(b, ", N'SCHEMA', ");
            other_text(b, schema && schema[0] ? schema : "dbo", strlen(schema && schema[0] ? schema : "dbo"), dbms);
            sb_puts(b, ", N'TABLE', ");
            other_text(b, table, strlen(table), dbms);
            sb_puts(b, ";\n");
            break;
        }
        sb_free(&tmp);
    }
    for (i = 0; i < s->table.col_count; i++) {
        const char *cc = col_comment(s, i);
        SQL_SB name, tmp;
        if (!cc[0]) continue;
        memset(&name, 0, sizeof(name));
        memset(&tmp, 0, sizeof(tmp));
        sb_puts(&name, cvt_text(ctx, s->table.columns[i].name));
        sb_puts(&tmp, cvt_text(ctx, cc));
        switch (dbms) {
        case DBMS_ORACLE:
            ora_comment(b, schema, table, sb_str(&name), sb_str(&tmp));
            break;
        case DBMS_POSTGRES:
            sb_puts(b, "COMMENT ON COLUMN ");
            put_qname(b, schema, table, dbms);
            sb_putc(b, '.');
            put_ident(b, sb_str(&name), dbms);
            sb_puts(b, " IS ");
            other_text(b, tmp.p, tmp.len, dbms);
            sb_puts(b, ";\n");
            break;
        case DBMS_MYSQL:
            if (tmp.len > 1024 * 4) {
                sb_printf(nb, "-- NOTE: the comment of column \"%s\" is longer than MySQL allows (1024 characters) and was not written\n",
                          sb_str(&name));
                break;
            }
            sb_puts(b, "ALTER TABLE ");
            put_qname(b, schema, table, dbms);
            sb_puts(b, " MODIFY COLUMN ");
            {
                SQL_SB junk;
                memset(&junk, 0, sizeof(junk));
                col_definition(ctx, i, sb_str(&name), b, &junk, 0, 1);
                sb_free(&junk);
            }
            sb_puts(b, " COMMENT ");
            other_text(b, tmp.p, tmp.len, dbms);
            sb_puts(b, ";\n");
            break;
        default:
            sb_puts(b, "EXEC sp_addextendedproperty N'MS_Description', ");
            other_text(b, tmp.p, tmp.len, dbms);
            sb_puts(b, ", N'SCHEMA', ");
            other_text(b, schema && schema[0] ? schema : "dbo", strlen(schema && schema[0] ? schema : "dbo"), dbms);
            sb_puts(b, ", N'TABLE', ");
            other_text(b, table, strlen(table), dbms);
            sb_puts(b, ", N'COLUMN', ");
            other_text(b, name.p, name.len, dbms);
            sb_puts(b, ";\n");
            break;
        }
        sb_free(&name);
        sb_free(&tmp);
    }
}

/* Column list (c1, c2) of a constraint, names converted to the output charset */
static void put_col_list(SQL_CONTEXT *ctx, SQL_SB *b, const char cols[][ODV_OBJNAME_LEN + 1], int n)
{
    int j;
    sb_putc(b, '(');
    for (j = 0; j < n; j++) {
        if (j > 0) sb_puts(b, ", ");
        put_ident(b, cvt_text(ctx, cols[j]), ctx->dbms);
    }
    sb_putc(b, ')');
}

/* Is this a system-generated constraint name (SYS_C...)? Written unnamed. */
static int generated_name(const char *n)
{
    return n[0] == '\0' || (strncmp(n, "SYS_C", 5) == 0 && isdigit((unsigned char)n[5]));
}

static int col_is_lob(SQL_CONTEXT *ctx, const char *name)
{
    ODV_SESSION *s = ctx->session;
    int i;
    for (i = 0; i < s->table.col_count && i < ctx->ci_count; i++) {
        if (strcmp(s->table.columns[i].name, name) == 0) {
            int k = ctx->ci[i].kind;
            return k == K_CLOB || k == K_NCLOB || k == K_LONG || k == K_BLOB || k == K_LONGRAW ||
                   k == K_JSON || k == K_XML || k == K_VECTOR || k == K_UDT;
        }
    }
    return 0;
}

/* One constraint / index as DDL (b), or a note (nb) when it cannot be
   written for the dialect. FKs go to fk. */
static void render_constraint(SQL_CONTEXT *ctx, const char *schema, const char *table,
                              const ODV_CONSTRAINT *c, SQL_SB *b, SQL_SB *fk, SQL_SB *nb)
{
    ODV_SESSION *s = ctx->session;
    int dbms = ctx->dbms;
    SQL_SB *o = (c->type == CONSTRAINT_FK) ? fk : b;
    char cname[ODV_OBJNAME_LEN * 4 + 1];
    int named;

    odv_strcpy(cname, cvt_text(ctx, c->name), sizeof(cname) - 1);
    named = !generated_name(c->name);
    (void)s;

    if (c->type == CONSTRAINT_INDEX) {
        const char *expr;
        SQL_SB e, tx;
        memset(&e, 0, sizeof(e));
        memset(&tx, 0, sizeof(tx));
        trim_paren_ws(cvt_text(ctx, ODV_TEXT(c->index_expr, c->index_expr_long)), &tx);
        expr = sb_str(&tx);
        if (dbms == DBMS_ORACLE) {
            sb_puts(o, "CREATE INDEX ");
            if (c->name[0]) { put_ident(o, cname, dbms); sb_putc(o, ' '); }
            sb_puts(o, "ON ");
            put_qname(o, schema, table, dbms);
            sb_putc(o, ' ');
            if (expr[0]) put_wrapped(o, expr);
            else put_col_list(ctx, o, c->columns, c->col_count);
            sb_puts(o, ";\n");
            sb_free(&tx);
            return;
        }
        if (expr[0]) {
            if (!translate_expr(expr, &e, dbms)) {
                sb_printf(nb, "-- NOTE: index \"%s\" uses an Oracle expression and was not written: %s\n",
                          cname, expr);
                sb_free(&e);
                sb_free(&tx);
                return;
            }
        } else {
            put_col_list(ctx, &e, c->columns, c->col_count);
        }
        sb_free(&tx);
        if (dbms == DBMS_MYSQL) {
            int j;
            for (j = 0; j < c->col_count; j++)
                if (col_is_lob(ctx, c->columns[j])) {
                    sb_printf(nb, "-- NOTE: index \"%s\" is on a TEXT / BLOB column (MySQL needs a prefix length) and was not written\n", cname);
                    sb_free(&e);
                    return;
                }
        }
        sb_puts(o, "CREATE INDEX ");
        put_ident(o, c->name[0] ? cname : "IX", dbms);
        sb_puts(o, " ON ");
        put_qname(o, schema, table, dbms);
        sb_putc(o, ' ');
        sb_puts(o, sb_str(&e));
        sb_puts(o, ";\n");
        sb_free(&e);
        return;
    }

    if (c->type == CONSTRAINT_CHECK) {
        const char *cond;
        SQL_SB e, cc;
        memset(&cc, 0, sizeof(cc));
        /* exp gives the condition with its own outer parentheses, Data Pump
           without: drop one pair that encloses all of it, then write
           CHECK (...) for both */
        strip_outer_parens(cvt_text(ctx, ODV_TEXT(c->condition, c->condition_long)), &cc);
        cond = sb_str(&cc);
        if (!cond[0]) { sb_free(&cc); return; }
        memset(&e, 0, sizeof(e));
        if (dbms != DBMS_ORACLE) {
            if (!translate_expr(cond, &e, dbms)) {
                sb_printf(nb, "-- NOTE: CHECK constraint \"%s\" uses an Oracle expression and was not written: %s\n",
                          cname, cond);
                sb_free(&e);
                sb_free(&cc);
                return;
            }
            if (c->disabled && dbms != DBMS_SQLSERVER && dbms != DBMS_MYSQL) {
                sb_printf(nb, "-- NOTE: CHECK constraint \"%s\" is disabled in the source; %s cannot create a disabled constraint and it was not written\n",
                          cname, "PostgreSQL");
                sb_free(&e);
                sb_free(&cc);
                return;
            }
        }
        sb_puts(o, "ALTER TABLE ");
        put_qname(o, schema, table, dbms);
        if (dbms == DBMS_SQLSERVER && c->disabled) sb_puts(o, " WITH NOCHECK");
        sb_puts(o, " ADD ");
        if (named) { sb_puts(o, "CONSTRAINT "); put_ident(o, cname, dbms); sb_putc(o, ' '); }
        sb_puts(o, "CHECK ");
        if (dbms == DBMS_ORACLE) {
            sb_putc(o, '(');
            put_wrapped(o, cond);
            sb_putc(o, ')');
        } else {
            sb_putc(o, '(');
            sb_puts(o, sb_str(&e));
            sb_putc(o, ')');
        }
        if (dbms == DBMS_ORACLE && c->disabled) sb_puts(o, " DISABLE");
        if (dbms == DBMS_MYSQL && c->disabled) sb_puts(o, " NOT ENFORCED");
        sb_puts(o, ";\n");
        if (dbms == DBMS_SQLSERVER && c->disabled && named) {
            sb_puts(o, "ALTER TABLE ");
            put_qname(o, schema, table, dbms);
            sb_puts(o, " NOCHECK CONSTRAINT ");
            put_ident(o, cname, dbms);
            sb_puts(o, ";\n");
        }
        sb_free(&e);
        sb_free(&cc);
        return;
    }

    /* PK / UNIQUE / FK */
    if (c->col_count <= 0) return;
    if (dbms == DBMS_MYSQL && c->type != CONSTRAINT_FK) {
        int j;
        for (j = 0; j < c->col_count; j++)
            if (col_is_lob(ctx, c->columns[j])) {
                sb_printf(nb, "-- NOTE: %s \"%s\" is on a TEXT / BLOB column (MySQL needs a prefix length) and was not written\n",
                          c->type == CONSTRAINT_PK ? "PRIMARY KEY" : "UNIQUE", cname);
                return;
            }
    }
    if (c->type == CONSTRAINT_UNIQUE && c->unique_index) {
        sb_puts(o, "CREATE UNIQUE INDEX ");
        put_ident(o, c->name[0] ? cname : "UX", dbms);
        sb_puts(o, " ON ");
        put_qname(o, schema, table, dbms);
        sb_putc(o, ' ');
        put_col_list(ctx, o, c->columns, c->col_count);
        sb_puts(o, ";\n");
        return;
    }
    if (c->disabled && dbms != DBMS_ORACLE && !(dbms == DBMS_SQLSERVER && c->type == CONSTRAINT_FK)) {
        sb_printf(nb, "-- NOTE: constraint \"%s\" is disabled in the source and cannot be created disabled here; it was not written\n", cname);
        return;
    }
    if (c->type == CONSTRAINT_FK) {
        /* sort key for write_tail (named ones by name, then system-named
           ones as listed): exp writes its foreign keys after all tables and
           in another order than Data Pump */
        sb_putc(o, FK_MARK_START);
        sb_putc(o, named ? '0' : '1');
        if (named) sb_puts(o, cname);
        sb_putc(o, FK_MARK_END);
    }
    sb_puts(o, "ALTER TABLE ");
    put_qname(o, schema, table, dbms);
    if (dbms == DBMS_SQLSERVER && c->disabled) sb_puts(o, " WITH NOCHECK");
    sb_puts(o, " ADD ");
    if (named) { sb_puts(o, "CONSTRAINT "); put_ident(o, cname, dbms); sb_putc(o, ' '); }
    if (c->type == CONSTRAINT_PK) sb_puts(o, "PRIMARY KEY ");
    else if (c->type == CONSTRAINT_UNIQUE) sb_puts(o, "UNIQUE ");
    else sb_puts(o, "FOREIGN KEY ");
    put_col_list(ctx, o, c->columns, c->col_count);
    if (c->type == CONSTRAINT_FK) {
        char rs[ODV_OBJNAME_LEN * 4 + 1];
        odv_strcpy(rs, cvt_text(ctx, c->ref_schema[0] ? c->ref_schema : ""), sizeof(rs) - 1);
        sb_puts(o, " REFERENCES ");
        put_qname(o, rs[0] ? rs : schema, cvt_text(ctx, c->ref_table), dbms);
        sb_putc(o, ' ');
        if (c->ref_col_count > 0)
            put_col_list(ctx, o, c->ref_columns, c->ref_col_count);
        if (c->delete_rule[0]) { sb_puts(o, " ON DELETE "); sb_puts(o, c->delete_rule); }
    }
    if (c->deferrable && (dbms == DBMS_ORACLE || dbms == DBMS_POSTGRES)) {
        sb_puts(o, " DEFERRABLE");
        if (c->initially_deferred) sb_puts(o, " INITIALLY DEFERRED");
    } else if (c->deferrable) {
        sb_printf(nb, "-- NOTE: constraint \"%s\" is DEFERRABLE in the source; %s has no deferred constraints\n",
                  cname, dbms == DBMS_MYSQL ? "MySQL" : "SQL Server");
    }
    if (dbms == DBMS_ORACLE && c->disabled) sb_puts(o, " DISABLE");
    sb_puts(o, ";\n");
    if (dbms == DBMS_SQLSERVER && c->disabled && named) {
        sb_puts(o, "ALTER TABLE ");
        put_qname(o, schema, table, dbms);
        sb_puts(o, " NOCHECK CONSTRAINT ");
        put_ident(o, cname, dbms);
        sb_puts(o, ";\n");
    }
}

/* Order of a table's constraints / indexes in the SQL (see render_table_ddl) */
static int con_rank(const ODV_CONSTRAINT *c)
{
    switch (c->type) {
    case CONSTRAINT_PK:     return 1;
    case CONSTRAINT_UNIQUE: return c->unique_index ? 0 : 2;
    case CONSTRAINT_CHECK:  return 3;
    case CONSTRAINT_FK:     return 4;
    default:                return 5;
    }
}

/* An index (CREATE [UNIQUE] INDEX) rather than a constraint */
static int con_is_index(const ODV_CONSTRAINT *c)
{
    return c->type == CONSTRAINT_INDEX || (c->type == CONSTRAINT_UNIQUE && c->unique_index);
}

static char *sql_strdup(const char *p)
{
    size_t n = strlen(p);
    char *d = (char *)malloc(n + 1);
    if (d) memcpy(d, p, n + 1);
    return d;
}

/* Keep c as written (ddl / note) with its place in the order. 0 = kept */
static int piece_add(SQL_CPIECE **arr, int *n, int *cap, const ODV_CONSTRAINT *c, int seq,
                     const SQL_SB *ddl, const SQL_SB *note)
{
    SQL_CPIECE *p;
    if (ddl->err || note->err) return -1;
    if (*n >= *cap) {
        int nc = *cap ? *cap * 2 : 16;
        SQL_CPIECE *na = (SQL_CPIECE *)realloc(*arr, (size_t)nc * sizeof(SQL_CPIECE));
        if (!na) return -1;
        *arr = na;
        *cap = nc;
    }
    p = &(*arr)[*n];
    memset(p, 0, sizeof(*p));
    p->rank = con_rank(c);
    p->gen = !c->name[0] || generated_name(c->name);
    p->seq = seq;
    odv_strcpy(p->name, c->name, ODV_OBJNAME_LEN);
    p->ddl = sql_strdup(sb_str(ddl));
    p->note = sql_strdup(sb_str(note));
    if (!p->ddl || !p->note) {
        free(p->ddl);
        free(p->note);
        return -1;
    }
    (*n)++;
    return 0;
}

static void pieces_free(SQL_CPIECE *arr, int n)
{
    int i;
    for (i = 0; i < n; i++) {
        free(arr[i].ddl);
        free(arr[i].note);
    }
}

/* Unique indexes (a key on the same columns then uses the index in Oracle
   instead of failing with ORA-02261), PRIMARY KEY, UNIQUE, CHECK, FOREIGN KEY
   (to the end of the file), then the indexes; within a kind by name
   (system-named ones last, as listed): the same SQL for exp and expdp, which
   list them in different orders, also for the indexes exp writes after the
   last table (listed after the table's own, as in the list's JSON) */
static int piece_cmp(const SQL_CPIECE *a, const SQL_CPIECE *b)
{
    int r;
    if (a->rank != b->rank) return a->rank - b->rank;
    if (a->gen != b->gen) return a->gen - b->gen;
    if (!a->gen && (r = strcmp(a->name, b->name)) != 0) return r;
    return a->seq - b->seq;
}

/* DDL that follows the rows of the current table: written at the end of
   the file (rendered again on every table callback of the table; exp gives
   one per partition and the last one has the complete set). */
static void render_table_ddl(SQL_CONTEXT *ctx, int ti, const char *schema, const char *table)
{
    ODV_SESSION *s = ctx->session;
    SQL_TAB *t = &ctx->tabs[ti];
    SQL_SB d, nb;
    int i;

    pieces_free(t->pc, t->npc);
    t->npc = 0;
    sb_clear(&t->fk);
    sb_clear(&t->cm);
    sb_clear(&t->notes);
    memset(&d, 0, sizeof(d));
    memset(&nb, 0, sizeof(nb));
    if (ctx->create_index || ctx->create_table) {
        for (i = 0; i < s->table.constraint_count; i++) {
            const ODV_CONSTRAINT *c = &s->table.constraints[i];
            if (!(con_is_index(c) ? ctx->create_index : ctx->create_table)) continue;
            sb_clear(&d);
            sb_clear(&nb);
            render_constraint(ctx, schema, table, c, &d, &t->fk, &nb);
            if ((d.len || nb.len || d.err || nb.err) &&
                piece_add(&t->pc, &t->npc, &t->cpc, c, i, &d, &nb) != 0)
                ctx->failed = 1;
        }
    }
    sb_free(&d);
    sb_free(&nb);
    if (ctx->write_comments)
        render_comments(ctx, schema, table, &t->cm, &t->notes);
    if (s->table.notes) {
        const char *p = s->table.notes;
        while (*p) {
            const char *e = strchr(p, '\n');
            size_t n = e ? (size_t)(e - p) : strlen(p);
            sb_puts(&t->notes, "-- NOTE: ");
            sb_putn(&t->notes, p, n);
            sb_putc(&t->notes, '\n');
            p += n + (e ? 1 : 0);
        }
    }
}

/*---------------------------------------------------------------------------
    CREATE TABLE and the INSERT prefix
 ---------------------------------------------------------------------------*/
static void col_definition(SQL_CONTEXT *ctx, int i, const char *colname, SQL_SB *b,
                           SQL_SB *tn, int mysql_text, int with_default)
{
    ODV_SESSION *s = ctx->session;
    const ODV_COLUMN *c = &s->table.columns[i];
    const char *dflt = ODV_TEXT(c->default_val, c->default_long);
    put_ident(b, colname, ctx->dbms);
    sb_putc(b, ' ');
    map_type(ctx, i, colname, b, tn, mysql_text);
    /* DEFAULT before NOT NULL: Oracle rejects the other order (ORA-00907, M-32) */
    if (with_default && dflt[0]) {
        const char *d = cvt_text(ctx, dflt);
        if (ctx->dbms == DBMS_ORACLE) {
            size_t n = strlen(d);
            while (n > 0 && isspace((unsigned char)d[n - 1])) n--;
            while (n > 0 && isspace((unsigned char)*d)) { d++; n--; }
            if (n > 0) {
                char *dd = (char *)malloc(n + 1);
                if (dd) {
                    memcpy(dd, d, n);
                    dd[n] = '\0';
                    sb_puts(b, " DEFAULT ");
                    put_wrapped(b, dd);
                    free(dd);
                }
            }
        } else {
            SQL_SB lit;
            memset(&lit, 0, sizeof(lit));
            if (default_portable(d, &lit, ctx->dbms, ctx->ci[i].kind)) {
                sb_puts(b, " DEFAULT ");
                sb_puts(b, sb_str(&lit));
            } else {
                note_col(tn, colname, "DEFAULT %s is an Oracle expression and was not written", d);
            }
            sb_free(&lit);
        }
    }
    if (c->not_null && !c->not_null_pk) sb_puts(b, " NOT NULL");
}

static void write_create_table(SQL_CONTEXT *ctx, SQL_SB *b, SQL_SB *tn, const char *schema,
                               const char *table, int col_count, const char **col_names)
{
    ODV_SESSION *s = ctx->session;
    int dbms = ctx->dbms, i, mysql_text = 0;

    /* DROP TABLE IF EXISTS */
    switch (dbms) {
    case DBMS_ORACLE:
        /* PL/SQL block (no IF EXISTS before 23ai); the names sit in a string
           literal, so a ' in them is doubled */
        sb_puts(b, "BEGIN EXECUTE IMMEDIATE 'DROP TABLE ");
        if (schema && schema[0]) { put_plsql_ident(b, schema); sb_putc(b, '.'); }
        put_plsql_ident(b, table);
        sb_puts(b, " CASCADE CONSTRAINTS PURGE'; EXCEPTION WHEN OTHERS THEN NULL; END;\n/\n\n");
        break;
    case DBMS_MYSQL:
        sb_puts(b, "DROP TABLE IF EXISTS ");
        put_qname(b, schema, table, dbms);
        sb_puts(b, ";\n\n");
        break;
    case DBMS_SQLSERVER: {
        SQL_SB q;
        memset(&q, 0, sizeof(q));
        put_qname(&q, schema, table, dbms);
        sb_puts(b, "IF OBJECT_ID(");
        other_text(b, q.p, q.len, dbms);
        sb_puts(b, ", N'U') IS NOT NULL DROP TABLE ");
        sb_puts(b, sb_str(&q));
        sb_puts(b, ";\n\n");
        sb_free(&q);
        break;
    }
    default:
        sb_puts(b, "DROP TABLE IF EXISTS ");
        put_qname(b, schema, table, dbms);
        sb_puts(b, " CASCADE;\n\n");
        break;
    }

    if (dbms == DBMS_MYSQL) {
        /* MySQL's row size limit (65,535 bytes over all columns): if the
           VARCHARs would exceed it, the long ones become TEXT */
        SQL_SB tmp, junk;
        long total = 0;
        memset(&tmp, 0, sizeof(tmp));
        memset(&junk, 0, sizeof(junk));
        for (i = 0; i < col_count && i < s->table.col_count; i++) {
            sb_clear(&tmp);
            map_type(ctx, i, col_names[i], &tmp, &junk, 0);
            total += mysql_col_bytes(sb_str(&tmp));
        }
        if (total > MYSQL_ROW_MAX) {
            mysql_text = 1;
            sb_printf(tn, "-- NOTE: the VARCHAR columns would exceed MySQL's row size limit (%d bytes); those longer than 255 characters are TEXT\n",
                      MYSQL_ROW_MAX);
        }
        sb_free(&tmp);
        sb_free(&junk);
    }

    sb_puts(b, "CREATE TABLE ");
    put_qname(b, schema, table, dbms);
    sb_puts(b, " (\n");
    for (i = 0; i < col_count; i++) {
        if (i > 0) sb_puts(b, ",\n");
        sb_puts(b, "    ");
        if (i < s->table.col_count)
            col_definition(ctx, i, col_names[i], b, tn, mysql_text, 1);
        else {
            put_ident(b, col_names[i], dbms);
            sb_puts(b, dbms == DBMS_ORACLE ? " VARCHAR2(4000)" : " VARCHAR(255)");
        }
    }
    sb_puts(b, "\n);\n\n");
}

/* "INSERT INTO s.t (c1, ...) VALUES (" of any length (it was cut at 4096
   bytes, M-28); for Oracle broken into lines that SQL*Plus accepts. */
static void build_prefix(SQL_CONTEXT *ctx, const char *schema, const char *table,
                         int col_count, const char **col_names)
{
    SQL_SB *b = &ctx->prefix;
    int i;
    sb_clear(b);
    sb_puts(b, "INSERT INTO ");
    put_qname(b, schema, table, ctx->dbms);
    sb_puts(b, " (");
    for (i = 0; i < col_count; i++) {
        if (i > 0) {
            sb_puts(b, ", ");
            if (ctx->dbms == DBMS_ORACLE &&
                sb_col(b) + strlen(col_names[i]) + 8 > SQL_LINE_MAX)
                sb_putc(b, '\n');
        }
        put_ident(b, col_names[i], ctx->dbms);
    }
    sb_puts(b, ") VALUES (");
}

/* Column info for the current table (map_type fills it) */
static int prepare_columns(SQL_CONTEXT *ctx, int col_count, const char **col_names)
{
    ODV_SESSION *s = ctx->session;
    SQL_SB tmp, junk;
    int i, n = col_count;
    if (n < s->table.col_count) n = s->table.col_count;
    if (n < 1) n = 1;
    if (n > ctx->ci_count) {
        SQL_COLINFO *nc = (SQL_COLINFO *)realloc(ctx->ci, (size_t)n * sizeof(SQL_COLINFO));
        if (!nc) return 0;
        ctx->ci = nc;
        ctx->ci_count = n;
    }
    memset(ctx->ci, 0, (size_t)ctx->ci_count * sizeof(SQL_COLINFO));
    memset(&tmp, 0, sizeof(tmp));
    memset(&junk, 0, sizeof(junk));
    for (i = 0; i < s->table.col_count && i < col_count; i++) {
        sb_clear(&tmp);
        map_type(ctx, i, col_names[i], &tmp, &junk, 0);
    }
    for (; i < ctx->ci_count; i++) ctx->ci[i].kind = K_TEXT;
    sb_free(&tmp);
    sb_free(&junk);
    return 1;
}

static void out_sb(SQL_CONTEXT *ctx, const SQL_SB *b)
{
    if (b->err) { ctx->failed = 1; return; }
    if (b->len && fwrite(b->p, 1, b->len, ctx->fp) != b->len) ctx->failed = 1;
}

/* Header, table notes, DROP / CREATE TABLE of a table met for the first time */
static void begin_table(SQL_CONTEXT *ctx, const char *schema, const char *table,
                        int col_count, const char **col_names)
{
    ODV_SESSION *s = ctx->session;
    SQL_SB b, tn;
    char key[ODV_OBJNAME_LEN * 8 + 4];
    int ti;

    make_key(key, sizeof(key), schema, table);
    ti = tab_find(ctx, key, 1);
    ctx->cur_tab = ti;
    free(ctx->last_key);
    ctx->last_key = (char *)malloc(strlen(key) + 1);
    if (ctx->last_key) strcpy(ctx->last_key, key);
    ctx->header_written = 1;
    prepare_columns(ctx, col_count, col_names);
    build_prefix(ctx, schema, table, col_count, col_names);

    memset(&b, 0, sizeof(b));
    memset(&tn, 0, sizeof(tn));
    if (ti >= 0 && ctx->tabs[ti].begun) {
        /* expdp writes a table's partitions apart: its INSERTs go on */
        sb_puts(&b, "\n-- Table: ");
        if (schema && schema[0]) { sb_puts(&b, schema); sb_putc(&b, '.'); }
        sb_puts(&b, table);
        sb_puts(&b, " (continued)\n\n");
        out_sb(ctx, &b);
        sb_free(&b);
        return;
    }
    if (ti >= 0) ctx->tabs[ti].begun = 1;

    sb_puts(&b, "-- Table: ");
    if (schema && schema[0]) { sb_puts(&b, schema); sb_putc(&b, '.'); }
    sb_puts(&b, table);
    sb_putc(&b, '\n');
    if (s && s->app_version[0])
        sb_printf(&b, "-- Generated by Open DUMP Viewer for Oracle database v%s\n", s->app_version);
    else
        sb_puts(&b, "-- Generated by Open DUMP Viewer for Oracle database\n");

    if (ctx->create_table) {
        SQL_SB ddl;
        memset(&ddl, 0, sizeof(ddl));
        write_create_table(ctx, &ddl, &tn, schema, table, col_count, col_names);
        if (tn.len) sb_puts(&b, sb_str(&tn));
        sb_putc(&b, '\n');
        sb_puts(&b, sb_str(&ddl));
        sb_free(&ddl);
    } else {
        /* the type notes still apply to the values */
        int i;
        SQL_SB tmp;
        memset(&tmp, 0, sizeof(tmp));
        for (i = 0; i < s->table.col_count && i < col_count; i++) {
            sb_clear(&tmp);
            map_type(ctx, i, col_names[i], &tmp, &tn, 0);
        }
        sb_free(&tmp);
        if (tn.len) sb_puts(&b, sb_str(&tn));
        sb_putc(&b, '\n');
    }
    out_sb(ctx, &b);
    sb_free(&b);
    sb_free(&tn);
}

/*---------------------------------------------------------------------------
    Values (TYPE_MAPPING_SPEC 4)
 ---------------------------------------------------------------------------*/

/* A row note: "-- NOTE: column "C" of the next row: ..." */
static void row_note(SQL_CONTEXT *ctx, const char *col, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    put_note(&ctx->rnotes, "-- NOTE: column \"", col, "\" of the next row: ", fmt, ap);
    va_end(ap);
}

static int is_numeric_value(const char *val)
{
    int digits = 0, dots = 0;
    if (!val || !*val) return 0;
    if (*val == '-' || *val == '+') val++;
    if (!*val) return 0;
    while (*val) {
        if (*val == '.') {
            if (++dots > 1) return 0;
        } else if (*val >= '0' && *val <= '9') {
            digits++;
        } else if ((*val == 'E' || *val == 'e') && digits > 0) {
            /* exponent: [+-]digits to the end */
            const char *q = val + 1;
            if (*q == '+' || *q == '-') q++;
            if (!*q) return 0;
            while (*q) { if (*q < '0' || *q > '9') return 0; q++; }
            return 1;
        } else {
            return 0;
        }
        val++;
    }
    return digits > 0;
}

/* Integer / fractional / significant digits of a decimal text (with an
   optional exponent), for the precision notes. */
static void number_digits(const char *v, int *int_digits, int *frac_digits, int *sig)
{
    const char *p = v;
    int intd = 0, fracd = 0, exp10 = 0, first = -1, last = -1, pos = 0;
    char digs[160];
    int nd = 0, dot = -1, i;
    if (*p == '-' || *p == '+') p++;
    for (; *p && *p != 'E' && *p != 'e'; p++) {
        if (*p == '.') { dot = nd; continue; }
        if (nd < (int)sizeof(digs)) digs[nd++] = *p;
    }
    if (*p == 'E' || *p == 'e') exp10 = atoi(p + 1);
    if (dot < 0) dot = nd;
    for (i = 0; i < nd; i++) if (digs[i] != '0') { if (first < 0) first = i; last = i; }
    *sig = (first < 0) ? 0 : last - first + 1;
    if (first < 0) { *int_digits = 0; *frac_digits = 0; return; }
    pos = dot + exp10;                    /* decimal point position in digs */
    intd = pos - first;
    if (intd < 0) intd = 0;
    fracd = last + 1 - pos;
    if (fracd < 0) fracd = 0;
    *int_digits = intd;
    *frac_digits = fracd;
}

/* The canonical date text the parser gives the SQL export: SYYYY/MM/DD
   HH24:MI:SS[.F...] (s->date_format is held at SLASH while exporting). */
typedef struct { int y, mo, d, h, mi, se; char frac[16]; } SQL_DT;

static int parse_dt(const char *v, SQL_DT *t)
{
    int n = 0;
    const char *dot;
    memset(t, 0, sizeof(*t));
    if (sscanf(v, "%d/%d/%d %d:%d:%d%n", &t->y, &t->mo, &t->d, &t->h, &t->mi, &t->se, &n) != 6)
        return 0;
    dot = v + n;
    if (*dot == '.') {
        size_t k = 0;
        dot++;
        while (isdigit((unsigned char)dot[k]) && k + 1 < sizeof(t->frac)) { t->frac[k] = dot[k]; k++; }
        t->frac[k] = '\0';
        if (dot[k]) return 0;
    } else if (*dot) {
        return 0;
    }
    return t->y != 0;
}

/* frac digits truncated to `keep` (trailing zeros of the source kept) */
static void put_frac(SQL_SB *b, const char *frac, int keep)
{
    int n = (int)strlen(frac);
    if (n > keep) n = keep;
    if (n > 0) { sb_putc(b, '.'); sb_putn(b, frac, (size_t)n); }
}

static void put_year(SQL_SB *b, int y)
{
    if (y < 0) sb_printf(b, "-%04d", -y);
    else sb_printf(b, "%04d", y);
}

/* DATE / TIMESTAMP value */
static void put_datetime(SQL_CONTEXT *ctx, int i, const char *col, const char *v)
{
    SQL_SB *b = &ctx->vals;
    SQL_COLINFO *ci = &ctx->ci[i];
    int dbms = ctx->dbms;
    SQL_DT t;
    if (!parse_dt(v, &t)) {
        /* not the canonical form (should not happen): as a string */
        if (dbms == DBMS_ORACLE) ora_text(b, v, strlen(v));
        else other_text(b, v, strlen(v), dbms);
        return;
    }
    if (dbms == DBMS_ORACLE) {
        sb_puts(b, ci->kind == K_DATE ? "TO_DATE('" : "TO_TIMESTAMP('");
        put_year(b, t.y);
        sb_printf(b, "/%02d/%02d %02d:%02d:%02d", t.mo, t.d, t.h, t.mi, t.se);
        if (ci->kind != K_DATE) put_frac(b, t.frac, 9);
        sb_puts(b, ci->kind == K_DATE ? "', 'SYYYY/MM/DD HH24:MI:SS')" : "', 'SYYYY/MM/DD HH24:MI:SS.FF')");
        return;
    }
    if (t.y < 0 && dbms != DBMS_POSTGRES) {
        sb_puts(b, "NULL");
        row_note(ctx, col, "%04d BC is outside the %s date range; written as NULL", -t.y,
                 dbms == DBMS_MYSQL ? "MySQL" : "SQL Server");
        return;
    }
    if (dbms == DBMS_MYSQL && t.y < 1000)
        row_note(ctx, col, "year %d is before MySQL's supported DATETIME range (1000-9999)", t.y);
    if ((int)strlen(t.frac) > ci->tgt_frac && ci->kind != K_DATE) {
        const char *f = t.frac + ci->tgt_frac;
        while (*f == '0') f++;
        if (*f) row_note(ctx, col, "fractional seconds .%s cut to %d digits", t.frac, ci->tgt_frac);
    }
    sb_puts(b, dbms == DBMS_SQLSERVER ? "N'" : "'");
    sb_printf(b, "%04d-%02d-%02d%c%02d:%02d:%02d", t.y < 0 ? -t.y : t.y, t.mo, t.d,
              dbms == DBMS_SQLSERVER ? 'T' : ' ', t.h, t.mi, t.se);
    if (ci->kind != K_DATE) put_frac(b, t.frac, ci->tgt_frac);
    if (t.y < 0) sb_puts(b, " BC");
    sb_putc(b, '\'');
}

/* TSTZ / TSLTZ in raw form for a non-Oracle dialect */
static void put_tz_other(SQL_CONTEXT *ctx, int i, const char *col, const char *v)
{
    SQL_SB *b = &ctx->vals;
    SQL_COLINFO *ci = &ctx->ci[i];
    ODV_SESSION *s = ctx->session;
    int dbms = ctx->dbms;
    ODV_TS_PARTS lp, up;
    int32_t off = 0;
    int has_off = 0;
    char frac[16];
    const ODV_TS_PARTS *w;
    int use_off;

    if (odv_tz_sql_parts(s, s->table.columns[i].type, v, &lp, &up, &off, &has_off) != ODV_OK) {
        sb_puts(b, "NULL");
        row_note(ctx, col, "the time zone value could not be decoded; written as NULL");
        return;
    }
    if (dbms == DBMS_MYSQL) { w = &up; use_off = 0; }
    else { w = &lp; use_off = has_off; }
    if (dbms == DBMS_SQLSERVER && use_off &&
        (off % 60 != 0 || off > 14 * 3600 || off < -14 * 3600)) {
        w = &up; off = 0;           /* datetimeoffset takes whole minutes within +-14:00 */
    }
    if (w->year < 0 && dbms != DBMS_POSTGRES) {
        sb_puts(b, "NULL");
        row_note(ctx, col, "%04d BC is outside the %s date range; written as NULL", -w->year,
                 dbms == DBMS_MYSQL ? "MySQL" : "SQL Server");
        return;
    }
    snprintf(frac, sizeof(frac), "%09u", (unsigned)w->nano);
    {
        int keep = ci->ts_frac < ci->tgt_frac ? ci->ts_frac : ci->tgt_frac;
        const char *f = frac + keep;
        while (*f == '0') f++;
        if (*f) row_note(ctx, col, "fractional seconds .%s cut to %d digits", frac, keep);
        frac[keep] = '\0';
    }
    sb_puts(b, dbms == DBMS_SQLSERVER ? "N'" : "'");
    sb_printf(b, "%04d-%02d-%02d%c%02d:%02d:%02d", w->year < 0 ? -w->year : w->year, w->mon, w->day,
              dbms == DBMS_SQLSERVER ? 'T' : ' ', w->hour, w->min, w->sec);
    if (frac[0]) { sb_putc(b, '.'); sb_puts(b, frac); }
    if (use_off) {
        int32_t a = off < 0 ? -off : off;
        sb_printf(b, "%c%02d:%02d", off < 0 ? '-' : '+', (int)(a / 3600), (int)(a / 60 % 60));
        if (a % 60) sb_printf(b, ":%02d", (int)(a % 60));
    }
    if (w->year < 0) sb_puts(b, " BC");
    sb_putc(b, '\'');
}

/* INTERVAL for PostgreSQL: Oracle's +Y-M / +D HH:MI:SS.F as unit words */
static void put_interval_pg(SQL_CONTEXT *ctx, int i, const char *col, const char *v)
{
    SQL_SB *b = &ctx->vals;
    SQL_COLINFO *ci = &ctx->ci[i];
    const char *sg = (v[0] == '-') ? "-" : "";
    const char *p = (v[0] == '-' || v[0] == '+') ? v + 1 : v;
    if (ci->kind == K_IYM) {
        long y = 0, m = 0;
        if (sscanf(p, "%ld-%ld", &y, &m) != 2) { other_text(b, v, strlen(v), DBMS_POSTGRES); return; }
        /* PostgreSQL keeps the months of an interval in 32 bits:
           178956970 years 7 months is its largest (measured on 16) */
        if ((long long)y * 12 + m > 2147483647LL + (sg[0] ? 1 : 0)) {
            sb_puts(b, "NULL");
            row_note(ctx, col, "%s is outside PostgreSQL's interval range (about 178 million years); written as NULL", v);
            return;
        }
        sb_printf(b, "'%s%ld years %s%ld months'", sg, y, sg, m);
    } else {
        long d = 0;
        int h = 0, mi = 0, se = 0, n = 0;
        char frac[16] = "";
        if (sscanf(p, "%ld %d:%d:%d%n", &d, &h, &mi, &se, &n) != 4) {
            other_text(b, v, strlen(v), DBMS_POSTGRES);
            return;
        }
        if (p[n] == '.') {
            size_t k = 0;
            const char *f = p + n + 1;
            while (isdigit((unsigned char)f[k]) && k + 1 < sizeof(frac)) { frac[k] = f[k]; k++; }
            frac[k] = '\0';
        }
        if ((int)strlen(frac) > 6) {
            const char *f = frac + 6;
            while (*f == '0') f++;
            if (*f) row_note(ctx, col, "fractional seconds .%s cut to 6 digits", frac);
            frac[6] = '\0';
        }
        sb_printf(b, "'%s%ld days %s%d hours %s%d minutes %s%d%s%s seconds'", sg, d, sg, h, sg, mi,
                  sg, se, frac[0] ? "." : "", frac);
    }
}

/* Hex text check (RAW / BLOB values are upper-case hex, exp RAW "0x...") */
static const char *hex_body(const char *v, size_t *n)
{
    size_t i;
    if (v[0] == '0' && (v[1] == 'x' || v[1] == 'X')) { v += 2; *n -= 2; }
    for (i = 0; i < *n; i++) if (!isxdigit((unsigned char)v[i])) return NULL;
    if (*n % 2) return NULL;
    return v;
}

/* Declares a PL/SQL variable for column i (Oracle), returns its name in nm */
static void plsql_var(SQL_CONTEXT *ctx, int i, const char *type, char *nm, size_t cap)
{
    snprintf(nm, cap, "v%d", i + 1);
    sb_printf(&ctx->decl, "  %s %s;\n", nm, type);
    ctx->plsql = 1;
}

/* Oracle: a CLOB / NCLOB value in a temporary LOB filled piece by piece */
static void ora_lob_var(SQL_CONTEXT *ctx, int i, const char *v, size_t n, const char *type,
                        const char *fn, char *nm, size_t cap)
{
    size_t pos = 0;
    plsql_var(ctx, i, type, nm, cap);
    sb_printf(&ctx->pre, "  DBMS_LOB.CREATETEMPORARY(%s, TRUE);\n", nm);
    while (pos < n) {
        size_t k = next_piece(v, n, pos, SQL_PIECE, ORA_STR_MAX);
        sb_printf(&ctx->pre, "  DBMS_LOB.APPEND(%s, %s(", nm, fn);
        ora_piece(&ctx->pre, v + pos, k);
        sb_puts(&ctx->pre, "));\n");
        pos += k;
    }
    sb_printf(&ctx->post, "  DBMS_LOB.FREETEMPORARY(%s);\n", nm);
}

static void put_value(SQL_CONTEXT *ctx, int i, const char *col, const char *v, long n, int vflags)
{
    ODV_SESSION *s = ctx->session;
    SQL_SB *b = &ctx->vals;
    SQL_COLINFO *ci = &ctx->ci[i];
    int dbms = ctx->dbms;
    int kind = ci->kind;
    char nm[16];

    if (!v || n < 0) { sb_puts(b, "NULL"); return; }
    if (vflags & ODV_VFLAG_MARKER) {
        sb_puts(b, "NULL");
        if (kind == K_UDT)
            row_note(ctx, col, "the value of type %s cannot be decoded (its type is not in the dump); written as NULL",
                     s->table.columns[i].type_str);
        else
            row_note(ctx, col, "the value could not be decoded (%.*s); written as NULL", (int)(n > 40 ? 40 : n), v);
        return;
    }
    if (vflags & ODV_VFLAG_CUT) {
        sb_puts(b, "NULL");
        row_note(ctx, col, "the value is longer than %d MiB and was not kept whole; written as NULL",
                 ODV_FULL_VALUE_MAX / (1024 * 1024));
        return;
    }
    if (kind == K_UDT) {
        /* An object type / VARRAY value has no literal form (a string is
           ORA-00932 in Oracle); what the parser shows for one is not the value */
        sb_puts(b, "NULL");
        row_note(ctx, col, "the value of type %s has no SQL literal form; written as NULL",
                 s->table.columns[i].type_str);
        return;
    }
    if (n == 0) {
        /* a non-NULL empty value (only a LOB can be one in Oracle) */
        if (dbms == DBMS_ORACLE) {
            if (kind == K_CLOB) sb_puts(b, "EMPTY_CLOB()");
            else if (kind == K_NCLOB) sb_puts(b, "EMPTY_CLOB()");
            else if (kind == K_BLOB) sb_puts(b, "EMPTY_BLOB()");
            else sb_puts(b, "NULL");
        } else if (kind == K_BLOB || kind == K_RAW || kind == K_LONGRAW) {
            sb_puts(b, dbms == DBMS_POSTGRES ? "'\\x'::BYTEA" : dbms == DBMS_MYSQL ? "X''" : "0x");
        } else {
            other_text(b, "", 0, dbms);
        }
        return;
    }

    switch (kind) {
    case K_NUM: case K_FLOAT: case K_BINF: case K_BIND:
        if ((kind == K_BINF || kind == K_BIND) &&
            (strcmp(v, "NaN") == 0 || strcmp(v, "Inf") == 0 || strcmp(v, "-Inf") == 0)) {
            int is_f = (kind == K_BINF);
            if (dbms == DBMS_ORACLE) {
                if (strcmp(v, "NaN") == 0) sb_puts(b, is_f ? "BINARY_FLOAT_NAN" : "BINARY_DOUBLE_NAN");
                else if (strcmp(v, "Inf") == 0) sb_puts(b, is_f ? "BINARY_FLOAT_INFINITY" : "BINARY_DOUBLE_INFINITY");
                else sb_printf(b, "-%s", is_f ? "BINARY_FLOAT_INFINITY" : "BINARY_DOUBLE_INFINITY");
            } else if (dbms == DBMS_POSTGRES) {
                sb_printf(b, "'%s'::%s", strcmp(v, "Inf") == 0 ? "Infinity" :
                          strcmp(v, "-Inf") == 0 ? "-Infinity" : v, is_f ? "REAL" : "DOUBLE PRECISION");
            } else {
                sb_puts(b, "NULL");
                row_note(ctx, col, "%s has no %s; written as NULL", v, dbms == DBMS_MYSQL ? "MySQL value" : "SQL Server value");
            }
            return;
        }
        if (!is_numeric_value(v)) break;          /* not a number: as a string below */
        sb_putn(b, v, (size_t)n);
        /* Oracle: a BINARY_FLOAT / BINARY_DOUBLE literal (1.5f / 1E+300d) is
           converted straight to the binary type; without the suffix it is a
           NUMBER first, which overflows beyond 1E126 (ORA-01426) */
        if (dbms == DBMS_ORACLE && (kind == K_BINF || kind == K_BIND))
            sb_putc(b, kind == K_BINF ? 'f' : 'd');
        if (dbms != DBMS_ORACLE && (ci->dec_p > 0 || ci->tgt_double)) {
            int intd, fracd, sig;
            number_digits(v, &intd, &fracd, &sig);
            if (ci->tgt_double && sig > 15)
                row_note(ctx, col, "%s has %d significant digits; the double keeps 15-17", v, sig);
            else if (ci->dec_p > 0 && intd > ci->dec_p - ci->dec_s)
                row_note(ctx, col, "%s does not fit DECIMAL(%d,%d); the INSERT fails", v, ci->dec_p, ci->dec_s);
            else if (ci->dec_p > 0 && fracd > ci->dec_s)
                row_note(ctx, col, "%s is rounded to %d decimal places by DECIMAL(%d,%d)", v, ci->dec_s,
                         ci->dec_p, ci->dec_s);
        }
        return;

    case K_DATE: case K_TS:
        put_datetime(ctx, i, col, v);
        return;

    case K_TSTZ: case K_TSLTZ:
        if (v[0] == '#') {
            if (dbms == DBMS_ORACLE) {
                char tzsql[256];
                int is_expr = 0;
                if (odv_tz_sql_value(s, s->table.columns[i].type, s->table.columns[i].precision, v,
                                     dbms, tzsql, (int)sizeof(tzsql), &is_expr) != ODV_OK) {
                    sb_puts(b, "NULL");
                    row_note(ctx, col, "the time zone value could not be decoded; written as NULL");
                } else if (is_expr) {
                    sb_puts(b, tzsql);
                } else if (!tzsql[0]) {
                    sb_puts(b, "NULL");
                } else {
                    ora_text(b, tzsql, strlen(tzsql));
                }
            } else {
                put_tz_other(ctx, i, col, v);
            }
            return;
        }
        break;

    case K_IYM: case K_IDS:
        if (dbms == DBMS_POSTGRES) { put_interval_pg(ctx, i, col, v); return; }
        break;

    case K_BOOL:
        if (strcmp(v, "TRUE") == 0 || strcmp(v, "FALSE") == 0) {
            if (dbms == DBMS_SQLSERVER) sb_puts(b, v[0] == 'T' ? "1" : "0");
            else sb_puts(b, v);
            return;
        }
        break;

    case K_BFILE:
        if (dbms == DBMS_ORACLE && strncmp(v, "BFILENAME('", 11) == 0 && v[n - 1] == ')') {
            /* the parser's text BFILENAME('dir', 'file') has its quotes doubled:
               check its shape and use it as the expression */
            const char *p = v + 11;
            int part = 0, ok = 1;
            while (*p && ok) {
                if (*p == '\'') {
                    if (p[1] == '\'') { p += 2; continue; }
                    if (part == 0 && strncmp(p, "', '", 4) == 0) { part = 1; p += 4; continue; }
                    if (part == 1 && strcmp(p, "')") == 0) { part = 2; break; }
                    ok = 0;
                    break;
                }
                p++;
            }
            if (ok && part == 2) { sb_putn(b, v, (size_t)n); return; }
        }
        break;

    case K_RAW: case K_LONGRAW: case K_BLOB: {
        size_t hn = (size_t)n;
        const char *h = hex_body(v, &hn);
        size_t bytes = hn / 2;
        if (!h) {
            sb_puts(b, "NULL");
            row_note(ctx, col, "the value is not hexadecimal; written as NULL");
            return;
        }
        if (dbms == DBMS_POSTGRES) { sb_puts(b, "'\\x"); sb_putn(b, h, hn); sb_puts(b, "'::BYTEA"); return; }
        if (dbms == DBMS_MYSQL) { sb_puts(b, "X'"); sb_putn(b, h, hn); sb_putc(b, '\''); return; }
        if (dbms == DBMS_SQLSERVER) { sb_puts(b, "0x"); sb_putn(b, h, hn); return; }
        if (bytes <= ORA_RAW_MAX) {
            /* HEXTORAW('..') (pieces joined by || stay within RAW's 2000 bytes) */
            size_t pos = 0;
            if (hn + 12 <= SQL_PIECE) { sb_puts(b, "HEXTORAW('"); sb_putn(b, h, hn); sb_puts(b, "')"); return; }
            while (pos < hn) {
                size_t k = hn - pos > SQL_PIECE ? SQL_PIECE : hn - pos;
                if (pos > 0) sb_puts(b, "||\n");
                sb_puts(b, "HEXTORAW('");
                sb_putn(b, h + pos, k);
                sb_puts(b, "')");
                pos += k;
            }
            return;
        }
        if (kind == K_BLOB) {
            size_t pos = 0;
            plsql_var(ctx, i, "BLOB", nm, sizeof(nm));
            sb_printf(&ctx->pre, "  DBMS_LOB.CREATETEMPORARY(%s, TRUE);\n", nm);
            while (pos < hn) {
                size_t k = hn - pos > SQL_PIECE ? SQL_PIECE : hn - pos;
                sb_printf(&ctx->pre, "  DBMS_LOB.WRITEAPPEND(%s, %d, HEXTORAW('", nm, (int)(k / 2));
                sb_putn(&ctx->pre, h + pos, k);
                sb_puts(&ctx->pre, "'));\n");
                pos += k;
            }
            sb_printf(&ctx->post, "  DBMS_LOB.FREETEMPORARY(%s);\n", nm);
            sb_puts(b, nm);
            return;
        }
        if (bytes > ORA_PLSQL_LONG) {
            sb_puts(b, "NULL");
            row_note(ctx, col, "%ld bytes is more than a SQL script can insert into %s (PL/SQL limit 32760); written as NULL",
                     (long)bytes, kind == K_RAW ? "RAW" : "LONG RAW");
            return;
        }
        {
            size_t pos = 0;
            plsql_var(ctx, i, kind == K_RAW ? "RAW(32767)" : "LONG RAW", nm, sizeof(nm));
            while (pos < hn) {
                size_t k = hn - pos > SQL_PIECE ? SQL_PIECE : hn - pos;
                sb_printf(&ctx->pre, "  %s := UTL_RAW.CONCAT(%s, HEXTORAW('", nm, nm);
                sb_putn(&ctx->pre, h + pos, k);
                sb_puts(&ctx->pre, "'));\n");
                pos += k;
            }
            sb_puts(b, nm);
        }
        return;
    }
    default:
        break;
    }

    /* Text */
    if (dbms != DBMS_ORACLE) {
        if (dbms == DBMS_POSTGRES && has_byte(v, (size_t)n, '\0')) {
            /* PostgreSQL text cannot hold U+0000 (no literal or function
               gives one). The user's decision: write the value without its
               0x00 bytes and say so for the row and column. */
            char *w = (char *)malloc((size_t)n + 1);
            size_t k = 0, removed = 0;
            long j;
            if (!w) { ctx->failed = 1; sb_puts(b, "NULL"); return; }
            for (j = 0; j < n; j++) {
                if (v[j] == '\0') removed++;
                else w[k++] = v[j];
            }
            w[k] = '\0';
            if (k == 0) sb_puts(b, "''");
            else other_text(b, w, k, dbms);
            free(w);
            row_note(ctx, col, "%lu byte(s) 0x00 removed from the value (PostgreSQL text cannot hold U+0000)",
                     (unsigned long)removed);
            return;
        }
        other_text(b, v, (size_t)n, dbms);
        return;
    }
    if (n <= ORA_STR_MAX) {
        ora_text(b, v, (size_t)n);
        return;
    }
    switch (kind) {
    case K_CLOB: case K_JSON: case K_VECTOR:
        ora_lob_var(ctx, i, v, (size_t)n, "CLOB", "TO_CLOB", nm, sizeof(nm));
        sb_puts(b, nm);
        return;
    case K_NCLOB:
        ora_lob_var(ctx, i, v, (size_t)n, "NCLOB", "TO_NCLOB", nm, sizeof(nm));
        sb_puts(b, nm);
        return;
    case K_XML:
        ora_lob_var(ctx, i, v, (size_t)n, "CLOB", "TO_CLOB", nm, sizeof(nm));
        sb_printf(b, "XMLTYPE(%s)", nm);
        return;
    case K_LONG: {
        size_t pos = 0;
        if (n > ORA_PLSQL_LONG) {
            sb_puts(b, "NULL");
            row_note(ctx, col, "%ld bytes is more than a SQL script can insert into LONG (PL/SQL limit 32760); written as NULL",
                     n);
            return;
        }
        plsql_var(ctx, i, "LONG", nm, sizeof(nm));
        while (pos < (size_t)n) {
            size_t k = next_piece(v, (size_t)n, pos, SQL_PIECE, ORA_STR_MAX);
            sb_printf(&ctx->pre, "  %s := %s || ", nm, nm);
            ora_piece(&ctx->pre, v + pos, k);
            sb_puts(&ctx->pre, ";\n");
            pos += k;
        }
        sb_puts(b, nm);
        return;
    }
    case K_NTEXT:
        ora_clob_concat(b, v, (size_t)n, "TO_NCLOB");
        return;
    default:
        /* VARCHAR2 / CHAR beyond 4000 bytes (a non-UTF-8 source charset, or
           MAX_STRING_SIZE=EXTENDED): concatenated as CLOB */
        ora_clob_concat(b, v, (size_t)n, "TO_CLOB");
        return;
    }
}

/*---------------------------------------------------------------------------
    One row
 ---------------------------------------------------------------------------*/
static void write_row(SQL_CONTEXT *ctx, const char *schema, const char *table,
                      int col_count, const char **col_names,
                      const char **vals, const int *lens)
{
    ODV_SESSION *s = ctx->session;
    SQL_SB *out = &ctx->tmp;
    int i;
    size_t one_line;

    (void)schema; (void)table;
    sb_clear(&ctx->decl);
    sb_clear(&ctx->pre);
    sb_clear(&ctx->post);
    sb_clear(&ctx->rnotes);
    sb_clear(out);
    ctx->plsql = 0;

    /* render each value on its own, then lay the row out */
    {
        size_t *starts = NULL;
        starts = (size_t *)malloc(((size_t)col_count + 1) * sizeof(size_t));
        if (!starts) { ctx->failed = 1; return; }
        sb_clear(&ctx->vals);
        for (i = 0; i < col_count; i++) {
            int fl = 0;
            starts[i] = ctx->vals.len;
            if (s && i < s->record.col_count && i < s->record.max_columns)
                fl = s->record.values[i].flags;
            put_value(ctx, i, col_names[i], vals[i], lens[i], fl);
        }
        starts[col_count] = ctx->vals.len;

        /* one line when it fits (the form the INSERT always had) */
        one_line = ctx->prefix.len + ctx->vals.len + 2 * (size_t)col_count + 2;
        if (ctx->dbms != DBMS_ORACLE ||
            (one_line <= SQL_LINE_MAX && !memchr(sb_str(&ctx->prefix), '\n', ctx->prefix.len) &&
             !memchr(sb_str(&ctx->vals), '\n', ctx->vals.len))) {
            sb_puts(out, sb_str(&ctx->prefix));
            for (i = 0; i < col_count; i++) {
                if (i > 0) sb_puts(out, ", ");
                sb_putn(out, ctx->vals.p + starts[i], starts[i + 1] - starts[i]);
            }
            sb_puts(out, ");\n");
        } else {
            /* Oracle, too long for one line: values from a new line when the
               current one is full; a value split into pieces is already on
               lines of its own */
            sb_puts(out, sb_str(&ctx->prefix));
            for (i = 0; i < col_count; i++) {
                size_t len = starts[i + 1] - starts[i];
                const char *vp = ctx->vals.p + starts[i];
                const char *nl = memchr(vp, '\n', len);
                size_t first = nl ? (size_t)(nl - vp) : len;
                if (i > 0) sb_puts(out, ", ");
                if (sb_col(out) + first + 3 > SQL_LINE_MAX || (nl && sb_col(out) > 0))
                    sb_putc(out, '\n');
                sb_putn(out, vp, len);
            }
            sb_puts(out, ");\n");
        }
        free(starts);
    }

    if (ctx->rnotes.len) out_sb(ctx, &ctx->rnotes);
    if (ctx->plsql) {
        SQL_SB blk;
        memset(&blk, 0, sizeof(blk));
        sb_puts(&blk, "DECLARE\n");
        sb_puts(&blk, sb_str(&ctx->decl));
        sb_puts(&blk, "BEGIN\n");
        sb_puts(&blk, sb_str(&ctx->pre));
        sb_puts(&blk, sb_str(out));
        sb_puts(&blk, sb_str(&ctx->post));
        sb_puts(&blk, "END;\n/\n");
        out_sb(ctx, &blk);
        sb_free(&blk);
    } else {
        out_sb(ctx, out);
    }
}

/*---------------------------------------------------------------------------
    Table switch / end
 ---------------------------------------------------------------------------*/
/* One table's SQL: the target's name, and its schema when the session's
 * filter names one. The table callback also fires for the tables the parser
 * reads past (exp: the SQL export reads on to the DDL exp writes after the
 * last table, M-17), and another schema can have a table of the same name. */
static int same_table_as_target(SQL_CONTEXT *ctx, const char *schema, const char *table)
{
    const ODV_SESSION *s = ctx->session;
    if (!ctx->target_table || !ctx->target_table[0]) return 1;
    if (strcmp(table, ctx->target_table) != 0) return 0;
    if (s && s->filter_active && s->filter_schema[0] && schema &&
        odv_stricmp(schema, s->filter_schema) != 0)
        return 0;
    return 1;
}

static void switch_table(SQL_CONTEXT *ctx, const char *schema, const char *table,
                         int col_count, const char **col_names)
{
    char key[ODV_OBJNAME_LEN * 8 + 4];
    make_key(key, sizeof(key), schema, table);
    if (ctx->header_written && ctx->last_key && strcmp(ctx->last_key, key) == 0) return;
    if (ctx->header_written) {
        static const char nl[] = "\n";
        if (fwrite(nl, 1, 1, ctx->fp) != 1) ctx->failed = 1;
    }
    begin_table(ctx, schema, table, col_count, col_names);
}

/*---------------------------------------------------------------------------
    Callbacks
 ---------------------------------------------------------------------------*/
/* Rows with the values' lengths (ODV_ROW_CALLBACK_EX): a value may hold
   0x00, NULL is a length of -1 and an empty LOB (EMPTY_CLOB()) a length of 0 */
static void ODV_CALL sql_row_callback(
    const char *schema, const char *table,
    int col_count, const char **col_names, const char **col_values,
    const int *col_lens, void *user_data)
{
    SQL_CONTEXT *ctx = (SQL_CONTEXT *)user_data;

    if (!ctx || !ctx->fp) return;
    if (!same_table_as_target(ctx, schema, table)) return;

    switch_table(ctx, schema, table, col_count, col_names);
    if (!ctx->write_inserts) return;

    write_row(ctx, schema, table, col_count, col_names, col_values, col_lens);

    ctx->row_count++;
    if (ctx->session && ctx->session->progress_cb && (ctx->row_count % 100) == 0)
        ctx->session->progress_cb(ctx->row_count, table, ctx->session->progress_ud);
}

/* Called once per table (and per partition), after its rows: the zero-row
   table's CREATE TABLE, and the DDL that follows the rows. */
static void ODV_CALL sql_table_callback(
    const char *schema, const char *table,
    int col_count, const char **col_names, const char **col_types,
    const int *col_not_nulls, const char **col_defaults,
    int constraint_count, const char *constraints_json,
    int64_t row_count, int64_t data_offset, void *user_data)
{
    SQL_CONTEXT *ctx = (SQL_CONTEXT *)user_data;
    char key[ODV_OBJNAME_LEN * 8 + 4];
    int ti;

    (void)col_types; (void)col_not_nulls; (void)col_defaults;
    (void)constraint_count; (void)constraints_json;
    (void)row_count; (void)data_offset;

    if (!ctx || !ctx->fp) return;
    if (!same_table_as_target(ctx, schema, table)) return;
    /* a table callback of another table than the one parsed (the session's
       table has moved on) carries no DDL to take */
    switch_table(ctx, schema, table, col_count, col_names);
    make_key(key, sizeof(key), schema, table);
    ti = tab_find(ctx, key, 1);
    if (ti >= 0) render_table_ddl(ctx, ti, schema, table);
}

/* exp: a foreign key / index written after the last table (M-33) */
static void sql_late_ddl(ODV_SESSION *s, const char *schema, const char *table,
                         const ODV_CONSTRAINT *c, void *user_data)
{
    SQL_CONTEXT *ctx = (SQL_CONTEXT *)user_data;
    char sch[ODV_OBJNAME_LEN * 4 + 1], tab[ODV_OBJNAME_LEN * 4 + 1];
    char key[ODV_OBJNAME_LEN * 8 + 4];
    int ti;
    SQL_TAB *t;
    SQL_SB d, notes;
    (void)s;
    if (!ctx) return;
    odv_strcpy(sch, cvt_text(ctx, schema), sizeof(sch) - 1);
    odv_strcpy(tab, cvt_text(ctx, table), sizeof(tab) - 1);
    make_key(key, sizeof(key), sch, tab);
    ti = tab_find(ctx, key, 0);
    if (ti < 0) return;                      /* a table not in this file */
    if (!(con_is_index(c) ? ctx->create_index : ctx->create_table)) return;
    t = &ctx->tabs[ti];
    memset(&d, 0, sizeof(d));
    memset(&notes, 0, sizeof(notes));
    /* into the table's order (piece_cmp), after the table's own when tied */
    render_constraint(ctx, sch, tab, c, &d, &t->lfk, &notes);
    if ((d.len || notes.len || d.err || notes.err) &&
        piece_add(&t->lpc, &t->nlpc, &t->clpc, c, (1 << 20) + t->nlpc, &d, &notes) != 0)
        ctx->failed = 1;
    sb_free(&d);
    sb_free(&notes);
}

/*---------------------------------------------------------------------------
    write_sql_file
 ---------------------------------------------------------------------------*/
/* A table's foreign keys in key order (see render_constraint), without the marks */
static void put_sorted_fks(SQL_SB *out, const SQL_SB *fk)
{
    const char *p = sb_str(fk), *end = p + fk->len;
    const char **key, **body;
    size_t *klen, *blen;
    int n = 0, cap = 0, i, j;
    const char *q;
    for (q = p; q < end; q++) if (*q == FK_MARK_START) cap++;
    if (cap == 0) { sb_puts(out, p); return; }
    key = (const char **)malloc((size_t)cap * sizeof(char *));
    body = (const char **)malloc((size_t)cap * sizeof(char *));
    klen = (size_t *)malloc((size_t)cap * sizeof(size_t));
    blen = (size_t *)malloc((size_t)cap * sizeof(size_t));
    if (!key || !body || !klen || !blen) {
        free((void *)key); free((void *)body); free(klen); free(blen);
        sb_puts(out, p);
        return;
    }
    while (p < end && *p != FK_MARK_START) sb_putc(out, *p++);
    while (p < end && n < cap) {
        const char *k = p + 1, *ke = k, *bd;
        while (ke < end && *ke != FK_MARK_END) ke++;
        bd = (ke < end) ? ke + 1 : ke;
        q = bd;
        while (q < end && *q != FK_MARK_START) q++;
        key[n] = k; klen[n] = (size_t)(ke - k);
        body[n] = bd; blen[n] = (size_t)(q - bd);
        /* stable insertion by key */
        for (j = n; j > 0; j--) {
            size_t m = klen[j - 1] < klen[j] ? klen[j - 1] : klen[j];
            int c = memcmp(key[j - 1], key[j], m);
            if (c < 0 || (c == 0 && klen[j - 1] <= klen[j])) break;
            { const char *t = key[j]; key[j] = key[j - 1]; key[j - 1] = t; }
            { const char *t = body[j]; body[j] = body[j - 1]; body[j - 1] = t; }
            { size_t t = klen[j]; klen[j] = klen[j - 1]; klen[j - 1] = t; }
            { size_t t = blen[j]; blen[j] = blen[j - 1]; blen[j - 1] = t; }
        }
        n++;
        p = q;
    }
    for (i = 0; i < n; i++) sb_putn(out, body[i], blen[i]);
    free((void *)key); free((void *)body); free(klen); free(blen);
}

static void write_tail(SQL_CONTEXT *ctx)
{
    SQL_SB b;
    int i, any = 0;
    memset(&b, 0, sizeof(b));
    for (i = 0; i < ctx->ntabs; i++) {
        SQL_TAB *t = &ctx->tabs[i];
        int n = t->npc + t->nlpc, j, k;
        const SQL_CPIECE **ord;
        if (!n && !t->cm.len && !t->notes.len) continue;
        if (!any) sb_puts(&b, "\n-- Constraints, indexes and comments\n");
        any = 1;
        sb_putc(&b, '\n');
        /* the table's constraints / indexes and the ones exp writes after
           the last table in one order, then the comments, then the notes in
           the same order (3.2 / 8.1 of the type mapping) */
        ord = (const SQL_CPIECE **)malloc((size_t)(n > 0 ? n : 1) * sizeof(*ord));
        if (!ord) { ctx->failed = 1; break; }
        for (j = 0; j < n; j++) {
            ord[j] = j < t->npc ? &t->pc[j] : &t->lpc[j - t->npc];
            for (k = j; k > 0 && piece_cmp(ord[k - 1], ord[k]) > 0; k--) {
                const SQL_CPIECE *tmp = ord[k];
                ord[k] = ord[k - 1];
                ord[k - 1] = tmp;
            }
        }
        for (j = 0; j < n; j++) sb_puts(&b, ord[j]->ddl);
        sb_puts(&b, sb_str(&t->cm));
        for (j = 0; j < n; j++) sb_puts(&b, ord[j]->note);
        sb_puts(&b, sb_str(&t->notes));
        free((void *)ord);
    }
    any = 0;
    for (i = 0; i < ctx->ntabs; i++) {
        SQL_TAB *t = &ctx->tabs[i];
        SQL_SB all;
        if (!t->fk.len && !t->lfk.len) continue;
        if (!any) sb_puts(&b, "\n-- Foreign keys\n\n");
        any = 1;
        memset(&all, 0, sizeof(all));
        sb_puts(&all, sb_str(&t->fk));
        sb_puts(&all, sb_str(&t->lfk));
        if (all.err) ctx->failed = 1;
        put_sorted_fks(&b, &all);
        sb_free(&all);
    }
    out_sb(ctx, &b);
    sb_free(&b);
}

static void ctx_free(SQL_CONTEXT *ctx)
{
    int i;
    for (i = 0; i < ctx->ntabs; i++) {
        free(ctx->tabs[i].key);
        pieces_free(ctx->tabs[i].pc, ctx->tabs[i].npc);
        pieces_free(ctx->tabs[i].lpc, ctx->tabs[i].nlpc);
        free(ctx->tabs[i].pc);
        free(ctx->tabs[i].lpc);
        sb_free(&ctx->tabs[i].cm);
        sb_free(&ctx->tabs[i].notes);
        sb_free(&ctx->tabs[i].fk);
        sb_free(&ctx->tabs[i].lfk);
    }
    free(ctx->tabs);
    free(ctx->bucket);
    free(ctx->ci);
    free(ctx->cvt);
    free(ctx->last_key);
    sb_free(&ctx->prefix);
    sb_free(&ctx->vals);
    sb_free(&ctx->decl);
    sb_free(&ctx->pre);
    sb_free(&ctx->post);
    sb_free(&ctx->rnotes);
    sb_free(&ctx->tmp);
    sb_free(&ctx->tmp2);
}

int write_sql_file(ODV_SESSION *s, const char *table_name,
                   const char *output_path, int dbms_type)
{
    SQL_CONTEXT ctx;
    ODV_ROW_CALLBACK saved_row_cb;
    ODV_ROW_CALLBACK_EX saved_row_cb_ex;
    void *saved_row_ud;
    ODV_TABLE_CALLBACK saved_table_cb;
    void *saved_table_ud;
    int saved_filter_active = 0;
    int64_t saved_seek_offset = 0;
    int saved_date_format;
    char saved_custom[sizeof(s->custom_date_format)];
    int rc;
    int all_tables;

    if (!s || !output_path) return ODV_ERROR_INVALID_ARG;

    all_tables = (!table_name || table_name[0] == '\0');

    memset(&ctx, 0, sizeof(ctx));
    ctx.fp = odv_fopen_path(output_path, "wb");   /* UTF-8 path (M-26) */
    if (!ctx.fp) {
        odv_strcpy(s->last_error, "Cannot create SQL output file", ODV_MSG_LEN);
        return ODV_ERROR_FOPEN;
    }
    ctx.target_table = table_name;
    ctx.dbms = dbms_type;
    ctx.session = s;
    ctx.cur_tab = -1;
    ctx.create_table   = (s->sql_flags & ODV_SQL_CREATE_TABLE) ? 1 : 0;
    ctx.create_index   = (s->sql_flags & ODV_SQL_CREATE_INDEX) ? 1 : 0;
    ctx.write_comments = (s->sql_flags & ODV_SQL_WRITE_COMMENTS) ? 1 : 0;
    ctx.write_inserts  = (s->sql_flags & ODV_SQL_WRITE_INSERTS) ? 1 : 0;
    ctx.all_tables = all_tables;

    /* File head */
    if (dbms_type == DBMS_ORACLE) {
        /* SQL*Plus: no '&' substitution, blank lines inside statements (H-20) */
        fputs("SET DEFINE OFF\n", ctx.fp);
        fputs("SET SQLBLANKLINES ON\n\n", ctx.fp);
    } else if (dbms_type == DBMS_POSTGRES) {
        fputs("SET standard_conforming_strings = on;\n\n", ctx.fp);
    } else if (dbms_type == DBMS_MYSQL) {
        fputs("-- Backslashes in string literals are escaped (\\\\): run with the default sql_mode, not NO_BACKSLASH_ESCAPES.\n\n", ctx.fp);
    }

    /* Save and replace callbacks */
    saved_row_cb = s->row_cb;
    saved_row_cb_ex = s->row_cb_ex;
    saved_row_ud = s->row_ud;
    saved_table_cb = s->table_cb;
    saved_table_ud = s->table_ud;
    s->row_cb = NULL;
    s->row_cb_ex = sql_row_callback;
    s->row_ud = &ctx;
    s->table_cb = sql_table_callback;
    s->table_ud = &ctx;

    if (all_tables) {
        saved_filter_active = s->filter_active;
        saved_seek_offset = s->seek_offset;
        s->filter_active = 0;
        s->seek_offset = 0;
    }

    /* Dates in a fixed form whatever the display date format (M-13): the
       SQL writer reads SYYYY/MM/DD HH24:MI:SS[.F] */
    saved_date_format = s->date_format;
    memcpy(saved_custom, s->custom_date_format, sizeof(saved_custom));
    s->date_format = DATE_FMT_SLASH;
    s->custom_date_format[0] = '\0';

    s->cancelled = 0;
    s->total_rows = 0;

    if (s->dump_type == DUMP_UNKNOWN) {
        rc = detect_dump_kind(s);
        if (rc != ODV_OK) {
            fclose(ctx.fp);
            s->row_cb = saved_row_cb;
            s->row_cb_ex = saved_row_cb_ex;
            s->row_ud = saved_row_ud;
            s->table_cb = saved_table_cb;
            s->table_ud = saved_table_ud;
            s->date_format = saved_date_format;
            memcpy(s->custom_date_format, saved_custom, sizeof(saved_custom));
            if (all_tables) {
                s->filter_active = saved_filter_active;
                s->seek_offset = saved_seek_offset;
            }
            ctx_free(&ctx);
            return rc;
        }
    }

    /* TSTZ / TSLTZ values reach the row callback as raw bytes (M-09); LOB /
       LONG values whole; the DDL of the tables (expdp: from the master table;
       exp: also what comes after the last table) */
    s->tz_raw_values = 1;
    s->full_values = 1;
    s->want_ddl_meta = (ctx.create_table || ctx.create_index || ctx.write_comments) ? 1 : 0;
    s->late_ddl = sql_late_ddl;
    s->late_ddl_ud = &ctx;

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

    s->tz_raw_values = 0;
    s->full_values = 0;
    s->want_ddl_meta = 0;
    s->late_ddl = NULL;
    s->late_ddl_ud = NULL;
    s->date_format = saved_date_format;
    memcpy(s->custom_date_format, saved_custom, sizeof(saved_custom));

    write_tail(&ctx);
    if (ctx.failed && rc >= ODV_OK) {
        odv_strcpy(s->last_error, "Writing the SQL output file failed", ODV_MSG_LEN);
        rc = ODV_ERROR_FWRITE;
    }
    if (fclose(ctx.fp) != 0 && rc >= ODV_OK) {
        odv_strcpy(s->last_error, "Writing the SQL output file failed", ODV_MSG_LEN);
        rc = ODV_ERROR_FWRITE;
    }

    s->row_cb = saved_row_cb;
    s->row_cb_ex = saved_row_cb_ex;
    s->row_ud = saved_row_ud;
    s->table_cb = saved_table_cb;
    s->table_ud = saved_table_ud;
    if (all_tables) {
        s->filter_active = saved_filter_active;
        s->seek_offset = saved_seek_offset;
    }
    ctx_free(&ctx);
    return rc;
}
