/*****************************************************************************
    Open DUMP Viewer for Oracle database

    sb_selftest.c
    Self-check for the single-byte tables (gen_sbmap.py --check; not part of
    the DLL).

    usage: sb_selftest <sbmap.txt>

    For every single-byte character set in Oracle's measured conversions
    (query_sbmap.sql):
      "<id> <byte>=<AL32UTF8> <back>" -> convert_charset(set -> UTF-8) gives
          the same character, and convert_charset(UTF-8 -> set) gives <back>
          (a result Oracle gave in a non-shortest form is compared as the
          character it stands for, and not converted back)
      the 256 bytes in one call give the 256 characters in order
      "R <id> <byte>" -> U+4E00, U+FFFD and an invalid UTF-8 byte (FF) give
          the replacement <byte>, an incomplete sequence at the end is dropped
      the name and the id lead to the same charset constant
    Exit status 0 = all equal.

    Copyright (C) 2026 YANAI Taketo
 *****************************************************************************/

#include "odv_types.h"

static long g_checked = 0, g_bad = 0;

static int unhex(const char *h, unsigned char *b, int max)
{
    int n = 0;
    while (h[0] && h[1] && n < max) {
        unsigned v;
        if (sscanf(h, "%2x", &v) != 1) return -1;
        b[n++] = (unsigned char)v;
        h += 2;
    }
    return n;
}

static int put_utf8(unsigned u, unsigned char *b)
{
    if (u < 0x80) { b[0] = (unsigned char)u; return 1; }
    if (u < 0x800) { b[0] = (unsigned char)(0xC0 | (u >> 6)); b[1] = (unsigned char)(0x80 | (u & 0x3F)); return 2; }
    b[0] = (unsigned char)(0xE0 | (u >> 12));
    b[1] = (unsigned char)(0x80 | ((u >> 6) & 0x3F));
    b[2] = (unsigned char)(0x80 | (u & 0x3F));
    return 3;
}

/* the character an AL32UTF8 result stands for (also a non-shortest form) */
static unsigned ucs_of_utf8(const unsigned char *b, int n, int *shortest)
{
    unsigned u;
    unsigned char canon[4];
    int i;
    if (n == 1) u = b[0];
    else if (n == 2) u = ((unsigned)(b[0] & 0x1F) << 6) | (b[1] & 0x3F);
    else u = ((unsigned)(b[0] & 0x0F) << 12) | ((unsigned)(b[1] & 0x3F) << 6) | (b[2] & 0x3F);
    *shortest = put_utf8(u, canon) == n;
    for (i = 0; *shortest && i < n; i++)
        if (canon[i] != b[i]) *shortest = 0;
    return u;
}

static void bad(const char *what, int id, int byte, const char *detail)
{
    g_bad++;
    if (g_bad <= 30) fprintf(stderr, "MISMATCH %s: id %d byte %02X %s\n", what, id, byte, detail);
}

