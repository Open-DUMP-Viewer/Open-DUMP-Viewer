/*****************************************************************************
    Open DUMP Viewer for Oracle database

    odv_xml.c
    Lightweight XML parser for EXPDP DDL metadata extraction

    Parses the XML DDL blocks found in Oracle DataPump dump files.
    These blocks contain table/column definitions in <ROWSET> format.

    Copyright (C) 2026 YANAI Taketo
 *****************************************************************************/

#include "odv_types.h"
#include <ctype.h>

/*---------------------------------------------------------------------------
    Constants
 ---------------------------------------------------------------------------*/
#define XML_MAX_TAG_LEN   128
#define XML_MAX_VALUE_LEN 65536

/*---------------------------------------------------------------------------
    xml_unescape

    Replaces the character references in s[0..len) in place (the result is
    never longer) and NUL-terminates it.  Returns the new length.  Numeric
    references are decoded only for ASCII code points: the text is in the
    database character set, so a code point above 0x7F has no single byte
    form to write here; such a reference, and any unknown entity, is kept
    verbatim.
 ---------------------------------------------------------------------------*/
static int xml_unescape(char *s, int len)
{
    int r = 0, w = 0;

    while (r < len) {
        if (s[r] == '&') {
            int e = r + 1;
            while (e < len && e - r <= 10 && s[e] != ';' && s[e] != '&')
                e++;
            if (e < len && s[e] == ';') {
                const char *ent = s + r + 1;
                int elen = e - r - 1;
                int ch = -1;

                if (elen == 3 && memcmp(ent, "amp", 3) == 0)       ch = '&';
                else if (elen == 2 && memcmp(ent, "lt", 2) == 0)   ch = '<';
                else if (elen == 2 && memcmp(ent, "gt", 2) == 0)   ch = '>';
                else if (elen == 4 && memcmp(ent, "apos", 4) == 0) ch = '\'';
                else if (elen == 4 && memcmp(ent, "quot", 4) == 0) ch = '"';
                else if (elen >= 2 && ent[0] == '#') {
                    int hex = (ent[1] == 'x' || ent[1] == 'X');
                    int k = hex ? 2 : 1;
                    long cp = 0;
                    int ok = (k < elen);
                    for (; ok && k < elen; k++) {
                        int c = (unsigned char)ent[k], d;
                        if (c >= '0' && c <= '9')                d = c - '0';
                        else if (hex && c >= 'a' && c <= 'f')    d = c - 'a' + 10;
                        else if (hex && c >= 'A' && c <= 'F')    d = c - 'A' + 10;
                        else { ok = 0; break; }
                        cp = cp * (hex ? 16 : 10) + d;
                        if (cp > 0x7F) { ok = 0; break; }
                    }
                    if (ok && cp >= 1) ch = (int)cp;
                }
                if (ch >= 0) {
                    s[w++] = (char)ch;
                    r = e + 1;
                    continue;
                }
            }
        }
        s[w++] = s[r++];
    }
    s[w] = '\0';
    return w;
}

/*---------------------------------------------------------------------------
    parse_xml_ddl

    Parses XML text byte-by-byte and invokes the callback for each tag.

    For opening tags:  cb(tag, "",    depth, ctx)
    For closing tags:  cb(tag, value, depth, ctx)   where value = text content
    For self-closing:  cb(tag, "",    depth, ctx)

    The closing-tag value is the element's text with the XML character
    references replaced (&amp; &lt; &gt; &apos; &quot; and ASCII &#N; /
    &#xH;), exactly once, and is NOT trimmed: a quoted Oracle name such as
    " lead" or "DUP " keeps its spaces (M-23).  Text that is only whitespace
    (the indentation between container tags) is delivered as "", which is how
    the caller tells an opening tag from a closing container tag.

    The typical EXPDP DDL structure:
      <ROWSET>
        <ROW>
          <NAME>table_name</NAME>
          <OWNER_NAME>schema</OWNER_NAME>
          <COL_LIST_ITEM>
            <COL_NAME>col1</COL_NAME>
            <TYPE_NUM>1</TYPE_NUM>
            <LENGTH>100</LENGTH>
            ...
          </COL_LIST_ITEM>
        </ROW>
      </ROWSET>
 ---------------------------------------------------------------------------*/

