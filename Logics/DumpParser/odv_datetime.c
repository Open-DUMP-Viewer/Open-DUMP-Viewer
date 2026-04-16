/*****************************************************************************
    Open DUMP Viewer for Oracle database

    odv_datetime.c
    Oracle DATE / TIMESTAMP / BINARY_FLOAT / BINARY_DOUBLE decoding

    Oracle DATE:  7 bytes
      [0] century (base 100, offset 100)
      [1] year    (base 100, offset 100)
      [2] month   (1-12)
      [3] day     (1-31)
      [4] hour    (1-24, stored as value+1)
      [5] minute  (1-60, stored as value+1)
      [6] second  (1-60, stored as value+1)

    Oracle TIMESTAMP: 7-11 bytes
      [0-6] same as DATE
      [7-10] nanoseconds (big-endian 32-bit)

    BINARY_FLOAT:  4 bytes (Oracle-modified IEEE 754)
    BINARY_DOUBLE: 8 bytes (Oracle-modified IEEE 754)

    Copyright (C) 2026 YANAI Taketo
 *****************************************************************************/

#include "odv_types.h"
#include <stdio.h>
#include <string.h>

/*---------------------------------------------------------------------------
    decode_oracle_date

    Decodes 7-byte Oracle DATE to string.

    fmt: DATE_FMT_SLASH   => "YYYY/MM/DD HH:MI:SS"
         DATE_FMT_COMPACT => "YYYYMMDD"
         DATE_FMT_FULL    => "YYYYMMDDHHMMSS"
 ---------------------------------------------------------------------------*/
/*---------------------------------------------------------------------------
    format_date_custom

    Formats date components using a custom format string.
    Tokens: YYYY, MM, DD, HH24, MI, SS
    All other characters are output literally.
 ---------------------------------------------------------------------------*/
static int format_date_custom(const char *fmt_str, int yyyy, int mm, int dd,
                               int hh, int mi, int ss, char *out, int out_size)
{
    const char *p = fmt_str;
    int pos = 0;

    while (*p && pos < out_size - 1) {
        if (strncmp(p, "YYYY", 4) == 0) {
            pos += snprintf(out + pos, out_size - pos, "%04d", yyyy);
            p += 4;
        } else if (strncmp(p, "MM", 2) == 0) {
            pos += snprintf(out + pos, out_size - pos, "%02d", mm);
            p += 2;
        } else if (strncmp(p, "DD", 2) == 0) {
            pos += snprintf(out + pos, out_size - pos, "%02d", dd);
            p += 2;
        } else if (strncmp(p, "HH24", 4) == 0) {
            pos += snprintf(out + pos, out_size - pos, "%02d", hh);
            p += 4;
        } else if (strncmp(p, "MI", 2) == 0) {
            pos += snprintf(out + pos, out_size - pos, "%02d", mi);
            p += 2;
        } else if (strncmp(p, "SS", 2) == 0) {
            pos += snprintf(out + pos, out_size - pos, "%02d", ss);
            p += 2;
        } else {
            out[pos++] = *p++;
        }
    }
    out[pos] = '\0';
    return ODV_OK;
}

