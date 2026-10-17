/*****************************************************************************
    Open DUMP Viewer for Oracle database

    odv_exp.c
    Legacy EXP (conventional export) format dump file parsing

    EXP format structure:
      - 256-byte binary header (version, mode, charset)
      - DDL text (CREATE TABLE, INSERT INTO)
      - Binary record data (2-byte LE length + column data)

    Copyright (C) 2026 YANAI Taketo
 *****************************************************************************/

#include "odv_types.h"
#include <stdio.h>
#include <stdarg.h>
#include <ctype.h>

/*---------------------------------------------------------------------------
    Constants
 ---------------------------------------------------------------------------*/
#define EXP_HEADER_SIZE     0x100
#define EXP_DDL_BUF_SIZE    262144   /* initial size of the statement buffer */
#define EXP_DDL_BUF_MAX     (64 * 1024 * 1024)  /* it grows up to this (one statement) */
#define EXP_DEF_CONV_LEN    1024     /* name / short DEFAULT, client→UTF-8 (longer: heap) */

/*---------------------------------------------------------------------------
    Forward declarations
 ---------------------------------------------------------------------------*/
static int parse_exp_header(ODV_SESSION *s, ODV_VFILE *fp);
static int parse_exp_ddl_and_data(ODV_SESSION *s, ODV_VFILE *fp, int list_only);
static int parse_column_type(const char *type_str, ODV_COLUMN *col);
static void trim_right(char *str);

/*---------------------------------------------------------------------------
    Helper: case-insensitive prefix match
 ---------------------------------------------------------------------------*/
static int starts_with_ci(const char *str, const char *prefix)
{
    while (*prefix) {
        if (toupper((unsigned char)*str) != toupper((unsigned char)*prefix))
            return 0;
        str++;
        prefix++;
    }
    return 1;
}

/*---------------------------------------------------------------------------
    Helper: skip whitespace
 ---------------------------------------------------------------------------*/
static const char *skip_ws(const char *p)
{
    while (*p && ((unsigned char)*p <= ' ')) p++;
    return p;
}

/*---------------------------------------------------------------------------
    Helper: extract quoted identifier or bare word
    Returns pointer past the extracted token.
 ---------------------------------------------------------------------------*/
static const char *extract_identifier(const char *p, char *out, int max_len)
{
    int len = 0;
    p = skip_ws(p);

    if (*p == '"') {
        p++; /* skip opening quote */
        while (*p && *p != '"' && len < max_len - 1) {
            out[len++] = *p++;
        }
        if (*p == '"') p++; /* skip closing quote */
    } else {
        while (*p && *p != ' ' && *p != ',' && *p != '(' && *p != ')'
               && *p != '\t' && *p != '\n' && *p != '\r'
               && len < max_len - 1) {
            out[len++] = *p++;
        }
    }
    out[len] = '\0';
    return p;
}

/*---------------------------------------------------------------------------
    byte_to_charset

    Maps a charset indicator byte from the EXP header to an internal constant.
    The byte is the low 8 bits of the Oracle NLS_CHARACTERSET ID.
    Byte ranges:
      0x30-0x3f -> JA16EUC                 (e.g. JA16EUC=830=0x33E)
      0x40-0x4f -> JA16SJIS                (e.g. JA16SJIS=832=0x340)
      0x50-0x5f -> ZHS16GBK (Simplified Chinese, includes CGB231280/GB18030)
      0x60-0x6f -> UTF8                    (e.g. AL32UTF8=873=0x369)
      0xd0-0xdf -> UTF16                   (e.g. AL16UTF16=2000=0x7D0)
      other     -> US7ASCII
 ---------------------------------------------------------------------------*/
static int byte_to_charset(unsigned char b)
{
    if (b >= 0x30 && b <= 0x3f) return CHARSET_EUC;
    if (b >= 0x40 && b <= 0x4f) return CHARSET_SJIS;
    if (b >= 0x50 && b <= 0x5f) return CHARSET_GBK;
    if (b >= 0x60 && b <= 0x6f) return CHARSET_UTF8;
    if (b >= 0xd0 && b <= 0xdf) return CHARSET_UTF16LE;
    return CHARSET_US7;
}

/*---------------------------------------------------------------------------
    id_to_charset

    Maps a full 16-bit Oracle NLS_CHARACTERSET id to an internal constant.
    byte_to_charset() above looks only at the low 8 bits, which collides for
    several ids that matter here: JA16EUCTILDE=837=0x345 would look like SJIS,
    and US7ASCII=1 / WE8ISO8859P1=31=0x01f / WE8ISO8859P15=46=0x02e /
    WE8MSWIN1252=178=0x0b2 all fall into the low-byte "other" (US7) bucket.
    The EXP header carries the real 2-byte id (header record 7), so resolve the
    known ones exactly and fall back to the low-byte heuristic for the rest.

    UTF8 (id 871, CESU-8) and AL32UTF8 (id 873) both map to CHARSET_UTF8; the
    CESU-8 vs UTF-8 difference (supplementary-plane characters) is handled where
    names are stored, by content, not by id.
 ---------------------------------------------------------------------------*/
static int id_to_charset(int id)
{
    switch (id) {
    case 1:    return CHARSET_US7;         /* US7ASCII */
    case 31:   return CHARSET_US8;         /* WE8ISO8859P1 */
    case 46:   return CHARSET_ISO8859P15;  /* WE8ISO8859P15 */
    case 178:  return CHARSET_WIN1252;     /* WE8MSWIN1252 */
    case 830:                              /* JA16EUC */
    case 831:                              /* JA16EUCYEN */
    case 837:  return CHARSET_EUC;         /* JA16EUCTILDE */
    case 832:                              /* JA16SJIS */
    case 834:                              /* JA16SJISYEN */
    case 838:  return CHARSET_SJIS;        /* JA16SJISTILDE */
    case 852:                              /* ZHS16GBK */
    case 854:  return CHARSET_GBK;         /* ZHS32GB18030 (treated as GBK) */
    case 871:                              /* UTF8 (CESU-8) */
    case 873:  return CHARSET_UTF8;        /* AL32UTF8 */
    case 2000: return CHARSET_UTF16LE;     /* AL16UTF16 */
    default:   return byte_to_charset((unsigned char)(id & 0xFF));
    }
}

/*---------------------------------------------------------------------------
    normalize_cesu8

    Collapses CESU-8 surrogate pairs into 4-byte UTF-8, in place. The Oracle
    charset "UTF8" (id 871) is CESU-8: it encodes a supplementary-plane
    character (U+10000..U+10FFFF) as two 3-byte surrogate halves
    (ED A0-AF 80-BF  ED B0-BF 80-BF) instead of one 4-byte sequence like
    AL32UTF8 (id 873). The engine otherwise treats both as CHARSET_UTF8 and
    would hand the raw CESU-8 bytes to the product, which are not valid UTF-8,
    so a name such as "絵文字😀" round-trips to U+FFFD and its table can no
    longer be opened (H-09).

    A valid UTF-8 string never contains the surrogate byte range (ED A0..ED BF),
    so this rewrites only genuine CESU-8 pairs and is a no-op on canonical
    UTF-8 (AL32UTF8). It is therefore safe to run on any UTF8-family name, with
    no dependence on the exact charset id. The result is never longer than the
    input (6 bytes -> 4), so it stays within the buffer. The short-circuited
    checks never read past the terminating NUL. */
static void normalize_cesu8(char *str)
{
    unsigned char *s = (unsigned char *)str;
    unsigned char *d = s;
    while (*s) {
        if (s[0] == 0xED && (s[1] & 0xF0) == 0xA0 && (s[2] & 0xC0) == 0x80 &&
            s[3] == 0xED && (s[4] & 0xF0) == 0xB0 && (s[5] & 0xC0) == 0x80) {
            unsigned int hi = (unsigned int)((s[1] & 0x0F) << 6) | (s[2] & 0x3F);
            unsigned int lo = (unsigned int)((s[4] & 0x0F) << 6) | (s[5] & 0x3F);
            unsigned int cp = 0x10000u + ((hi << 10) | lo);
            d[0] = (unsigned char)(0xF0 | (cp >> 18));
            d[1] = (unsigned char)(0x80 | ((cp >> 12) & 0x3F));
            d[2] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
            d[3] = (unsigned char)(0x80 | (cp & 0x3F));
            s += 6;
            d += 4;
        } else {
            *d++ = *s++;
        }
    }
    *d = '\0';
}

/*---------------------------------------------------------------------------
    parse_exp_header

    Reads the 256-byte EXP header and extracts:
    - Oracle version
    - Export mode (TABLE/USER/DATABASE)
    - Character sets (client, database, NLS)

    Header structure: records separated by 0x00 or 0x0A, starting at offset 6.
    Record 0: Oracle version (e.g. "V11.02.00")
    Record 1: Export user/schema
    Record 2: Export mode ("RTABLES"/"RUSERS"/"RENTIRE")
    Records 3-6: (misc)
    Record 7: Charset info - byte[1]=env, byte[3]=tbl, byte[5]=nls
 ---------------------------------------------------------------------------*/
static int parse_exp_header(ODV_SESSION *s, ODV_VFILE *fp)
{
    unsigned char hdr[EXP_HEADER_SIZE];
    unsigned char word[256];
    int i, ct, step, cs_start;

    odv_vseek(fp, 0, SEEK_SET);
    if (odv_vread(hdr, 1, EXP_HEADER_SIZE, fp) != EXP_HEADER_SIZE) {
        odv_strcpy(s->last_error, "Cannot read EXP header", ODV_MSG_LEN);
        return ODV_ERROR_FREAD;
    }

    /* Detect direct-path export. It inserts "D\n" immediately after the 3-byte
       charset prefix and in front of the "EXPORT" signature, shifting the rest
       of the header two bytes:
         conventional: 03 <cs hi> <cs lo> 'E' 'X' 'P' 'O' 'R' 'T' ...
         direct:       03 <cs hi> <cs lo> 'D' 0x0A 'E' 'X' 'P' 'O' 'R' 'T' ...
       The marker sits at a fixed position, so key off "D\n" directly in front
       of "EXPORT". The previous scan for any "D\n" in the first 32 bytes also
       matched the 'D'+newline ending an export-user name of <=10 chars that
       ends in 'D' (e.g. ODV_C_NUMD), wrongly flagging a conventional dump as
       direct and corrupting its LOB tables (H-12/H-19). */
    for (i = 0; i <= 8 && i + 6 <= EXP_HEADER_SIZE; i++) {
        if (memcmp(hdr + i, "EXPORT", 6) == 0) {
            if (i >= 2 && hdr[i - 2] == 'D' && hdr[i - 1] == 0x0A)
                s->dump_type = DUMP_EXP_DIRECT;
            break;
        }
    }

    /* Parse header records separated by 0x00 or 0x0A */
    ct = 0;
    step = 0;
    cs_start = -1;
    s->exp_state.exp_mode = EXP_MODE_TABLE;

    for (i = 6; i < 0x100; i++) {
        switch (hdr[i]) {
        case 0x00:
        case 0x0A:
            word[ct] = '\0';
            ct = 0;

            switch (step) {
            case 0:
                /* Record 0: Oracle version ("V11.02.00") */
                if (word[0] == 'V' || word[0] == 'v')
                    s->exp_state.oracle_version = atoi((char *)&word[1]);
                else
                    s->exp_state.oracle_version = atoi((char *)&word[2]);
                break;
            case 1:
                /* Record 1: Export user/schema
                   First byte is a mode flag ('D'=direct, 'R'=conventional, etc.)
                   followed by the actual schema name. Skip the first byte. */
                if (word[0] != '\0' && word[1] != '\0')
                    odv_strcpy(s->exp_state.export_user, (char *)&word[1],
                               ODV_OBJNAME_LEN);
                else if (word[0] != '\0')
                    odv_strcpy(s->exp_state.export_user, (char *)word,
                               ODV_OBJNAME_LEN);
                break;
            case 2:
                /* Record 2: Export mode */
                if (memcmp(word, "RTABLES", 7) == 0)
                    s->exp_state.exp_mode = EXP_MODE_TABLE;
                else if (memcmp(word, "RUSERS", 6) == 0)
                    s->exp_state.exp_mode = EXP_MODE_USER;
                else if (memcmp(word, "RENTIRE", 7) == 0)
                    s->exp_state.exp_mode = EXP_MODE_DATABASE;
                break;
            default:
                /* Record 7 (charset region) is handled via cs_start below:
                   it is raw binary, not a 0x00/0x0A-delimited text record. */
                break;
            }
            step++;
            /* Record 7 is the binary charset region; it can legitimately begin
               with a 0x00 byte (the high byte of a sub-256 charset id such as
               US7ASCII=1 or WE8MSWIN1252=178), which would otherwise be read as
               a record separator and leave the real charset unread (H-10). Note
               its start and read it directly. */
            if (step == 7) cs_start = i + 1;
            break;

        default:
            if (ct < (int)sizeof(word) - 1) {
                word[ct] = hdr[i];
                ct++;
            }
            break;
        }

        /* Stop after charset record */
        if (step > 7) break;
    }

    /* Charset region layout: <client CS 2B BE><DB CS 2B BE><NCHAR CS 2B BE>.
       DDL text (names, comments, CHECK, DEFAULT) is written in the client
       charset; row data is written in the DB charset. Keep them separate so
       names are converted from the client charset while values are decoded
       from the DB charset (H-09).
         s->dump_charset       = client charset (used for every name/DDL-text
                                 conversion, incl. the shared meta cache and the
                                 reverse filter-name match)
         s->exp_state.db_charset = DB charset (copied to table.dump_charset for
                                 row-value decoding) */
    if (cs_start >= 0 && cs_start + 4 <= EXP_HEADER_SIZE) {
        int client_id = ((int)hdr[cs_start]     << 8) | (int)hdr[cs_start + 1];
        int db_id     = ((int)hdr[cs_start + 2] << 8) | (int)hdr[cs_start + 3];
        s->dump_charset         = id_to_charset(client_id);
        s->exp_state.db_charset = id_to_charset(db_id);
    } else {
        /* Malformed header: fall back to a safe default rather than the
           tentative guess detect_dump_kind() made from the "EXPORT" bytes. */
        s->dump_charset         = CHARSET_UTF8;
        s->exp_state.db_charset = CHARSET_UTF8;
    }

    s->exp_state.header_size = EXP_HEADER_SIZE;
    return ODV_OK;
}

/*---------------------------------------------------------------------------
    parse_constraint_columns

    Extracts column names from a parenthesized list like ("col1", "col2").
    Returns pointer past the closing paren, or NULL on failure.
 ---------------------------------------------------------------------------*/
static const char *parse_constraint_columns(const char *p, ODV_CONSTRAINT *c, int is_ref)
{
    char (*cols)[ODV_OBJNAME_LEN + 1] = is_ref ? c->ref_columns : c->columns;
    int *cnt = is_ref ? &c->ref_col_count : &c->col_count;
    *cnt = 0;

    p = skip_ws(p);
    if (*p != '(') return NULL;
    p++;

    while (*p && *p != ')') {
        char col[ODV_OBJNAME_LEN + 1] = {0};
        int len = 0;
        p = skip_ws(p);
        if (*p == '"') {
            p++;
            while (*p && *p != '"' && len < ODV_OBJNAME_LEN)
                col[len++] = *p++;
            col[len] = '\0';
            if (*p == '"') p++;
        } else {
            while (*p && *p != ',' && *p != ')' && *p != ' ' && len < ODV_OBJNAME_LEN)
                col[len++] = *p++;
            col[len] = '\0';
            trim_right(col);
        }
        if (col[0] && *cnt < ODV_MAX_CONSTRAINT_COLS) {
            odv_strcpy(cols[*cnt], col, ODV_OBJNAME_LEN);
            (*cnt)++;
        }
        /* Skip the rest of this item (an index key's ASC / DESC): it is not
           another column. */
        p = skip_ws(p);
        while (*p && *p != ',' && *p != ')') {
            if (*p == '"') {
                p++;
                while (*p && *p != '"') p++;
                if (*p) p++;
            } else {
                p++;
            }
        }
        if (*p == ',') p++;
    }
    if (*p == ')') p++;
    return p;
}

/*---------------------------------------------------------------------------
    add_constraint

    Adds a constraint to the current table, growing the array as needed (it
    was a fixed 50 entries, past which constraints and indexes were dropped
    without notice). Returns the new, zeroed constraint, or NULL — with a
    warning in last_error — only if memory runs out or a table has more than
    ODV_MAX_CONSTRAINTS.
 ---------------------------------------------------------------------------*/
static ODV_CONSTRAINT *add_constraint(ODV_SESSION *s, int type)
{
    ODV_CONSTRAINT *c;
    if (s->table.constraint_count >= s->table.constraint_cap) {
        int nc = s->table.constraint_cap ? s->table.constraint_cap * 2 : 16;
        ODV_CONSTRAINT *na;
        if (nc > ODV_MAX_CONSTRAINTS) nc = ODV_MAX_CONSTRAINTS;
        na = (nc > s->table.constraint_cap)
             ? (ODV_CONSTRAINT *)realloc(s->table.constraints,
                                         (size_t)nc * sizeof(ODV_CONSTRAINT))
             : NULL;
        if (!na) {
            snprintf(s->last_error, sizeof(s->last_error),
                     "Constraints of table %s beyond %d were not kept (%s)",
                     s->table.name, s->table.constraint_count,
                     nc > s->table.constraint_cap ? "out of memory" : "too many");
            return NULL;
        }
        s->table.constraints = na;
        s->table.constraint_cap = nc;
    }
    c = &s->table.constraints[s->table.constraint_count];
    memset(c, 0, sizeof(ODV_CONSTRAINT));
    c->type = type;
    s->table.constraint_count++;
    return c;
}

/* Find existing constraint by name. Returns NULL if not found. */
static ODV_CONSTRAINT *find_constraint_by_name(ODV_SESSION *s, const char *name)
{
    int i;
    if (!name || !name[0]) return NULL;
    for (i = 0; i < s->table.constraint_count; i++) {
        if (strcmp(s->table.constraints[i].name, name) == 0)
            return &s->table.constraints[i];
    }
    return NULL;
}

/*---------------------------------------------------------------------------
    Growable JSON text buffer

    snprintf() returns the length it *would* have written, so accumulating
    that return value into a position without checking turns a silent
    truncation into a **negative** size argument on the next call. Negative
    converts to a huge size_t, and the write runs off the end of the heap
    block. Every string serialized below (constraint names, CHECK
    conditions, table/column comments) comes straight out of an untrusted
    .dmp file, so the buffer grows to fit instead of trusting a fixed
    per-item budget.

    All appenders are no-ops once an allocation has failed; the failure is
    reported by buf == NULL at the end, so callers need one check, not one
    per append.
 ---------------------------------------------------------------------------*/
typedef struct {
    char *buf;   /* NUL-terminated, or NULL after an allocation failure */
    int   size;  /* allocated bytes */
    int   pos;   /* bytes used, excluding the terminating NUL */
} ODV_JBUF;

static int jbuf_init(ODV_JBUF *b, int initial)
{
    b->buf  = (char *)malloc(initial);
    b->size = b->buf ? initial : 0;
    b->pos  = 0;
    if (b->buf) b->buf[0] = '\0';
    return b->buf != NULL;
}

static void jbuf_fail(ODV_JBUF *b)
{
    free(b->buf);
    b->buf  = NULL;
    b->size = 0;
    b->pos  = 0;
}

/* Make room for `extra` more bytes plus the terminating NUL. */
static int jbuf_reserve(ODV_JBUF *b, int extra)
{
    int grown;
    char *p;

    if (!b->buf) return 0;
    if (extra < 0 || b->pos > INT_MAX - extra - 1) { jbuf_fail(b); return 0; }
    if (b->pos + extra + 1 <= b->size) return 1;

    grown = b->size;
    while (grown < b->pos + extra + 1) {
        if (grown > INT_MAX / 2) { grown = b->pos + extra + 1; break; }
        grown *= 2;
    }

    p = (char *)realloc(b->buf, grown);
    if (!p) { jbuf_fail(b); return 0; }
    b->buf  = p;
    b->size = grown;
    return 1;
}

static void jbuf_putc(ODV_JBUF *b, char c)
{
    if (!jbuf_reserve(b, 1)) return;
    b->buf[b->pos++] = c;
    b->buf[b->pos] = '\0';
}

static void jbuf_puts(ODV_JBUF *b, const char *str)
{
    size_t len;

    if (!str) return;
    len = strlen(str);
    if (len > (size_t)INT_MAX || !jbuf_reserve(b, (int)len)) return;
    memcpy(b->buf + b->pos, str, len);
    b->pos += (int)len;
    b->buf[b->pos] = '\0';
}

static void jbuf_printf(ODV_JBUF *b, const char *fmt, ...)
{
    va_list ap, ap_write;
    int n;

    if (!b->buf) return;

    /* Measure first (vsnprintf into NULL/0), grow, then write. The size
       argument passed below is always the real remaining space. */
    va_start(ap, fmt);
    va_copy(ap_write, ap);
    n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);

    if (n < 0 || !jbuf_reserve(b, n)) { va_end(ap_write); return; }
    vsnprintf(b->buf + b->pos, (size_t)(b->size - b->pos), fmt, ap_write);
    va_end(ap_write);
    b->pos += n;
}

/* Append `str` as a complete JSON string literal, including the quotes.
   Escapes the control characters System.Text.Json rejects on the VB.NET
   side, and cannot truncate its input (no fixed-size scratch buffer). */
static void jbuf_put_json_string(ODV_JBUF *b, const char *str)
{
    const unsigned char *p = (const unsigned char *)str;

    jbuf_putc(b, '"');
    if (str) {
        for (; *p; p++) {
            switch (*p) {
            case '"':  jbuf_puts(b, "\\\""); break;
            case '\\': jbuf_puts(b, "\\\\"); break;
            case '\b': jbuf_puts(b, "\\b");  break;
            case '\f': jbuf_puts(b, "\\f");  break;
            case '\n': jbuf_puts(b, "\\n");  break;
            case '\r': jbuf_puts(b, "\\r");  break;
            case '\t': jbuf_puts(b, "\\t");  break;
            default:
                if (*p < 0x20) jbuf_printf(b, "\\u%04x", (unsigned int)*p);
                else           jbuf_putc(b, (char)*p);
                break;
            }
        }
    }
    jbuf_putc(b, '"');
}