int main(int argc, char **argv)
{
    FILE *f;
    char line[256], name[64];
    int id = 0, cs = CHARSET_UNKNOWN, nsets = 0;
    unsigned expect[256];
    int have[256];

    if (argc < 2) { fprintf(stderr, "usage: sb_selftest <sbmap.txt>\n"); return 2; }
    f = fopen(argv[1], "r");
    if (!f) { perror(argv[1]); return 2; }
    memset(have, 0, sizeof(have));
    while (fgets(line, sizeof(line), f)) {
        char dep[16], width[8];
        int lid, byte;
        char res[64], back[64];
        if (sscanf(line, "CS %d %63s %15s %7s", &lid, name, dep, width) == 4) {
            id = lid;
            cs = (strcmp(width, "1") == 0) ? odv_sb_charset_from_id(id) : CHARSET_UNKNOWN;
            if (strcmp(width, "1") == 0) {
                nsets++;
                if (cs == CHARSET_UNKNOWN) bad("not in tables", id, 0, name);
                else if (odv_sb_charset_from_name(name) != cs) bad("name", id, 0, name);
                if (odv_charset_single_byte(id) == CHARSET_UNKNOWN ||
                    !odv_charset_is_single_byte(odv_charset_single_byte(id)))
                    bad("single-byte constant", id, 0, name);
            }
            memset(have, 0, sizeof(have));
            continue;
        }
        if (cs == CHARSET_UNKNOWN) continue;
        if (sscanf(line, "R %d %63s", &lid, res) == 2 && lid == id) {
            unsigned char rb[4], out[16];
            int rn = unhex(res, rb, 4), olen = 0, b;
            static const unsigned char u4e00[] = { 0xE4, 0xB8, 0x80 };
            static const unsigned char fffd[] = { 0xEF, 0xBF, 0xBD };
            static const unsigned char inval[] = { 0x41, 0xFF, 0x42 };
            static const unsigned char trunc[] = { 0x41, 0xC3 };
            unsigned char all[256];
            char allout[256 * 3 + 1];
            /* whole-string conversion */
            for (b = 0; b < 256; b++) all[b] = (unsigned char)b;
            if (convert_charset((const char *)all, 256, cs, allout, sizeof(allout), CHARSET_UTF8, &olen) == ODV_OK) {
                int pos = 0;
                for (b = 0; b < 256 && have[b]; b++) {
                    unsigned char u8[4];
                    int n = put_utf8(expect[b], u8);
                    if (pos + n > olen || memcmp(allout + pos, u8, (size_t)n) != 0) { bad("string", id, b, name); break; }
                    pos += n;
                }
                if (b == 256 && pos != olen) bad("string length", id, 0, name);
            } else bad("string rc", id, 0, name);
            g_checked++;
            if (rn != 1) { bad("replacement line", id, 0, res); continue; }
            convert_charset((const char *)u4e00, 3, CHARSET_UTF8, (char *)out, sizeof(out), cs, &olen);
            if (olen != 1 || out[0] != rb[0]) bad("U+4E00", id, 0, name);
            convert_charset((const char *)fffd, 3, CHARSET_UTF8, (char *)out, sizeof(out), cs, &olen);
            if (olen != 1 || out[0] != rb[0]) bad("U+FFFD", id, 0, name);
            convert_charset((const char *)inval, 3, CHARSET_UTF8, (char *)out, sizeof(out), cs, &olen);
            if (olen != 3 || out[1] != rb[0]) bad("invalid UTF-8", id, 0, name);
            convert_charset((const char *)trunc, 2, CHARSET_UTF8, (char *)out, sizeof(out), cs, &olen);
            if (olen != 1) bad("incomplete UTF-8", id, 0, name);
            g_checked += 4;
            continue;
        }
        if (sscanf(line, "%d %x=%63s %63s", &lid, &byte, res, back) == 4 && lid == id && byte >= 0 && byte < 256) {
            unsigned char in = (unsigned char)byte, rb[8], bb[4], out[16], u8[4];
            int rn = unhex(res, rb, 8), bn = unhex(back, bb, 4), olen = 0, shortest = 0, n;
            unsigned u;
            if (rn < 1 || rn > 3) { bad("result line", id, byte, res); continue; }
            u = ucs_of_utf8(rb, rn, &shortest);
            expect[byte] = u;
            have[byte] = 1;
            n = put_utf8(u, u8);
            convert_charset((const char *)&in, 1, cs, (char *)out, sizeof(out), CHARSET_UTF8, &olen);
            if (olen != n || memcmp(out, u8, (size_t)n) != 0) bad("forward", id, byte, res);
            g_checked++;
            if (shortest) {
                convert_charset((const char *)u8, n, CHARSET_UTF8, (char *)out, sizeof(out), cs, &olen);
                if (bn != 1 || olen != 1 || out[0] != bb[0]) bad("reverse", id, byte, back);
                g_checked++;
            }
        }
    }
    fclose(f);
    printf("sb_selftest: %d single-byte character sets, %ld checks, %ld mismatches\n", nsets, g_checked, g_bad);
    return (g_bad == 0 && nsets > 0) ? 0 : 1;
}
