/*****************************************************************************
    Open DUMP Viewer for Oracle database

    odv_ja16.c
    Oracle JA16SJIS / JA16SJISTILDE / JA16SJISYEN / JA16EUC / JA16EUCTILDE /
    JA16EUCYEN <-> UTF-8

    Decodes with Oracle's own mapping (generated tables in odv_ja16map.c,
    measured with UTL_RAW.CONVERT for every code on 11.2 / 21c / 23.26; the
    YEN variants on 21c / 23.26), not with the Windows code pages: CP20932
    does not treat 0x8F + 2 bytes (JIS X 0212) as one character, CP932 /
    CP20932 map ~, ¢, £, ¬ etc. to different Unicode characters than Oracle
    does, and in the YEN variants Oracle reads 0x5C as U+00A5 (YEN SIGN).
    Works the same on every platform (no Win32 API).

    Byte rules (Oracle's behaviour, tools/ja16map/query_edge.sql):
      SJIS: 00-7F and A1-DF are one byte.  A lead byte (81-84, 87-9F, E0-EA,
            ED, EE, F0-FC) always takes 2 bytes; a trail outside 40-7E/80-FC
            or an undefined code gives U+FFFD.  Other bytes are U+FFFD (1 byte).
      EUC : 00-7F one byte; A1-FE and 8E take 2 bytes, 8F takes 3 bytes
            (JIS X 0212); out-of-range or undefined gives U+FFFD.  Other
            bytes are U+FFFD (1 byte).
      Both: an incomplete character at the end of the input is dropped.

    UTF-8 -> JA16 (used to match table-name filters against the dump's
    names) follows Oracle's reverse conversion: a decoded name always
    re-encodes to its original bytes.  A character the charset cannot
    represent becomes Oracle's replacement (SJIS 81 48, EUC A1 A9), an invalid
    UTF-8 byte becomes '?', an incomplete sequence at the end is dropped.

    Copyright (C) 2026 YANAI Taketo
 *****************************************************************************/

#include "odv_types.h"
#include "odv_ja16map.h"

#define UCS_REPL 0xFFFD

static int variant_of(int cs)
{
    switch (cs) {
    case CHARSET_SJIS:       return ODV_JA16_SJIS;
    case CHARSET_SJIS_TILDE: return ODV_JA16_SJIS_TILDE;
    case CHARSET_EUC:        return ODV_JA16_EUC;
    case CHARSET_EUC_TILDE:  return ODV_JA16_EUC_TILDE;
    case CHARSET_SJIS_YEN:   return ODV_JA16_SJIS_YEN;
    case CHARSET_EUC_YEN:    return ODV_JA16_EUC_YEN;
    default:                 return -1;
    }
}

int odv_ja16_is_table_charset(int cs)
{
    return variant_of(cs) >= 0;
}

static int is_sjis(int v)
{
    return v == ODV_JA16_SJIS || v == ODV_JA16_SJIS_TILDE || v == ODV_JA16_SJIS_YEN;
}

static int sjis_trail_index(unsigned t)
{
    if (t >= 0x40 && t <= 0x7E) return (int)t - 0x40;
    if (t >= 0x80 && t <= 0xFC) return (int)t - 0x41;
    return -1;
}

/* Base-table lookups (JA16SJIS / JA16EUC) */
static unsigned sjis_base2(unsigned lead, unsigned trail)
{
    int ti = sjis_trail_index(trail);
    if (ti < 0 || odv_ja16_sjis_single[lead] != ODV_JA16_LEAD) return UCS_REPL;
    return odv_ja16_sjis_rows[odv_ja16_sjis_lead_row[lead]][ti];
}

static unsigned jis0208(unsigned b1, unsigned b2)
{
    if (b1 < 0xA1 || b1 > 0xFE || b2 < 0xA1 || b2 > 0xFE) return UCS_REPL;
    return odv_ja16_jis0208_rows[odv_ja16_jis0208_row[b1 - 0xA1]][b2 - 0xA1];
}

static unsigned jis0212(unsigned b2, unsigned b3)
{
    if (b2 < 0xA1 || b2 > 0xFE || b3 < 0xA1 || b3 > 0xFE) return UCS_REPL;
    return odv_ja16_jis0212_rows[odv_ja16_jis0212_row[b2 - 0xA1]][b3 - 0xA1];
}

/* Variant (TILDE) differences from the base table */
static unsigned fwd_override(int v, uint32_t code, unsigned u)
{
    int i;
    for (i = 0; i < odv_ja16_fwd_ovr_count; i++)
        if (odv_ja16_fwd_ovr[i].variant == v && odv_ja16_fwd_ovr[i].code == code)
            return odv_ja16_fwd_ovr[i].ucs;
    return u;
}