/* DDL text converted from the client charset into buf (cap bytes), or into a
   heap block when it may not fit — a CHECK condition or DEFAULT is a LONG and
   a comment up to 4000 bytes, which a fixed buffer used to cut or leave
   unconverted (invalid UTF-8 in the JSON). Returns the converted text (buf,
   a heap block the caller frees when != buf, or src itself when no
   conversion applies), or NULL if memory runs out. */
static const char *exp_conv_text(const char *src, int src_cs, int dst_cs,
                                 char *buf, size_t cap)
{
    size_t len, need;
    char *out;
    int out_len = 0;

    if (!src || !src[0] || src_cs == dst_cs || src_cs == CHARSET_UNKNOWN)
        return src;
    len = strlen(src);
    if (len > (size_t)(INT_MAX - 1) / 4) return src;
    need = len * 4 + 1;           /* every supported charset grows < 4x into UTF-8 */
    out = (need <= cap) ? buf : (char *)malloc(need);
    if (!out) return NULL;
    if (convert_charset(src, (int)len, src_cs, out, (int)need, dst_cs, &out_len) != ODV_OK) {
        if (out != buf) free(out);
        return src;               /* not convertible: as before, pass through */
    }
    out[out_len] = '\0';
    return out;
}

/* Like jbuf_put_json_string, but first converts DDL text (constraint names,
   CHECK conditions, comments, ...) from the client charset to the output
   charset. EXP writes all DDL text in the client charset (H-09); converting it
   here keeps the JSON valid UTF-8 and consistent with the names emitted via
   conv_name(). The source is read-only, so this is safe to call repeatedly —
   a partitioned table serializes the same constraints once per partition. */
static void jbuf_put_json_string_cs(ODV_JBUF *b, const char *str,
                                    int src_cs, int dst_cs)
{
    char tmp[4096];
    const char *t = exp_conv_text(str, src_cs, dst_cs, tmp, sizeof(tmp));
    if (!t) { jbuf_fail(b); return; }
    jbuf_put_json_string(b, t);
    if (t != tmp && t != str) free((char *)t);
}

/*---------------------------------------------------------------------------
    serialize_constraints_json

    Serialize constraint array to JSON string.
    Caller must free the returned buffer.

    The array also carries the table/column comments as a trailing type=5
    entry, so it must be built even for a table with no constraints at all —
    returning "[]" early on constraint_count == 0 silently dropped the
    comments of every table that happened to have none.
 ---------------------------------------------------------------------------*/
/* One constraint as a JSON object (DDL text → client charset, H-09). Used for
   the table's own constraints and for the ones exp writes after all tables
   (foreign keys, function-based indexes), which are serialized on arrival and
   appended to the already-built list of their table (M-33). */
static void jbuf_put_constraint(ODV_JBUF *b, const ODV_CONSTRAINT *c,
                                int src_cs, int dst_cs)
{
    int j;

    /* type, name */
    jbuf_printf(b, "{\"type\":%d,\"name\":", c->type);
    jbuf_put_json_string_cs(b, c->name, src_cs, dst_cs);

    /* columns */
    jbuf_puts(b, ",\"columns\":[");
    for (j = 0; j < c->col_count; j++) {
        if (j > 0) jbuf_putc(b, ',');
        jbuf_put_json_string_cs(b, c->columns[j], src_cs, dst_cs);
    }
    jbuf_putc(b, ']');

    /* FK ref */
    if (c->type == CONSTRAINT_FK) {
        jbuf_puts(b, ",\"ref_schema\":");
        jbuf_put_json_string_cs(b, c->ref_schema, src_cs, dst_cs);
        jbuf_puts(b, ",\"ref_table\":");
        jbuf_put_json_string_cs(b, c->ref_table, src_cs, dst_cs);
        jbuf_puts(b, ",\"ref_columns\":[");
        for (j = 0; j < c->ref_col_count; j++) {
            if (j > 0) jbuf_putc(b, ',');
            jbuf_put_json_string_cs(b, c->ref_columns[j], src_cs, dst_cs);
        }
        jbuf_putc(b, ']');
    }

    /* CHECK condition */
    if (c->type == CONSTRAINT_CHECK && ODV_TEXT(c->condition, c->condition_long)[0]) {
        jbuf_puts(b, ",\"condition\":");
        jbuf_put_json_string_cs(b, ODV_TEXT(c->condition, c->condition_long), src_cs, dst_cs);
    }

    /* INDEX expression */
    if (c->type == CONSTRAINT_INDEX && ODV_TEXT(c->index_expr, c->index_expr_long)[0]) {
        jbuf_puts(b, ",\"index_expr\":");
        jbuf_put_json_string_cs(b, ODV_TEXT(c->index_expr, c->index_expr_long), src_cs, dst_cs);
    }

    /* State, only when it differs from the default (names follow the
       Oracle dictionary: DELETE_RULE / DEFERRABLE / DEFERRED / STATUS). */
    if (c->delete_rule[0]) {
        jbuf_puts(b, ",\"delete_rule\":");
        jbuf_put_json_string(b, c->delete_rule);
    }
    if (c->deferrable)         jbuf_puts(b, ",\"deferrable\":\"DEFERRABLE\"");
    if (c->initially_deferred) jbuf_puts(b, ",\"deferred\":\"DEFERRED\"");
    if (c->disabled)           jbuf_puts(b, ",\"status\":\"DISABLED\"");

    jbuf_putc(b, '}');
}

/* The table and column comments of s->table as the type=5 object.
   Returns 0 (and writes nothing) when the table has no comment at all. */
static int jbuf_put_comments(ODV_JBUF *b, ODV_SESSION *s)
{
    int i, first_col = 1, has_comment;

    has_comment = (ODV_TEXT(s->table.comment, s->table.comment_long)[0] != '\0');
    for (i = 0; !has_comment && i < s->table.col_count; i++) {
        if (ODV_TEXT(s->table.columns[i].comment, s->table.columns[i].comment_long)[0])
            has_comment = 1;
    }
    if (!has_comment) return 0;

    /* table_comment (DDL text → client charset, H-09) */
    jbuf_puts(b, "{\"type\":5,\"table_comment\":");
    jbuf_put_json_string_cs(b, ODV_TEXT(s->table.comment, s->table.comment_long),
                            s->dump_charset, s->out_charset);

    /* col_comments */
    jbuf_puts(b, ",\"col_comments\":{");
    for (i = 0; i < s->table.col_count; i++) {
        const char *cm = ODV_TEXT(s->table.columns[i].comment, s->table.columns[i].comment_long);
        if (!cm[0]) continue;
        if (!first_col) jbuf_putc(b, ',');
        jbuf_put_json_string_cs(b, s->table.columns[i].name, s->dump_charset, s->out_charset);
        jbuf_putc(b, ':');
        jbuf_put_json_string_cs(b, cm, s->dump_charset, s->out_charset);
        first_col = 0;
    }
    jbuf_puts(b, "}}");
    return 1;
}

/* The notes of a table (see ODV_TABLE.notes) as the type=6 object
   {"type":6,"notes":["...", ...]}. Returns 0 (and writes nothing) when there
   are none. */
static int jbuf_put_notes(ODV_JBUF *b, const char *notes, int src_cs, int dst_cs)
{
    const char *p = notes;
    int first = 1;

    if (!notes || !notes[0]) return 0;
    jbuf_puts(b, "{\"type\":6,\"notes\":[");
    while (*p) {
        const char *e = strchr(p, '\n');
        size_t n = e ? (size_t)(e - p) : strlen(p);
        char *line = (char *)malloc(n + 1);
        if (!line) { jbuf_fail(b); return 1; }
        memcpy(line, p, n);
        line[n] = '\0';
        if (!first) jbuf_putc(b, ',');
        jbuf_put_json_string_cs(b, line, src_cs, dst_cs);
        free(line);
        first = 0;
        p += n + (e ? 1 : 0);
    }
    jbuf_puts(b, "]}");
    return 1;
}

static char *serialize_constraints_json(ODV_SESSION *s)
{
    ODV_JBUF b;
    ODV_JBUF cm;
    int i, any;

    if (!jbuf_init(&b, 4096)) return NULL;

    jbuf_putc(&b, '[');
    for (i = 0; i < s->table.constraint_count; i++) {
        if (i > 0) jbuf_putc(&b, ',');
        jbuf_put_constraint(&b, &s->table.constraints[i],
                            s->dump_charset, s->out_charset);
    }

    any = (s->table.constraint_count > 0);

    /* Append comments entry (type=5) if table or any column has a comment */
    if (jbuf_init(&cm, 1024)) {
        if (jbuf_put_comments(&cm, s)) {
            if (cm.buf) {
                /* Only separate from a preceding constraint object, not from '[' */
                if (any) jbuf_putc(&b, ',');
                jbuf_puts(&b, cm.buf);
                any = 1;
            } else {
                jbuf_fail(&b);
            }
        }
        free(cm.buf);
    } else {
        jbuf_fail(&b);
    }

    /* Append the notes entry (type=6): DDL that could not be kept whole */
    if (s->table.notes && s->table.notes[0]) {
        if (any) jbuf_putc(&b, ',');
        jbuf_put_notes(&b, s->table.notes, s->dump_charset, s->out_charset);
    }

    jbuf_putc(&b, ']');

    return b.buf;  /* NULL if any allocation along the way failed */
}

/*---------------------------------------------------------------------------
    parse_create_table

    Parses a CREATE TABLE DDL statement to extract table name and columns.
    Returns 1 if a table was found, 0 otherwise.
 ---------------------------------------------------------------------------*/
static int parse_create_table(ODV_SESSION *s, const char *ddl)
{
    const char *p;
    char schema[ODV_OBJNAME_LEN + 1] = {0};
    char table_name[ODV_OBJNAME_LEN + 1] = {0};
    char col_name[ODV_OBJNAME_LEN + 1];
    char type_str[256];
    const char *def_start;  /* inline DEFAULT expression (any length) */
    size_t def_len;
    int not_null_flag;
    int col_count = 0;
    int paren_depth;

    /* Find "CREATE TABLE" */
    p = ddl;
    while (*p) {
        if (starts_with_ci(p, "CREATE") && ((unsigned char)p[6] <= ' ')) {
            p += 6;
            p = skip_ws(p);
            if (starts_with_ci(p, "TABLE") && ((unsigned char)p[5] <= ' ')) {
                p += 5;
                break;
            }
        }
        p++;
    }
    if (!*p) return 0;

    /* Extract table name: "schema"."table" or "table" */
    p = extract_identifier(p, table_name, ODV_OBJNAME_LEN);
    p = skip_ws(p);

    if (*p == '.') {
        /* schema.table format */
        odv_strcpy(schema, table_name, ODV_OBJNAME_LEN);
        p++;
        p = extract_identifier(p, table_name, ODV_OBJNAME_LEN);
        p = skip_ws(p);
    }

    /* Store table info (releasing the previous table's heap texts first) */
    odv_table_free_text(&s->table);
    memset(&s->table, 0, sizeof(ODV_TABLE));
    odv_strcpy(s->table.schema, schema, ODV_OBJNAME_LEN);
    odv_strcpy(s->table.name, table_name, ODV_OBJNAME_LEN);
    /* CESU-8 client (Oracle "UTF8"): fold surrogate pairs in the stored names to
       canonical UTF-8 so the list, the shared meta cache and the reverse filter
       match all agree, and a supplementary-plane name can be opened (H-09).
       No-op for AL32UTF8. */
    if (s->dump_charset == CHARSET_UTF8) {
        normalize_cesu8(s->table.schema);
        normalize_cesu8(s->table.name);
    }
    /* Row values are written in the DB charset, not the client charset that
       s->dump_charset now holds for names/DDL text (H-09). */
    s->table.dump_charset = s->exp_state.db_charset;
    s->table.col_count = 0;
    s->table.lob_col_count = 0;

    /* Expect opening parenthesis */
    if (*p != '(') return 0;
    p++;

    /* Parse column definitions */
    while (*p && col_count < ODV_MAX_COLUMNS) {
        int type_len = 0;
        not_null_flag = 0;
        def_start = NULL;
        def_len = 0;

        p = skip_ws(p);
        if (*p == ')') break;

        /* Extract column name.
           EXP always double-quotes column names ("CHECK_DATE" DATE, ...) and
           writes table-level constraints unquoted (CONSTRAINT "PK" PRIMARY KEY,
           PRIMARY KEY (...), CHECK (...), the unquoted CONSTRAINT "PK" PRIMARY
           KEY of an IOT, ...). Only a BARE keyword here is a table constraint;
           a quoted identifier is a real column even when it begins with one of
           those words, so treating "CHECK_DATE"/"PRIMARY_FLAG"/"CHECKSUM"/... as
           a constraint drops the column and corrupts the row layout (H-17). */
        int name_quoted = (*p == '"');
        col_name[0] = '\0';
        p = extract_identifier(p, col_name, ODV_OBJNAME_LEN);
        if (col_name[0] == '\0') break;

        /* Skip table-level constraint clauses (bare keywords only) */
        if (!name_quoted &&
            (starts_with_ci(col_name, "CONSTRAINT") ||
            starts_with_ci(col_name, "PRIMARY") ||
            starts_with_ci(col_name, "UNIQUE") ||
            starts_with_ci(col_name, "FOREIGN") ||
            starts_with_ci(col_name, "CHECK"))) {
            /* Skip to next comma at same depth or closing paren */
            paren_depth = 0;
            while (*p) {
                if (*p == '(') paren_depth++;
                else if (*p == ')') {
                    if (paren_depth == 0) break;
                    paren_depth--;
                }
                else if (*p == ',' && paren_depth == 0) { p++; break; }
                p++;
            }
            continue;
        }

        p = skip_ws(p);

        /* Extract type string (up to comma, closing paren, or constraint) */
        type_str[0] = '\0';
        type_len = 0;
        paren_depth = 0;

        while (*p && type_len < 250) {
            if (*p == '(') {
                paren_depth++;
                type_str[type_len++] = *p++;
            } else if (*p == ')') {
                if (paren_depth > 0) {
                    paren_depth--;
                    type_str[type_len++] = *p++;
                } else {
                    break; /* End of column list */
                }
            } else if (*p == ',' && paren_depth == 0) {
                p++; /* skip comma */
                break;
            } else if (starts_with_ci(p, "NOT ") || starts_with_ci(p, "DEFAULT ") ||
                       starts_with_ci(p, "CONSTRAINT ") || starts_with_ci(p, "ENABLE")) {
                /* Parse constraint keywords before skipping */
                type_str[type_len] = '\0';
                trim_right(type_str);
                /* Parse NOT NULL, DEFAULT, etc. until comma or closing paren */
                while (*p && *p != ',' && *p != ')') {
                    if (starts_with_ci(p, "NOT NULL")) {
                        not_null_flag = 1;
                        p += 8;
                    } else if (starts_with_ci(p, "DEFAULT ")) {
                        /* DEFAULT value expression, of any length (it was cut
                           at 250 bytes), up to the ',' / ')' that ends the
                           column or a following NOT / CONSTRAINT / ENABLE.
                           ',' and ')' inside a '...' literal do not end it. */
                        const char *end;
                        int d = 0;
                        char q = 0;
                        p = skip_ws(p + 8);
                        def_start = p;
                        end = p;
                        while (*p) {
                            if (q) {
                                if (*p == q) q = 0;
                                p++;
                                end = p;
                                continue;
                            }
                            if (*p == '\'' || *p == '"') { q = *p++; end = p; }
                            else if (*p == '(') { d++; p++; end = p; }
                            else if (*p == ')') {
                                if (d == 0) break;
                                d--; p++; end = p;
                            }
                            else if (*p == ',' && d == 0) break;
                            else if (*p == ' ' && d == 0) {
                                /* a space followed by NOT/CONSTRAINT/ENABLE ends it */
                                const char *np = skip_ws(p);
                                if (starts_with_ci(np, "NOT ") ||
                                    starts_with_ci(np, "CONSTRAINT ") ||
                                    starts_with_ci(np, "ENABLE")) {
                                    p = np;
                                    break;
                                }
                                p++;
                            }
                            else { p++; end = p; }
                        }
                        def_len = (size_t)(end - def_start);
                    } else if (*p == '(') {
                        /* Skip nested parens in CONSTRAINT expressions */
                        int d = 1;
                        p++;
                        while (*p && d > 0) {
                            if (*p == '(') d++;
                            else if (*p == ')') d--;
                            p++;
                        }
                    } else {
                        p++;
                    }
                }
                if (*p == ',') p++;
                break;
            } else {
                type_str[type_len++] = *p++;
            }
        }
        type_str[type_len] = '\0';
        trim_right(type_str);

        /* Store column */
        if (col_name[0] != '\0' && type_str[0] != '\0') {
            ODV_COLUMN *col = &s->table.columns[col_count];
            memset(col, 0, sizeof(ODV_COLUMN));
            odv_strcpy(col->name, col_name, ODV_OBJNAME_LEN);
            /* CESU-8 client: fold surrogate pairs so a supplementary-plane
               column name matches on open and displays correctly (H-09). */
            if (s->dump_charset == CHARSET_UTF8)
                normalize_cesu8(col->name);
            parse_column_type(type_str, col);
            col->not_null = not_null_flag;
            if (def_start && def_len > 0 &&
                !odv_text_set(col->default_val, sizeof(col->default_val),
                              &col->default_long, def_start, def_len))
                snprintf(s->last_error, sizeof(s->last_error),
                         "Out of memory: DEFAULT of %s.%s not kept", s->table.name, col->name);
            col_count++;
        }
    }

    s->table.col_count = col_count;

    /* Count LOB columns.
       LOB types: BLOB, CLOB, NCLOB, BFILE, USER_DEFINE
       Note: LONG and LONG_RAW are NOT counted as LOB */
    {
        int i;
        for (i = 0; i < col_count; i++) {
            int t = s->table.columns[i].type;
            if (t == COL_BLOB || t == COL_CLOB || t == COL_NCLOB ||
                t == COL_BFILE || t == COL_USER_DEFINE) {
                s->table.lob_col_count++;
            }
        }
    }

    return 1;
}

/*---------------------------------------------------------------------------
    parse_column_type

    Parses a type string like "VARCHAR2(100)" into ODV_COLUMN fields.
 ---------------------------------------------------------------------------*/
static int parse_column_type(const char *type_str, ODV_COLUMN *col)
{
    char upper[256];
    int i;
    const char *p;

    /* Copy and uppercase for matching */
    for (i = 0; type_str[i] && i < 255; i++) {
        upper[i] = (char)toupper((unsigned char)type_str[i]);
    }
    upper[i] = '\0';

    /* Store original type string */
    odv_strcpy(col->type_str, type_str, 63);

    /* Match type and extract parameters */
    if (starts_with_ci(upper, "VARCHAR2") || starts_with_ci(upper, "VARCHAR")) {
        col->type = COL_VARCHAR;
    } else if (starts_with_ci(upper, "NVARCHAR2") || starts_with_ci(upper, "NVARCHAR")) {
        col->type = COL_NVARCHAR;
    } else if (starts_with_ci(upper, "NCHAR")) {
        col->type = COL_NCHAR;
    } else if (starts_with_ci(upper, "CHAR")) {
        col->type = COL_CHAR;
    } else if (starts_with_ci(upper, "NUMBER")) {
        col->type = COL_NUMBER;
    } else if (starts_with_ci(upper, "FLOAT")) {
        col->type = COL_FLOAT;
    } else if (starts_with_ci(upper, "BINARY_FLOAT")) {
        col->type = COL_BIN_FLOAT;
    } else if (starts_with_ci(upper, "BINARY_DOUBLE")) {
        col->type = COL_BIN_DOUBLE;
    } else if (starts_with_ci(upper, "TIMESTAMP") && strstr(upper, "LOCAL")) {
        col->type = COL_TIMESTAMP_LTZ;
    } else if (starts_with_ci(upper, "TIMESTAMP") && strstr(upper, "TIME ZONE")) {
        col->type = COL_TIMESTAMP_TZ;
    } else if (starts_with_ci(upper, "TIMESTAMP")) {
        col->type = COL_TIMESTAMP;
    } else if (starts_with_ci(upper, "DATE")) {
        col->type = COL_DATE;
    } else if (starts_with_ci(upper, "INTERVAL") && strstr(upper, "YEAR")) {
        col->type = COL_INTERVAL_YM;
    } else if (starts_with_ci(upper, "INTERVAL") && strstr(upper, "DAY")) {
        col->type = COL_INTERVAL_DS;
    } else if (starts_with_ci(upper, "LONG RAW")) {
        col->type = COL_LONG_RAW;
    } else if (starts_with_ci(upper, "LONG")) {
        col->type = COL_LONG;
    } else if (starts_with_ci(upper, "RAW")) {
        col->type = COL_RAW;
    } else if (starts_with_ci(upper, "BLOB")) {
        col->type = COL_BLOB;
    } else if (starts_with_ci(upper, "NCLOB")) {
        col->type = COL_NCLOB;
    } else if (starts_with_ci(upper, "CLOB")) {
        col->type = COL_CLOB;
    } else if (starts_with_ci(upper, "BFILE")) {
        col->type = COL_BFILE;
    } else if (starts_with_ci(upper, "XMLTYPE")) {
        col->type = COL_XMLTYPE;
    } else if (starts_with_ci(upper, "ROWID") || starts_with_ci(upper, "UROWID")) {
        col->type = COL_ROWID;
    } else {
        col->type = COL_VARCHAR;  /* default fallback */
    }

    /* Extract length/precision/scale from parentheses */
    {
        int has_parens = 0;
        p = strchr(upper, '(');
        if (p) {
            has_parens = 1;
            p++;
            col->length = atoi(p);

            /* For NUMBER(p,s) */
            p = strchr(p, ',');
            if (p) {
                p++;
                col->scale = atoi(p);
                col->precision = col->length;
            }
        }

        /* For TIMESTAMP types, parenthesized value is fractional seconds precision */
        switch (col->type) {
        case COL_TIMESTAMP:
        case COL_TIMESTAMP_TZ:
        case COL_TIMESTAMP_LTZ:
            if (has_parens) {
                col->precision = col->length;
                col->length = 0; /* Reset to get default byte length below */
            } else {
                col->precision = 6; /* Oracle default */
            }
            break;
        }
    }

    /* Default lengths for types without explicit size */
    if (col->length == 0) {
        switch (col->type) {
        case COL_DATE:          col->length = 7; break;
        case COL_TIMESTAMP:     col->length = 11; break;
        case COL_TIMESTAMP_TZ:  col->length = 13; break;
        case COL_TIMESTAMP_LTZ: col->length = 11; break;
        case COL_BIN_FLOAT:     col->length = 4; break;
        case COL_BIN_DOUBLE:    col->length = 8; break;
        case COL_ROWID:         col->length = 18; break;
        default: break;
        }
    }

    return ODV_OK;
}

