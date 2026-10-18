/*****************************************************************************
    Open DUMP Viewer for Oracle database

    ja16_selftest.c
    Self-check for the JA16 tables (gen_ja16map.py --check; not part of the DLL).

    usage: ja16_selftest <CS> <map_CS.txt> <rev_CS.txt> [<CS> <map> <rev> ...]

    For every line of Oracle's measured conversions:
      map: "<JA16 hex>=<AL32UTF8 hex>"  -> convert_charset(JA16 -> UTF-8) must give the same bytes
      rev: "<UCS hex>=<JA16 hex>"       -> convert_charset(UTF-8 -> JA16) must give the same bytes
    plus the edge cases measured with query_edge.sql / query_reverse_edge.sql
    (embedded below).  Exit status 0 = all equal.

    Copyright (C) 2026 YANAI Taketo
 *****************************************************************************/

#include "odv_types.h"

typedef struct { const char *cs; const char *in; const char *out; } EDGE;

/* query_edge.sql (JA16 -> AL32UTF8), 11.2 / 21c / 23.26 identical */
static const EDGE EDGE_FWD[] = {
    { "JA16SJIS", "8200", "EFBFBD" },   { "JA16SJIS", "8220", "EFBFBD" },
    { "JA16SJIS", "823F", "EFBFBD" },   { "JA16SJIS", "827F", "EFBFBD" },
    { "JA16SJIS", "82FD", "EFBFBD" },   { "JA16SJIS", "82FE", "EFBFBD" },
    { "JA16SJIS", "82FF", "EFBFBD" },   { "JA16SJIS", "822041", "EFBFBD41" },
    { "JA16SJIS", "82A1", "E38183" },   { "JA16SJIS", "82E0", "E38282" },
    { "JA16SJIS", "8120", "EFBFBD" },   { "JA16SJIS", "FC20", "EFBFBD" },
    { "JA16SJIS", "85A1", "EFBFBDEFBDA1" }, { "JA16SJIS", "8541", "EFBFBD41" },
    { "JA16EUC", "A441", "EFBFBD" },    { "JA16EUC", "A420", "EFBFBD" },
    { "JA16EUC", "A480", "EFBFBD" },    { "JA16EUC", "A4A0", "EFBFBD" },
    { "JA16EUC", "A4FF", "EFBFBD" },    { "JA16EUC", "A4A1", "E38181" },
    { "JA16EUC", "8E41", "EFBFBD" },    { "JA16EUC", "8EE0", "EFBFBD" },
    { "JA16EUC", "8EA0", "EFBFBD" },    { "JA16EUC", "8EFE", "EFBFBD" },
    { "JA16EUC", "8F4141", "EFBFBD" },  { "JA16EUC", "8FA141", "EFBFBD" },
    { "JA16EUC", "8FA1", "" },          { "JA16EUC", "8F41", "" },
    { "JA16EUC", "8FA1FF", "EFBFBD" },  { "JA16EUC", "8EA1A4A2", "EFBDA1E38182" },
    { "JA16EUC", "A4A2", "E38182" },
    /* YEN variants (21c / 23.26 identical; 11.2 XE does not have them) */
    { "JA16SJISYEN", "5C", "C2A5" },   { "JA16SJISYEN", "815C", "E28095" },
    { "JA16SJISYEN", "8200", "EFBFBD" }, { "JA16SJISYEN", "822041", "EFBFBD41" },
    { "JA16SJISYEN", "8541", "EFBFBD41" }, { "JA16SJISYEN", "855C", "EFBFBDC2A5" },
    { "JA16SJISYEN", "5C7E", "C2A57E" }, { "JA16SJISYEN", "82A0", "E38182" },
    { "JA16EUCYEN", "5C", "C2A5" },    { "JA16EUCYEN", "8FA1", "" },
    { "JA16EUCYEN", "8EA1A4A2", "EFBDA1E38182" }, { "JA16EUCYEN", "A1EF", "EFBFA5" },
    { "JA16EUCYEN", "8FA2B7", "EFBD9E" }, { "JA16EUCYEN", "A441", "EFBFBD" },
    { "JA16EUCYEN", "5C7E", "C2A57E" },
};