int decode_oracle_date(const unsigned char *buf, int len, char *out, int out_size, int fmt, const char *custom_fmt)
{
    int yyyy, mm, dd, hh, mi, ss;

    if (!buf || !out || out_size < 2) return ODV_ERROR_INVALID_ARG;
    if (len < 7) {
        out[0] = '\0';
        return ODV_ERROR_INVALID_ARG;
    }

    /* Decode year: century and year are both offset by 100 */
    if (buf[0] >= 100) {
        yyyy = ((int)buf[0] - 100) * 100 + ((int)buf[1] - 100);
    } else {
        /* Negative year (BC dates) */
        yyyy = -((100 - (int)buf[0]) * 100 + ((int)buf[1] - 100));
    }

    mm = buf[2];
    dd = buf[3];
    hh = buf[4] - 1;
    mi = buf[5] - 1;
    ss = buf[6] - 1;

    /* Validate */
    if (mm < 1 || mm > 12) mm = 1;
    if (dd < 1 || dd > 31) dd = 1;
    if (hh < 0 || hh > 23) hh = 0;
    if (mi < 0 || mi > 59) mi = 0;
    if (ss < 0 || ss > 59) ss = 0;

    switch (fmt) {
    case DATE_FMT_SLASH:
        if (out_size < 20) return ODV_ERROR_BUFFER_OVER;
        snprintf(out, out_size, "%04d/%02d/%02d %02d:%02d:%02d", yyyy, mm, dd, hh, mi, ss);
        break;
    case DATE_FMT_COMPACT:
        if (out_size < 9) return ODV_ERROR_BUFFER_OVER;
        snprintf(out, out_size, "%04d%02d%02d", yyyy, mm, dd);
        break;
    case DATE_FMT_FULL:
        if (out_size < 15) return ODV_ERROR_BUFFER_OVER;
        snprintf(out, out_size, "%04d%02d%02d%02d%02d%02d", yyyy, mm, dd, hh, mi, ss);
        break;
    case DATE_FMT_CUSTOM:
        if (custom_fmt && custom_fmt[0]) {
            return format_date_custom(custom_fmt, yyyy, mm, dd, hh, mi, ss, out, out_size);
        }
        /* fall through */
    default:
        if (out_size < 20) return ODV_ERROR_BUFFER_OVER;
        snprintf(out, out_size, "%04d/%02d/%02d %02d:%02d:%02d", yyyy, mm, dd, hh, mi, ss);
        break;
    }

    return ODV_OK;
}

/*---------------------------------------------------------------------------
    decode_oracle_timestamp

    Decodes 7-11 byte Oracle TIMESTAMP to string.

    ts_precision: fractional seconds digits (0-9).
      0       => no fractional part (like DATE)
      1-9     => fixed-width fractional digits (Oracle-compliant)
      negative => use Oracle default (6)

    Output: "YYYY/MM/DD HH:MI:SS.FFFFFF" (for precision=6)
 ---------------------------------------------------------------------------*/
int decode_oracle_timestamp(const unsigned char *buf, int len, char *out, int out_size, int fmt, const char *custom_fmt, int ts_precision)
{
    int yyyy, mm, dd, hh, mi, ss;
    unsigned int nano = 0;

    if (!buf || !out || out_size < 2) return ODV_ERROR_INVALID_ARG;
    if (len < 7) {
        out[0] = '\0';
        return ODV_ERROR_INVALID_ARG;
    }

    /* Clamp precision: negative => Oracle default (6), max 9 */
    if (ts_precision < 0) ts_precision = 6;
    if (ts_precision > 9) ts_precision = 9;

    /* Decode date portion (same as DATE) */
    if (buf[0] >= 100) {
        yyyy = ((int)buf[0] - 100) * 100 + ((int)buf[1] - 100);
    } else {
        yyyy = -((100 - (int)buf[0]) * 100 + ((int)buf[1] - 100));
    }

    mm = buf[2];
    dd = buf[3];
    hh = buf[4] - 1;
    mi = buf[5] - 1;
    ss = buf[6] - 1;

    if (mm < 1 || mm > 12) mm = 1;
    if (dd < 1 || dd > 31) dd = 1;
    if (hh < 0 || hh > 23) hh = 0;
    if (mi < 0 || mi > 59) mi = 0;
    if (ss < 0 || ss > 59) ss = 0;

    /* Extract nanoseconds from bytes 7-10 (big-endian 32-bit) */
    if (len >= 11) {
        nano = ((unsigned int)buf[7] << 24)
             | ((unsigned int)buf[8] << 16)
             | ((unsigned int)buf[9] << 8)
             |  (unsigned int)buf[10];
    }

    /* Custom format: delegate to format_date_custom (ignores nanoseconds) */
    if (fmt == DATE_FMT_CUSTOM && custom_fmt && custom_fmt[0]) {
        return format_date_custom(custom_fmt, yyyy, mm, dd, hh, mi, ss, out, out_size);
    }

    /* Format output based on column precision */
    if (ts_precision > 0) {
        char nano_str[16];

        if (out_size < 30) return ODV_ERROR_BUFFER_OVER;

        /* Format nanoseconds and truncate to requested precision */
        snprintf(nano_str, sizeof(nano_str), "%09u", nano);
        nano_str[ts_precision] = '\0';

        switch (fmt) {
        case DATE_FMT_SLASH:
        default:
            snprintf(out, out_size, "%04d/%02d/%02d %02d:%02d:%02d.%s",
                     yyyy, mm, dd, hh, mi, ss, nano_str);
            break;
        case DATE_FMT_COMPACT:
            snprintf(out, out_size, "%04d%02d%02d", yyyy, mm, dd);
            break;
        case DATE_FMT_FULL:
            snprintf(out, out_size, "%04d%02d%02d%02d%02d%02d.%s",
                     yyyy, mm, dd, hh, mi, ss, nano_str);
            break;
        }
    } else {
        /* TIMESTAMP(0): no fractional seconds, same as DATE */
        return decode_oracle_date(buf, 7, out, out_size, fmt, custom_fmt);
    }

    return ODV_OK;
}