/*---------------------------------------------------------------------------
    conv_name: DDL text (client charset) -> output charset
 ---------------------------------------------------------------------------*/
static void conv_name(const char *src, int src_cs, int dst_cs,
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
    odv_strcpy(dst, src, dst_size - 1);
}

/*---------------------------------------------------------------------------
    Table notification (table_list entry + table_cb)

    exp_deliver() is the one place an exp table reaches the caller. It
    appends the table_list entry and calls table_cb with the same data, so
    callback i and odv_get_table_entry(i) always describe the same table.

    Streaming (full parse, CSV / SQL export): notify_exp_table() delivers the
    current table at once, from s->table.

    Listing (list_only): delivery is deferred to the end of the parse. exp
    writes foreign keys and function-based / DESC indexes after ALL tables
    (sections "referential integrity constraints" and "bitmap, functional and
    extensible indexes"), long after their table's callback had been made, so
    they were attached to whatever table was still pending — the last table
    of the dump (M-33). Now each table's callback data is snapshotted when its
    own section ends (EXP_DTAB), a late constraint is appended to the snapshot
    of the table its statement names, and every entry (one per table or
    partition, EXP_DENT) is delivered in the original order once the whole
    dump has been read. Partitions of one table share one snapshot, so every
    partition now carries the table's complete constraint list (before, only
    the last one did: the earlier partitions were reported before the trailing
    CREATE INDEX / ALTER TABLE / COMMENT had been read).
 ---------------------------------------------------------------------------*/
typedef struct {
    char     schema[ODV_OBJNAME_LEN + 1];  /* raw names, as in s->table (lookup key) */
    char     name[ODV_OBJNAME_LEN + 1];
    int      next;        /* hash chain, -1 = end */
    int      ready;       /* snapshot taken (the table's own section ended) */
    char    *strs;        /* packed callback strings, see exp_pack_strings() */
    int     *not_nulls;
    int      col_count;
    int      con_count;
    ODV_JBUF cons;        /* constraint objects, comma-separated, no brackets */
    char    *cmt;         /* type=5 comments object, or NULL */
    char    *notes;       /* ODV_TABLE.notes of the table (also late ones), or NULL */
    char    *json;        /* complete constraints_json, built at delivery */
    int64_t  ddl_offset;
} EXP_DTAB;

typedef struct {
    int      tab;         /* EXP_DTAB index */
    int      is_partition;
    char     partition[ODV_OBJNAME_LEN + 1];
    int64_t  row_count;
} EXP_DENT;

typedef struct {
    int       active;     /* 1 = list_only: deliveries are deferred */
    int       cur;        /* EXP_DTAB of the pending table, -1 = none */
    EXP_DTAB *tab;
    int       ntab, cap_tab;
    EXP_DENT *ent;
    int       nent, cap_ent;
    int      *bucket;     /* hash heads over tab[].name, nbucket a power of 2 */
    int       nbucket;
} EXP_DEFER;

/* Converted callback strings of s->table packed back to back, each
   NUL-terminated: schema, table name, then per column its name, type_str
   and DEFAULT. Names and DEFAULT expressions are DDL text in the client
   charset (H-09) and are converted to the output charset here. Heap, so the
   ~3x-expanded per-column strings do not sit on the 1 MB Windows stack. */
static char *exp_pack_strings(ODV_SESSION *s, int ncols)
{
    char tmp[EXP_DEF_CONV_LEN];
    char *buf, *heap = NULL;
    size_t cap = 4096, len = 0;
    int i, k;

    buf = (char *)malloc(cap);
    if (!buf) return NULL;
    for (i = -2; i < ncols; i++) {
        for (k = 0; k < (i < 0 ? 1 : 3); k++) {
            const char *src;
            size_t n;
            if (i == -2)      { conv_name(s->table.schema, s->dump_charset, s->out_charset, tmp, ODV_OBJNAME_LEN * 4 + 1); src = tmp; }
            else if (i == -1) { conv_name(s->table.name, s->dump_charset, s->out_charset, tmp, ODV_OBJNAME_LEN * 4 + 1); src = tmp; }
            else if (k == 0)  { conv_name(s->table.columns[i].name, s->dump_charset, s->out_charset, tmp, ODV_OBJNAME_LEN * 4 + 1); src = tmp; }
            else if (k == 1)  { src = s->table.columns[i].type_str; }
            else {
                /* DEFAULT: any length (a LONG), converted in full */
                const char *dv = ODV_TEXT(s->table.columns[i].default_val,
                                          s->table.columns[i].default_long);
                src = exp_conv_text(dv, s->dump_charset, s->out_charset, tmp, sizeof(tmp));
                if (!src) { free(buf); return NULL; }
                if (src != tmp && src != dv) heap = (char *)src;
            }
            n = strlen(src) + 1;
            if (len + n > cap) {
                char *nb;
                while (len + n > cap) cap *= 2;
                nb = (char *)realloc(buf, cap);
                if (!nb) { free(heap); free(buf); return NULL; }
                buf = nb;
            }
            memcpy(buf + len, src, n);
            len += n;
            free(heap);
            heap = NULL;
        }
    }
    return buf;
}

static const char *exp_next_str(const char *p)
{
    return p + strlen(p) + 1;
}

static void exp_deliver(ODV_SESSION *s, const char *strs, const int *not_nulls,
                        int ncols, int con_count, const char *cjson,
                        int is_partition, const char *partition,
                        int64_t row_count, int64_t ddl_offset)
{
    const char *col_names[ODV_MAX_COLUMNS];
    const char *col_types[ODV_MAX_COLUMNS];
    const char *col_defaults[ODV_MAX_COLUMNS];
    const char *conv_schema = strs;
    const char *conv_tname = exp_next_str(conv_schema);
    const char *p = exp_next_str(conv_tname);
    ODV_TABLE_ENTRY *e;
    int i;

    if (ncols > ODV_MAX_COLUMNS) ncols = ODV_MAX_COLUMNS;
    for (i = 0; i < ncols; i++) {
        col_names[i] = p;    p = exp_next_str(p);
        col_types[i] = p;    p = exp_next_str(p);
        col_defaults[i] = p; p = exp_next_str(p);
    }

    /* No entry, no callback: the product pairs callback i with entry i. */
    if (!odv_table_list_reserve(s)) return;

    e = &s->table_list[s->table_count];
    memset(e, 0, sizeof(*e));
    odv_strcpy(e->schema, conv_schema, ODV_OBJNAME_LEN);
    odv_strcpy(e->name, conv_tname, ODV_OBJNAME_LEN);
    e->col_count = ncols;
    e->row_count = row_count;

    /* Set partition info from EXP PARTITION marker */
    if (is_partition && partition[0]) {
        odv_strcpy(e->partition, partition, ODV_OBJNAME_LEN);
        /* Detect partition type: first occurrence = PARTITION_TABLE,
           subsequent = PARTITION (same as EXPDP logic). The first occurrence
           is looked up through an index: scanning the whole list for every
           partition is quadratic, and the list is no longer capped at 1000
           entries (M-29). */
        {
            int k = odv_table_list_find(s, e->schema, e->name);
            if (k >= 0 && s->table_list[k].type == TABLE_TYPE_TABLE)
                s->table_list[k].type = TABLE_TYPE_PARTITION_TABLE;
            e->type = (k >= 0) ? TABLE_TYPE_PARTITION : TABLE_TYPE_PARTITION_TABLE;
        }
    } else {
        e->type = TABLE_TYPE_TABLE;
    }
    s->table_count++;

    if (s->table_cb) {
        s->table_cb(conv_schema, conv_tname, ncols,
                    col_names, col_types, not_nulls, col_defaults,
                    con_count, cjson ? cjson : "[]",
                    row_count, ddl_offset, s->table_ud);
    }
}

/* Deliver s->table now (streaming, or a deferral that could not be set up). */
static void exp_deliver_current(ODV_SESSION *s, int64_t row_count)
{
    int not_nulls[ODV_MAX_COLUMNS];
    int ncols = (s->table.col_count < ODV_MAX_COLUMNS)
                ? s->table.col_count : ODV_MAX_COLUMNS;
    char *strs, *cjson;
    int i;

    for (i = 0; i < ncols; i++) not_nulls[i] = s->table.columns[i].not_null;
    strs = exp_pack_strings(s, ncols);
    if (!strs) return;
    cjson = serialize_constraints_json(s);
    exp_deliver(s, strs, not_nulls, ncols, s->table.constraint_count, cjson,
                s->table.is_partition, s->table.partition,
                row_count, s->table.ddl_offset);
    free(cjson);
    free(strs);
}

static unsigned int exp_name_hash(const char *nm)
{
    unsigned int h = 2166136261u;
    while (*nm) { h ^= (unsigned char)*nm++; h *= 16777619u; }
    return h;
}

static void defer_rehash(EXP_DEFER *d, int nb)
{
    int *bk = (int *)malloc((size_t)nb * sizeof(int));
    int i;
    if (!bk) return;   /* keep the old (smaller) table; lookups stay correct */
    for (i = 0; i < nb; i++) bk[i] = -1;
    for (i = 0; i < d->ntab; i++) {
        int h = (int)(exp_name_hash(d->tab[i].name) & (unsigned int)(nb - 1));
        d->tab[i].next = bk[h];
        bk[h] = i;
    }
    free(d->bucket);
    d->bucket = bk;
    d->nbucket = nb;
}

/* New snapshot slot for the table just parsed. Returns its index, or -1. */
static int defer_new_table(EXP_DEFER *d, const char *sch, const char *nm)
{
    EXP_DTAB *t;
    int idx;

    if (d->ntab == d->cap_tab) {
        int nc = d->cap_tab ? d->cap_tab * 2 : 64;
        EXP_DTAB *nt = (EXP_DTAB *)realloc(d->tab, (size_t)nc * sizeof(EXP_DTAB));
        if (!nt) return -1;
        d->tab = nt;
        d->cap_tab = nc;
    }
    idx = d->ntab++;
    t = &d->tab[idx];
    memset(t, 0, sizeof(*t));
    odv_strcpy(t->schema, sch, ODV_OBJNAME_LEN);
    odv_strcpy(t->name, nm, ODV_OBJNAME_LEN);
    t->next = -1;
    if (d->nbucket == 0 || d->ntab > d->nbucket * 2) {
        int old = d->nbucket;
        defer_rehash(d, old ? old * 4 : 1024);
        if (d->nbucket != old) return idx;    /* rebuilt, idx linked in */
        if (d->nbucket == 0) return idx;      /* no index: never found */
    }
    {
        int h = (int)(exp_name_hash(nm) & (unsigned int)(d->nbucket - 1));
        t->next = d->bucket[h];
        d->bucket[h] = idx;
    }
    return idx;
}

/* Snapshot of the table named sch.nm (raw names). With no schema in the
   statement (exp writes ALTER TABLE "T" ..., ON "T" ... unqualified) or no
   exact schema match, a name that identifies a single table is accepted. */
static int defer_find(const EXP_DEFER *d, const char *sch, const char *nm)
{
    int i, any = -1, nany = 0;
    if (d->nbucket == 0) return -1;
    for (i = d->bucket[exp_name_hash(nm) & (unsigned int)(d->nbucket - 1)];
         i >= 0; i = d->tab[i].next) {
        if (strcmp(d->tab[i].name, nm) != 0) continue;
        if (sch && sch[0] && strcmp(d->tab[i].schema, sch) == 0) return i;
        any = i;
        nany++;
    }
    return (nany == 1) ? any : -1;
}

static int defer_push(EXP_DEFER *d, int tab, int is_partition,
                      const char *partition, int64_t row_count)
{
    EXP_DENT *en;
    if (d->nent == d->cap_ent) {
        int nc = d->cap_ent ? d->cap_ent * 2 : 256;
        EXP_DENT *ne = (EXP_DENT *)realloc(d->ent, (size_t)nc * sizeof(EXP_DENT));
        if (!ne) return 0;
        d->ent = ne;
        d->cap_ent = nc;
    }
    en = &d->ent[d->nent++];
    en->tab = tab;
    en->is_partition = is_partition;
    odv_strcpy(en->partition, partition, ODV_OBJNAME_LEN);
    en->row_count = row_count;
    return 1;
}

/* The pending table's own section has ended: snapshot its callback data. */
static void defer_finalize(ODV_SESSION *s, EXP_DEFER *d)
{
    EXP_DTAB *t;
    ODV_JBUF cm;
    int i, ncols;

    if (!d->active || d->cur < 0) return;
    t = &d->tab[d->cur];
    d->cur = -1;
    if (t->ready) return;
    t->ready = 1;

    ncols = (s->table.col_count < ODV_MAX_COLUMNS)
            ? s->table.col_count : ODV_MAX_COLUMNS;
    t->col_count = ncols;
    t->strs = exp_pack_strings(s, ncols);
    t->not_nulls = (int *)malloc((size_t)(ncols > 0 ? ncols : 1) * sizeof(int));
    if (t->not_nulls)
        for (i = 0; i < ncols; i++) t->not_nulls[i] = s->table.columns[i].not_null;
    if (jbuf_init(&t->cons, 1024)) {
        for (i = 0; i < s->table.constraint_count; i++) {
            if (i > 0) jbuf_putc(&t->cons, ',');
            jbuf_put_constraint(&t->cons, &s->table.constraints[i],
                                s->dump_charset, s->out_charset);
        }
    }
    t->con_count = s->table.constraint_count;
    if (jbuf_init(&cm, 1024)) {
        if (jbuf_put_comments(&cm, s)) t->cmt = cm.buf;
        else free(cm.buf);
    }
    if (s->table.notes) {
        size_t n = strlen(s->table.notes) + 1;
        t->notes = (char *)malloc(n);
        if (t->notes) memcpy(t->notes, s->table.notes, n);
    }
    t->ddl_offset = s->table.ddl_offset;
}

/* A constraint whose statement came after its table's section (M-33). */
static void defer_attach(ODV_SESSION *s, EXP_DEFER *d, int idx,
                         const ODV_CONSTRAINT *c)
{
    EXP_DTAB *t = &d->tab[idx];
    if (!t->cons.buf) return;
    if (t->cons.pos > 0) jbuf_putc(&t->cons, ',');
    jbuf_put_constraint(&t->cons, c, s->dump_charset, s->out_charset);
    t->con_count++;
}

static void defer_flush(ODV_SESSION *s, EXP_DEFER *d)
{
    int i;
    for (i = 0; i < d->nent; i++) {
        EXP_DENT *en = &d->ent[i];
        EXP_DTAB *t = &d->tab[en->tab];
        if (!t->ready || !t->strs || !t->not_nulls) continue;
        if (!t->json && t->cons.buf) {
            ODV_JBUF j;
            if (jbuf_init(&j, t->cons.pos + (t->cmt ? (int)strlen(t->cmt) : 0) + 8)) {
                int any = (t->cons.pos > 0);
                jbuf_putc(&j, '[');
                jbuf_puts(&j, t->cons.buf);
                if (t->cmt) {
                    if (any) jbuf_putc(&j, ',');
                    jbuf_puts(&j, t->cmt);
                    any = 1;
                }
                if (t->notes && t->notes[0]) {
                    if (any) jbuf_putc(&j, ',');
                    jbuf_put_notes(&j, t->notes, s->dump_charset, s->out_charset);
                }
                jbuf_putc(&j, ']');
                t->json = j.buf;
            }
        }
        exp_deliver(s, t->strs, t->not_nulls, t->col_count, t->con_count,
                    t->json, en->is_partition, en->partition,
                    en->row_count, t->ddl_offset);
    }
}

static void defer_free(EXP_DEFER *d)
{
    int i;
    for (i = 0; i < d->ntab; i++) {
        free(d->tab[i].strs);
        free(d->tab[i].not_nulls);
        free(d->tab[i].cons.buf);
        free(d->tab[i].cmt);
        free(d->tab[i].notes);
        free(d->tab[i].json);
    }
    free(d->tab);
    free(d->ent);
    free(d->bucket);
    memset(d, 0, sizeof(*d));
    d->cur = -1;
}

/* A table (or, for a partitioned table, one partition) is complete. */
static void notify_exp_table(ODV_SESSION *s, EXP_DEFER *d, int64_t row_count)
{
    if (d && d->active && d->cur >= 0 &&
        defer_push(d, d->cur, s->table.is_partition && s->table.partition[0],
                   s->table.partition, row_count))
        return;
    exp_deliver_current(s, row_count);
}

/*---------------------------------------------------------------------------
    decode_exp_column

    Decodes a single EXP column value from binary data.
    Stores result as string in the record value.
 ---------------------------------------------------------------------------*/