/* query_reverse_edge.sql (AL32UTF8 -> JA16), 11.2 / 21c / 23.26 identical */
static const EDGE EDGE_REV[] = {
    { "JA16SJIS", "F09F9880", "8148" }, { "JA16SJIS", "EFBFBD", "8148" },
    { "JA16SJIS", "EFBDA1", "A1" },     { "JA16SJIS", "5C7E", "5C7E" },
    { "JA16SJIS", "EE8080", "F040" },   { "JA16SJIS", "41FF42", "413F42" },
    { "JA16SJIS", "E282AC", "8148" },   { "JA16SJIS", "E6B189", "8148" },
    { "JA16EUC", "F09F9880", "A1A9" },  { "JA16EUC", "EFBFBD", "A1A9" },
    { "JA16EUC", "EFBDA1", "8EA1" },    { "JA16EUC", "C3A9", "8FABB1" },
    { "JA16EUC", "E4B882", "8FB0A1" },  { "JA16EUC", "E6B189", "A1A9" },
    { "JA16EUC", "E282AC", "A1A9" },
    { "JA16SJIS", "EFBD9E", "8160" },   { "JA16SJISTILDE", "E3809C", "8160" },
    { "JA16EUC", "EFBD9E", "8FA2B7" },  { "JA16EUCTILDE", "E3809C", "A1C1" },
    { "JA16SJISTILDE", "F09F9880", "8148" }, { "JA16EUCTILDE", "F09F9880", "A1A9" },
    { "JA16EUC", "41FF42", "413F42" },  { "JA16EUC", "41E381", "41" },
    { "JA16SJIS", "41E381", "41" },     { "JA16SJIS", "EDA0BDEDB880", "81488148" },
    /* YEN variants (21c / 23.26 identical) */
    { "JA16SJISYEN", "F09F9880", "8148" }, { "JA16SJISYEN", "EFBFBD", "8148" },
    { "JA16SJISYEN", "C2A5", "5C" },   { "JA16SJISYEN", "5C", "5C" },
    { "JA16SJISYEN", "7E", "7E" },     { "JA16SJISYEN", "E280BE", "8148" },
    { "JA16SJISYEN", "41FF42", "413F42" }, { "JA16SJISYEN", "41E381", "41" },
    { "JA16SJISYEN", "E28496", "FA59" }, { "JA16SJISYEN", "E285A0", "FA4A" },
    { "JA16SJISYEN", "EFBFA2", "81CA" }, { "JA16SJISYEN", "C3A9", "8148" },
    { "JA16SJISYEN", "EFBD9E", "8160" }, { "JA16SJISYEN", "E3809C", "8160" },
    { "JA16EUCYEN", "F09F9880", "A1A9" }, { "JA16EUCYEN", "EFBFBD", "A1A9" },
    { "JA16EUCYEN", "C2A5", "5C" },    { "JA16EUCYEN", "5C", "5C" },
    { "JA16EUCYEN", "7E", "7E" },      { "JA16EUCYEN", "E280BE", "A1A9" },
    { "JA16EUCYEN", "41FF42", "413F42" }, { "JA16EUCYEN", "41E381", "41" },
    { "JA16EUCYEN", "E285A0", "A1A9" }, { "JA16EUCYEN", "C3A9", "8FABB1" },
    { "JA16EUCYEN", "EFBD9E", "8FA2B7" }, { "JA16EUCYEN", "E3809C", "A1C1" },
};
/* Not reproduced on purpose: Oracle's best-fit to a different letter for
   characters outside the charset (SJIS C3A9 'e-acute' -> 65 'e', C2A5 yen -> 5C).
   Such characters never come out of a JA16 decode, so a decoded name always
   re-encodes to its original bytes; they become the replacement character. */

static int cs_id(const char *name)
{
    if (strcmp(name, "JA16SJIS") == 0) return CHARSET_SJIS;
    if (strcmp(name, "JA16SJISTILDE") == 0) return CHARSET_SJIS_TILDE;
    if (strcmp(name, "JA16EUC") == 0) return CHARSET_EUC;
    if (strcmp(name, "JA16EUCTILDE") == 0) return CHARSET_EUC_TILDE;
    if (strcmp(name, "JA16SJISYEN") == 0) return CHARSET_SJIS_YEN;
    if (strcmp(name, "JA16EUCYEN") == 0) return CHARSET_EUC_YEN;
    return -1;
}

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

