/*****************************************************************************
    Open DUMP Viewer for Oracle database

    odv_charset.c
    Character set conversion

    JA16SJIS / JA16SJISTILDE / JA16SJISYEN / JA16EUC / JA16EUCTILDE /
    JA16EUCYEN <-> UTF-8 use Oracle's own mapping tables (odv_ja16.c) on
    every platform.  Everything else uses Win32 MultiByteToWideChar /
    WideCharToMultiByte (Windows only): GBK (CP936), ISO-8859-1,
    Windows-1252, ISO-8859-15, UTF-16LE/BE, US-ASCII, and other charsets
    that only look like Shift_JIS / EUC-JP (CP932 / CP20932).

    Copyright (C) 2026 YANAI Taketo
 *****************************************************************************/

#include "odv_types.h"

#ifdef WINDOWS
#include <windows.h>
#endif

#ifdef WINDOWS
/*---------------------------------------------------------------------------
    Map internal charset constant to Windows code page
 ---------------------------------------------------------------------------*/
static unsigned int charset_to_codepage(int cs)
{
    switch (cs) {
    case CHARSET_UTF8:      return 65001;  /* CP_UTF8 */
    /* JA16SJIS / JA16EUC and their TILDE / YEN variants never get here (odv_ja16.c) */
    case CHARSET_SJIS:
    case CHARSET_SJIS_TILDE:
    case CHARSET_SJIS_YEN:
    case CHARSET_SJIS_CP932:  return 932;    /* CP932 (Shift_JIS) */
    case CHARSET_EUC:
    case CHARSET_EUC_TILDE:
    case CHARSET_EUC_YEN:
    case CHARSET_EUC_CP20932: return 20932;  /* EUC-JP */
    case CHARSET_US7:       return 20127;  /* US-ASCII */
    case CHARSET_US8:       return 28591;  /* ISO-8859-1 (WE8ISO8859P1) */
    case CHARSET_WIN1252:   return 1252;   /* Windows-1252 (WE8MSWIN1252) */
    case CHARSET_ISO8859P15:return 28605;  /* ISO-8859-15 (WE8ISO8859P15) */
    case CHARSET_GBK:       return 936;    /* GBK (ZHS16GBK) */
    case CHARSET_UTF16LE:   return 1200;   /* UTF-16LE (special) */
    case CHARSET_UTF16BE:   return 1201;   /* UTF-16BE (special) */
    default:                return 65001;  /* Default to UTF-8 */
    }
}
#endif /* WINDOWS */

/*---------------------------------------------------------------------------
    convert_native

    Converts with the Windows code pages (through UTF-16).  On other
    platforms a plain copy (assumes UTF-8).
 ---------------------------------------------------------------------------*/