/* Does the variant change a single-byte character? (YEN variants: 5C) */
static int has_single_override(int v)
{
    int i;
    for (i = 0; i < odv_ja16_fwd_ovr_count; i++)
        if (odv_ja16_fwd_ovr[i].variant == v && odv_ja16_fwd_ovr[i].code <= 0xFF)
            return 1;
    return 0;
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
    odv_ja16_to_utf8

    Output is NUL-terminated.  If dst is too small, the output stops at the
    last whole character that fits (still ODV_OK).
 ---------------------------------------------------------------------------*/
int odv_ja16_to_utf8(const unsigned char *src, int src_len, int cs,
                     char *dst, int dst_size, int *out_len)
{
    int v = variant_of(cs);
    int i = 0, pos = 0, cap, has_single_ovr;
    const uint16_t *single;

    if (!src || !dst || dst_size <= 0 || v < 0) return ODV_ERROR_INVALID_ARG;
    cap = dst_size - 1;
    single = is_sjis(v) ? odv_ja16_sjis_single : odv_ja16_euc_single;
    has_single_ovr = has_single_override(v);

    while (i < src_len) {
        unsigned b = src[i], u = single[b];
        uint32_t code;
        int np;
        if (u != ODV_JA16_LEAD) {
            if (has_single_ovr) u = fwd_override(v, b, u);   /* YEN: 5C -> U+00A5 */
            i++;
        } else if (is_sjis(v) || b != 0x8F) {
            unsigned t;
            if (i + 1 >= src_len) break;              /* incomplete: dropped */
            t = src[i + 1];
            code = ((uint32_t)b << 8) | t;
            if (is_sjis(v))   u = sjis_base2(b, t);
            else if (b == 0x8E) u = (t >= 0xA1 && t <= 0xDF) ? 0xFF61 + (t - 0xA1) : UCS_REPL;
            else              u = jis0208(b, t);
            u = fwd_override(v, code, u);
            i += 2;
        } else {
            if (i + 2 >= src_len) break;              /* incomplete: dropped */
            code = ((uint32_t)b << 16) | ((uint32_t)src[i + 1] << 8) | src[i + 2];
            u = fwd_override(v, code, jis0212(src[i + 1], src[i + 2]));
            i += 3;
        }
        np = put_utf8(u, dst, pos, cap);
        if (np < 0) break;                            /* dst full */
        pos = np;
    }
    dst[pos] = '\0';
    if (out_len) *out_len = pos;
    return ODV_OK;
}

/*---------------------------------------------------------------------------
    UTF-8 -> JA16
 ---------------------------------------------------------------------------*/

/* UCS of a reverse-table entry (base table) */
static unsigned rev_entry_ucs(int sjis, uint16_t e)
{
    if (sjis) return sjis_base2(e >> 8, e & 0xFF);
    if (e < 0x2400) return jis0212(0xA1 + e / 94, 0xA1 + e % 94);
    return jis0208(e >> 8, e & 0xFF);
}

static uint32_t encode_char(int v, unsigned u)
{
    int sjis = is_sjis(v);
    const uint16_t *tab = sjis ? odv_ja16_sjis_rev : odv_ja16_euc_rev;
    int lo = 0, hi = (sjis ? odv_ja16_sjis_rev_count : odv_ja16_euc_rev_count) - 1;
    int i;

    for (i = 0; i < odv_ja16_rev_ovr_count; i++)
        if (odv_ja16_rev_ovr[i].variant == v && odv_ja16_rev_ovr[i].ucs == u)
            return odv_ja16_rev_ovr[i].code;
    if (u < 0x80) return u;
    if (u >= 0xFF61 && u <= 0xFF9F)
        return sjis ? (u - 0xFF61 + 0xA1) : (0x8E00u | (u - 0xFF61 + 0xA1));
    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        unsigned x = rev_entry_ucs(sjis, tab[mid]);
        if (x == u) {
            uint16_t e = tab[mid];
            if (!sjis && e < 0x2400)
                return (0x8Fu << 16) | ((0xA1u + e / 94) << 8) | (0xA1u + e % 94);
            return e;
        }
        if (x < u) lo = mid + 1; else hi = mid - 1;
    }
    return odv_ja16_replacement[v];
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

int odv_utf8_to_ja16(const unsigned char *src, int src_len, int cs,
                     char *dst, int dst_size, int *out_len)
{
    int v = variant_of(cs);
    int i = 0, pos = 0, cap;

    if (!src || !dst || dst_size <= 0 || v < 0) return ODV_ERROR_INVALID_ARG;
    cap = dst_size - 1;

    while (i < src_len) {
        unsigned cp = 0;
        uint32_t code;
        int n = utf8_next(src, src_len, i, &cp), nb;
        if (n == 0) break;                            /* incomplete: dropped */
        if (n < 0) { code = '?'; n = 1; }
        else code = encode_char(v, cp);
        nb = code > 0xFFFF ? 3 : (code > 0xFF ? 2 : 1);
        if (pos + nb > cap) break;
        if (nb == 3) dst[pos++] = (char)(code >> 16);
        if (nb >= 2) dst[pos++] = (char)((code >> 8) & 0xFF);
        dst[pos++] = (char)(code & 0xFF);
        i += n;
    }
    dst[pos] = '\0';
    if (out_len) *out_len = pos;
    return ODV_OK;
}