static void tohex(const unsigned char *b, int n, char *out)
{
    int i;
    for (i = 0; i < n; i++) sprintf(out + i * 2, "%02X", b[i]);
    out[n * 2] = '\0';
}

static int put_utf8(unsigned u, unsigned char *b)
{
    if (u < 0x80) { b[0] = (unsigned char)u; return 1; }
    if (u < 0x800) { b[0] = (unsigned char)(0xC0 | (u >> 6)); b[1] = (unsigned char)(0x80 | (u & 63)); return 2; }
    b[0] = (unsigned char)(0xE0 | (u >> 12));
    b[1] = (unsigned char)(0x80 | ((u >> 6) & 63));
    b[2] = (unsigned char)(0x80 | (u & 63));
    return 3;
}

/* direction 0: JA16 -> UTF-8, 1: UTF-8 -> JA16 */
static void one(int dir, int cs, const unsigned char *in, int in_len, const char *exp_hex,
                const char *what, long *bad)
{
    char out[256], got[600];
    int out_len = -1, rc;
    if (dir == 0)
        rc = convert_charset((const char *)in, in_len, cs, out, (int)sizeof(out), CHARSET_UTF8, &out_len);
    else
        rc = convert_charset((const char *)in, in_len, CHARSET_UTF8, out, (int)sizeof(out), cs, &out_len);
    if (rc != ODV_OK || out_len < 0) out_len = 0;
    tohex((const unsigned char *)out, out_len, got);
    if (rc != ODV_OK || strcmp(got, exp_hex) != 0) {
        if (*bad < 20) printf("  MISMATCH %s %s: rc=%d got=%s expected=%s\n",
                              dir ? "encode" : "decode", what, rc, got, exp_hex);
        (*bad)++;
    }
}

int main(int argc, char **argv)
{
    long total = 0, bad = 0;
    int a;
    size_t i;
    char line[256];

    for (a = 1; a + 2 < argc; a += 3) {
        int cs = cs_id(argv[a]);
        FILE *fp;
        long n_f = 0, n_r = 0;
        if (cs < 0) { printf("unknown charset %s\n", argv[a]); return 2; }
        fp = fopen(argv[a + 1], "r");
        if (!fp) { printf("cannot open %s\n", argv[a + 1]); return 2; }
        while (fgets(line, sizeof(line), fp)) {
            char *eq = strchr(line, '='), *nl;
            unsigned char in[8];
            int n;
            if (!eq) continue;
            *eq = '\0';
            if ((nl = strpbrk(eq + 1, "\r\n")) != NULL) *nl = '\0';
            n = unhex(line, in, (int)sizeof(in));
            one(0, cs, in, n, eq + 1, line, &bad);
            n_f++;
        }
        fclose(fp);
        fp = fopen(argv[a + 2], "r");
        if (!fp) { printf("cannot open %s\n", argv[a + 2]); return 2; }
        while (fgets(line, sizeof(line), fp)) {
            char *eq = strchr(line, '='), *nl;
            unsigned char in[8];
            unsigned u;
            if (!eq) continue;
            *eq = '\0';
            if ((nl = strpbrk(eq + 1, "\r\n")) != NULL) *nl = '\0';
            if (sscanf(line, "%x", &u) != 1) continue;
            one(1, cs, in, put_utf8(u, in), eq + 1, line, &bad);
            n_r++;
        }
        fclose(fp);
        printf("%s: %ld forward + %ld reverse lines\n", argv[a], n_f, n_r);
        total += n_f + n_r;
    }
    for (i = 0; i < sizeof(EDGE_FWD) / sizeof(EDGE_FWD[0]); i++) {
        unsigned char in[16];
        int n = unhex(EDGE_FWD[i].in, in, (int)sizeof(in));
        one(0, cs_id(EDGE_FWD[i].cs), in, n, EDGE_FWD[i].out, EDGE_FWD[i].in, &bad);
        total++;
    }
    for (i = 0; i < sizeof(EDGE_REV) / sizeof(EDGE_REV[0]); i++) {
        unsigned char in[16];
        int n = unhex(EDGE_REV[i].in, in, (int)sizeof(in));
        one(1, cs_id(EDGE_REV[i].cs), in, n, EDGE_REV[i].out, EDGE_REV[i].in, &bad);
        total++;
    }
    printf("ja16_selftest: %ld checks, %ld mismatches\n", total, bad);
    return bad == 0 ? 0 : 1;
}