static int decode_exp_column(ODV_SESSION *s, int col_idx,
                             const unsigned char *data, int data_len)
{
    ODV_COLUMN *col;
    ODV_VALUE *val;
    char tmp[1024];
    int rc;

    if (col_idx >= s->table.col_count) return ODV_OK;

    col = &s->table.columns[col_idx];
    val = &s->record.values[col_idx];

    switch (col->type) {
    case COL_NUMBER:
    case COL_FLOAT:
        rc = decode_oracle_number(data, data_len, tmp, sizeof(tmp));
        if (rc == ODV_OK) {
            set_value_string(val, tmp, (int)strlen(tmp));
        } else {
            set_value_null(val);
        }
        break;

    case COL_DATE:
        rc = decode_oracle_date(data, data_len, tmp, sizeof(tmp), s->date_format, s->custom_date_format);
        if (rc == ODV_OK) {
            set_value_string(val, tmp, (int)strlen(tmp));
        } else {
            set_value_null(val);
        }
        break;

    case COL_TIMESTAMP:
    case COL_TIMESTAMP_TZ:
    case COL_TIMESTAMP_LTZ:
        rc = decode_oracle_timestamp(data, data_len, tmp, sizeof(tmp), s->date_format, s->custom_date_format, col->precision);
        if (rc == ODV_OK) {
            set_value_string(val, tmp, (int)strlen(tmp));
        } else {
            set_value_null(val);
        }
        break;

    case COL_BIN_FLOAT:
        rc = decode_binary_float(data, tmp, sizeof(tmp));
        if (rc == ODV_OK) {
            set_value_string(val, tmp, (int)strlen(tmp));
        } else {
            set_value_null(val);
        }
        break;

    case COL_BIN_DOUBLE:
        rc = decode_binary_double(data, tmp, sizeof(tmp));
        if (rc == ODV_OK) {
            set_value_string(val, tmp, (int)strlen(tmp));
        } else {
            set_value_null(val);
        }
        break;

    case COL_INTERVAL_YM:
        rc = decode_interval_ym(data, data_len, tmp, sizeof(tmp));
        if (rc == ODV_OK) {
            set_value_string(val, tmp, (int)strlen(tmp));
        } else {
            set_value_null(val);
        }
        break;

    case COL_INTERVAL_DS:
        rc = decode_interval_ds(data, data_len, tmp, sizeof(tmp));
        if (rc == ODV_OK) {
            set_value_string(val, tmp, (int)strlen(tmp));
        } else {
            set_value_null(val);
        }
        break;

    case COL_ROWID:
        /* ROWID / UROWID: Oracle's notation (AAATxFAAMAAAEi9AAA, *BAMA...),
           not the stored bytes.  exp stores a ROWID column in its external
           form and a UROWID column in the internal one (M-35). */
        if (data_len > 0 &&
            ensure_value_buf(val, 19 + (data_len * 4 + 2) / 3) == ODV_OK &&
            decode_oracle_rowid(data, data_len,
                                odv_strnicmp(col->type_str, "UROWID", 6) == 0,
                                (char *)val->data, val->buf_size) == ODV_OK) {
            val->data_len = (int)strlen((const char *)val->data);
            val->is_null = 0;
            break;
        }
        /* An unrecognised form is shown as hex, like RAW. */
        /* FALLTHROUGH */
    case COL_RAW: {
        /* Hex output: "0x" + hex bytes */
        int needed = data_len * 2 + 3;
        rc = ensure_value_buf(val, needed);
        if (rc == ODV_OK) {
            int j, pos = 0;
            val->data[pos++] = '0';
            val->data[pos++] = 'x';
            for (j = 0; j < data_len; j++) {
                static const char hex[] = "0123456789ABCDEF";
                val->data[pos++] = hex[(data[j] >> 4) & 0x0F];
                val->data[pos++] = hex[data[j] & 0x0F];
            }
            val->data[pos] = '\0';
            val->data_len = pos;
            val->is_null = 0;
        }
        break;
    }

    case COL_BLOB: {
        /* The 'data' here is the BLOB locator (~100 byte structure) read in
           the regular column phase. The actual BLOB content lives in the LOB
           section that follows the row's regular columns and is consumed by
           parse_exp_records()'s LOB section reader. Just set a placeholder;
           do NOT accumulate the locator into the LOB extract buffer. */
        const char *ph = "%BLOB%";
        set_value_string(val, ph, (int)strlen(ph));
        break;
    }

    case COL_LONG_RAW: {
        /* LONG RAW is inline (length-prefixed, or in parts that
           parse_exp_records has already joined), so its bytes are here. Show
           them as upper-case hex without a prefix, as the expdp path does and
           as the product's BLOB / LONG RAW cells read them; the SQL writer
           turns that into HEXTORAW('..') / '\x..' / X'..' / 0x... The value
           used to be the constant %BLOB%, so a LONG RAW never reached the
           preview, CSV or SQL (M-40).
           It is not a LOB-section column: odv_lob_check_column() never makes
           it an extraction target, and feeding it to odv_lob_accumulate()
           only prepended it to the next LOB column's file (M-39). */
        static const char hex[] = "0123456789ABCDEF";
        int j, pos = 0;
        if (data_len <= 0 ||
            ensure_value_buf(val, data_len * 2 + 1) != ODV_OK) {
            set_value_null(val);
            break;
        }
        for (j = 0; j < data_len; j++) {
            val->data[pos++] = hex[(data[j] >> 4) & 0x0F];
            val->data[pos++] = hex[data[j] & 0x0F];
        }
        val->data[pos] = '\0';
        val->data_len = pos;
        val->is_null = 0;
        break;
    }

    case COL_CLOB: {
        /* CLOB locator — actual content read from the LOB section. */
        const char *ph = "%CLOB%";
        set_value_string(val, ph, (int)strlen(ph));
        break;
    }

    case COL_LONG: {
        /* LONG is inline (NOT a LOB-section column) and is never a LOB
           extraction target, so it is not accumulated: doing so prepended it
           to the next LOB column's extracted file (M-39). */
        if (s->table.dump_charset != s->out_charset &&
            s->table.dump_charset != CHARSET_UNKNOWN) {
            int out_len = 0;
            rc = ensure_value_buf(val, data_len * 4 + 1);
            if (rc == ODV_OK) {
                rc = convert_charset((const char *)data, data_len,
                                     s->table.dump_charset,
                                     (char *)val->data, val->buf_size,
                                     s->out_charset, &out_len);
                if (rc == ODV_OK) {
                    val->data[out_len] = '\0';
                    val->data_len = out_len;
                    val->is_null = 0;
                } else {
                    set_value_string(val, (const char *)data, data_len);
                }
            }
        } else {
            set_value_string(val, (const char *)data, data_len);
        }
        break;
    }

    case COL_NCLOB: {
        /* NCLOB locator — actual content read from the LOB section. */
        const char *ph = "%NCLOB%";
        set_value_string(val, ph, (int)strlen(ph));
        break;
    }

    case COL_BFILE: {
        const char *ph = "%BFILE%";
        set_value_string(val, ph, (int)strlen(ph));
        break;
    }

    case COL_NCHAR:
    case COL_NVARCHAR: {
        /* EXP stores NCHAR/NVARCHAR2 data in national charset (AL16UTF16 = UTF-16BE).
           Convert UTF-16BE → UTF-8 for display. An NCHAR value keeps its
           blank padding, as Oracle returns it and the expdp path shows it:
           trimming it made an all-blank value empty, i.e. NULL (M-10). */
        char conv_buf[ODV_VARCHAR_LEN];
        int conv_len = 0;
        if (data_len > 0 &&
            convert_charset((const char *)data, data_len,
                            CHARSET_UTF16BE,
                            conv_buf, sizeof(conv_buf),
                            CHARSET_UTF8, &conv_len) == ODV_OK) {
            set_value_string(val, conv_buf, conv_len);
        } else {
            /* Fallback: copy raw */
            set_value_string(val, (const char *)data, data_len);
        }
        break;
    }

    default:
        /* String types: CHAR, VARCHAR, etc. */
        if (s->table.dump_charset != s->out_charset &&
            s->table.dump_charset != CHARSET_UNKNOWN) {
            int out_len = 0;
            rc = ensure_value_buf(val, data_len * 4 + 1);
            if (rc == ODV_OK) {
                rc = convert_charset((const char *)data, data_len,
                                     s->table.dump_charset,
                                     (char *)val->data, val->buf_size,
                                     s->out_charset, &out_len);
                if (rc == ODV_OK) {
                    val->data[out_len] = '\0';
                    val->data_len = out_len;
                    val->is_null = 0;
                } else {
                    set_value_string(val, (const char *)data, data_len);
                }
            }
        } else {
            set_value_string(val, (const char *)data, data_len);
        }
        /* A CHAR value is kept as stored, blank padding included (Oracle
           returns CHAR padded, and the expdp path shows it so). The trailing
           blanks used to be trimmed to length 0, so ' ' / '     ' became an
           empty value, which the row callback cannot tell from NULL (M-10). */
        break;
    }

    return ODV_OK;
}

/*---------------------------------------------------------------------------
    is_lob_type

    True for column types that participate in the EXP LOB section
    (i.e. their actual content is delivered after the regular columns,
    not inline as a length-prefixed value).
 ---------------------------------------------------------------------------*/
static int is_lob_type(int t)
{
    return (t == COL_BLOB || t == COL_CLOB || t == COL_NCLOB || t == COL_BFILE);
}

/*---------------------------------------------------------------------------
    read_one_lob_column

    Reads a single LOB column from the EXP LOB section.

    Wire format (verified empirically + against ARK e2c_expdmp.c reference):
      - 2-byte length value:
          > 0xFF00 (e.g. 0xFFFC) => extended:
              [4-byte total length LE]
              [4 reserved zero bytes]
              Then a stream of [2-byte chunk_size LE][chunk_size bytes data]
              chunks until total bytes accumulated.
          else => single chunk of (length) bytes.
      - In both single- and multi-chunk cases the chunk payload is the
        raw LOB content (no per-chunk headers other than the size).

    The (col_idx) parameter is the actual table column index for this LOB
    (used to address the placeholder slot in s->record). The (lob_idx)
    parameter is the LOB-only index used for odv_lob_accumulate addressing
    in extract mode.
 ---------------------------------------------------------------------------*/
static int read_one_lob_column(ODV_SESSION *s, ODV_VFILE *fp,
                               int col_idx, int lob_idx,
                               unsigned char **chunk_buf_ptr,
                               int *chunk_buf_size_ptr)
{
    unsigned char hdr[2];
    int first_val;
    int total_len = 0;
    int multi_chunk = 0;
    int accumulated = 0;
    int accum_lob_idx = lob_idx;

    /* odv_lob_accumulate() matches against s->lob_column_index, which
       odv_lob_check_column() set to the target's index among ALL LOB columns
       (BLOB/CLOB/NCLOB). The caller's lob_idx is only the position among THIS
       row's NON-NULL LOBs, so when an earlier LOB column is NULL in a row the
       two disagree and a different column's bytes get written to the target's
       file while the target's own bytes are dropped (H-27). col_idx is the real
       table column for this LOB, so derive the all-LOB index from it. (col_idx
       is -1 only when the row claims more LOBs than were tracked; keep lob_idx
       as the fallback there.) */
    if (col_idx >= 0 && col_idx <= s->table.col_count) {
        int k;
        accum_lob_idx = 0;
        for (k = 0; k < col_idx; k++) {
            int t = s->table.columns[k].type;
            if (t == COL_BLOB || t == COL_CLOB || t == COL_NCLOB)
                accum_lob_idx++;
        }
    }

    if (odv_vread(hdr, 1, 2, fp) != 2) return ODV_ERROR_FREAD;
    first_val = (int)hdr[0] | ((int)hdr[1] << 8);

    if (first_val > 0xFF00) {
        /* Extended length: 4-byte total + 4 reserved zero bytes */
        unsigned char ext[8];
        if (odv_vread(ext, 1, 8, fp) != 8) return ODV_ERROR_FREAD;
        total_len = (int)ext[0] | ((int)ext[1] << 8)
                  | ((int)ext[2] << 16) | ((int)ext[3] << 24);
        /* total_len is the whole LOB value, not a single read. ODV_EXP_RECORD_LEN
           is an engine-chosen cap, not a format boundary, and rejecting a LOB
           above it made that row and the rest of its table vanish (rc stayed OK)
           for any BLOB/CLOB/NCLOB over ~6 MB (H-26). The payload arrives as a
           stream of small chunks (each bounded below by ODV_EXP_RECORD_LEN), so
           the only state that scales with the value is the extract buffer in
           odv_lob_accumulate(), which is wanted when a large LOB is being
           extracted and is bounded by the bytes actually present in the file
           (a truncated/garbage length hits EOF and errors out). List/analyze
           never accumulates — it just streams past. So only reject a negative
           length (a >2 GB value, or a corrupt sign bit). */
        if (total_len < 0) {
            return ODV_ERROR_FORMAT;
        }
        multi_chunk = 1;
    } else {
        total_len = first_val;
        multi_chunk = 0;
    }

    while (accumulated < total_len) {
        int chunk_size;
        if (multi_chunk) {
            unsigned char ch[2];
            if (odv_vread(ch, 1, 2, fp) != 2) return ODV_ERROR_FREAD;
            chunk_size = (int)ch[0] | ((int)ch[1] << 8);
        } else {
            chunk_size = total_len - accumulated;
        }

        if (chunk_size <= 0 || chunk_size > ODV_EXP_RECORD_LEN
            || chunk_size > total_len - accumulated) {
            return ODV_ERROR_FORMAT;
        }

        if (chunk_size > *chunk_buf_size_ptr) {
            unsigned char *nb = (unsigned char *)realloc(
                *chunk_buf_ptr, (size_t)chunk_size + 1);
            if (!nb) return ODV_ERROR_MALLOC;
            *chunk_buf_ptr = nb;
            *chunk_buf_size_ptr = chunk_size + 1;
        }

        if ((int)odv_vread(*chunk_buf_ptr, 1, (size_t)chunk_size, fp) != chunk_size) {
            return ODV_ERROR_FREAD;
        }

        if (s->lob_extract_mode && s->lob_column_index >= 0) {
            odv_lob_accumulate(s, accum_lob_idx, *chunk_buf_ptr, chunk_size);
        }

        accumulated += chunk_size;
    }

    /* Display placeholder is already set when the locator was decoded
       in the regular column phase; nothing more to do here for display. */
    return ODV_OK;
}

/*---------------------------------------------------------------------------
    exp_skip_to_table_end (M-43)

    Moves fp just past the next FF FF (the table-data end marker) at or after
    the current position: the resynchronisation used when a table's row data
    cannot be read (and to step over a table that a filter leaves out). It
    used to read two bytes and seek one back for every byte. The MSVC CRT
    discards its read buffer on every fseek, so each byte re-read the 64 KB
    buffer: a long stretch without FF FF (a corrupt or crafted dump, or a
    misread big LONG) took tens of seconds with the UI frozen and no progress
    shown. The bytes are now read forward in blocks, carrying the last byte
    of a block into the next, with one repositioning when the marker is found
    — the same position the byte-wise scan stopped at. Progress is reported
    (and cancellation honoured) once per block.
    Returns 1 (found), 0 (end of file first) or -1 (cancelled).
 ---------------------------------------------------------------------------*/
#define EXP_SCAN_BLOCK 8192

static int exp_skip_to_table_end(ODV_SESSION *s, ODV_VFILE *fp)
{
    unsigned char blk[EXP_SCAN_BLOCK];
    int prev_ff = 0;

    for (;;) {
        int64_t base;
        size_t n, i;

        if (s->cancelled) return -1;
        base = odv_vtell(fp);
        n = odv_vread(blk, 1, sizeof(blk), fp);
        if (n == 0) return 0;
        for (i = 0; i < n; i++) {
            if (blk[i] != 0xFF) {
                prev_ff = 0;
            } else if (prev_ff) {
                odv_vseek(fp, base + (int64_t)i + 1, SEEK_SET);
                return 1;
            } else {
                prev_ff = 1;
            }
        }
        odv_report_progress(s, fp);
    }
}

/*---------------------------------------------------------------------------
    LONG / LONG RAW values in parts (H-22 / M-41)

    exp writes a LONG / LONG RAW value of up to 65,532 bytes with a plain
    2-byte length (65,280 bytes is 00 FF; it is not a marker) and a longer
    one in parts:
      conventional rows  FD FF, then (<len16><bytes>)* closed by 00 00
                         (4000-byte chunks in 11g / 21c / 23ai)
      DIRECT=Y rows      pieces of about one block, each <len16> whose bit 15
                         says that another piece follows (the low 15 bits are
                         the piece's length)
    Reading 00 FF as "a 4-byte length follows" and FD FF as a 65,533-byte
    value dropped the row and the rest of the table (M-41). A value is kept
    up to ODV_EXP_RECORD_LEN bytes, the exp limit for one column (the expdp
    path keeps the same); past that its bytes are read and dropped, so the
    row and the rest of the table stay in step.
 ---------------------------------------------------------------------------*/

/* Reads n bytes of a value, appending to *buf at *kept what fits under
   ODV_EXP_RECORD_LEN when `keep` is set (nothing otherwise, as when only
   rows are counted). Bytes that were wanted but not kept set *dropped.
   Returns 1, 0 at end of file, -1 when out of memory. */
static int exp_read_part(ODV_VFILE *fp, int n, int keep, unsigned char **buf,
                         int *buf_size, int *kept, int *dropped)
{
    int take = 0;

    if (keep) {
        take = ODV_EXP_RECORD_LEN - *kept;
        if (take > n) take = n;
        if (take < 0) take = 0;
    }
    if (take > 0) {
        if (*kept + take + 1 > *buf_size) {
            /* Grow by doubling (up to the cap) so that a value of many
               chunks is not copied again for every chunk. */
            int want = *kept + take + 1;
            int ns = (*buf_size < 4096) ? 4096 : *buf_size;
            unsigned char *nb;
            while (ns < want)
                ns = (ns > (ODV_EXP_RECORD_LEN + 1) / 2) ? ODV_EXP_RECORD_LEN + 1 : ns * 2;
            nb = (unsigned char *)realloc(*buf, (size_t)ns);
            if (!nb) return -1;
            *buf = nb;
            *buf_size = ns;
        }
        if ((int)odv_vread(*buf + *kept, 1, (size_t)take, fp) != take) return 0;
        *kept += take;
        n -= take;
    }
    if (n > 0) {
        unsigned char tmp[4096];
        if (keep) *dropped = 1;
        while (n > 0) {
            int k = (n < (int)sizeof(tmp)) ? n : (int)sizeof(tmp);
            if ((int)odv_vread(tmp, 1, (size_t)k, fp) != k) return 0;
            n -= k;
        }
    }
    return 1;
}

/* Reads the parts of a LONG / LONG RAW value whose first 2-byte length
   `first` has been consumed (FD FF for conventional chunks; the first
   piece's length in a direct-path row) into *buf. Returns the number of
   bytes kept, -1 if the value is cut short or malformed, -2 when out of
   memory. */
static int exp_read_long_parts(ODV_VFILE *fp, int first, int direct, int keep,
                               unsigned char **buf, int *buf_size, int *dropped)
{
    unsigned char lb[2];
    int kept = 0;
    int cur = first;

    for (;;) {
        int n, rc;
        if (direct) {
            n = cur & 0x7FFF;
        } else {
            if (odv_vread(lb, 1, 2, fp) != 2) return -1;
            n = (int)lb[0] | ((int)lb[1] << 8);
            if (n == 0) break;                  /* 00 00 closes the chunks */
            if (n >= 0xFFFD) return -1;         /* a marker, not a chunk */
        }
        rc = exp_read_part(fp, n, keep, buf, buf_size, &kept, dropped);
        if (rc <= 0) return (rc < 0) ? -2 : -1;
        if (direct) {
            if (!(cur & 0x8000)) break;         /* the last piece */
            if (odv_vread(lb, 1, 2, fp) != 2) return -1;
            cur = (int)lb[0] | ((int)lb[1] << 8);
        }
    }
    return kept;
}

/*---------------------------------------------------------------------------
    parse_exp_records

    Reads binary records from EXP dump.
    Each column: 2-byte LE length + data
    Record end: 0x0000
    Table end: 0xFFFF
    NULL: 0xFFFE

    The values of a row come in the order of the INSERT INTO column list,
    which is not always the CREATE TABLE order: wire_col[i] is the table
    column of the i-th value (NULL = the same order; see exp_insert_columns).

    For tables with LOB columns:
      After all regular columns of a row are read, a LOB section follows:
        - 2-byte rec_lob_num (count of NON-NULL LOB columns in this row)
          (or 0x0000 row terminator if all LOB columns in this row are NULL)
        - For each non-NULL LOB column (in declaration order):
            chunk stream — see read_one_lob_column().
 ---------------------------------------------------------------------------*/