int parse_xml_ddl(const char *xml, int xml_len, xml_tag_callback cb, void *ctx)
{
    int i = 0;
    int depth = 0;
    char tag[XML_MAX_TAG_LEN + 1];
    int tag_len;
    int tag_type;
    char *value_buf;
    int value_len;
    int in_value;

    if (!xml || xml_len <= 0 || !cb) return ODV_ERROR_INVALID_ARG;

    value_buf = (char *)malloc(XML_MAX_VALUE_LEN);
    if (!value_buf) return ODV_ERROR_MALLOC;

    value_len = 0;
    in_value = 0;

    while (i < xml_len) {
        if (xml[i] == '<') {
            /* Save accumulated value text */
            if (in_value && value_len > 0) {
                value_buf[value_len] = '\0';
            }

            i++; /* skip '<' */
            tag_len = 0;
            tag_type = 1; /* OPEN */

            /* Closing tag? */
            if (i < xml_len && xml[i] == '/') {
                tag_type = 2; /* CLOSE */
                i++;
            }

            /* Processing instruction <?...?> */
            if (i < xml_len && xml[i] == '?') {
                while (i < xml_len) {
                    if (xml[i] == '?' && i + 1 < xml_len && xml[i + 1] == '>') {
                        i += 2;
                        break;
                    }
                    i++;
                }
                continue;
            }

            /* Comment <!--...--> or DOCTYPE <!...> */
            if (i < xml_len && xml[i] == '!') {
                if (i + 2 < xml_len && xml[i + 1] == '-' && xml[i + 2] == '-') {
                    i += 3;
                    while (i + 2 < xml_len) {
                        if (xml[i] == '-' && xml[i + 1] == '-' && xml[i + 2] == '>') {
                            i += 3;
                            break;
                        }
                        i++;
                    }
                } else {
                    while (i < xml_len && xml[i] != '>') i++;
                    if (i < xml_len) i++;
                }
                continue;
            }

            /* Extract tag name */
            while (i < xml_len && xml[i] != '>' && xml[i] != ' ' &&
                   xml[i] != '/' && xml[i] != '\t' && xml[i] != '\n' &&
                   xml[i] != '\r') {
                if (tag_len < XML_MAX_TAG_LEN)
                    tag[tag_len++] = xml[i];
                i++;
            }
            tag[tag_len] = '\0';

            /* Skip attributes */
            while (i < xml_len && xml[i] != '>' && xml[i] != '/') {
                i++;
            }

            /* Self-closing? */
            if (i < xml_len && xml[i] == '/') {
                tag_type = 3; /* SINGLE */
                i++;
            }

            /* Skip '>' */
            if (i < xml_len && xml[i] == '>') i++;

            /* Dispatch */
            if (tag_type == 1) {
                /* Opening tag */
                value_len = 0;
                in_value = 1;
                depth++;
                cb(tag, "", depth, ctx);

            } else if (tag_type == 2) {
                /* Closing tag: deliver the unescaped, untrimmed value, or ""
                   when it is only whitespace (see the header comment) */
                value_buf[value_len] = '\0';
                {
                    int k, blank = 1;
                    for (k = 0; k < value_len; k++) {
                        if ((unsigned char)value_buf[k] > ' ') { blank = 0; break; }
                    }
                    if (blank) value_buf[0] = '\0';
                    else xml_unescape(value_buf, value_len);
                    cb(tag, value_buf, depth, ctx);
                }
                depth--;
                if (depth < 0) depth = 0;
                value_len = 0;
                in_value = (depth > 0) ? 1 : 0;

            } else {
                /* Self-closing */
                depth++;
                cb(tag, "", depth, ctx);
                depth--;
                if (depth < 0) depth = 0;
            }

        } else {
            /* Accumulate text content */
            if (in_value && value_len < XML_MAX_VALUE_LEN - 1) {
                value_buf[value_len++] = xml[i];
            }
            i++;
        }
    }

    free(value_buf);
    return ODV_OK;
}
