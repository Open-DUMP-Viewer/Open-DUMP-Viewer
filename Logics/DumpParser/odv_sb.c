/*****************************************************************************
    Open DUMP Viewer for Oracle database

    odv_sb.c
    Oracle single-byte character sets <-> UTF-8

    Decodes the single-byte character sets that have no Windows code page of
    their own in odv_charset.c (CL8MSWIN1251, EE8MSWIN1250, WE8ISO8859P9,
    the EBCDIC and 7-bit national sets, ...) with Oracle's own mapping
    (generated tables in odv_sbmap.c, measured with UTL_RAW.CONVERT for every
    byte on 11.2 / 21c / 23.26).  Works the same on every platform (no Win32
    API).  Such a character set is CHARSET_ORA_SB_BASE + its NLS_CHARSET_ID.

    US7ASCII / WE8ISO8859P1 / WE8ISO8859P15 / WE8MSWIN1252 keep their Windows
    code pages (CHARSET_US7 / US8 / ISO8859P15 / WIN1252); the tables also
    hold them, so that every single-byte character set can be recognised: a
    CLOB of a database whose character set is single-byte is stored in that
    character set, and as AL16UTF16 only in a multi-byte database.

    Byte rules (Oracle's behaviour, tools/sbmap/query_sbmap.sql):
      single byte -> UTF-8: every byte is one character (undefined bytes are
            whatever Oracle gives them, mostly U+FFFD or the C1 control).
      UTF-8 -> single byte (matching table-name filters against the dump's
            names, and the names / DDL text of the expdp master table, which
            are kept in the dump's character set): a character read from one
            byte goes back to that byte, so a decoded name re-encodes to its
            original bytes (Oracle itself converts AR8XBASIC U+0649,
            WE8EBCDIC924 U+0178 and RU8PC866 U+00A0 to another byte); a
            character that several bytes read as goes back to the byte Oracle
            converts it to (the lowest of them, or a generated exception);
            U+FFFD, a character the set lacks and an invalid UTF-8 byte become
            the set's replacement character (mostly '?' or an inverted
            question mark); an incomplete sequence at the end is dropped.

    Copyright (C) 2026 YANAI Taketo
 *****************************************************************************/

#include <ctype.h>

#include "odv_types.h"
#include "odv_sbmap.h"

static const ODV_SBMAP_CHARSET *find_id(int id)
{
    int lo = 0, hi = odv_sbmap_charset_count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        int v = odv_sbmap_charsets[mid].id;
        if (v == id) return &odv_sbmap_charsets[mid];
        if (v < id) lo = mid + 1;
        else hi = mid - 1;
    }
    return NULL;
}

static const ODV_SBMAP_CHARSET *find_cs(int cs)
{
    if (!CHARSET_IS_ORA_SB(cs)) return NULL;
    return find_id(cs - CHARSET_ORA_SB_BASE);
}

static unsigned ucs_of(const ODV_SBMAP_CHARSET *m, unsigned b)
{
    if (b >= 0x80) return m->hi[b - 0x80];
    return m->lo ? m->lo[b] : b;
}

/* Is this Oracle NLS_CHARSET_ID a single-byte character set? */
int odv_sb_id_is_single_byte(int oracle_id)
{
    return oracle_id > 0 && find_id(oracle_id) != NULL;
}

/* CHARSET_ORA_SB_BASE + id for a single-byte character set, else CHARSET_UNKNOWN */
int odv_sb_charset_from_id(int oracle_id)
{
    return odv_sb_id_is_single_byte(oracle_id) ? CHARSET_ORA_SB_BASE + oracle_id : CHARSET_UNKNOWN;
}

/* The same by name (case-insensitive), else CHARSET_UNKNOWN */
int odv_sb_charset_from_name(const char *name)
{
    int i;
    if (!name || !name[0]) return CHARSET_UNKNOWN;
    for (i = 0; i < odv_sbmap_charset_count; i++) {
        const char *a = odv_sbmap_charsets[i].name, *b = name;
        while (*a && *b && toupper((unsigned char)*a) == toupper((unsigned char)*b)) { a++; b++; }
        if (*a == '\0' && *b == '\0')
            return CHARSET_ORA_SB_BASE + odv_sbmap_charsets[i].id;
    }
    return CHARSET_UNKNOWN;
}

int odv_sb_is_table_charset(int cs)
{
    return find_cs(cs) != NULL;
}