static int parse_exp_records(ODV_SESSION *s, ODV_VFILE *fp, int64_t data_start,
                             int list_only, const int *wire_col)
{
    unsigned char len_buf[2];
    unsigned char *col_buf = NULL;
    int col_buf_size = 0;
    unsigned char *lob_chunk_buf = NULL;
    int lob_chunk_buf_size = 0;
    int *non_null_lob_cols = NULL;
    int non_null_lob_count = 0;
    int col_idx = 0;
    int col_len;
    int rc = ODV_OK;
    int64_t row_count = 0;
    int long_cut = 0;       /* 1 = a LONG of this table was kept only in part */
    /* A DIRECT=Y dump writes a table without LOB columns in the direct
       layout (no 00 00 after a row; LONG / LONG RAW in pieces). A table with
       LOB columns is exported conventionally even then (H-12), as its INSERT
       column list in CREATE TABLE order also shows. */
    int direct_rows = (s->dump_type == DUMP_EXP_DIRECT &&
                       s->table.lob_col_count == 0);

    /* Seek to data start */
    odv_vseek(fp, data_start, SEEK_SET);

    /* Ensure record can hold all columns */
    if (s->table.col_count > s->record.max_columns) {
        /* grow_record is static in odv_record.c, use realloc approach */
        ODV_VALUE *new_vals;
        int new_max = s->table.col_count + 64;
        new_vals = (ODV_VALUE *)realloc(s->record.values,
                                        new_max * sizeof(ODV_VALUE));
        if (!new_vals) return ODV_ERROR_MALLOC;
        memset(new_vals + s->record.max_columns, 0,
               (new_max - s->record.max_columns) * sizeof(ODV_VALUE));
        s->record.values = new_vals;
        s->record.max_columns = new_max;
    }

    /* In LOB extraction mode, validate the target column exists */
    if (s->lob_extract_mode) {
        rc = odv_lob_check_column(s);
        if (rc != ODV_OK) return rc;
    }

    /* Allocate column data buffer */
    col_buf_size = ODV_EXP_READ_BUF_LEN;
    col_buf = (unsigned char *)malloc(col_buf_size);
    if (!col_buf) return ODV_ERROR_MALLOC;

    /* Allocate per-row tracking of non-NULL LOB column indices.
       Used to map rec_lob_num entries (LOB section) back to table columns. */
    if (s->table.lob_col_count > 0) {
        non_null_lob_cols = (int *)malloc(
            (size_t)s->table.lob_col_count * sizeof(int));
        if (!non_null_lob_cols) { rc = ODV_ERROR_MALLOC; goto rec_done; }
    }

    reset_record(&s->record);
    col_idx = 0;
    non_null_lob_count = 0;


    while (!s->cancelled) {
        int cix;            /* table column of this value (-1: past the table) */
        int ctype;          /* its type (-1: none) */
        int long_done = 0;  /* 1 = value already read (LONG in parts) */

        /* Read 2-byte length prefix */
        if (odv_vread(len_buf, 1, 2, fp) != 2) {
            break; /* EOF */
        }

        col_len = (int)((unsigned int)len_buf[0] | ((unsigned int)len_buf[1] << 8));


        /* Special markers */
        if (col_len == 0xFFFF) {
            /* Table data end */
            break;
        }

        if (col_len == 0x0000) {
            /* Record end (no LOB section follows). */
            if (col_idx > 0) {
                s->record.col_count = col_idx;
                if (!list_only) {
                    deliver_row(s);
                    odv_report_progress(s, fp);
                }
                row_count++;
                s->table.record_count++;

                /* LOB extraction: write accumulated data */
                if (s->lob_extract_mode && s->lob_column_index >= 0) {
                    int lrc = odv_lob_write_file(s);
                    if (lrc != ODV_OK) { rc = lrc; goto rec_done; }
                }
            }
            reset_record(&s->record);
            col_idx = 0;
            non_null_lob_count = 0;
            continue;
        }

        /* ----------------------------------------------------------------
           LOB section header.
           After all regular columns are read, the next 2-byte value is
           rec_lob_num — the count of NON-NULL LOB columns whose chunk
           streams follow (in declaration order). When all LOB columns
           in this row are NULL the prior 0x0000 row terminator already
           triggered delivery above and we never reach this point.
           ---------------------------------------------------------------- */
        if (col_idx == s->table.col_count
            && s->table.lob_col_count > 0
            && col_len > 0 && col_len <= s->table.lob_col_count) {
            int rec_lob_num = col_len;
            int li;
            int lob_rc = ODV_OK;

            for (li = 0; li < rec_lob_num; li++) {
                int target_col = -1;
                int lob_only_idx = li;
                if (li < non_null_lob_count) {
                    target_col = non_null_lob_cols[li];
                }
                lob_rc = read_one_lob_column(s, fp, target_col, lob_only_idx,
                                             &lob_chunk_buf, &lob_chunk_buf_size);
                if (lob_rc != ODV_OK) break;
            }

            if (lob_rc != ODV_OK) {
                /* Corrupt LOB section — recover to the next 0xFFFF */
                exp_skip_to_table_end(s, fp);
                break;
            }

            /* Row complete */
            s->record.col_count = col_idx;
            if (!list_only) {
                deliver_row(s);
                odv_report_progress(s, fp);
            }
            row_count++;
            s->table.record_count++;
            if (s->lob_extract_mode && s->lob_column_index >= 0) {
                int lrc = odv_lob_write_file(s);
                if (lrc != ODV_OK) { rc = lrc; goto rec_done; }
            }
            reset_record(&s->record);
            col_idx = 0;
            non_null_lob_count = 0;
            continue;
        }

        /* The table column this value belongs to (M-01). */
        cix = -1;
        if (col_idx < s->table.col_count)
            cix = wire_col ? wire_col[col_idx] : col_idx;
        ctype = (cix >= 0) ? s->table.columns[cix].type : -1;

        if (col_len == 0xFFFE) {
            /* NULL column */
            if (cix >= 0) {
                set_value_null(&s->record.values[cix]);
            }
            col_idx++;
            /* DIRECT=Y: rows have no 00 00 terminator, so a row whose last
               column is NULL is completed here. Only when the table has no LOB
               columns, though: a LOB table (written conventionally even inside a
               direct dump) always has a LOB section (non-NULL LOB count, or
               00 00 when all are NULL) after the regular columns, so completing
               it here would read that section as the next row and swallow the
               rest of the table (H-12). direct_rows carries that guard, as for
               the non-NULL completion below. */
            if (direct_rows && col_idx == s->table.col_count) {
                s->record.col_count = col_idx;
                if (!list_only) {
                    deliver_row(s);
                    odv_report_progress(s, fp);
                }
                row_count++;
                s->table.record_count++;
                if (s->lob_extract_mode && s->lob_column_index >= 0) {
                    int lrc = odv_lob_write_file(s);
                    if (lrc != ODV_OK) { rc = lrc; goto rec_done; }
                }
                reset_record(&s->record);
                col_idx = 0;
                non_null_lob_count = 0;
            }
            continue;
        }

        /* A LONG / LONG RAW in parts: DIRECT=Y pieces (a length with bit 15
           set, H-22) or a conventional chunk list (FD FF, M-41). Read as a
           plain 16-bit count, 0x8001 was taken as 32769 bytes and FD FF as
           65533, and the following rows and tables were swallowed. Other
           types never come in parts, and a conventional row uses bit 15 for a
           genuine 32 KB - 64 KB single length. */
        if ((ctype == COL_LONG || ctype == COL_LONG_RAW) &&
            (direct_rows ? (col_len & 0x8000) != 0 : col_len == 0xFFFD)) {
            int dropped = 0;
            int n = exp_read_long_parts(fp, col_len, direct_rows, !list_only,
                                        &col_buf, &col_buf_size, &dropped);
            if (n == -2) { rc = ODV_ERROR_MALLOC; break; }
            if (n < 0) {
                /* Truncated / corrupt parts — recover to the next 0xFFFF. */
                exp_skip_to_table_end(s, fp);
                break;
            }
            if (dropped && !long_cut) {
                long_cut = 1;
                snprintf(s->last_error, sizeof(s->last_error),
                         "A LONG value in %s.%s is longer than %d bytes; "
                         "only its first %d bytes are shown",
                         s->table.schema, s->table.name,
                         ODV_EXP_RECORD_LEN, ODV_EXP_RECORD_LEN);
            }
            col_len = n;
            long_done = 1;
        } else if (col_len == 0xFF00 &&
                   ctype != COL_LONG && ctype != COL_LONG_RAW) {
            /* 0xFF00 followed by a 4-byte length (kept for other types).
               A LONG / LONG RAW of exactly 65,280 bytes is written with the
               plain length 00 FF, and its bytes follow at once (M-41). */
            unsigned char big_len[4];
            unsigned int ulen;
            if (odv_vread(big_len, 1, 4, fp) != 4) break;
            ulen = (unsigned int)big_len[0]
                 | ((unsigned int)big_len[1] << 8)
                 | ((unsigned int)big_len[2] << 16)
                 | ((unsigned int)big_len[3] << 24);
            if (ulen > (unsigned int)ODV_EXP_RECORD_LEN) {
                break; /* Invalid length — corrupted record */
            }
            col_len = (int)ulen;
        }

        /* Type-specific length validation (ref: check_column_length) */
        if (cix >= 0) {
            int bad = 0;
            switch (ctype) {
            case COL_NUMBER: case COL_FLOAT:
                if (col_len > 32) bad = 1;
                break;
            case COL_DATE:
                if (col_len > 7) bad = 1;
                break;
            case COL_TIMESTAMP: case COL_TIMESTAMP_TZ: case COL_TIMESTAMP_LTZ:
                if (col_len > 13) bad = 1;
                break;
            case COL_INTERVAL_YM: case COL_INTERVAL_DS:
                if (col_len > 11) bad = 1;
                break;
            case COL_BFILE:
                if (col_len > 1000) bad = 1;
                break;
            case COL_ROWID:
                /* A UROWID holds up to 4000 bytes: the logical ROWID of an
                   index-organized table row carries the whole primary key.
                   The old 100-byte bound dropped such a row and the rest of
                   the table (M-35). */
                if (col_len > 4000) bad = 1;
                break;
            case COL_CHAR: case COL_NCHAR: case COL_VARCHAR: case COL_NVARCHAR:
                if (col_len > ODV_VARCHAR_LEN * 3) bad = 1;
                break;
            default:
                break;
            }
            if (bad) {
                /* Corrupt data — skip to next table */
                exp_skip_to_table_end(s, fp);
                break;
            }
        }

        /* Sanity check: reject absurdly large column lengths */
        if (col_len < 0 || col_len > ODV_EXP_RECORD_LEN) {
            /* Corrupt data — try to find 0xFFFF end marker */
            exp_skip_to_table_end(s, fp);
            break;
        }

        /* Read column data (already read above for a LONG in parts). */
        if (!long_done) {
            /* Ensure buffer is large enough */
            if (col_len > col_buf_size) {
                unsigned char *new_buf = (unsigned char *)realloc(col_buf, col_len + 1);
                if (!new_buf) { rc = ODV_ERROR_MALLOC; break; }
                col_buf = new_buf;
                col_buf_size = col_len + 1;
            }
            if (col_len > 0 &&
                (int)odv_vread(col_buf, 1, col_len, fp) != col_len) {
                break; /* Truncated */
            }
        }

        /* Track non-NULL LOB locators so the LOB section reader can match
           them up with the chunk streams. The locator data is consumed
           here but the actual LOB content is delivered later. */
        if (cix >= 0
            && non_null_lob_cols
            && is_lob_type(ctype)
            && non_null_lob_count < s->table.lob_col_count) {
            non_null_lob_cols[non_null_lob_count++] = cix;
        }

        /* Decode and store.
           In list_only the decoded value is never delivered to the caller, so
           skip the NUMBER/DATE/charset conversion work: listing a large dump
           only needs the row count. */
        if (!list_only && cix >= 0) {
            decode_exp_column(s, cix, col_buf, col_len);
        }
        col_idx++;

        /* DIRECT=Y: rows are concatenated without 00 00 end-of-row separator.
           Deliver the row as soon as all columns have been read. */
        if (direct_rows && col_idx == s->table.col_count) {
            s->record.col_count = col_idx;
            if (!list_only) {
                deliver_row(s);
                odv_report_progress(s, fp);
            }
            row_count++;
            s->table.record_count++;
            if (s->lob_extract_mode && s->lob_column_index >= 0) {
                int lrc = odv_lob_write_file(s);
                if (lrc != ODV_OK) { rc = lrc; break; }
            }
            reset_record(&s->record);
            col_idx = 0;
            non_null_lob_count = 0;
            continue;
        }

        /* Safety check — too many columns means record structure is corrupt. */
        if (col_idx > s->table.col_count) {
            /* Scan forward to find 0xFFFF table end marker */
            exp_skip_to_table_end(s, fp);
            break;
        }
    }

rec_done:
    free(col_buf);
    free(lob_chunk_buf);
    free(non_null_lob_cols);

    if (s->cancelled) return ODV_ERROR_CANCELLED;
    return rc;
}

/*---------------------------------------------------------------------------
    Framed DDL records (M-08 / M-38)

    Besides plain 0x0a-terminated lines, exp writes some DDL as a header line
    followed by a length-framed record:
      single  <len16 LE><text>0a          after "ALTER TABLE "T" MODIFY DEFAULT",
                                          "COMMENT ON TABLE/COLUMN ... IS ",
                                          "ANALSTATS ..."
      chunks  FD FF (<len16 LE><text>)* 00 00 0a
                                          after "CALTER TABLE" (CHECK constraints),
                                          PROCACTION, CREATE TYPE and the
                                          "CREATE FORMAT71 PROCEDURE/FUNCTION/
                                          PACKAGE/TRIG" stored-code headers
                                          (one chunk per source line)
      chunks without FD FF                after "CREATE VIEW ... AS " (view text)
    The lengths used to be read as text: the low byte landed in the statement
    (CHECK (\x08 A <> -17\x01 )), a low byte of ')' or '\'' ended it early, a
    byte above 0x20 broke the keyword matching (CHECK constraints with longer
    names and most comments vanished), and 65 bytes ('A') or >= 256 bytes made
    a MODIFY DEFAULT unrecognisable (M-08). And because the 0x00 high byte of
    each chunk length split stored code into lines, a source line beginning
    with CREATE TABLE was read as a table definition (M-38). Reading the frame
    as a unit fixes both. A frame is accepted only if its text has no NUL and
    it ends in 0x0a (or EOF); otherwise the stream is rewound and the bytes
    are read as plain text, as before.
 ---------------------------------------------------------------------------*/
#define EXP_REC_NONE     0   /* only an FD FF chunk list is recognised */
#define EXP_REC_LEN      1   /* a single <len16><text> record is expected */
#define EXP_REC_LIST     2   /* a chunk list without FD FF is expected */

#define EXP_HDR_ALONE    0   /* the framed text is a statement by itself */
#define EXP_HDR_PREFIX   1   /* the framed text completes the header line */
#define EXP_HDR_IGNORE   2   /* not table DDL (statistics, view text) */

/* Makes the statement buffer *word (*cap bytes) at least `need` bytes,
   doubling it up to EXP_DDL_BUF_MAX. It was a fixed 256 KB, past which a
   statement was cut without notice. Returns 0 if it cannot grow. */
static int exp_word_grow(char **word, int *cap, int need)
{
    int nc = *cap;
    char *nw;
    if (need <= *cap) return 1;
    if (need > EXP_DDL_BUF_MAX) return 0;
    while (nc < need) nc = (nc > EXP_DDL_BUF_MAX / 2) ? EXP_DDL_BUF_MAX : nc * 2;
    nw = (char *)realloc(*word, (size_t)nc);
    if (!nw) return 0;
    *word = nw;
    *cap = nc;
    return 1;
}

/* Appends n bytes from fp to the statement buffer at *len, growing it; what
   does not fit even then is read and dropped, and *cut is set. Fails on EOF
   or on a NUL byte: DDL text never contains NUL, so a NUL means these bytes
   are not a framed text record. Two bytes stay free (NUL, as before). */
static int exp_read_text(ODV_VFILE *fp, int n, char **buf, int *cap, int *len,
                         int *cut)
{
    unsigned char tmp[4096];
    while (n > 0) {
        int k = (n > (int)sizeof(tmp)) ? (int)sizeof(tmp) : n;
        int room;
        if ((int)odv_vread(tmp, 1, (size_t)k, fp) != k) return 0;
        if (memchr(tmp, 0, (size_t)k)) return 0;
        if (*len + k + 2 > *cap && !exp_word_grow(buf, cap, *len + k + 2))
            *cut = 1;
        room = *cap - 2 - *len;
        if (room > 0) {
            int cp = (k < room) ? k : room;
            memcpy(*buf + *len, tmp, (size_t)cp);
            *len += cp;
        }
        n -= k;
    }
    return 1;
}

/* Reads one framed record whose first byte c0 has just been consumed. On
   success its text follows the first `pre` bytes of the statement buffer
   *buf (which may move as it grows; NUL-terminated), *out_len is the text's
   length, *cut tells whether some of it had to be dropped, fp is past the
   closing 0x0a, and 1 is returned. Otherwise fp is put back to just after
   c0 and 0 is returned. */
static int exp_read_framed(ODV_VFILE *fp, unsigned char c0, int mode,
                           char **buf, int *cap, int pre, int *out_len,
                           int *cut)
{
    int64_t start = odv_vtell(fp);
    unsigned char b[2];
    unsigned int n;
    int len = pre, list;

    *cut = 0;

    if (odv_vread(b, 1, 1, fp) != 1) goto fail;
    if (c0 == 0xFD && b[0] == 0xFF) {
        list = 1;
        if (odv_vread(b, 1, 2, fp) != 2) goto fail;
        n = (unsigned int)b[0] | ((unsigned int)b[1] << 8);
    } else if (mode == EXP_REC_LEN || mode == EXP_REC_LIST) {
        list = (mode == EXP_REC_LIST);
        n = (unsigned int)c0 | ((unsigned int)b[0] << 8);
        if (n == 0) goto fail;
    } else {
        goto fail;
    }
    for (;;) {
        if (n == 0) break;                      /* 00 00 closes a chunk list */
        if (!exp_read_text(fp, (int)n, buf, cap, &len, cut)) goto fail;
        if (!list) break;
        if (odv_vread(b, 1, 2, fp) != 2) goto fail;
        n = (unsigned int)b[0] | ((unsigned int)b[1] << 8);
    }
    if (odv_vread(b, 1, 1, fp) == 1 && b[0] != 0x0A) goto fail;
    (*buf)[len] = '\0';
    *out_len = len - pre;
    return 1;

fail:
    odv_vseek(fp, start, SEEK_SET);
    return 0;
}

static int ends_with_ci(const char *str, int len, const char *suffix)
{
    int n = (int)strlen(suffix);
    while (len > 0 && (unsigned char)str[len - 1] <= ' ') len--;
    return len >= n && starts_with_ci(str + len - n, suffix);
}

/* After a plain line: does a framed record follow it, and how is it used? */
static int exp_header_mode(const char *w, int wlen, int *kind)
{
    *kind = EXP_HDR_ALONE;
    if (starts_with_ci(w, "COMMENT ON ") && ends_with_ci(w, wlen, " IS")) {
        *kind = EXP_HDR_PREFIX;
        return EXP_REC_LEN;
    }
    if (starts_with_ci(w, "ALTER TABLE ") && ends_with_ci(w, wlen, " MODIFY DEFAULT"))
        return EXP_REC_LEN;
    if (starts_with_ci(w, "ANALSTATS ")) {
        *kind = EXP_HDR_IGNORE;
        return EXP_REC_LEN;
    }
    if (starts_with_ci(w, "CREATE ") && strstr(w, " VIEW ") && ends_with_ci(w, wlen, " AS")) {
        *kind = EXP_HDR_IGNORE;
        return EXP_REC_LIST;
    }
    return EXP_REC_NONE;
}

/*---------------------------------------------------------------------------
    Statement text helpers
 ---------------------------------------------------------------------------*/

/* ["schema".]"name" (quoted or bare) at p, raw bytes. */
static const char *parse_obj_ref(const char *p, char *sch, char *nm)
{
    char a[ODV_OBJNAME_LEN + 1];
    sch[0] = '\0';
    nm[0] = '\0';
    p = extract_identifier(p, a, ODV_OBJNAME_LEN + 1);
    p = skip_ws(p);
    if (*p == '.') {
        odv_strcpy(sch, a, ODV_OBJNAME_LEN);
        p = extract_identifier(p + 1, nm, ODV_OBJNAME_LEN + 1);
    } else {
        odv_strcpy(nm, a, ODV_OBJNAME_LEN);
    }
    return p;
}

/* End of the parenthesised group at p, '(' to its matching ')'.
   Parentheses inside '...' literals and "..." identifiers are not counted,
   so CHECK (CODE <> ')') or DEFAULT TO_DATE('2000-01-01','YYYY-MM-DD') is
   not cut at the first ')' (M-31). Returns the position just past the
   group, or NULL if it is unbalanced (the statement was cut). The group is
   taken whole: a CHECK condition or DEFAULT is a LONG, and the fixed 1023 /
   255-byte copies this replaced dropped longer ones. */
static const char *paren_group_end(const char *p)
{
    int depth = 0;
    char q = 0;

    if (*p != '(') return NULL;
    while (*p) {
        char ch = *p++;
        if (q) {
            if (ch == q) q = 0;
        } else if (ch == '\'' || ch == '"') {
            q = ch;
        } else if (ch == '(') {
            depth++;
        } else if (ch == ')') {
            if (--depth == 0) return p;
        }
    }
    return NULL;
}

/* Stores the body of the SQL string literal starting just after its opening
   quote, with '' unescaped, in fixed (cap bytes) or — when longer — in a heap
   copy at *longp (see odv_text_set). A COMMENT is up to 4000 bytes; it used to
   be cut at 511. Returns 0 if memory runs out (nothing is stored then), 2 if
   the literal has no closing quote (the statement is cut short), else 1. */
static int exp_set_literal(char *fixed, size_t cap, char **longp, const char *cp)
{
    const char *q = cp;
    size_t n = 0, i = 0;
    int closed = 0;
    char *out;

    while (*q) {                         /* unescaped length */
        if (*q == '\'') {
            if (q[1] != '\'') { closed = 1; break; }
            q += 2;
        } else {
            q++;
        }
        n++;
    }
    free(*longp);
    *longp = NULL;
    fixed[0] = '\0';
    if (n < cap) {
        out = fixed;
    } else {
        out = (char *)malloc(n + 1);
        if (!out) return 0;
        *longp = out;
    }
    while (i < n) {
        out[i++] = *cp;
        cp += (*cp == '\'') ? 2 : 1;
    }
    out[n] = '\0';
    return closed ? 1 : 2;
}

/* Constraint state words after the definition: ON DELETE CASCADE / SET
   NULL, [NOT] DEFERRABLE, INITIALLY DEFERRED, DISABLE (M-33). exp writes
   enabled CHECK / FK constraints as ENABLE NOVALIDATE and validates them
   later ("Enable enforce constraints"), so NOVALIDATE is not recorded. */
static void exp_parse_state(const char *p, ODV_CONSTRAINT *c)
{
    char prev[16] = "", prev2[16] = "";
    while (*p) {
        char tok[16];
        int n = 0;
        p = skip_ws(p);
        if (!*p) break;
        if (*p == '"' || *p == '\'') {
            char q = *p++;
            while (*p && *p != q) p++;
            if (*p) p++;
            prev[0] = prev2[0] = '\0';
            continue;
        }
        while (*p && (unsigned char)*p > ' ' && *p != '(' && *p != ')') {
            if (n < (int)sizeof(tok) - 1) tok[n++] = (char)toupper((unsigned char)*p);
            p++;
        }
        if (n == 0) { p++; continue; }   /* a lone '(' or ')' */
        tok[n] = '\0';
        if (strcmp(tok, "CASCADE") == 0 && strcmp(prev, "DELETE") == 0)
            odv_strcpy(c->delete_rule, "CASCADE", (int)sizeof(c->delete_rule) - 1);
        else if (strcmp(tok, "NULL") == 0 && strcmp(prev, "SET") == 0 &&
                 strcmp(prev2, "DELETE") == 0)
            odv_strcpy(c->delete_rule, "SET NULL", (int)sizeof(c->delete_rule) - 1);
        else if (strcmp(tok, "DEFERRABLE") == 0)
            c->deferrable = (strcmp(prev, "NOT") != 0);
        else if (strcmp(tok, "DEFERRED") == 0 && strcmp(prev, "INITIALLY") == 0)
            c->initially_deferred = 1;
        else if (strcmp(tok, "DISABLE") == 0)
            c->disabled = 1;
        odv_strcpy(prev2, prev, (int)sizeof(prev2) - 1);
        odv_strcpy(prev, tok, (int)sizeof(prev) - 1);
    }
}

/* FOREIGN KEY tail at p: (cols) REFERENCES ["s".]"t" (cols) [state]. */
static void exp_parse_fk(const char *p, ODV_CONSTRAINT *c)
{
    p = parse_constraint_columns(p, c, 0);
    if (!p) return;
    p = skip_ws(p);
    if (!starts_with_ci(p, "REFERENCES")) return;
    p = parse_obj_ref(skip_ws(p + 10), c->ref_schema, c->ref_table);
    p = parse_constraint_columns(skip_ws(p), c, 1);
    if (p) exp_parse_state(p, c);
}

/*---------------------------------------------------------------------------
    Which table a DDL statement belongs to (M-33)

    exp writes a table's indexes, constraints and comments right after its
    rows, but also writes, between or after the tables, DDL of objects that
    are not listed (the PK / index / comment of a global temporary table, a
    view's comment), and puts every foreign key and function-based / DESC
    index after the last table. Attaching each statement to whichever table
    was pending put all of those on the wrong table. Every such statement
    names its table (ALTER TABLE "T", ON "T", COMMENT ON TABLE "T" / COLUMN
    "T"."C"), so it is attached by that name: to the pending table, or —
    while listing, where deliveries are deferred — to the snapshot of an
    earlier table. A statement naming any other object is dropped.
 ---------------------------------------------------------------------------*/
#define EXP_TGT_NONE     0
#define EXP_TGT_PENDING  1
#define EXP_TGT_LATE     2

typedef struct {
    ODV_SESSION *s;
    EXP_DEFER   *d;
    int          pending_table;
    const char  *current_schema;
} EXP_DDLCTX;

static int exp_target(const EXP_DDLCTX *x, const char *sch_in,
                      const char *nm_in, int *late_idx)
{
    ODV_SESSION *s = x->s;
    char sch[ODV_OBJNAME_LEN + 1], nm[ODV_OBJNAME_LEN + 1];

    odv_strcpy(sch, sch_in, ODV_OBJNAME_LEN);
    odv_strcpy(nm, nm_in, ODV_OBJNAME_LEN);
    if (s->dump_charset == CHARSET_UTF8) {   /* match folded stored names (H-09) */
        normalize_cesu8(sch);
        normalize_cesu8(nm);
    }
    if (!nm[0]) return EXP_TGT_NONE;
    if (x->pending_table && s->table.name[0] != '\0' &&
        strcmp(s->table.name, nm) == 0 &&
        (!sch[0] || strcmp(s->table.schema, sch) == 0))
        return EXP_TGT_PENDING;
    if (x->d && x->d->active) {
        int i = defer_find(x->d, sch[0] ? sch : x->current_schema, nm);
        if (i >= 0 && x->d->tab[i].ready) {
            *late_idx = i;
            return EXP_TGT_LATE;
        }
    }
    return EXP_TGT_NONE;
}

