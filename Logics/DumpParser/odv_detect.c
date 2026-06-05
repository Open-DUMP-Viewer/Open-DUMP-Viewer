/*****************************************************************************
    Open DUMP Viewer for Oracle database

    odv_detect.c
    Dump file format detection (EXP / EXPDP / compressed EXPDP)

    Copyright (C) 2026 YANAI Taketo
 *****************************************************************************/

#include "odv_types.h"

/*---------------------------------------------------------------------------
    Constants for detection
 ---------------------------------------------------------------------------*/
#define CHECK_HEADER_LEN  1280   /* Bytes to read for initial check */
#define EXPORT_SIGNATURE  "EXPORT:"
#define DETECT_SCAN_BUF   1048576  /* 1MB scan buffer (multiple of block len) */

/*---------------------------------------------------------------------------
    detect_dump_kind
    Determines whether a dump file is EXP or EXPDP format.

    EXP detection:
      - buffer[0] is in range 0x01-0x05
      - "EXPORT:" string found at offset 0x03-0x1f

    EXPDP detection:
      - "xml version" found in file
      - Schema name extracted from offset ~0x43
      - Charset extracted from offset ~0x127

    EXPDP compressed:
      - "KGC" + "HDR" markers found
 ---------------------------------------------------------------------------*/

/* Search for a byte pattern in a buffer */
static int find_bytes(const unsigned char *buf, int buf_len,
                      const char *pattern, int pat_len)
{
    int i;
    if (pat_len <= 0 || pat_len > buf_len) return -1;
    for (i = 0; i <= buf_len - pat_len; i++) {
        if (memcmp(buf + i, pattern, pat_len) == 0)
            return i;
    }
    return -1;
}

/* Map NLS charset name to internal constant */
static int get_charset_from_name(const char *name)
{
    if (!name || name[0] == '\0') return CHARSET_UTF8;

    if (strstr(name, "UTF8") || strstr(name, "AL32UTF8"))
        return CHARSET_UTF8;
    if (strstr(name, "JA16SJIS") || strstr(name, "SJIS"))
        return CHARSET_SJIS;
    if (strstr(name, "JA16EUC") || strstr(name, "EUC"))
        return CHARSET_EUC;
    if (strstr(name, "AL16UTF16"))
        return CHARSET_UTF16LE;
    if (strstr(name, "US7ASCII"))
        return CHARSET_US7;
    /* WE8MSWIN1252 must be checked before generic WE8 patterns */
    if (strstr(name, "WE8MSWIN1252"))
        return CHARSET_WIN1252;
    if (strstr(name, "WE8ISO8859P15"))
        return CHARSET_ISO8859P15;
    if (strstr(name, "WE8ISO"))
        return CHARSET_US8;  /* WE8ISO8859P1 = ISO-8859-1 */
    if (strstr(name, "WE8MSWIN"))
        return CHARSET_US8;  /* Other WE8MSWIN → fallback to ISO-8859-1 */
    if (strstr(name, "ZHS16GBK") || strstr(name, "ZHS16CGB"))
        return CHARSET_GBK;

    return CHARSET_UTF8; /* Default fallback */
}

/* Extract null-terminated string from buffer at given offset range */
static int extract_string(const unsigned char *buf, int start, int end,
                          char *out, int out_len)
{
    int i, len = 0;
    for (i = start; i < end && buf[i] != 0; i++) {
        if (len < out_len - 1)
            out[len++] = (char)buf[i];
    }
    out[len] = '\0';
    return len;
}

