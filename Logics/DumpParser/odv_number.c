/*****************************************************************************
    Open DUMP Viewer for Oracle database

    odv_number.c
    Oracle NUMBER binary format decoding (and ROWID / UROWID notation)

    Oracle NUMBER is a variable-length format (1-22 bytes):
      Byte 0: Exponent byte (determines sign and magnitude)
      Bytes 1..N: Base-100 digit pairs (mantissa)

    Positive: exponent >= 0xC0, digits = (byte - 1), range 0-99
    Negative: exponent <  0x3F, digits = (101 - byte), terminator = 0x66
    Zero:     single byte 0x80

    Copyright (C) 2026 YANAI Taketo
 *****************************************************************************/

#include "odv_types.h"
#include <stdio.h>

/*---------------------------------------------------------------------------
    decode_oracle_number

    Decodes Oracle NUMBER binary format to decimal string.

    buf      : raw NUMBER bytes
    len      : number of bytes
    out      : output buffer for decimal string
    out_size : size of output buffer

    Returns ODV_OK on success.
 ---------------------------------------------------------------------------*/
int decode_oracle_number(const unsigned char *buf, int len, char *out, int out_size)
{
    int exp_byte;
    int num_int_pairs;  /* number of base-100 pairs in integer part */
    int is_negative;
    int digit;
    int i, pos;
    char int_buf[256];  /* integer part digits (exponent can imply up to 63 pairs = 126 chars) */
    int int_len;
    char frac_buf[256]; /* fractional part digits */
    int frac_len;
    int leading_frac_zeros; /* pairs of "00" before fractional digits */

    if (!buf || !out || out_size < 2) return ODV_ERROR_INVALID_ARG;
    if (len < 1) { out[0] = '\0'; return ODV_ERROR_INVALID_ARG; }

    /* Oracle NUMBER is at most 22 bytes. Cap to prevent buffer overflow
       in int_buf[64]/frac_buf[64] which hold base-100 digit pairs. */
    if (len > 22) len = 22;

    out[0] = '\0';
    exp_byte = buf[0];

    /*-----------------------------------------------------------------------
        Zero
     -----------------------------------------------------------------------*/
    if (exp_byte == 0x80 || len == 1) {
        if (out_size < 2) return ODV_ERROR_BUFFER_OVER;
        out[0] = '0';
        out[1] = '\0';
        return ODV_OK;
    }

    /*-----------------------------------------------------------------------
        Oracle 12c+ extended precision: exponent 0xFF signals 39+ digit
        precision with continuation encoding in bytes 1-2.
        Seen in direct-path exports with extended NUMBER storage.
     -----------------------------------------------------------------------*/
    if (exp_byte == 0xFF && len >= 3 && buf[1] == 0xFE) {
        /* Continuation marker: re-interpret byte 2 as actual exponent,
           remaining bytes as standard mantissa */
        exp_byte = buf[2];
        buf += 2;
        len -= 2;
        if (len < 2) {
            out[0] = '0';
            out[1] = '\0';
            return ODV_OK;
        }
    }

    int_len = 0;
    frac_len = 0;
    leading_frac_zeros = 0;

    /*-----------------------------------------------------------------------
        Positive number (exponent >= 0xC0)
     -----------------------------------------------------------------------*/
    if (exp_byte >= 0xC0) {
        is_negative = 0;
        num_int_pairs = exp_byte - 0xC0;

        for (i = 1; i < len; i++) {
            digit = buf[i] - 1;
            if (digit < 0) digit = 0;
            if (digit > 99) digit = 99;

            if (i <= num_int_pairs) {
                /* Integer part */
                if (int_len == 0 && digit == 0) {
                    if (int_len + 2 <= (int)sizeof(int_buf)) {
                        int_buf[int_len++] = '0';
                        int_buf[int_len++] = '0';
                    }
                } else {
                    if (int_len == 0 && digit < 10) {
                        if (int_len + 1 <= (int)sizeof(int_buf))
                            int_buf[int_len++] = '0' + digit;
                    } else {
                        if (int_len + 2 <= (int)sizeof(int_buf)) {
                            int_buf[int_len++] = '0' + (digit / 10);
                            int_buf[int_len++] = '0' + (digit % 10);
                        }
                    }
                }
            } else {
                /* Fractional part */
                if (frac_len + 2 <= (int)sizeof(frac_buf)) {
                    frac_buf[frac_len++] = '0' + (digit / 10);
                    frac_buf[frac_len++] = '0' + (digit % 10);
                }
            }
        }

        /* Pad missing integer pairs with "00"
           Oracle omits trailing zero base-100 pairs from the mantissa.
           e.g. 2700 is stored as exp=0xC2 (2 pairs) with only [28] (=27),
           the second pair (00) is not stored and must be inferred. */
        {
            int pairs_seen = (len - 1 < num_int_pairs) ? len - 1 : num_int_pairs;
            int missing_pairs = num_int_pairs - pairs_seen;
            for (i = 0; i < missing_pairs && int_len + 2 <= (int)sizeof(int_buf); i++) {
                int_buf[int_len++] = '0';
                int_buf[int_len++] = '0';
            }
        }

        /* If no integer digits produced, it's 0 */
        if (int_len == 0 || num_int_pairs == 0) {
            int_len = 0;
            int_buf[int_len++] = '0';
            /* All mantissa bytes are fractional */
            frac_len = 0;
            leading_frac_zeros = 0xC0 - exp_byte;
            for (i = 1; i < len; i++) {
                digit = buf[i] - 1;
                if (digit < 0) digit = 0;
                if (digit > 99) digit = 99;
                if (frac_len + 2 <= (int)sizeof(frac_buf)) {
                    frac_buf[frac_len++] = '0' + (digit / 10);
                    frac_buf[frac_len++] = '0' + (digit % 10);
                }
            }
        }

    /*-----------------------------------------------------------------------
        Positive small decimal (0xAE <= exp < 0xC0)
        Integer part = "0", fractional part has leading zero pairs
     -----------------------------------------------------------------------*/
    } else if (exp_byte >= 0x80) {
        is_negative = 0;
        int_buf[0] = '0';
        int_len = 1;
        leading_frac_zeros = 0xC0 - exp_byte;
        for (i = 1; i < len; i++) {
            digit = buf[i] - 1;
            if (digit < 0) digit = 0;
            if (digit > 99) digit = 99;
            if (frac_len + 2 <= (int)sizeof(frac_buf)) {
                frac_buf[frac_len++] = '0' + (digit / 10);
                frac_buf[frac_len++] = '0' + (digit % 10);
            }
        }

    /*-----------------------------------------------------------------------
        Negative number (exponent <= 0x3F)
        Digits are complementary: value = (101 - byte)
        Terminator: 0x66 (102)
     -----------------------------------------------------------------------*/
    } else if (exp_byte < 0x40) {
        is_negative = 1;
        num_int_pairs = 0x3F - exp_byte;

        for (i = 1; i < len; i++) {
            if (buf[i] == 0x66) break;  /* terminator */

            digit = 101 - buf[i];
            if (digit < 0) digit = 0;
            if (digit > 99) digit = 99;

            if (i <= num_int_pairs) {
                /* Integer part */
                if (int_len == 0 && digit < 10) {
                    if (int_len + 1 <= (int)sizeof(int_buf))
                        int_buf[int_len++] = '0' + digit;
                } else {
                    if (int_len + 2 <= (int)sizeof(int_buf)) {
                        int_buf[int_len++] = '0' + (digit / 10);
                        int_buf[int_len++] = '0' + (digit % 10);
                    }
                }
            } else {
                /* Fractional part */
                if (frac_len + 2 <= (int)sizeof(frac_buf)) {
                    frac_buf[frac_len++] = '0' + (digit / 10);
                    frac_buf[frac_len++] = '0' + (digit % 10);
                }
            }
        }

        /* Pad missing integer pairs with "00" (same as positive case) */
        {
            int data_bytes = 0;
            for (i = 1; i < len; i++) {
                if (buf[i] == 0x66) break;
                data_bytes++;
            }
            {
                int pairs_seen = (data_bytes < num_int_pairs) ? data_bytes : num_int_pairs;
                int missing_pairs = num_int_pairs - pairs_seen;
                for (i = 0; i < missing_pairs && int_len + 2 <= (int)sizeof(int_buf); i++) {
                    int_buf[int_len++] = '0';
                    int_buf[int_len++] = '0';
                }
            }
        }

        if (int_len == 0 || num_int_pairs == 0) {
            int_len = 0;
            int_buf[int_len++] = '0';
        }

    /*-----------------------------------------------------------------------
        Negative small decimal (0x40 <= exp < 0x80)
        Integer part = "0", fractional part complementary
     -----------------------------------------------------------------------*/
    } else {
        /* exp_byte in [0x40..0x7F] */
        is_negative = 1;
        int_buf[0] = '0';
        int_len = 1;
        leading_frac_zeros = exp_byte - 0x3F;
        for (i = 1; i < len; i++) {
            if (buf[i] == 0x66) break;  /* terminator */
            digit = 101 - buf[i];
            if (digit < 0) digit = 0;
            if (digit > 99) digit = 99;
            if (frac_len + 2 <= (int)sizeof(frac_buf)) {
                frac_buf[frac_len++] = '0' + (digit / 10);
                frac_buf[frac_len++] = '0' + (digit % 10);
            }
        }
    }

    /*-----------------------------------------------------------------------
        Trim trailing zeros from fractional part
     -----------------------------------------------------------------------*/
    while (frac_len > 0 && frac_buf[frac_len - 1] == '0') {
        frac_len--;
    }

    /*-----------------------------------------------------------------------
        Build output string
     -----------------------------------------------------------------------*/
    pos = 0;

    /* Sign */
    if (is_negative) {
        if (pos < out_size - 1) out[pos++] = '-';
    }

    /* Integer part */
    for (i = 0; i < int_len && pos < out_size - 1; i++) {
        out[pos++] = int_buf[i];
    }

    /* Fractional part */
    if (frac_len > 0 || leading_frac_zeros > 0) {
        int total_frac = leading_frac_zeros * 2 + frac_len;
        /* Only output decimal point if there are meaningful fractional digits */
        if (total_frac > 0) {
            if (pos < out_size - 1) out[pos++] = '.';

            /* Leading zero pairs */
            for (i = 0; i < leading_frac_zeros * 2 && pos < out_size - 1; i++) {
                out[pos++] = '0';
            }

            /* Fractional digits */
            for (i = 0; i < frac_len && pos < out_size - 1; i++) {
                out[pos++] = frac_buf[i];
            }
        }
    }

    out[pos] = '\0';

    /* Normalize negative zero variants: "-0", "-0.0", "-0.00" etc. → "0"
       A negative number whose digits all resolved to zero should not
       carry the sign. Check if the string matches -0[.0*] */
    if (out[0] == '-' && out[1] == '0') {
        int all_zero = 1;
        int k;
        for (k = 2; k < pos; k++) {
            if (out[k] != '.' && out[k] != '0') { all_zero = 0; break; }
        }
        if (all_zero) {
            out[0] = '0';
            out[1] = '\0';
        }
    }

    return ODV_OK;
}