/* Adds c to its table. c's heap texts (condition_long / index_expr_long)
   move into the table's array, or are released here when c is serialized
   into a deferred snapshot or cannot be added. */
static void exp_commit_constraint(const EXP_DDLCTX *x, int tgt, int idx,
                                  ODV_CONSTRAINT *c)
{
    if (tgt == EXP_TGT_PENDING) {
        ODV_CONSTRAINT *n = add_constraint(x->s, c->type);
        if (n) {
            memcpy(n, c, sizeof(*n));
            c->condition_long = NULL;
            c->index_expr_long = NULL;
        }
    } else if (tgt == EXP_TGT_LATE) {
        defer_attach(x->s, x->d, idx, c);
    }
    free(c->condition_long);
    free(c->index_expr_long);
    c->condition_long = NULL;
    c->index_expr_long = NULL;
}

/* Out of memory while keeping a DDL text: say so instead of losing it silently. */
static void exp_text_oom(ODV_SESSION *s, const char *what, const char *obj)
{
    snprintf(s->last_error, sizeof(s->last_error),
             "Out of memory: %s of %s was not kept", what, obj);
}

/* DDL of a table that the dump does not hold whole (exp itself cuts a
   DEFAULT longer than 4000 bytes when it converts the character set, and a
   damaged file can end a statement early). It is not used — a partial
   expression is invalid SQL or a different value — and it is not dropped
   silently either: a note goes into the table's notes (constraints_json
   type=6, written as "-- NOTE:" lines by the SQL exports) and last_error. */
static void exp_note(const EXP_DDLCTX *x, int tgt, int idx, const char *fmt, ...)
{
    ODV_SESSION *s = x->s;
    char **notes = (tgt == EXP_TGT_LATE) ? &x->d->tab[idx].notes : &s->table.notes;
    char msg[512];
    size_t old, n;
    char *nn;
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    snprintf(s->last_error, sizeof(s->last_error), "%s", msg);
    old = *notes ? strlen(*notes) : 0;
    n = strlen(msg);
    nn = (char *)realloc(*notes, old + n + 2);
    if (!nn) return;
    memcpy(nn + old, msg, n);
    nn[old + n] = '\n';
    nn[old + n + 1] = '\0';
    *notes = nn;
}

/* CREATE [UNIQUE | BITMAP] INDEX "name" ON ["s".]"t" (cols | exprs) ...
   `after` points just past "CREATE ". */
static void exp_ddl_create_index(const EXP_DDLCTX *x, const char *after)
{
    ODV_CONSTRAINT c;
    char sch[ODV_OBJNAME_LEN + 1], nm[ODV_OBJNAME_LEN + 1];
    const char *cp = after;
    int is_unique = 0, tgt, idx = -1;

    if (starts_with_ci(cp, "UNIQUE ")) {
        is_unique = 1;
        cp = skip_ws(cp + 7);
    } else if (starts_with_ci(cp, "BITMAP ")) {
        cp = skip_ws(cp + 7);
    }
    if (!starts_with_ci(cp, "INDEX")) return;
    memset(&c, 0, sizeof(c));
    cp = skip_ws(cp + 5);
    if (*cp == '"') cp = extract_identifier(cp, c.name, ODV_OBJNAME_LEN + 1);
    cp = skip_ws(cp);
    if (!starts_with_ci(cp, "ON")) return;
    cp = parse_obj_ref(skip_ws(cp + 2), sch, nm);
    tgt = exp_target(x, sch, nm, &idx);
    if (tgt == EXP_TGT_NONE) return;
    cp = skip_ws(cp);
    if (is_unique) {
        /* UNIQUE INDEX → CONSTRAINT_UNIQUE (may be upgraded to PK later) */
        c.type = CONSTRAINT_UNIQUE;
        parse_constraint_columns(cp, &c, 0);
        if (c.col_count == 0) return;
    } else {
        /* Non-unique INDEX: keep the full (parenthesised) column / expression
           list for function-based indexes, plus the simple column names. */
        const char *ge = (*cp == '(') ? paren_group_end(cp) : NULL;
        c.type = CONSTRAINT_INDEX;
        if (*cp == '(' && !ge) {
            exp_note(x, tgt, idx, "Index \"%s\" on \"%s\" is incomplete in the dump "
                     "and was not kept", c.name, nm);
            return;
        }
        if (ge && !odv_text_set(c.index_expr, sizeof(c.index_expr), &c.index_expr_long,
                                cp, (size_t)(ge - cp)))
            exp_text_oom(x->s, "index expression", c.name);
        parse_constraint_columns(cp, &c, 0);
    }
    if (c.col_count == 0 && !ODV_TEXT(c.index_expr, c.index_expr_long)[0]) return;
    exp_commit_constraint(x, tgt, idx, &c);
}

/* ALTER TABLE ["s".]"t" ADD [CONSTRAINT "n"] PRIMARY KEY | UNIQUE |
   FOREIGN KEY | CHECK ...,  ALTER TABLE ... MODIFY ("c" DEFAULT expr). */
static void exp_ddl_alter_table(const EXP_DDLCTX *x, const char *word)
{
    ODV_SESSION *s = x->s;
    ODV_CONSTRAINT c;
    char sch[ODV_OBJNAME_LEN + 1], nm[ODV_OBJNAME_LEN + 1];
    const char *ap;
    int tgt, idx = -1;

    ap = parse_obj_ref(skip_ws(word + 11), sch, nm);
    tgt = exp_target(x, sch, nm, &idx);
    if (tgt == EXP_TGT_NONE) return;
    ap = skip_ws(ap);

    if (starts_with_ci(ap, "ADD")) {
        int named = 0;
        memset(&c, 0, sizeof(c));
        ap = skip_ws(ap + 3);
        if (starts_with_ci(ap, "CONSTRAINT")) {
            ap = extract_identifier(skip_ws(ap + 10), c.name, ODV_OBJNAME_LEN + 1);
            ap = skip_ws(ap);
            named = 1;
        }

        if (starts_with_ci(ap, "PRIMARY KEY")) {
            c.type = CONSTRAINT_PK;
            ap = parse_constraint_columns(skip_ws(ap + 11), &c, 0);
            /* exp writes a bare "ADD  PRIMARY KEY" for an identity column's
               key; without columns it would export as invalid SQL. */
            if (!ap || c.col_count == 0) return;
            exp_parse_state(ap, &c);
            if (tgt == EXP_TGT_PENDING) {
                if (named) {
                    /* The PK's index came first as CREATE UNIQUE INDEX "n":
                       upgrade that entry rather than list it twice. */
                    ODV_CONSTRAINT *e = find_constraint_by_name(s, c.name);
                    if (e) {
                        e->type = CONSTRAINT_PK;
                        e->deferrable = c.deferrable;
                        e->initially_deferred = c.initially_deferred;
                        e->disabled = c.disabled;
                        return;
                    }
                } else {
                    int ci;
                    for (ci = 0; ci < s->table.constraint_count; ci++)
                        if (s->table.constraints[ci].type == CONSTRAINT_PK) return;
                }
            }
            exp_commit_constraint(x, tgt, idx, &c);
        } else if (starts_with_ci(ap, "UNIQUE")) {
            /* Named or system-named (ADD  UNIQUE ("C") ...) (M-34). */
            c.type = CONSTRAINT_UNIQUE;
            ap = parse_constraint_columns(skip_ws(ap + 6), &c, 0);
            if (!ap || c.col_count == 0) return;
            exp_parse_state(ap, &c);
            if (tgt == EXP_TGT_PENDING && named && find_constraint_by_name(s, c.name))
                return;
            exp_commit_constraint(x, tgt, idx, &c);
        } else if (starts_with_ci(ap, "FOREIGN KEY")) {
            /* Named or system-named (ADD FOREIGN KEY (...) REFERENCES ...) (M-34). */
            c.type = CONSTRAINT_FK;
            exp_parse_fk(skip_ws(ap + 11), &c);
            if (c.col_count == 0 || !c.ref_table[0]) return;
            exp_commit_constraint(x, tgt, idx, &c);
        } else if (starts_with_ci(ap, "CHECK")) {
            const char *g = skip_ws(ap + 5);
            const char *end = paren_group_end(g);
            c.type = CONSTRAINT_CHECK;
            if (!end) {
                /* unbalanced: the statement is cut short in the dump */
                exp_note(x, tgt, idx, "CHECK constraint \"%s\" is incomplete in the "
                         "dump and was not kept", c.name);
                return;
            }
            if (!odv_text_set(c.condition, sizeof(c.condition), &c.condition_long,
                              g, (size_t)(end - g))) {
                exp_text_oom(s, "CHECK condition", c.name);
                return;
            }
            exp_parse_state(end, &c);
            exp_commit_constraint(x, tgt, idx, &c);
        }
    } else if (starts_with_ci(ap, "MODIFY") && tgt == EXP_TGT_PENDING) {
        /* ALTER TABLE "T" MODIFY ("C" DEFAULT expr). The preamble line
           "ALTER TABLE "T" MODIFY DEFAULT" has no '(' and is skipped. */
        char acol[ODV_OBJNAME_LEN + 1];
        const char *gp, *ge;
        size_t len;
        int ci;

        ap = skip_ws(ap + 6);
        if (*ap != '(') return;
        gp = extract_identifier(ap + 1, acol, ODV_OBJNAME_LEN + 1);
        gp = skip_ws(gp);
        if (!starts_with_ci(gp, "DEFAULT")) return;
        /* The expression ends at the ')' matching MODIFY's '(' (M-31), and is
           kept whole: DATA_DEFAULT is a LONG (it was dropped past 255 bytes).
           exp itself keeps only the first 4000 bytes of a DEFAULT when the
           export converts the character set (seen with JA16SJIS on 11g-23ai),
           which leaves the group unbalanced. */
        ge = paren_group_end(ap);
        if (!ge) {
            exp_note(x, tgt, idx, "DEFAULT of column \"%s\" is incomplete in the dump "
                     "(exp keeps only 4000 bytes of a DEFAULT when it converts the "
                     "character set) and was not kept", acol);
            return;
        }
        gp = skip_ws(gp + 7);
        if (gp >= ge - 1) return;
        len = (size_t)((ge - 1) - gp);      /* up to the group's closing ')' */
        while (len > 0 && (unsigned char)gp[len - 1] <= ' ') len--;
        if (len == 0) return;
        if (len == 4000 && s->table.dump_charset != s->dump_charset &&
            s->table.dump_charset != CHARSET_UNKNOWN && s->dump_charset != CHARSET_UNKNOWN)
            /* balanced, but exactly at exp's limit: may still be cut */
            exp_note(x, tgt, idx, "DEFAULT of column \"%s\" is exactly 4000 bytes in a "
                     "dump made with character set conversion; exp cuts longer ones "
                     "there, so it may be incomplete", acol);
        if (s->dump_charset == CHARSET_UTF8)
            normalize_cesu8(acol);          /* match folded stored name (H-09) */
        for (ci = 0; ci < s->table.col_count; ci++) {
            ODV_COLUMN *col = &s->table.columns[ci];
            if (strcmp(col->name, acol) == 0) {
                if (!odv_text_set(col->default_val, sizeof(col->default_val),
                                  &col->default_long, gp, len))
                    exp_text_oom(s, "DEFAULT", col->name);
                break;
            }
        }
    }
}

/* COMMENT ON TABLE ["s".]"t" IS '...' / COMMENT ON COLUMN ["s".]"t"."c" IS '...' */
static void exp_ddl_comment(const EXP_DDLCTX *x, const char *word)
{
    ODV_SESSION *s = x->s;
    char ids[3][ODV_OBJNAME_LEN + 1];
    const char *cp = skip_ws(word + 11);
    const char *tsch, *tnm;
    int n = 0, is_col, tgt, idx = -1;

    if (starts_with_ci(cp, "TABLE"))       { is_col = 0; cp += 5; }
    else if (starts_with_ci(cp, "COLUMN")) { is_col = 1; cp += 6; }
    else return;

    while (n < 3) {
        cp = skip_ws(cp);
        if (*cp != '"') break;
        cp = extract_identifier(cp, ids[n], ODV_OBJNAME_LEN + 1);
        n++;
        cp = skip_ws(cp);
        if (*cp != '.') break;
        cp++;
    }
    if (n < (is_col ? 2 : 1)) return;
    tnm  = ids[n - (is_col ? 2 : 1)];
    tsch = (n - (is_col ? 2 : 1) > 0) ? ids[0] : "";
    /* Comments come in the table's own section; one naming another object
       (a view, a global temporary table) is not this table's. */
    tgt = exp_target(x, tsch, tnm, &idx);
    if (tgt != EXP_TGT_PENDING) return;

    cp = skip_ws(cp);
    if (!starts_with_ci(cp, "IS")) return;
    cp = skip_ws(cp + 2);
    if (*cp != '\'') return;
    cp++;

    if (!is_col) {
        int r = exp_set_literal(s->table.comment, sizeof(s->table.comment),
                                &s->table.comment_long, cp);
        if (r == 0) exp_text_oom(s, "comment", s->table.name);
        if (r == 2)   /* kept as far as it goes: free text, still readable */
            exp_note(x, tgt, idx, "Comment on table \"%s\" is cut short in the dump",
                     s->table.name);
    } else {
        char col_name[ODV_OBJNAME_LEN + 1];
        int ki;
        odv_strcpy(col_name, ids[n - 1], ODV_OBJNAME_LEN);
        if (s->dump_charset == CHARSET_UTF8)
            normalize_cesu8(col_name);      /* match folded stored name (H-09) */
        for (ki = 0; ki < s->table.col_count; ki++) {
            ODV_COLUMN *col = &s->table.columns[ki];
            if (strcmp(col->name, col_name) == 0) {
                int r = exp_set_literal(col->comment, sizeof(col->comment),
                                        &col->comment_long, cp);
                if (r == 0) exp_text_oom(s, "comment", col->name);
                if (r == 2)
                    exp_note(x, tgt, idx, "Comment on column \"%s\" is cut short in the dump",
                             col->name);
                break;
            }
        }
    }
}

/*---------------------------------------------------------------------------
    Object tables: CREATE TABLE "T" OF "TYPE" ... (M-37)

    An object table has no column list of its own; its columns are the
    attributes of its type, defined by the CREATE TYPE that exp writes (as an
    FD FF chunk list) in the table's section just before the CREATE TABLE. The
    rows carry two columns, INSERT INTO "T" ("SYS_NC_OID$", "SYS_NC_ROWINFO$"):
    the 16-byte object id and the object as a pickled image,
        84 01 <total length> then <length><bytes> per attribute (FF = NULL),
    e.g. ODV_PT_T(1, 2) = 84 01 09 02 C1 02 02 C1 03. The table used to fail
    in parse_create_table (no '('), leaving its name set so its rows were
    read against no columns; it never appeared in the list. Types of scalar
    attributes are supported; any other image shape gives NULL attributes
    rather than a misdecoded value.
 ---------------------------------------------------------------------------*/
typedef struct {
    char    name[ODV_OBJNAME_LEN + 1];   /* raw type name */
    char   *attrs;                       /* "(X NUMBER, Y NUMBER)" */
    int64_t pos;                         /* file position of its CREATE TYPE record */
} EXP_OTYPE;

typedef struct {
    EXP_OTYPE *t;
    int        n, cap;
} EXP_TYPES;

static const EXP_OTYPE *exp_find_type(const EXP_TYPES *ty, const char *name)
{
    int i;
    for (i = ty->n - 1; i >= 0; i--)
        if (strcmp(ty->t[i].name, name) == 0) return &ty->t[i];
    return NULL;
}

static void exp_free_types(EXP_TYPES *ty)
{
    int i;
    for (i = 0; i < ty->n; i++) free(ty->t[i].attrs);
    free(ty->t);
    memset(ty, 0, sizeof(*ty));
}

/* CREATE TYPE "T" [TIMESTAMP '...'] [OID '...'] AS|IS OBJECT (attrs) ...
   from its framed record at file position pos. Other kinds of type
   (collections, subtypes, bodies) are not recorded. */
static void exp_ddl_create_type(EXP_TYPES *ty, const char *word, int64_t pos)
{
    char name[ODV_OBJNAME_LEN + 1], sch[ODV_OBJNAME_LEN + 1];
    const char *p = skip_ws(word + 7);
    const char *q;
    char *attrs;
    size_t alen;
    EXP_OTYPE *t;

    if (!starts_with_ci(p, "TYPE") || (unsigned char)p[4] > ' ') return;
    p = skip_ws(p + 4);
    if (starts_with_ci(p, "BODY") && (unsigned char)p[4] <= ' ') return;
    p = parse_obj_ref(p, sch, name);
    if (!name[0]) return;

    /* the AS | IS OBJECT ( that opens the attribute list */
    for (q = p; *q; q++) {
        const char *r;
        if (*q == '\'') {                     /* skip TIMESTAMP '...' / OID '...' */
            q++;
            while (*q && *q != '\'') q++;
            if (!*q) return;
            continue;
        }
        if (!(starts_with_ci(q, "AS") || starts_with_ci(q, "IS"))) continue;
        if ((unsigned char)q[-1] > ' ' || (unsigned char)q[2] > ' ') continue;
        r = skip_ws(q + 2);
        if (!starts_with_ci(r, "OBJECT")) continue;
        r = skip_ws(r + 6);
        if (*r != '(') return;
        q = paren_group_end(r);
        if (!q) return;
        alen = (size_t)(q - r);
        attrs = (char *)malloc(alen + 1);
        if (!attrs) return;
        memcpy(attrs, r, alen);
        attrs[alen] = '\0';

        if (ty->n == ty->cap) {
            int nc = ty->cap ? ty->cap * 2 : 16;
            EXP_OTYPE *nt = (EXP_OTYPE *)realloc(ty->t, (size_t)nc * sizeof(EXP_OTYPE));
            if (!nt) { free(attrs); return; }
            ty->t = nt;
            ty->cap = nc;
        }
        t = &ty->t[ty->n++];
        odv_strcpy(t->name, name, ODV_OBJNAME_LEN);
        t->attrs = attrs;
        t->pos = pos;
        return;
    }
}

/* Attribute types whose image form is a plain <length><bytes> value. */
static int exp_scalar_attr(const ODV_COLUMN *col, const char *type_str)
{
    static const char *const names[] = {
        "VARCHAR2", "VARCHAR", "NVARCHAR2", "CHAR", "NCHAR", "NUMBER",
        "FLOAT", "INTEGER", "INT", "SMALLINT", "DECIMAL", "NUMERIC", "REAL",
        "DOUBLE PRECISION", "BINARY_FLOAT", "BINARY_DOUBLE", "DATE",
        "TIMESTAMP", "INTERVAL", "RAW"
    };
    size_t i;
    for (i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        size_t n = strlen(names[i]);
        if (starts_with_ci(type_str, names[i]) &&
            (type_str[n] == '\0' || type_str[n] == '(' ||
             (unsigned char)type_str[n] <= ' ')) {
            return col->type != COL_LONG && col->type != COL_LONG_RAW;
        }
    }
    return 0;
}

/* The type's attributes as the table's columns. Unquoted names are folded to
   upper case, as Oracle does. Returns 0 if an attribute is not scalar. */
static int exp_type_columns(ODV_SESSION *s, const char *attrs)
{
    const char *p = attrs + 1;
    int n = 0;

    while (*p && n < ODV_MAX_COLUMNS) {
        ODV_COLUMN *col;
        char cname[ODV_OBJNAME_LEN + 1];
        char type_str[256];
        int quoted, tl = 0, depth = 0;
        char q = 0;

        p = skip_ws(p);
        if (*p == ')' || !*p) break;
        quoted = (*p == '"');
        p = extract_identifier(p, cname, ODV_OBJNAME_LEN + 1);
        if (!cname[0]) return 0;
        if (!quoted) {
            char *u;
            for (u = cname; *u; u++) *u = (char)toupper((unsigned char)*u);
        }
        p = skip_ws(p);
        while (*p) {
            char ch = *p;
            if (q) { if (ch == q) q = 0; }
            else if (ch == '\'' || ch == '"') q = ch;
            else if (ch == '(') depth++;
            else if (ch == ')') { if (depth == 0) break; depth--; }
            else if (ch == ',' && depth == 0) break;
            if (tl < (int)sizeof(type_str) - 1) type_str[tl++] = ch;
            p++;
        }
        type_str[tl] = '\0';
        trim_right(type_str);
        if (*p == ',') p++;

        col = &s->table.columns[n];
        memset(col, 0, sizeof(ODV_COLUMN));
        odv_strcpy(col->name, cname, ODV_OBJNAME_LEN);
        if (s->dump_charset == CHARSET_UTF8) normalize_cesu8(col->name);
        parse_column_type(type_str, col);
        if (!exp_scalar_attr(col, type_str)) return 0;
        n++;
    }
    s->table.col_count = n;
    return n > 0;
}

/* Decode one object image into the attribute columns. Returns 0 if the
   image is not the simple form described above. Trailing NULL attributes
   may be left out of an image, so a short image is accepted. */
static int exp_decode_object_image(ODV_SESSION *s, const unsigned char *img,
                                   int len, int list_only)
{
    int pos = 3, a;

    if (len < 3 || img[0] != 0x84 || img[1] != 0x01 || (int)img[2] != len)
        return 0;
    for (a = 0; a < s->table.col_count && pos < len; a++) {
        unsigned int n = img[pos++];
        if (n == 0xFF) continue;                 /* NULL attribute */
        if (n >= 0xFE || pos + (int)n > len) return 0;
        if (!list_only) decode_exp_column(s, a, img + pos, (int)n);
        pos += (int)n;
    }
    return pos == len;
}

