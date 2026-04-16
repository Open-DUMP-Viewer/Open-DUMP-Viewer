/*****************************************************************************
    Open DUMP Viewer for Oracle database

    odv_number.c
    Oracle NUMBER binary format decoding

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