/*---------------------------------------------------------------------------
    decode_binary_float

    Decodes 4-byte Oracle BINARY_FLOAT.
    Oracle stores floats in a modified IEEE 754 format:
      - If high bit set (byte[0] >= 0x80): subtract 0x80 from byte[0]
      - If high bit clear (byte[0] < 0x80): XOR all bytes with 0xFF
      - Stored big-endian, needs reversal for little-endian platforms
 ---------------------------------------------------------------------------*/
int decode_binary_float(const unsigned char *buf, char *out, int out_size)
{
    unsigned char tmp[4];
    float fval;

    if (!buf || !out || out_size < 2) return ODV_ERROR_INVALID_ARG;

    /* Copy and transform */
    memcpy(tmp, buf, 4);

    if (tmp[0] >= 0x80) {
        tmp[0] -= 0x80;
    } else {
        tmp[0] ^= 0xFF;
        tmp[1] ^= 0xFF;
        tmp[2] ^= 0xFF;
        tmp[3] ^= 0xFF;
    }

    /* Reverse byte order: big-endian to little-endian */
    {
        unsigned char t;
        t = tmp[0]; tmp[0] = tmp[3]; tmp[3] = t;
        t = tmp[1]; tmp[1] = tmp[2]; tmp[2] = t;
    }

    memcpy(&fval, tmp, sizeof(float));

    /* Format and trim trailing zeros */
    if (out_size < 32) return ODV_ERROR_BUFFER_OVER;

    /* IEEE 754 special values */
    if (fval != fval) {
        /* NaN: use uppercase representation */
        odv_strcpy(out, "NaN", out_size - 1);
    } else if (fval > 3.402823e+38f) {
        odv_strcpy(out, "Inf", out_size - 1);
    } else if (fval < -3.402823e+38f) {
        odv_strcpy(out, "-Inf", out_size - 1);
    } else {
        snprintf(out, out_size, "%.10g", (double)fval);
    }

    return ODV_OK;
}

/*---------------------------------------------------------------------------
    decode_binary_double

    Decodes 8-byte Oracle BINARY_DOUBLE.
    Same transformation as BINARY_FLOAT but for 8 bytes.
 ---------------------------------------------------------------------------*/
int decode_binary_double(const unsigned char *buf, char *out, int out_size)
{
    unsigned char tmp[8];
    double dval;

    if (!buf || !out || out_size < 2) return ODV_ERROR_INVALID_ARG;

    /* Copy and transform */
    memcpy(tmp, buf, 8);

    if (tmp[0] >= 0x80) {
        tmp[0] -= 0x80;
    } else {
        tmp[0] ^= 0xFF;
        tmp[1] ^= 0xFF;
        tmp[2] ^= 0xFF;
        tmp[3] ^= 0xFF;
        tmp[4] ^= 0xFF;
        tmp[5] ^= 0xFF;
        tmp[6] ^= 0xFF;
        tmp[7] ^= 0xFF;
    }

    /* Reverse byte order: big-endian to little-endian */
    {
        unsigned char t;
        t = tmp[0]; tmp[0] = tmp[7]; tmp[7] = t;
        t = tmp[1]; tmp[1] = tmp[6]; tmp[6] = t;
        t = tmp[2]; tmp[2] = tmp[5]; tmp[5] = t;
        t = tmp[3]; tmp[3] = tmp[4]; tmp[4] = t;
    }

    memcpy(&dval, tmp, sizeof(double));

    /* Format and trim trailing zeros */
    if (out_size < 32) return ODV_ERROR_BUFFER_OVER;

    /* IEEE 754 special values */
    if (dval != dval) {
        odv_strcpy(out, "NaN", out_size - 1);
    } else if (dval > 1.7976931348623157e+308) {
        odv_strcpy(out, "Inf", out_size - 1);
    } else if (dval < -1.7976931348623157e+308) {
        odv_strcpy(out, "-Inf", out_size - 1);
    } else {
        snprintf(out, out_size, "%.17g", dval);
    }

    return ODV_OK;
}