static void exp_null_record(ODV_SESSION *s)
{
    int i;
    for (i = 0; i < s->table.col_count && i < s->record.max_columns; i++)
        set_value_null(&s->record.values[i]);
}

/* Row data of an object table: per row <len16><OID> <len16><image> 00 00,
   0xFFFF ends the table (written conventionally also by DIRECT=Y). */
static int parse_exp_object_rows(ODV_SESSION *s, ODV_VFILE *fp,
                                 int64_t data_start, int list_only)
{
    unsigned char lb[2];
    unsigned char *buf = NULL;
    int cap = 0, phys = 0, rc = ODV_OK;

    odv_vseek(fp, data_start, SEEK_SET);
    if (s->table.col_count > s->record.max_columns) {
        int new_max = s->table.col_count + 64;
        ODV_VALUE *nv = (ODV_VALUE *)realloc(s->record.values,
                                             (size_t)new_max * sizeof(ODV_VALUE));
        if (!nv) return ODV_ERROR_MALLOC;
        memset(nv + s->record.max_columns, 0,
               (size_t)(new_max - s->record.max_columns) * sizeof(ODV_VALUE));
        s->record.values = nv;
        s->record.max_columns = new_max;
    }
    reset_record(&s->record);
    exp_null_record(s);

    while (!s->cancelled) {
        unsigned int n;
        if (odv_vread(lb, 1, 2, fp) != 2) break;
        n = (unsigned int)lb[0] | ((unsigned int)lb[1] << 8);
        if (n == 0xFFFF) break;
        if (n == 0x0000) {
            if (phys > 0) {
                s->record.col_count = s->table.col_count;
                if (!list_only) {
                    deliver_row(s);
                    odv_report_progress(s, fp);
                }
                s->table.record_count++;
            }
            reset_record(&s->record);
            exp_null_record(s);
            phys = 0;
            continue;
        }
        if (n == 0xFFFE) { phys++; continue; }  /* NULL OID / NULL object */
        if (n == 0xFF00) {
            unsigned char bl[4];
            if (odv_vread(bl, 1, 4, fp) != 4) break;
            n = (unsigned int)bl[0] | ((unsigned int)bl[1] << 8) |
                ((unsigned int)bl[2] << 16) | ((unsigned int)bl[3] << 24);
        }
        if (phys > 1 || n > (unsigned int)ODV_EXP_RECORD_LEN) {
            /* Not the two-column layout: skip to the table end marker. */
            exp_skip_to_table_end(s, fp);
            break;
        }
        if ((int)n > cap) {
            unsigned char *nb = (unsigned char *)realloc(buf, n);
            if (!nb) { rc = ODV_ERROR_MALLOC; break; }
            buf = nb;
            cap = (int)n;
        }
        if ((unsigned int)odv_vread(buf, 1, n, fp) != n) break;
        if (phys == 1 && !exp_decode_object_image(s, buf, (int)n, list_only))
            exp_null_record(s);
        phys++;
    }

    free(buf);
    if (s->cancelled) return ODV_ERROR_CANCELLED;
    return rc;
}

/*---------------------------------------------------------------------------
    exp_insert_columns (M-01)

    Reads the column list of the INSERT INTO "T" ("A", "B", ...) statement in
    front of a table's rows: the values of a row come in that order. A
    conventional export lists the columns in CREATE TABLE order, but DIRECT=Y
    moves LONG / LONG RAW to the end — ("ID", "V", "L") for a table created
    as ("ID" NUMBER, "L" LONG, "V" VARCHAR2) — and writes the values so.
    Reading them in CREATE TABLE order put every value of such a table in
    another column and decoded it as another type. wire_col[i] receives the
    table column of the i-th value. A list that is missing, unreadable or not
    a permutation of the table's columns leaves the identity (the order the
    rows were always read in).
 ---------------------------------------------------------------------------*/

/* Reads a double-quoted name after blanks at p into out (keeping at most
   ODV_OBJNAME_LEN - 1 bytes, as extract_identifier does for the CREATE
   TABLE names). Returns the position after the closing quote, or NULL. */
static const char *exp_quoted_name(const char *p, char *out)
{
    int len = 0;
    p = skip_ws(p);
    if (*p != '"') return NULL;
    p++;
    while (*p && *p != '"') {
        if (len < ODV_OBJNAME_LEN - 1) out[len++] = *p;
        p++;
    }
    out[len] = '\0';
    return (*p == '"') ? p + 1 : NULL;
}

static void exp_insert_columns(ODV_SESSION *s, const char *stmt, int *wire_col)
{
    char name[ODV_OBJNAME_LEN + 1];
    unsigned char used[ODV_MAX_COLUMNS];
    int n = s->table.col_count;
    int i, k = 0;
    const char *p;

    for (i = 0; i < n; i++) wire_col[i] = i;
    if (n <= 0 || n > ODV_MAX_COLUMNS) return;

    /* INSERT INTO ["schema".]"table" ( */
    p = exp_quoted_name(stmt + 12, name);
    if (!p) return;
    p = skip_ws(p);
    if (*p == '.') {
        p = exp_quoted_name(p + 1, name);
        if (!p) return;
        p = skip_ws(p);
    }
    if (*p != '(') return;
    p++;

    memset(used, 0, (size_t)n);
    for (;;) {
        int j;
        p = exp_quoted_name(p, name);
        if (!p || k >= n) goto identity;
        /* Fold the client-charset name as the stored names were (H-09). */
        if (s->dump_charset == CHARSET_UTF8)
            normalize_cesu8(name);
        j = k;                                  /* usually the same place */
        if (used[j] || strcmp(s->table.columns[j].name, name) != 0) {
            for (j = 0; j < n; j++)
                if (!used[j] && strcmp(s->table.columns[j].name, name) == 0)
                    break;
            if (j == n) goto identity;
        }
        used[j] = 1;
        wire_col[k++] = j;
        p = skip_ws(p);
        if (*p == ',') { p++; continue; }
        if (*p == ')') break;
        goto identity;
    }
    if (k == n) return;

identity:
    for (i = 0; i < n; i++) wire_col[i] = i;
}

/* CREATE TABLE "T" OF "TYPE" ...: the columns are the type's attributes. */
static int exp_object_table(ODV_SESSION *s, const char *word,
                            const EXP_TYPES *ty, int64_t *type_pos)
{
    char sch[ODV_OBJNAME_LEN + 1], nm[ODV_OBJNAME_LEN + 1];
    const char *p = skip_ws(word + 7);
    const EXP_OTYPE *t;

    if (!s->table.name[0] || !starts_with_ci(p, "TABLE")) return 0;
    p = parse_obj_ref(skip_ws(p + 5), sch, nm);
    p = skip_ws(p);
    if (!starts_with_ci(p, "OF") || (unsigned char)p[2] > ' ') return 0;
    parse_obj_ref(skip_ws(p + 2), sch, nm);
    t = exp_find_type(ty, nm);
    if (!t || !exp_type_columns(s, t->attrs)) {
        s->table.col_count = 0;
        return 0;
    }
    *type_pos = t->pos;
    return 1;
}

/*---------------------------------------------------------------------------
    parse_exp_ddl_and_data

    Byte-by-byte state machine for EXP dump parsing.

    EXP files interleave ASCII DDL text with binary data:
      step 0: Skip 256-byte header
      step 1: Export mode recognition (METRICST, INTERPRETED, etc.)
      step 2: DDL parsing (CREATE TABLE, INSERT INTO, CONNECT)
      step 3: Binary metadata (column count, types, lengths, charset)
      After metadata: call parse_exp_records() for binary record data
      On 0xFFFF end-of-table: back to step 2 for next table

    DDL statements are terminated by \0 or \n.
 ---------------------------------------------------------------------------*/
