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
    parse_xml_ddl

    Parses XML text byte-by-byte and invokes the callback for each tag.

    For opening tags:  cb(tag, "",    depth, ctx)
    For closing tags:  cb(tag, value, depth, ctx)   where value = text content
    For self-closing:  cb(tag, "",    depth, ctx)

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
                /* Closing tag: deliver trimmed value */
                value_buf[value_len] = '\0';
                {
                    char *vs = value_buf;
                    char *ve = value_buf + value_len;
                    while (vs < ve && ((unsigned char)*vs <= ' ')) vs++;
                    while (ve > vs && ((unsigned char)*(ve - 1) <= ' ')) ve--;
                    *ve = '\0';
                    cb(tag, vs, depth, ctx);
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