int detect_dump_kind(ODV_SESSION *s)
{
    FILE *fp;
    unsigned char header[ODV_DUMP_BLOCK_LEN];
    int n, found_xml, found_kgc;
    char schema_buf[ODV_OBJNAME_LEN + 1];
    char charset_buf[64];

    if (!s || s->dump_path[0] == '\0') return ODV_ERROR_INVALID_ARG;

    fp = fopen(s->dump_path, "rb");
    if (!fp) {
        snprintf(s->last_error, ODV_MSG_LEN, "Cannot open file: %s", s->dump_path);
        return ODV_ERROR_FOPEN;
    }

    /* Read first block */
    n = (int)fread(header, 1, ODV_DUMP_BLOCK_LEN, fp);
    if (n < CHECK_HEADER_LEN) {
        fclose(fp);
        snprintf(s->last_error, ODV_MSG_LEN, "File too small: %d bytes", n);
        return ODV_ERROR_FORMAT;
    }

    /*--- EXP format detection ---*/
    if (header[0] >= 0x01 && header[0] <= 0x05) {
        int pos_exp = find_bytes(header, ODV_MIN(n, 0x20),
                                 EXPORT_SIGNATURE, 7);
        if (pos_exp >= 0) {
            s->dump_type = DUMP_EXP;

            /* Detect charset from header byte (low 8 bits of NLS charset ID) */
            if (n > 0x10) {
                unsigned char cs_byte = header[0x05];
                if (cs_byte >= 0x30 && cs_byte <= 0x3f)
                    s->dump_charset = CHARSET_EUC;
                else if (cs_byte >= 0x40 && cs_byte <= 0x4f)
                    s->dump_charset = CHARSET_SJIS;
                else if (cs_byte >= 0x50 && cs_byte <= 0x5f)
                    s->dump_charset = CHARSET_GBK;     /* ZHS16GBK / CGB231280 / GB18030 */
                else if (cs_byte >= 0x60 && cs_byte <= 0x6f)
                    s->dump_charset = CHARSET_UTF8;
                else if (cs_byte >= 0xd0 && cs_byte <= 0xdf)
                    s->dump_charset = CHARSET_UTF16LE;
                else
                    s->dump_charset = CHARSET_US8;
            }

            fclose(fp);
            return ODV_OK;
        }
    }

    /*--- EXPDP format detection ---*/
    /* Extract schema from header area (~0x43) */
    memset(schema_buf, 0, sizeof(schema_buf));
    extract_string(header, 0x43, 0x200, schema_buf, ODV_OBJNAME_LEN);

    /* Extract charset from header area (~0x127) */
    memset(charset_buf, 0, sizeof(charset_buf));
    if (n > 0x200) {
        extract_string(header, 0x127, 0x200, charset_buf, sizeof(charset_buf) - 1);
        if (charset_buf[0] == '\0') {
            /* Try alternative offset for Oracle 19c+ */
            extract_string(header, 0x2a2, ODV_MIN(n, 0x400),
                          charset_buf, sizeof(charset_buf) - 1);
        }
    }

    /*
     * EXPDP format classification.
     *
     * NOTE: Oracle DataPump files always use KGC block framing regardless of
     * the COMPRESSION setting.  KGC presence alone does NOT mean the row data
     * is compressed.  The real differentiator is whether readable XML metadata
     * (the STRMTABLE_T table-definition streams) exists anywhere in the file:
     *   XML found  → DUMP_EXPDP            (parseable: NONE or METADATA_ONLY)
     *   KGC, no XML → DUMP_EXPDP_COMPRESS  (row data compressed: COMPRESSION=ALL)
     *
     * IMPORTANT: expdp's DEFAULT is COMPRESSION=METADATA_ONLY.  That compresses
     * the human-readable DDL (CREATE TABLE text) into KGC blocks but leaves the
     * internal table-definition streams (<?xml ... STRMTABLE_T>) and the row
     * data UNCOMPRESSED — such dumps are fully parseable.  In large schema
     * exports the leading compressed-DDL region can be several MB, pushing the
     * first readable <?xml far past the start of the file.  The probe therefore
     * scans to EOF (with an early exit on the first <?xml) instead of giving up
     * after a fixed window, so default-compression dumps are not misclassified
     * as fully-compressed (COMPRESSION=ALL).  See issue #31.
     */
    found_xml = 0;
    found_kgc = 0;

    /* Check first block. */
    if (n >= 8 && memcmp(header, "KGC", 3) == 0
               && memcmp(header + 5, "HDR", 3) == 0)
        found_kgc = 1;
    if (find_bytes(header, n, "<?xml", 5) >= 0)
        found_xml = 1;

    /* Scan the remainder of the file until XML is found or EOF is reached.
     * <?xml always begins at (block boundary + 2); because the scan buffer is a
     * multiple of ODV_DUMP_BLOCK_LEN and reads are block-aligned, the marker can
     * never straddle a buffer boundary, so a simple per-buffer search is safe. */
    if (!found_xml) {
        unsigned char *scan = (unsigned char *)malloc(DETECT_SCAN_BUF);
        if (scan) {
            odv_fseek(fp, ODV_DUMP_BLOCK_LEN, SEEK_SET);
            for (;;) {
                int m = (int)fread(scan, 1, DETECT_SCAN_BUF, fp);
                if (m < 8) break;

                if (!found_kgc && memcmp(scan, "KGC", 3) == 0
                              && memcmp(scan + 5, "HDR", 3) == 0)
                    found_kgc = 1;

                if (find_bytes(scan, m, "<?xml", 5) >= 0) {
                    found_xml = 1;
                    break;
                }

                if (m < DETECT_SCAN_BUF) break;   /* short read = EOF */
            }
            free(scan);
        }
    }

    fclose(fp);

    /* XML takes priority: a parseable DataPump dump (COMPRESSION=NONE or the
     * default METADATA_ONLY) has KGC framing AND readable XML metadata.
     * Only classify as EXPDP_COMPRESS when KGC framing is present but no XML
     * can be found anywhere (indicating the row data itself is compressed,
     * i.e. COMPRESSION=ALL). */
    if (found_xml) {
        s->dump_type = DUMP_EXPDP;
        s->dump_charset = get_charset_from_name(charset_buf);
        if (schema_buf[0])
            odv_strcpy(s->table.schema, schema_buf, ODV_OBJNAME_LEN);
        return ODV_OK;
    }

    if (found_kgc) {
        s->dump_type = DUMP_EXPDP_COMPRESS;
        s->dump_charset = get_charset_from_name(charset_buf);
        if (schema_buf[0])
            odv_strcpy(s->table.schema, schema_buf, ODV_OBJNAME_LEN);
        return ODV_OK;
    }

    s->dump_type = DUMP_UNKNOWN;
    snprintf(s->last_error, ODV_MSG_LEN, "Unrecognized dump format");
    return ODV_ERROR_FORMAT;
}