static int parse_exp_ddl_and_data(ODV_SESSION *s, ODV_VFILE *fp, int list_only)
{
    int step;               /* 0=header, 1=mode, 2=ddl, 3=metadata */
    int data_step;          /* sub-state within step 3 */
    char *word;
    int wlen = 0;
    char current_schema[ODV_OBJNAME_LEN + 1] = {0};
    unsigned char c;
    int64_t address = 0;
    int rc = ODV_OK;

    /* Step 3 (metadata) state — some variables are set during parse but
       only used for state-machine transitions, not read afterward. */
    int meta_col_count = 0;
    int meta_col_idx = 0;
    int meta_col_type = 0;  /* set during parse, not read after */
    int is_char_type = 0;
    int meta_lob_idx = 0;
    int lob_total = 0;      /* set during parse, not read after */
    int meta_object_count = 0; /* object/REF columns (metadata type 0x79/0x7B) */
    int meta_name_count = 0;   /* names to read in the post-column name section
                                  (LOB names + object type-name sections) */
    int lob_name_len = 0;   /* set during parse, not read after */
    int lob_name_read = 0;  /* set during parse, not read after */
    unsigned char meta_buf[4];
    int null_count = 0;
    int pending_table = 0;  /* 1=table parsed but not yet notified */
    EXP_DEFER defer;        /* list_only: deferred table deliveries (M-33) */
    EXP_DDLCTX ddlx;        /* context for the ALTER / INDEX / COMMENT handlers */
    int rec_mode = EXP_REC_NONE;  /* framed record expected after a header line */
    int rec_kind = EXP_HDR_ALONE; /* how that record is used (EXP_HDR_*) */
    char rec_hdr[1024];     /* header line an EXP_HDR_PREFIX record completes */
    int rec_hdr_len = 0;
    int framed = 0;         /* 1 = `word` came from a framed record */
    int word_cap = EXP_DDL_BUF_SIZE; /* allocated size of `word` (grows) */
    int word_cut = 0;       /* 1 = `word` lost bytes past EXP_DDL_BUF_MAX */
    int64_t word_pos = 0;   /* file position of the first byte of `word` (M-30) */
    EXP_TYPES otypes;       /* object types seen so far (CREATE TYPE, M-37) */
    int obj_table = 0;      /* 1 = pending table is CREATE TABLE ... OF type */
    int64_t pending_row_count = 0; /* row count to pass when notifying */
    int filter_found = 0;   /* 1=filter target table already processed */
    int wire_col[ODV_MAX_COLUMNS]; /* table column of each row value (M-01) */

    memset(&defer, 0, sizeof(defer));
    memset(&otypes, 0, sizeof(otypes));
    {
        int i;
        for (i = 0; i < ODV_MAX_COLUMNS; i++) wire_col[i] = i;
    }
    defer.cur = -1;
    defer.active = list_only;
    ddlx.s = s;
    ddlx.d = &defer;
    ddlx.pending_table = 0;
    ddlx.current_schema = current_schema;

    word = (char *)malloc(EXP_DDL_BUF_SIZE);
    if (!word) return ODV_ERROR_MALLOC;

    /* Fast seek: if seek_offset is set (from previous list_tables),
       jump directly to the target table's DDL position.
       Header must already be parsed (charset info needed). */
    if (s->seek_offset > EXP_HEADER_SIZE && s->filter_active) {
        odv_vseek(fp, s->seek_offset, SEEK_SET);
        address = s->seek_offset;
        step = 2;   /* Start in DDL scan mode */
        wlen = 0;

        /* Pre-set current_schema from filter (we skipped past CONNECT) */
        if (s->filter_schema[0]) {
            char tmp[ODV_OBJNAME_LEN + 1];
            int tlen = 0;
            if (s->dump_charset != s->out_charset &&
                s->dump_charset != CHARSET_UNKNOWN) {
                if (convert_charset(s->filter_schema, (int)strlen(s->filter_schema),
                                    s->out_charset, tmp, ODV_OBJNAME_LEN,
                                    s->dump_charset, &tlen) == ODV_OK) {
                    tmp[tlen] = '\0';
                    odv_strcpy(current_schema, tmp, ODV_OBJNAME_LEN);
                }
            } else {
                odv_strcpy(current_schema, s->filter_schema, ODV_OBJNAME_LEN);
            }
        }
    } else {
        /* Normal: start from beginning of file */
        odv_vseek(fp, 0, SEEK_SET);
        step = 0;

        /* Pre-set current_schema from export user in the header.
           RTABLES (single-table export) has no CONNECT statement.
           RUSERS (schema export) may also have tables before the
           first CONNECT statement.  The CONNECT statement will
           override this when encountered. */
        if (s->exp_state.export_user[0] != '\0') {
            odv_strcpy(current_schema, s->exp_state.export_user,
                       ODV_OBJNAME_LEN);
        }
    }

    data_step = 0;

    while (!s->cancelled) {
        if (odv_vread(&c, 1, 1, fp) != 1) break;
        address++;

        /* Report progress periodically during DDL scan / metadata parse.
           Check every 64KB to keep overhead negligible.
           (odv_report_progress fires callback only when % changes) */
        if ((address & 0xFFFF) == 0) {
            odv_report_progress(s, fp);
        }

        switch (step) {

        case 0: /* Skip 256-byte header */
            if (address >= EXP_HEADER_SIZE) {
                step = 1;
                wlen = 0;
            }
            break;

        case 1: /* Export mode recognition */
            /* Accumulate chars until \0 or \n terminator.
               After the 256-byte header, RTABLES dumps have ~1.75KB of 0x00
               padding followed by binary metadata, then METRIC markers,
               then DDL text.  We need to recognize METRIC markers to
               properly transition to step 2 (DDL parsing). */
            if (c == 0x00 || c == 0x0a) {
                if (wlen > 0) {
                    word[wlen] = '\0';
                    /* DDL keyword → immediate transition */
                    if (starts_with_ci(word, "CREATE ") ||
                        starts_with_ci(word, "CONNECT ") ||
                        starts_with_ci(word, "ALTER ") ||
                        starts_with_ci(word, "GRANT ") ||
                        starts_with_ci(word, "INSERT ")) {
                        step = 2;
                        goto handle_ddl;
                    }
                    /* METRIC marker or INTERPRETED → transition to step 2.
                       METRICST = table mode, METRICSU/METRICEU = user mode,
                       METRICEE = database mode, INTERPRETED = direct export. */
                    if (strcmp(word, "METRICST") == 0 ||
                        strcmp(word, "METRICSU") == 0 ||
                        strcmp(word, "METRICEU") == 0 ||
                        strcmp(word, "METRICEE") == 0 ||
                        strcmp(word, "INTERPRETED") == 0) {
                        step = 2;
                        wlen = 0;
                        break;
                    }
                    wlen = 0;
                }
            } else {
                /* Only accumulate printable ASCII to avoid binary noise
                   from metadata blocks between header and DDL area */
                if (c >= 0x20 && c <= 0x7e) {
                    if (wlen == 0) word_pos = address - 1;
                    if (wlen < EXP_DDL_BUF_SIZE - 2)
                        word[wlen++] = (char)c;
                }
            }
            break;

        case 2: /* DDL text parsing */
            /* A framed record (FD FF chunk list anywhere a statement may
               start; a single or chunk-list record right after its header
               line) is read as a unit (M-08 / M-38). */
            if (wlen == 0 && (c == 0xFD || rec_mode != EXP_REC_NONE)) {
                int mode = rec_mode, kind = rec_kind, pre = 0, plen = 0;
                rec_mode = EXP_REC_NONE;
                rec_kind = EXP_HDR_ALONE;
                if (kind == EXP_HDR_PREFIX) {
                    pre = rec_hdr_len;
                    memcpy(word, rec_hdr, (size_t)pre);
                }
                if (exp_read_framed(fp, c, mode, &word, &word_cap, pre,
                                    &plen, &word_cut)) {
                    word_pos = address - 1;
                    address = odv_vtell(fp);
                    wlen = pre + plen;
                    word[wlen] = '\0';
                    /* Only constraint / DEFAULT / comment DDL and object type
                       definitions are taken from a frame. Stored code, view text and statistics never
                       define a table, so a CREATE TABLE / INSERT INTO /
                       CONNECT inside them is not a statement (M-38). */
                    if (kind != EXP_HDR_IGNORE &&
                        (starts_with_ci(word, "ALTER TABLE ") ||
                         starts_with_ci(word, "COMMENT ON ") ||
                         starts_with_ci(word, "CREATE TYPE "))) {
                        framed = 1;
                        goto handle_ddl;
                    }
                    wlen = 0;
                    break;
                }
                /* Not a framed record: read the byte as plain text below. */
            }
            if (c == 0x00 || c == 0x0a) {
                if (wlen == 0) break;
                word[wlen] = '\0';

            handle_ddl:
                ddlx.pending_table = pending_table;
                if (word_cut) {
                    /* Not silently: the statement is used only as far as it
                       was read, and a CHECK / DEFAULT left incomplete by the
                       cut is dropped with its own message. */
                    if (starts_with_ci(word, "CREATE ") ||
                        starts_with_ci(word, "ALTER TABLE ") ||
                        starts_with_ci(word, "COMMENT ON "))
                        snprintf(s->last_error, sizeof(s->last_error),
                                 "DDL statement at offset %lld is longer than %d MB; "
                                 "only its first %d MB were read (%.60s)",
                                 (long long)word_pos, EXP_DDL_BUF_MAX / (1024 * 1024),
                                 EXP_DDL_BUF_MAX / (1024 * 1024), word);
                    word_cut = 0;
                }
                if (starts_with_ci(word, "CONNECT ")) {
                    /* Extract schema name */
                    char sch[ODV_OBJNAME_LEN + 1] = {0};
                    extract_identifier(word + 8, sch, ODV_OBJNAME_LEN);
                    if (sch[0] != '\0')
                        odv_strcpy(current_schema, sch, ODV_OBJNAME_LEN);

                } else if (starts_with_ci(word, "CREATE ")) {
                    const char *after = skip_ws(word + 7);
                    /* Match "TABLE" but NOT "TABLESPACE" etc. */
                    int is_table = starts_with_ci(after, "TABLE") &&
                        (after[5] == ' ' || after[5] == '\t' ||
                         after[5] == '"' || after[5] == '\0');
                    /* A global temporary table has no rows and is not listed
                       (as with expdp), but it ends the previous table's
                       section: its PK, index and comment follow, and must not
                       be taken for that table's (M-33). */
                    int is_gtt = !is_table &&
                        starts_with_ci(after, "GLOBAL TEMPORARY TABLE ");
                    if (is_table || is_gtt) {
                        /* A data_offset (seek_offset) open targets one table.
                           All of its partitions share a single CREATE TABLE, and
                           its trailing COMMENT ON / CREATE INDEX / ALTER are not
                           CREATE TABLE, so the next CREATE TABLE is always a
                           different table — stop before reading it. Bound on the
                           CREATE TABLE boundary, not on the filter name match:
                           a table whose name differs only in case still matches
                           the case-insensitive filter, so otherwise the open
                           would read that table's rows as the target's (H-13). */
                        if (s->seek_offset > EXP_HEADER_SIZE && filter_found) {
                            goto done;
                        }
                        /* Notify previous pending table (with accumulated constraints) */
                        if (pending_table && s->table.name[0] != '\0') {
                            notify_exp_table(s, &defer, pending_row_count);
                            pending_table = 0;
                            pending_row_count = 0;
                        }
                        defer_finalize(s, &defer);
                        int64_t type_pos = 0;
                        int created = 0;
                        obj_table = 0;
                        if (!is_gtt) {
                            created = parse_create_table(s, word);
                            if (!created &&
                                exp_object_table(s, word, &otypes, &type_pos)) {
                                created = 1;
                                obj_table = 1;
                            }
                        }
                        /* Not a table we can list (a global temporary table,
                           an unknown OF type): clear its name, or the INSERT
                           INTO that follows would match it and read its rows
                           against no columns (M-37). */
                        if (!created) s->table.name[0] = '\0';
                        /* An object table is opened from its type's CREATE
                           TYPE, which its columns come from (M-37). */
                        if (obj_table && type_pos > EXP_HEADER_SIZE)
                            word_pos = type_pos;
                        if (created) {
                            /* File position of this CREATE TABLE for fast
                               seeking on later parse_dump calls: where the
                               statement starts, recorded as it was read. The
                               old address - wlen - 1 pointed into the middle
                               of a statement longer than the word buffer
                               (262,142 bytes, e.g. ~1700+ partitions), so the
                               table opened with 0 rows (M-30). */
                            s->table.ddl_offset = word_pos;

                            if (s->table.schema[0] == '\0' &&
                                current_schema[0] != '\0')
                                odv_strcpy(s->table.schema, current_schema,
                                           ODV_OBJNAME_LEN);
                            s->table.record_count = 0;
                            invalidate_meta_cache();
                            pending_table = 1;
                            if (defer.active)
                                defer.cur = defer_new_table(&defer, s->table.schema,
                                                            s->table.name);

                            /* Table filter check */
                            if (s->filter_active) {
                                int match = 1;
                                if (s->filter_table[0]) {
                                    char ft[ODV_OBJNAME_LEN + 1];
                                    int ft_len;
                                    odv_strcpy(ft, s->filter_table, ODV_OBJNAME_LEN);
                                    ft_len = (int)strlen(ft);
                                    /* Reverse-convert: UTF-8 -> dump charset */
                                    if (s->dump_charset != s->out_charset &&
                                        s->dump_charset != CHARSET_UNKNOWN) {
                                        char tmp[ODV_OBJNAME_LEN + 1];
                                        int tlen = 0;
                                        int cvt_rc = convert_charset(ft, ft_len, s->out_charset,
                                                            tmp, ODV_OBJNAME_LEN, s->dump_charset,
                                                            &tlen);
                                        if (cvt_rc == ODV_OK) {
                                            tmp[tlen] = '\0';
                                            odv_strcpy(ft, tmp, ODV_OBJNAME_LEN);
                                        }
                                    }
                                    if (odv_stricmp(s->table.name, ft) != 0)
                                        match = 0;
                                }
                                if (match && s->filter_schema[0]) {
                                    char fs[ODV_OBJNAME_LEN + 1];
                                    int fs_len;
                                    odv_strcpy(fs, s->filter_schema, ODV_OBJNAME_LEN);
                                    fs_len = (int)strlen(fs);
                                    if (s->dump_charset != s->out_charset &&
                                        s->dump_charset != CHARSET_UNKNOWN) {
                                        char tmp[ODV_OBJNAME_LEN + 1];
                                        int tlen = 0;
                                        if (convert_charset(fs, fs_len, s->out_charset,
                                                            tmp, ODV_OBJNAME_LEN, s->dump_charset,
                                                            &tlen) == ODV_OK) {
                                            tmp[tlen] = '\0';
                                            odv_strcpy(fs, tmp, ODV_OBJNAME_LEN);
                                        }
                                    }
                                    if (odv_stricmp(s->table.schema, fs) != 0)
                                        match = 0;
                                }
                                s->pass_flg = match ? 0 : 1;
                                /* Early exit: target table already processed,
                                   now a different table appeared → done */
                                if (filter_found && s->pass_flg) {
                                    goto done;
                                }
                                if (match && !s->filter_partition[0]) {
                                    /* Only set filter_found here when NOT using
                                     * partition filter. With partition filter,
                                     * filter_found is set by the PARTITION marker
                                     * handler when the matching partition is found. */
                                    filter_found = 1;
                                }
                            }

                            /* notify_exp_table is deferred to after record counting */
                        }
                    } else if (starts_with_ci(after, "TYPE ")) {
                        exp_ddl_create_type(&otypes, word, word_pos);
                    } else {
                        /* CREATE [UNIQUE] INDEX "name" ON "table" (...) */
                        exp_ddl_create_index(&ddlx, after);
                    }

                } else if (starts_with_ci(word, "ALTER TABLE ")) {
                    exp_ddl_alter_table(&ddlx, word);

                } else if (pending_table &&
                           (starts_with_ci(word, "PARTITION \"") ||
                            starts_with_ci(word, "SUBPARTITION \""))) {
                    /* EXP partition marker: PARTITION "name" appears before each
                     * partition's INSERT INTO statement.
                     * First occurrence: the table is partitioned.
                     * Subsequent: each is a new partition of the same table. */
                    const char *pp = word;
                    int is_sub = starts_with_ci(pp, "SUBPARTITION");
                    pp += is_sub ? 13 : 10;
                    pp = skip_ws(pp);
                    char part_name[ODV_OBJNAME_LEN + 1] = {0};
                    if (*pp == '"') {
                        pp++;
                        int pn = 0;
                        while (*pp && *pp != '"' && pn < ODV_OBJNAME_LEN) part_name[pn++] = *pp++;
                        part_name[pn] = '\0';
                    }

                    if (part_name[0]) {
                        /* Keep ddl_offset at the CREATE TABLE position
                         * (shared by all partitions). The partition filter
                         * (filter_partition) is used to select which partition. */

                        if (!s->table.is_partition) {
                            /* First partition marker for this table → mark as partitioned */
                            s->table.is_partition = 1;
                            odv_strcpy(s->table.partition, part_name, ODV_OBJNAME_LEN);
                        } else {
                            /* Subsequent partition: notify the previous partition,
                             * then start a new partition entry with same table structure */
                            notify_exp_table(s, &defer, pending_row_count > 0 ? pending_row_count : s->table.record_count);
                            pending_row_count = 0;
                            s->table.record_count = 0;
                            odv_strcpy(s->table.partition, part_name, ODV_OBJNAME_LEN);
                        }

                        /* Apply partition filter: if filter_partition is set,
                         * skip partitions that don't match */
                        if (s->filter_active && s->filter_partition[0]) {
                            if (odv_stricmp(part_name, s->filter_partition) != 0) {
                                s->pass_flg = 1;  /* Skip this partition's data */
                            } else {
                                s->pass_flg = 0;  /* Parse this partition */
                                filter_found = 1;
                            }
                        }
                    }

                } else if (starts_with_ci(word, "INSERT INTO ")) {
                    /* Extract table name from INSERT INTO and compare with
                       the table from the most recent CREATE TABLE.
                       Format: INSERT INTO "schema"."table" or INSERT INTO "table"
                       Only transition to binary metadata (step 3) if they
                       match; otherwise this is a DDL INSERT (e.g. PL/SQL)
                       and should be ignored. */
                    char ins_table[ODV_OBJNAME_LEN + 1] = {0};
                    {
                        const char *ip = extract_identifier(word + 12, ins_table, ODV_OBJNAME_LEN);
                        ip = skip_ws(ip);
                        if (*ip == '.') {
                            /* schema.table format — extract table part */
                            ip++;
                            extract_identifier(ip, ins_table, ODV_OBJNAME_LEN);
                        }
                    }
                    /* The INSERT INTO name is the raw client-charset name from the
                       dump; fold it the same way the stored name was so a CESU-8
                       (Oracle "UTF8") supplementary-plane table name still matches
                       and its rows are parsed (H-09). No-op for AL32UTF8. */
                    if (s->dump_charset == CHARSET_UTF8)
                        normalize_cesu8(ins_table);

                    if (s->table.name[0] != '\0' &&
                        strcmp(s->table.name, ins_table) == 0 && obj_table &&
                        !strstr(word, "(\"SYS_NC_OID$\", \"SYS_NC_ROWINFO$\")")) {
                        /* An object table stored any other way (nested
                           tables, LOB attributes, ...) is not supported. */
                        s->table.name[0] = '\0';
                        pending_table = 0;
                        defer.cur = -1;
                        obj_table = 0;
                    } else if (s->table.name[0] != '\0' &&
                        strcmp(s->table.name, ins_table) == 0) {
                        /* Table name matches — transition to binary metadata.
                           The rows follow the INSERT's column order (M-01). */
                        exp_insert_columns(s, word, wire_col);
                        step = 3;
                        data_step = 0;
                        meta_col_count = 0;
                        meta_col_idx = 0;
                        meta_lob_idx = 0;
                        meta_object_count = 0;
                        meta_name_count = 0;
                        null_count = 0;
                    }
                    /* else: DDL INSERT, ignore and stay in step 2 */

                } else if (starts_with_ci(word, "COMMENT ON ")) {
                    exp_ddl_comment(&ddlx, word);
                }

                /* A plain header line announces the framed record after it. */
                if (!framed && step == 2) {
                    rec_mode = exp_header_mode(word, wlen, &rec_kind);
                    if (rec_mode != EXP_REC_NONE && rec_kind == EXP_HDR_PREFIX) {
                        if (wlen < (int)sizeof(rec_hdr)) {
                            memcpy(rec_hdr, word, (size_t)wlen);
                            rec_hdr_len = wlen;
                        } else {
                            rec_kind = EXP_HDR_IGNORE;  /* header too long to join */
                        }
                    }
                }
                framed = 0;
                wlen = 0;
            } else {
                if (wlen == 0) {
                    word_pos = address - 1;
                    word_cut = 0;
                }
                if (wlen < word_cap - 2 || exp_word_grow(&word, &word_cap, wlen + 3))
                    word[wlen++] = (char)c;
                else
                    word_cut = 1;
            }
            break;

        case 3: /* Binary metadata after INSERT INTO */
            /*
             * Binary metadata structure:
             *
             * Structure (field order):
             *   2 bytes: column count (LE)         [data_step 0-1]
             *   For each column:
             *     1 byte: Oracle internal type code [data_step 2]
             *     1 byte: null flag                 [data_step 3]
             *     2 bytes: byte length (LE)         [data_step 4-5]
             *     [4 bytes: charset if char type]   [data_step 6-9]
             *   If LOB columns exist:
             *     2 bytes: LOB column count (LE)    [data_step 10-11]
             *     LOB column names (len-prefixed)   [data_step 12-13]
             *   Null padding (0x00 bytes)           [data_step 20]
             *   Record data starts at first non-0x00 byte
             *
             * Char types (need 4 charset bytes):
             *   0x01=VARCHAR2, 0x60=CHAR, 0x70=CLOB, 0xD0=NCLOB
             *   (per reference: 0x40=LONG_RAW also flagged)
             *
             * XMLTYPE (0x3A) triggers abort — back to DDL.
             */
            switch (data_step) {
            case 0: /* Column count byte 0 */
                meta_buf[0] = c;
                data_step = 1;
                break;

            case 1: /* Column count byte 1 */
                meta_col_count = (int)meta_buf[0] | ((int)c << 8);
                meta_col_idx = 0;
                is_char_type = 0;
                if (meta_col_count <= 0 || meta_col_count > ODV_MAX_COLUMNS) {
                    /* Discard this table. notify_exp_table is deferred to the
                       next CREATE TABLE, so the current (pending) table is NOT
                       in table_list yet — table_count still counts up to the
                       PREVIOUS, already-notified table. Decrementing it here
                       therefore deleted that good table's entry and shifted
                       every later entry's type/partition by one (H-24). Just
                       drop the pending table; leave table_count alone. */
                    s->table.name[0] = '\0';
                    pending_table = 0;
                    defer.cur = -1;
                    obj_table = 0;
                    step = 2;
                    wlen = 0;
                } else {
                    data_step = 2;
                }
                break;

            case 2: /* Column type byte */
                meta_col_type = (int)c;

                /* Check type code against known Oracle internal types */
                switch (c) {
                case 0x3A: /* XMLTYPE STORE AS CLOB / OF XMLTYPE / ANYDATA —
                              unsupported. Drop the pending table without
                              touching table_count: it is not in table_list yet
                              (deferred notify), so a decrement would delete the
                              previous good table and misalign the rest (H-24). */
                    s->table.name[0] = '\0';
                    pending_table = 0;
                    defer.cur = -1;
                    obj_table = 0;
                    step = 2;
                    wlen = 0;
                    goto meta_done;

                /* Char types: need 4 charset bytes after length */
                case 0x01: /* VARCHAR2 */
                case 0x40: /* LONG RAW (ref flags as char) */
                case 0x60: /* CHAR */
                case 0x70: /* CLOB */
                case 0xD0: /* NCLOB */
                    is_char_type = 1;
                    break;

                /* Non-char types: no charset bytes */
                case 0x02: /* NUMBER */
                case 0x08: /* LONG */
                case 0x09: /* VARCHAR (alternate) */
                case 0x0C: /* DATE */
                case 0x17: /* RAW */
                case 0x18: /* LONG RAW (alt) */
                case 0x45: /* ROWID */
                case 0x71: /* BLOB */
                case 0x72: /* BFILE */
                case 0xB4: /* TIMESTAMP */
                case 0xB5: /* TIMESTAMP WITH TIMEZONE */
                case 0xB6: /* INTERVAL YEAR TO MONTH */
                case 0xB7: /* INTERVAL DAY TO SECOND */
                case 0xE7: /* TIMESTAMP WITH LOCAL TIMEZONE */
                    is_char_type = 0;
                    break;

                case 0x79: /* object type (ADT / VARRAY / nested table / SDO_GEOMETRY) */
                case 0x7B: /* REF */
                    /* The value is opaque (the DDL type falls back to VARCHAR),
                       but after the last column the metadata carries a type-name
                       section — same shape as the LOB name section
                       (count, nulls, length-prefixed "TYPE\x04ATTR" name). If it
                       is not skipped, data_step 20 takes the type name as the
                       first row and reads past the table's 0xFFFF end into the
                       next table, dropping the following table (H-25). Count the
                       column so the name section is consumed below. */
                    is_char_type = 0;
                    meta_object_count++;
                    break;

                default:
                    /* Unknown type — WARNING and continue parsing */
                    is_char_type = 0;
                    break;
                }

                data_step = 3;
            meta_done:
                break;

            case 3: /* Null flag byte */
                data_step = 4;
                break;

            case 4: /* Length byte 0 */
                meta_buf[0] = c;
                data_step = 5;
                break;

            case 5: /* Length byte 1 — advance */
                meta_col_idx++;
                if (is_char_type) {
                    data_step = 6; /* read 4 charset bytes */
                } else {
                    if (meta_col_idx >= meta_col_count) {
                        /* Use DDL-parsed LOB count (ref: tbl_lob_num from
                           table->lob_column_ct, not from metadata bytes) plus
                           object/REF columns, which carry the same kind of
                           post-column name section (H-25). */
                        if (s->table.lob_col_count > 0 || meta_object_count > 0) {
                            meta_name_count = s->table.lob_col_count + meta_object_count;
                            data_step = 10; /* LOB / object name section */
                        } else {
                            data_step = 20; /* null padding */
                            null_count = 0;
                        }
                    } else {
                        data_step = 2; /* next column */
                    }
                }
                break;

            case 6: case 7: case 8: /* Charset bytes 0-2 */
                data_step++;
                break;

            case 9: /* Charset byte 3 — advance to next col or finish */
                if (meta_col_idx >= meta_col_count) {
                    /* Use DDL-parsed LOB count (ref: tbl_lob_num) plus object/REF
                       columns, which carry the same post-column name section (H-25). */
                    if (s->table.lob_col_count > 0 || meta_object_count > 0) {
                        meta_name_count = s->table.lob_col_count + meta_object_count;
                        data_step = 10; /* LOB / object name section */
                    } else {
                        data_step = 20; /* null padding */
                        null_count = 0;
                    }
                } else {
                    data_step = 2; /* next column */
                }
                break;

            case 10: /* LOB column count byte 0 */
                meta_buf[0] = c;
                data_step = 11;
                break;

            case 11: /* LOB column count byte 1 */
                lob_total = (int)meta_buf[0] | ((int)c << 8);
                meta_lob_idx = 0;
                /* Do NOT reset null_count here — ref does not reset null_ct
                   in data_step 11. The prior null_ct is used in data_step 12. */
                data_step = 12;
                break;

            case 12: /* LOB metadata: null skip / name length */
                if (c == 0xFF) {
                    /* End marker within LOB section */
                    step = 2;
                    wlen = 0;
                } else if (c != 0x00) {
                    if (null_count > 0) {
                        /* Non-zero after nulls = LOB column name length */
                        lob_name_len = (int)c;
                        lob_name_read = 0;
                        data_step = 13;
                    }
                    null_count++;
                } else {
                    null_count++;
                }
                break;

            case 13: /* LOB column name bytes */
                if (c < 0x04) {
                    /* End of a LOB / object name */
                    meta_lob_idx++;
                    if (meta_lob_idx >= meta_name_count) {
                        /* All names read — go to null padding */
                        null_count = 0;
                        meta_lob_idx = 0;
                        data_step = 20; /* final null padding */
                    } else {
                        /* Next name — do NOT reset null_count (ref behavior) */
                        data_step = 12; /* next LOB/object */
                    }
                }
                /* else: accumulate (we don't need the actual name) */
                break;

            case 20: { /* Null padding, then record data / 0xFFFF / next DDL */
                int found_row = 0;
                int64_t rec_start = 0;

                /* The byte values at the row-data boundary were being guessed
                   byte-by-byte, which could not tell a padding 0x00 from the low
                   byte of a 256-byte first column, mistook a 255-byte first
                   column's 0xFF for the no-row marker, took a 21c-direct 0-row
                   table's 0x0a as row data, and (for a LOB-as-last-column table)
                   waited for 3 nulls that never came — so those tables read 0
                   rows and often swallowed the next table (H-11). Use the fixed
                   layout instead. */
                if (s->table.lob_col_count == 0 && meta_object_count == 0) {
                    /* Plain table: a fixed 4-byte block — 00 00 00 00
                       (conventional) or 00 00 01 00 (direct) — then 0xFFFF (no
                       rows), the first row's first-column length, or (21c direct
                       with no rows, where no 0xFFFF is written) the 0x0a that
                       opens the next DDL line. `c` is the first padding byte. */
                    unsigned char pad[3];
                    unsigned char look[16];
                    int64_t p;
                    size_t n;
                    if (odv_vread(pad, 1, 3, fp) != 3) {
                        address = odv_vtell(fp); step = 2; wlen = 0; break;
                    }
                    p = odv_vtell(fp);           /* just past the 4 padding bytes */
                    memset(look, 0, sizeof(look));
                    n = odv_vread(look, 1, sizeof(look), fp);
                    odv_vseek(fp, p, SEEK_SET);  /* rewind; `look` was only a peek */
                    address = p;
                    if (n < 2 || (look[0] == 0xFF && look[1] == 0xFF)) {
                        /* No rows (0xFFFF table end, or EOF). */
                        pending_row_count = 0;
                        step = 2; wlen = 0;
                        break;
                    }
                    if (s->dump_type == DUMP_EXP_DIRECT && look[0] == 0x0A &&
                        (starts_with_ci((const char *)look + 1, "CREATE ") ||
                         starts_with_ci((const char *)look + 1, "ALTER ") ||
                         starts_with_ci((const char *)look + 1, "INSERT ") ||
                         starts_with_ci((const char *)look + 1, "CONNECT ") ||
                         starts_with_ci((const char *)look + 1, "COMMENT ") ||
                         starts_with_ci((const char *)look + 1, "GRANT ") ||
                         starts_with_ci((const char *)look + 1, "PARTITION ") ||
                         starts_with_ci((const char *)look + 1, "PARTITION\"") ||
                         starts_with_ci((const char *)look + 1, "SUBPARTITION") ||
                         starts_with_ci((const char *)look + 1, "REM ") ||
                         starts_with_ci((const char *)look + 1, "METRIC"))) {
                        /* 21c direct: a table/partition with no rows. The next
                           DDL line begins at this 0x0a. Leave fp there so step 2
                           reads it instead of treating it as a row (H-11 (3)). */
                        pending_row_count = 0;
                        step = 2; wlen = 0;
                        break;
                    }
                    rec_start = p;
                    found_row = 1;
                } else {
                    /* LOB / object table: conventional row layout (even inside a
                       direct dump). The row starts at the first non-null byte
                       after the name section; do NOT wait for 3 nulls, or a
                       LOB-as-last-column table's single pad null leaves the
                       locator misread and the table shows 0 rows (H-11 (1)). */
                    if (c == 0x00) { null_count++; break; }
                    if (c == 0xFF) {
                        pending_row_count = 0;
                        step = 2; wlen = 0;
                        break;
                    }
                    rec_start = odv_vtell(fp) - 1;
                    found_row = 1;
                }

                if (found_row) {
                    {
                        if (list_only && s->filter_active && s->pass_flg) {
                            /* Filtered out in list_only: skip records entirely */
                            /* Scan forward to find 0xFFFF end marker */
                            if (exp_skip_to_table_end(s, fp) == 0) goto done;
                            pending_row_count = 0;
                        } else if (!s->filter_active || !s->pass_flg) {
                            /* Parse records (list_only=count only, full=deliver) */
                            rc = obj_table
                                ? parse_exp_object_rows(s, fp, rec_start, list_only)
                                : parse_exp_records(s, fp, rec_start, list_only,
                                                    wire_col);
                            pending_row_count = s->table.record_count;
                        } else {
                            /* Filtered out in full parse: skip */
                            if (exp_skip_to_table_end(s, fp) == 0) goto done;
                            pending_row_count = 0;
                        }
                        /* Update address to match new file position */
                        address = odv_vtell(fp);
                    }
                    step = 2;
                    wlen = 0;
                    if (rc == ODV_ERROR_CANCELLED) goto done;
                    rc = ODV_OK;

                    /* A partition-name filter wants only its one partition, so
                     * stop once it has been read. A data_offset (seek_offset)
                     * open must NOT stop here: every partition of a table shares
                     * the CREATE TABLE offset, and the trailing COMMENT ON /
                     * CREATE INDEX / ALTER ... ADD CONSTRAINT come after the row
                     * data. Exiting after the first data block gave only the
                     * first partition and dropped that DDL (H-13). The table
                     * filter still bounds the scan: the next unrelated CREATE
                     * TABLE triggers the early exit above, so no other table is
                     * read. */
                    if (s->filter_active && filter_found && s->filter_partition[0])
                        goto done;
                }
                break;
            } /* case 20 */
            } /* switch data_step */
            break;

        } /* switch step */
    }

done:
    /* Notify last pending table if not yet notified (with constraints) */
    if (pending_table && s->table.name[0] != '\0') {
        notify_exp_table(s, &defer, pending_row_count > 0 ? pending_row_count : s->table.record_count);
    }
    /* Listing: every table is now complete, including the foreign keys and
       indexes exp writes after the last table — deliver them in order. */
    defer_finalize(s, &defer);
    if (defer.active) defer_flush(s, &defer);
    defer_free(&defer);
    exp_free_types(&otypes);

    free(word);

    /* Suppress GCC -Wunused-but-set-variable for state-machine variables
       that are assigned during parse but only used for transitions */
    (void)meta_col_type; (void)lob_total; (void)lob_name_len; (void)lob_name_read;

    if (s->cancelled) return ODV_ERROR_CANCELLED;
    return rc;
}

/*---------------------------------------------------------------------------
    parse_exp_dump

    Main entry point for EXP dump parsing.
 ---------------------------------------------------------------------------*/
int parse_exp_dump(ODV_SESSION *s, int list_only)
{
    ODV_VFILE vf;
    ODV_VFILE *fp = &vf;
    int rc;

    if (!s) return ODV_ERROR_INVALID_ARG;

    /* Open the dump as one logical stream (joins FILESIZE= split volumes). */
    rc = odv_vopen(&vf, s);
    if (rc != ODV_OK) {
        if (s->last_error[0] == '\0')
            odv_strcpy(s->last_error, "Cannot open dump file", ODV_MSG_LEN);
        return rc;
    }

    /* Reset progress tracking */
    s->last_progress_pct = -1;

    /* Parse header */
    rc = parse_exp_header(s, fp);
    if (rc != ODV_OK) {
        odv_vclose(&vf);
        return rc;
    }

    /* Parse DDL and data */
    rc = parse_exp_ddl_and_data(s, fp, list_only);

    odv_vclose(&vf);
    return rc;
}

/*---------------------------------------------------------------------------
    trim_right - trim trailing whitespace
 ---------------------------------------------------------------------------*/
static void trim_right(char *str)
{
    int len;
    if (!str) return;
    len = (int)strlen(str);
    while (len > 0 && ((unsigned char)str[len - 1] <= ' ')) {
        len--;
    }
    str[len] = '\0';
}