static int put_utf8(unsigned u, char *dst, int pos, int cap)
{
    if (u < 0x80) {
        if (pos + 1 > cap) return -1;
        dst[pos] = (char)u;
        return pos + 1;
    }
    if (u < 0x800) {
        if (pos + 2 > cap) return -1;
        dst[pos]     = (char)(0xC0 | (u >> 6));
        dst[pos + 1] = (char)(0x80 | (u & 0x3F));
        return pos + 2;
    }
    if (pos + 3 > cap) return -1;
    dst[pos]     = (char)(0xE0 | (u >> 12));
    dst[pos + 1] = (char)(0x80 | ((u >> 6) & 0x3F));
    dst[pos + 2] = (char)(0x80 | (u & 0x3F));
    return pos + 3;
}

/*---------------------------------------------------------------------------
    odv_sb_to_utf8

    Output is NUL-terminated.  If dst is too small, the output stops at the
    last whole character that fits (still ODV_OK).  A byte becomes at most
    3 UTF-8 bytes (every character is in the BMP).
 ---------------------------------------------------------------------------*/
int odv_sb_to_utf8(const unsigned char *src, int src_len, int cs,
                   char *dst, int dst_size, int *out_len)
{
    const ODV_SBMAP_CHARSET *m = find_cs(cs);
    int i, pos = 0, cap;

    if (!src || !dst || dst_size <= 0 || !m) return ODV_ERROR_INVALID_ARG;
    cap = dst_size - 1;
    for (i = 0; i < src_len; i++) {
        int np = put_utf8(ucs_of(m, src[i]), dst, pos, cap);
        if (np < 0) break;                            /* dst full */
        pos = np;
    }
    dst[pos] = '\0';
    if (out_len) *out_len = pos;
    return ODV_OK;
}

/* Decode one UTF-8 character at s[i].  Returns its length, 0 if the input
   ends in the middle of a character, or -1 for an invalid byte. */
static int utf8_next(const unsigned char *s, int n, int i, unsigned *cp)
{
    unsigned b = s[i], lo = 0x80, hi = 0xBF;
    int need, k;
    if (b < 0x80) { *cp = b; return 1; }
    if (b >= 0xC2 && b <= 0xDF) { need = 1; *cp = b & 0x1F; }
    else if (b >= 0xE0 && b <= 0xEF) { need = 2; *cp = b & 0x0F; if (b == 0xE0) lo = 0xA0; }
    else if (b >= 0xF0 && b <= 0xF4) {
        need = 3; *cp = b & 0x07;
        if (b == 0xF0) lo = 0x90;
        if (b == 0xF4) hi = 0x8F;
    }
    else return -1;
    for (k = 1; k <= need; k++) {
        unsigned c;
        if (i + k >= n) return 0;
        c = s[i + k];
        if (c < lo || c > hi) return -1;
        lo = 0x80; hi = 0xBF;
        *cp = (*cp << 6) | (c & 0x3F);
    }
    return need + 1;
}

static unsigned encode_char(const ODV_SBMAP_CHARSET *m, unsigned cp)
{
    int i;
    for (i = 0; i < m->rev_count; i++)
        if (m->rev[i].ucs == cp) return m->rev[i].byte;
    if (cp == 0xFFFD || cp > 0xFFFF) return m->repl;
    for (i = 0; i < 256; i++)
        if (ucs_of(m, (unsigned)i) == cp) return (unsigned)i;
    return m->repl;
}

int odv_utf8_to_sb(const unsigned char *src, int src_len, int cs,
                   char *dst, int dst_size, int *out_len)
{
    const ODV_SBMAP_CHARSET *m = find_cs(cs);
    int i = 0, pos = 0, cap;

    if (!src || !dst || dst_size <= 0 || !m) return ODV_ERROR_INVALID_ARG;
    cap = dst_size - 1;
    while (i < src_len) {
        unsigned cp = 0, code;
        int n = utf8_next(src, src_len, i, &cp);
        if (n == 0) break;                            /* incomplete: dropped */
        if (n < 0) { code = m->repl; n = 1; }
        else code = encode_char(m, cp);
        if (pos + 1 > cap) break;
        dst[pos++] = (char)code;
        i += n;
    }
    dst[pos] = '\0';
    if (out_len) *out_len = pos;
    return ODV_OK;
}