static int convert_native(const char *src, int src_len, int src_cs,
                          char *dst, int dst_size, int dst_cs, int *out_len)
{
#ifdef WINDOWS
    UINT src_cp, dst_cp;
    int wlen, result;
    wchar_t *wbuf;

    if (!src || !dst || dst_size <= 0) return ODV_ERROR_INVALID_ARG;

    /* Same charset or empty: simple copy */
    if (src_cs == dst_cs || src_len <= 0) {
        int copy_len = (src_len < dst_size - 1) ? src_len : dst_size - 1;
        if (copy_len > 0) memcpy(dst, src, copy_len);
        dst[copy_len] = '\0';
        if (out_len) *out_len = copy_len;
        return ODV_OK;
    }

    src_cp = charset_to_codepage(src_cs);
    dst_cp = charset_to_codepage(dst_cs);

    /* Special: src is UTF-16LE */
    if (src_cs == CHARSET_UTF16LE) {
        result = WideCharToMultiByte(dst_cp, 0,
                    (const wchar_t *)src, src_len / 2,
                    dst, dst_size - 1, NULL, NULL);
        if (result <= 0) { dst[0] = '\0'; if (out_len) *out_len = 0; return ODV_ERROR; }
        dst[result] = '\0';
        if (out_len) *out_len = result;
        return ODV_OK;
    }

    /* Special: dst is UTF-16LE */
    if (dst_cs == CHARSET_UTF16LE) {
        result = MultiByteToWideChar(src_cp, 0, src, src_len,
                    (wchar_t *)dst, (dst_size - 2) / 2);
        if (result <= 0) { dst[0] = dst[1] = '\0'; if (out_len) *out_len = 0; return ODV_ERROR; }
        ((wchar_t *)dst)[result] = L'\0';
        if (out_len) *out_len = result * 2;
        return ODV_OK;
    }

    /* Special: src is UTF-16BE — byte-swap to UTF-16LE, then convert */
    if (src_cs == CHARSET_UTF16BE) {
        int i, wlen_be = src_len / 2;
        wchar_t *wbuf_be;
        if (wlen_be <= 0) { dst[0] = '\0'; if (out_len) *out_len = 0; return ODV_OK; }
        wbuf_be = (wchar_t *)malloc((wlen_be + 1) * sizeof(wchar_t));
        if (!wbuf_be) return ODV_ERROR_MALLOC;
        for (i = 0; i < wlen_be; i++) {
            unsigned char hi = (unsigned char)src[i * 2];
            unsigned char lo = (unsigned char)src[i * 2 + 1];
            wbuf_be[i] = (wchar_t)((hi << 8) | lo);
        }
        wbuf_be[wlen_be] = L'\0';
        result = WideCharToMultiByte(dst_cp, 0, wbuf_be, wlen_be,
                    dst, dst_size - 1, NULL, NULL);
        free(wbuf_be);
        if (result <= 0) { dst[0] = '\0'; if (out_len) *out_len = 0; return ODV_ERROR; }
        dst[result] = '\0';
        if (out_len) *out_len = result;
        return ODV_OK;
    }

    /* General: src -> UTF-16 -> dst */
    wlen = MultiByteToWideChar(src_cp, 0, src, src_len, NULL, 0);
    if (wlen <= 0) {
        int copy_len = (src_len < dst_size - 1) ? src_len : dst_size - 1;
        memcpy(dst, src, copy_len);
        dst[copy_len] = '\0';
        if (out_len) *out_len = copy_len;
        return ODV_OK;
    }

    wbuf = (wchar_t *)malloc((wlen + 1) * sizeof(wchar_t));
    if (!wbuf) return ODV_ERROR_MALLOC;

    MultiByteToWideChar(src_cp, 0, src, src_len, wbuf, wlen);
    wbuf[wlen] = L'\0';

    result = WideCharToMultiByte(dst_cp, 0, wbuf, wlen,
                dst, dst_size - 1, NULL, NULL);
    free(wbuf);

    if (result <= 0) {
        int copy_len = (src_len < dst_size - 1) ? src_len : dst_size - 1;
        memcpy(dst, src, copy_len);
        dst[copy_len] = '\0';
        if (out_len) *out_len = copy_len;
        return ODV_OK;
    }

    dst[result] = '\0';
    if (out_len) *out_len = result;
    return ODV_OK;

#else
    /* Non-Windows: simple copy (assume UTF-8) */
    int copy_len;
    (void)src_cs; (void)dst_cs;
    if (!src || !dst) return ODV_ERROR_INVALID_ARG;
    copy_len = (src_len < dst_size - 1) ? src_len : dst_size - 1;
    memcpy(dst, src, copy_len);
    dst[copy_len] = '\0';
    if (out_len) *out_len = copy_len;
    return ODV_OK;
#endif
}

/* JA16 table charsets: convert through UTF-8 (out_charset is always UTF-8) */
static int convert_ja16(const char *src, int src_len, int src_cs,
                        char *dst, int dst_size, int dst_cs, int *out_len)
{
    char *tmp;
    int tmp_size, tmp_len = 0, rc;

    if (odv_ja16_is_table_charset(src_cs)) {
        if (dst_cs == CHARSET_UTF8)
            return odv_ja16_to_utf8((const unsigned char *)src, src_len, src_cs,
                                    dst, dst_size, out_len);
        tmp_size = src_len * 3 + 1;              /* every JA16 character is in the BMP */
        tmp = (char *)malloc((size_t)tmp_size);
        if (!tmp) return ODV_ERROR_MALLOC;
        rc = odv_ja16_to_utf8((const unsigned char *)src, src_len, src_cs,
                              tmp, tmp_size, &tmp_len);
        if (rc == ODV_OK)
            rc = convert_charset(tmp, tmp_len, CHARSET_UTF8, dst, dst_size, dst_cs, out_len);
        free(tmp);
        return rc;
    }
    /* dst is a JA16 table charset */
    if (src_cs == CHARSET_UTF8)
        return odv_utf8_to_ja16((const unsigned char *)src, src_len, dst_cs,
                                dst, dst_size, out_len);
    tmp_size = src_len * 4 + 4;
    tmp = (char *)malloc((size_t)tmp_size);
    if (!tmp) return ODV_ERROR_MALLOC;
    rc = convert_charset(src, src_len, src_cs, tmp, tmp_size, CHARSET_UTF8, &tmp_len);
    if (rc == ODV_OK)
        rc = odv_utf8_to_ja16((const unsigned char *)tmp, tmp_len, dst_cs,
                              dst, dst_size, out_len);
    free(tmp);
    return rc;
}

/*---------------------------------------------------------------------------
    convert_charset

    Converts string from src_cs encoding to dst_cs encoding.

    Returns ODV_OK on success.
    *out_len receives the number of bytes written (excluding NUL).
 ---------------------------------------------------------------------------*/
int convert_charset(const char *src, int src_len, int src_cs,
                    char *dst, int dst_size, int dst_cs, int *out_len)
{
    if (src && dst && dst_size > 0 && src_len > 0 && src_cs != dst_cs &&
        (odv_ja16_is_table_charset(src_cs) || odv_ja16_is_table_charset(dst_cs)))
        return convert_ja16(src, src_len, src_cs, dst, dst_size, dst_cs, out_len);
    return convert_native(src, src_len, src_cs, dst, dst_size, dst_cs, out_len);
}
