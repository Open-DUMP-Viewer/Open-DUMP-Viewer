/*****************************************************************************
    OraDB DUMP Viewer

    odv_charset.c
    Character set conversion using Win32 MultiByteToWideChar / WideCharToMultiByte

    Supports: UTF-8, Shift_JIS (CP932), EUC-JP (CP20932), UTF-16LE, US-ASCII

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
    case CHARSET_SJIS:      return 932;    /* CP932 (Shift_JIS) */
    case CHARSET_EUC:       return 20932;  /* EUC-JP */
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
    convert_charset

    Converts string from src_cs encoding to dst_cs encoding.
    Goes through UTF-16 as intermediate (Windows API).

    Returns ODV_OK on success.
    *out_len receives the number of bytes written (excluding NUL).
 ---------------------------------------------------------------------------*/
int convert_charset(const char *src, int src_len, int src_cs,
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