/*---------------------------------------------------------------------------
    ROWID / UROWID

    Oracle shows a ROWID in its own base-64 notation, not as the stored bytes
    (M-35).  The forms that reach the parsers:

      internal ROWID (expdp ROWID column, 10 bytes)
          data object number (4) | file << 22 | block (4) | row (2)
      physical UROWID (exp / expdp UROWID column, 13 bytes)
          0x01 | data object number (4) | file (2) | block (4) | row (2)
      logical UROWID (row of an index-organized table, variable length)
          0x02 | key bytes ...
      exp ROWID column (external form)
          5-byte header whose last byte is the total length, then the ROWID
          text exactly as Oracle prints it

    Notation (what SELECT ROWID returns):
      extended   OOOOOOFFFBBBBBBRRR  object 6, file 3, block 6, row 3 base-64
                 digits (A-Z a-z 0-9 + /, most significant first)
      restricted BBBBBBBB.RRRR.FFFF  hexadecimal block.row.file, used when the
                 data object number is 0 (an Oracle 7 style ROWID)
      logical    '*' and the base-64 encoding of the bytes after the 0x02
                 type byte, without '=' padding
 ---------------------------------------------------------------------------*/
static const char ROWID_B64[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/* Write the low ndigits*6 bits of v as ndigits base-64 digits. */
static void rowid_b64_digits(unsigned long v, int ndigits, char *out)
{
    int i;
    for (i = ndigits - 1; i >= 0; i--) {
        out[i] = ROWID_B64[v & 0x3F];
        v >>= 6;
    }
}

static unsigned long rowid_be(const unsigned char *p, int n)
{
    unsigned long v = 0;
    int i;
    for (i = 0; i < n; i++) v = (v << 8) | p[i];
    return v;
}

static int format_physical_rowid(unsigned long obj, unsigned long file,
                                 unsigned long block, unsigned long row,
                                 char *out, int out_size)
{
    if (out_size < 19) return ODV_ERROR_BUFFER_OVER;
    if (obj == 0) {
        snprintf(out, (size_t)out_size, "%08lX.%04lX.%04lX",
                 block & 0xFFFFFFFFUL, row & 0xFFFFUL, file & 0xFFFFUL);
        return ODV_OK;
    }
    /* obj is 32 bits in 6 digits (36 bits); the file (10 bits) / block
       (22 bits) / row (16 bits) fields fit their 3 / 6 / 3 digits. */
    rowid_b64_digits(obj   & 0xFFFFFFFFUL, 6, out);
    rowid_b64_digits(file  & 0x3FFFFUL,    3, out + 6);
    rowid_b64_digits(block & 0xFFFFFFFFUL, 6, out + 9);
    rowid_b64_digits(row   & 0x3FFFFUL,    3, out + 15);
    out[18] = '\0';
    return ODV_OK;
}

static int is_rowid_text_char(unsigned char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9') || c == '+' || c == '/' || c == '.';
}

/*---------------------------------------------------------------------------
    decode_oracle_rowid

    buf / len  : the column value as stored in the dump
    is_urowid  : 1 for a UROWID column, 0 for a ROWID column
    out        : receives the Oracle notation; 19 + (len * 4 + 2) / 3 bytes
                 always suffice

    Returns ODV_OK, ODV_ERROR_FORMAT when the bytes are none of the forms
    above (the caller then shows them as hexadecimal), or
    ODV_ERROR_BUFFER_OVER.
 ---------------------------------------------------------------------------*/
int decode_oracle_rowid(const unsigned char *buf, int len, int is_urowid,
                        char *out, int out_size)
{
    if (!buf || !out || out_size < 1 || len <= 0) return ODV_ERROR_INVALID_ARG;

    if (!is_urowid) {
        if (len == 10) {
            unsigned long fb = rowid_be(buf + 4, 4);
            return format_physical_rowid(rowid_be(buf, 4), fb >> 22,
                                         fb & 0x3FFFFFUL, rowid_be(buf + 8, 2),
                                         out, out_size);
        }
        /* exp: external form (header ending in the total length, then text) */
        if (len > 5 && buf[4] == (unsigned char)len) {
            int i, n = len - 5;
            for (i = 5; i < len; i++) {
                if (!is_rowid_text_char(buf[i])) return ODV_ERROR_FORMAT;
            }
            if (n + 1 > out_size) return ODV_ERROR_BUFFER_OVER;
            memcpy(out, buf + 5, (size_t)n);
            out[n] = '\0';
            return ODV_OK;
        }
        return ODV_ERROR_FORMAT;
    }

    if (buf[0] == 0x01 && len == 13) {
        return format_physical_rowid(rowid_be(buf + 1, 4), rowid_be(buf + 5, 2),
                                     rowid_be(buf + 7, 4), rowid_be(buf + 11, 2),
                                     out, out_size);
    }
    if (buf[0] == 0x02 && len >= 2) {
        int i = 1, pos = 0;
        int nbytes = len - 1;
        if (2 + (nbytes * 4 + 2) / 3 > out_size) return ODV_ERROR_BUFFER_OVER;
        out[pos++] = '*';
        for (; len - i >= 3; i += 3, pos += 4)
            rowid_b64_digits(rowid_be(buf + i, 3), 4, out + pos);
        if (len - i == 2) {                       /* 16 bits -> 3 digits */
            rowid_b64_digits(rowid_be(buf + i, 2) << 2, 3, out + pos);
            pos += 3;
        } else if (len - i == 1) {                /*  8 bits -> 2 digits */
            rowid_b64_digits((unsigned long)buf[i] << 4, 2, out + pos);
            pos += 2;
        }
        out[pos] = '\0';
        return ODV_OK;
    }
    return ODV_ERROR_FORMAT;
}