/*---------------------------------------------------------------------------
    decode_interval_ym

    Decodes Oracle INTERVAL YEAR TO MONTH binary format (5 bytes).
      Bytes 0-3: Year  (big-endian, excess-0x80000000)
      Byte  4  : Month (excess-60)

    Output: "+YY-MM" or "-YY-MM"
 ---------------------------------------------------------------------------*/
int decode_interval_ym(const unsigned char *buf, int len, char *out, int out_size)
{
    int32_t year;
    int month;
    const char *sign;

    if (!buf || !out || out_size < 16) return ODV_ERROR_INVALID_ARG;
    if (len < 5) { out[0] = '\0'; return ODV_ERROR_FORMAT; }

    year = (int32_t)(((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16) |
                     ((uint32_t)buf[2] << 8) | (uint32_t)buf[3]) - (int32_t)0x80000000;
    month = (int)buf[4] - 60;

    /* Normalize sign: if year is negative, month is also negative */
    if (year < 0 || (year == 0 && month < 0)) {
        sign = "-";
        if (year < 0) year = -year;
        if (month < 0) month = -month;
    } else {
        sign = "+";
    }

    snprintf(out, out_size, "%s%d-%d", sign, year, month);
    return ODV_OK;
}

/*---------------------------------------------------------------------------
    decode_interval_ds

    Decodes Oracle INTERVAL DAY TO SECOND binary format (11 bytes).
      Bytes 0-3: Day          (big-endian, excess-0x80000000)
      Byte  4  : Hour         (excess-60)
      Byte  5  : Minute       (excess-60)
      Byte  6  : Second       (excess-60)
      Bytes 7-10: Frac second (big-endian, excess-0x80000000, in nanoseconds)

    Output: "+DD HH:MI:SS.FFFFFFFFF" or "-DD HH:MI:SS.FFFFFFFFF"
 ---------------------------------------------------------------------------*/
int decode_interval_ds(const unsigned char *buf, int len, char *out, int out_size)
{
    int32_t day;
    int hour, minute, second;
    int32_t frac;
    const char *sign;

    if (!buf || !out || out_size < 40) return ODV_ERROR_INVALID_ARG;
    if (len < 11) { out[0] = '\0'; return ODV_ERROR_FORMAT; }

    day = (int32_t)(((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16) |
                    ((uint32_t)buf[2] << 8) | (uint32_t)buf[3]) - (int32_t)0x80000000;
    hour   = (int)buf[4] - 60;
    minute = (int)buf[5] - 60;
    second = (int)buf[6] - 60;
    frac = (int32_t)(((uint32_t)buf[7] << 24) | ((uint32_t)buf[8] << 16) |
                     ((uint32_t)buf[9] << 8) | (uint32_t)buf[10]) - (int32_t)0x80000000;

    /* Normalize sign */
    if (day < 0 || (day == 0 && (hour < 0 || minute < 0 || second < 0 || frac < 0))) {
        sign = "-";
        if (day < 0) day = -day;
        if (hour < 0) hour = -hour;
        if (minute < 0) minute = -minute;
        if (second < 0) second = -second;
        if (frac < 0) frac = -frac;
    } else {
        sign = "+";
    }

    if (frac > 0) {
        snprintf(out, out_size, "%s%d %02d:%02d:%02d.%09d",
                 sign, day, hour, minute, second, frac);
        /* Trim trailing zeros from fractional part */
        {
            int p = (int)strlen(out);
            while (p > 0 && out[p - 1] == '0') p--;
            if (p > 0 && out[p - 1] == '.') p--;
            out[p] = '\0';
        }
    } else {
        snprintf(out, out_size, "%s%d %02d:%02d:%02d",
                 sign, day, hour, minute, second);
    }

    return ODV_OK;
}
