/*****************************************************************************
    OraDB DUMP Viewer

    odv_expdp.c
    DataPump (EXPDP) format dump file parsing

    Oracle DataPump files consist of:
    1. Fixed header (~1KB) with schema, platform, charset info
    2. Repeating blocks of:
       - XML DDL blocks (<?xml version="1.0"?> ... </ROWSET>)
       - Binary data blocks (table record data)

    The XML DDL defines table structure (columns, types).
    Binary data follows immediately after DDL and contains row records.

    Copyright (C) 2026 YANAI Taketo
 *****************************************************************************/

#include "odv_types.h"
#include <ctype.h>

/*---------------------------------------------------------------------------
    XML tag section IDs for DDL parsing
 ---------------------------------------------------------------------------*/
#define SEC_NONE             0
#define SEC_ROWSET        1000
#define SEC_ROW           1001
#define SEC_NAME          1002
#define SEC_OWNER_NAME    1003
#define SEC_COL_LIST_ITEM 1004
#define SEC_COL_NAME      1005
#define SEC_TYPE_NUM      1006
#define SEC_LENGTH        1007
#define SEC_PRECISION     1008
#define SEC_SCALE         1009
#define SEC_CHARSET       1010
#define SEC_CHARSETID     1011
#define SEC_FLAGS         1012
#define SEC_PROPERTY      1013
#define SEC_NOT_NULL      1014

/*---------------------------------------------------------------------------
    DDL parse context (passed to XML callback)
 ---------------------------------------------------------------------------*/
typedef struct {
    ODV_SESSION *session;
    int  in_col_list;       /* inside <COL_LIST_ITEM> */
    int  col_idx;           /* current column being defined */
    char cur_schema[ODV_OBJNAME_LEN + 1];
    char cur_table[ODV_OBJNAME_LEN + 1];
    int  property;
} DDL_CONTEXT;

/*---------------------------------------------------------------------------
    Map EXPDP TYPE_NUM to internal column type
 ---------------------------------------------------------------------------*/
static int type_num_to_col_type(int type_num, int length, int flags)
{
    switch (type_num) {
    case 1:   return COL_VARCHAR;
    case 2:   return COL_NUMBER;
    case 8:   return COL_LONG;
    case 12:  return COL_DATE;
    case 23:  return (length > 8000 || flags > 0) ? COL_BLOB : COL_RAW;
    case 96:  return COL_CHAR;
    case 112: return COL_CLOB;
    case 113: return COL_BLOB;
    case 180: return COL_TIMESTAMP;
    case 181: return COL_TIMESTAMP_TZ;
    case 231: return COL_TIMESTAMP_LTZ;
    default:  return COL_VARCHAR;  /* fallback */
    }
}

/*---------------------------------------------------------------------------
    XML callback: processes each tag from the EXPDP DDL
 ---------------------------------------------------------------------------*/
static void ddl_xml_callback(const char *tag, const char *value,
                             int depth, void *ctx)
{
    DDL_CONTEXT *dc = (DDL_CONTEXT *)ctx;
    ODV_SESSION *s = dc->session;

    if (!tag || !tag[0]) return;

    /* Opening tags */
    if (value[0] == '\0') {
        if (strcmp(tag, "COL_LIST_ITEM") == 0) {
            dc->in_col_list = 1;
            /* Prepare next column slot */
            if (dc->col_idx < ODV_MAX_COLUMNS) {
                memset(&s->table.columns[dc->col_idx], 0, sizeof(ODV_COLUMN));
            }
        } else if (strcmp(tag, "ROW") == 0) {
            /* Reset for new table definition */
            dc->col_idx = 0;
            dc->in_col_list = 0;
            dc->property = 0;
            dc->cur_schema[0] = '\0';
            dc->cur_table[0] = '\0';
        }
        return;
    }

    /* Closing tags with values */
    if (strcmp(tag, "NAME") == 0 && !dc->in_col_list) {
        odv_strcpy(dc->cur_table, value, ODV_OBJNAME_LEN);
    }
    else if (strcmp(tag, "OWNER_NAME") == 0) {
        odv_strcpy(dc->cur_schema, value, ODV_OBJNAME_LEN);
    }
    else if (strcmp(tag, "PROPERTY") == 0 && !dc->in_col_list) {
        dc->property = atoi(value);
    }
    else if (dc->in_col_list && dc->col_idx < ODV_MAX_COLUMNS) {
        ODV_COLUMN *col = &s->table.columns[dc->col_idx];

        if (strcmp(tag, "COL_NAME") == 0 || strcmp(tag, "NAME") == 0) {
            /* Skip system-generated columns (SYS_NC*****$) */
            if (strncmp(value, "SYS_NC", 6) == 0 &&
                value[strlen(value) - 1] == '$') {
                /* Mark to skip */
                col->type = -1;
            } else {
                odv_strcpy(col->name, value, ODV_OBJNAME_LEN);
            }
        }
        else if (strcmp(tag, "TYPE_NUM") == 0) {
            int tn = atoi(value);
            col->type = type_num_to_col_type(tn, col->length, col->flags);
        }
        else if (strcmp(tag, "LENGTH") == 0) {
            col->length = atoi(value);
        }
        else if (strcmp(tag, "PRECISION_NUM") == 0) {
            col->precision = atoi(value);
        }
        else if (strcmp(tag, "SCALE") == 0) {
            col->scale = atoi(value);
        }
        else if (strcmp(tag, "CHARSET") == 0 || strcmp(tag, "CHARSETID") == 0) {
            col->charset = atoi(value);
        }
        else if (strcmp(tag, "FLAGS") == 0) {
            col->flags = atoi(value);
        }
        else if (strcmp(tag, "PROPERTY") == 0) {
            col->property = atoi(value);
        }
    }

    /* End of column definition */
    if (strcmp(tag, "COL_LIST_ITEM") == 0 && dc->in_col_list) {
        dc->in_col_list = 0;
        if (dc->col_idx < ODV_MAX_COLUMNS) {
            ODV_COLUMN *col = &s->table.columns[dc->col_idx];
            /* Only count non-system columns */
            if (col->type != -1 && col->name[0] != '\0') {
                /* Build type string */
                switch (col->type) {
                case COL_VARCHAR:
                    snprintf(col->type_str, sizeof(col->type_str),
                             "VARCHAR2(%d)", col->length);
                    break;
                case COL_CHAR:
                    snprintf(col->type_str, sizeof(col->type_str),
                             "CHAR(%d)", col->length);
                    break;
                case COL_NUMBER:
                    if (col->precision > 0 && col->scale > 0)
                        snprintf(col->type_str, sizeof(col->type_str),
                                 "NUMBER(%d,%d)", col->precision, col->scale);
                    else if (col->precision > 0)
                        snprintf(col->type_str, sizeof(col->type_str),
                                 "NUMBER(%d)", col->precision);
                    else
                        snprintf(col->type_str, sizeof(col->type_str), "NUMBER");
                    break;
                case COL_DATE:
                    snprintf(col->type_str, sizeof(col->type_str), "DATE");
                    break;
                case COL_TIMESTAMP:
                    snprintf(col->type_str, sizeof(col->type_str), "TIMESTAMP");
                    break;
                case COL_TIMESTAMP_TZ:
                    snprintf(col->type_str, sizeof(col->type_str), "TIMESTAMP WITH TIME ZONE");
                    break;
                case COL_BLOB:
                    snprintf(col->type_str, sizeof(col->type_str), "BLOB");
                    break;
                case COL_CLOB:
                    snprintf(col->type_str, sizeof(col->type_str), "CLOB");
                    break;
                case COL_RAW:
                    snprintf(col->type_str, sizeof(col->type_str), "RAW(%d)", col->length);
                    break;
                default:
                    snprintf(col->type_str, sizeof(col->type_str), "VARCHAR2(%d)", col->length);
                    break;
                }

                /* Count LOB columns */
                if (col->type == COL_BLOB || col->type == COL_CLOB ||
                    col->type == COL_NCLOB || col->type == COL_LONG_RAW) {
                    s->table.lob_col_count++;
                }

                dc->col_idx++;
            }
        }
    }

    /* End of ROW = end of table definition */
    if (strcmp(tag, "ROW") == 0) {
        if (dc->cur_table[0] != '\0') {
            odv_strcpy(s->table.schema, dc->cur_schema, ODV_OBJNAME_LEN);
            odv_strcpy(s->table.name, dc->cur_table, ODV_OBJNAME_LEN);
            s->table.col_count = dc->col_idx;
        }
    }
}

/*---------------------------------------------------------------------------
    Check if a table is the EXPDP dictionary table (system metadata)
    Dictionary tables have columns like: SCN, SEED, OPERATION, etc.
 ---------------------------------------------------------------------------*/
static int is_dictionary_table(ODV_TABLE *t)
{
    int i, match = 0;
    static const char *dict_cols[] = {
        "SCN", "SEED", "OPERATION", "BASE_OBJECT_NAME",
        "BASE_OBJECT_SCHEMA", "COMPLETED_ROWS", "PROCESS_ORDER", NULL
    };

    if (t->col_count < 5) return 0;

    for (i = 0; dict_cols[i]; i++) {
        int j;
        for (j = 0; j < t->col_count; j++) {
            if (strcmp(t->columns[j].name, dict_cols[i]) == 0) {
                match++;
                break;
            }
        }
    }
    return (match >= 5);
}

/*---------------------------------------------------------------------------
    Notify table_callback with current table info
 ---------------------------------------------------------------------------*/
static void convert_name(const char *src, int src_cs, int dst_cs,
                         char *dst, int dst_size)
{
    if (src_cs != dst_cs && src_cs != CHARSET_UNKNOWN) {
        int out_len = 0;
        if (convert_charset(src, (int)strlen(src), src_cs,
                            dst, dst_size, dst_cs, &out_len) == ODV_OK) {
            dst[out_len] = '\0';
            return;
        }
    }
    odv_strcpy(dst, src, dst_size - 1);
}

static void notify_table(ODV_SESSION *s, int64_t row_count)
{
    char conv_schema[ODV_OBJNAME_LEN * 4 + 1];
    char conv_name[ODV_OBJNAME_LEN * 4 + 1];
    char conv_col_names_buf[ODV_MAX_COLUMNS][ODV_OBJNAME_LEN * 4 + 1];

    /* Convert schema/table/column names to output charset */
    convert_name(s->table.schema, s->dump_charset, s->out_charset,
                 conv_schema, sizeof(conv_schema));
    convert_name(s->table.name, s->dump_charset, s->out_charset,
                 conv_name, sizeof(conv_name));

    if (s->table_cb && s->table.name[0] != '\0') {
        const char *col_names[ODV_MAX_COLUMNS];
        const char *col_types[ODV_MAX_COLUMNS];
        int i;

        for (i = 0; i < s->table.col_count && i < ODV_MAX_COLUMNS; i++) {
            convert_name(s->table.columns[i].name, s->dump_charset, s->out_charset,
                         conv_col_names_buf[i], sizeof(conv_col_names_buf[i]));
            col_names[i] = conv_col_names_buf[i];
            col_types[i] = s->table.columns[i].type_str;
        }

        s->table_cb(conv_schema, conv_name,
                     s->table.col_count, col_names, col_types,
                     row_count, s->table.ddl_offset, s->table_ud);
    }

    /* Add to internal table list (store converted names) */
    if (s->table_count < ODV_MAX_TABLES && s->table.name[0] != '\0') {
        ODV_TABLE_ENTRY *e = &s->table_list[s->table_count];
        odv_strcpy(e->schema, conv_schema, ODV_OBJNAME_LEN);
        odv_strcpy(e->name, conv_name, ODV_OBJNAME_LEN);
        e->col_count = s->table.col_count;
        e->row_count = row_count;
        s->table_count++;
    }
}

/*---------------------------------------------------------------------------
    Search for a byte pattern in buffer
 ---------------------------------------------------------------------------*/
static int find_pattern(const unsigned char *buf, int buf_len,
                        const char *pat, int pat_len)
{
    int i;
    for (i = 0; i <= buf_len - pat_len; i++) {
        if (memcmp(buf + i, pat, pat_len) == 0) return i;
    }
    return -1;
}

/*---------------------------------------------------------------------------
    Parse EXPDP binary records for one table

    Record header byte values:
      0x01, 0x04   = normal record
      0x08, 0x09   = LOB record
      0x18, 0x19   = >255 columns record
      0x0c         = single-chunk LOB
      0xff         = end of table data
      0x00         = end of table (when between records)

    Column length encoding:
      0x00         = length 0 (empty)
      0x01-0xfd    = direct length (1-253 bytes)
      0xfe         = 2-byte length follows (LE)
      0xff         = NULL
 ---------------------------------------------------------------------------*/
static int parse_expdp_records(ODV_SESSION *s, FILE *fp, int64_t *address,
                               int list_only)
{
    unsigned char buf[ODV_DUMP_BLOCK_LEN];
    int buf_pos = 0, buf_len = 0;
    int step = 1;       /* 1=expect header, 2=reading columns */
    int data_step = 0;
    int col_idx = 0;
    int col_len = 0;
    int col_remaining = 0;
    int is_over255 = 0;
    int over255_count = 0;
    int record_count = 0;
    int non_lob_cols;
    int rc;
    char decode_buf[ODV_VARCHAR_LEN + 4];

    non_lob_cols = s->table.col_count - s->table.lob_col_count;
    if (non_lob_cols <= 0) non_lob_cols = s->table.col_count;

    /* In LOB extraction mode, validate the target column exists */
    if (s->lob_extract_mode) {
        rc = odv_lob_check_column(s);
        if (rc != ODV_OK) return rc;
    }

    /* Ensure record has enough columns */
    if (s->record.max_columns < s->table.col_count) {
        free_record(&s->record);
        rc = init_record(&s->record, s->table.col_count + 16);
        if (rc != ODV_OK) return rc;
    }

    while (!s->cancelled) {
        /* Refill buffer if needed */
        if (buf_pos >= buf_len) {
            buf_len = (int)fread(buf, 1, ODV_DUMP_BLOCK_LEN, fp);
            if (buf_len <= 0) break;
            buf_pos = 0;
            *address += buf_len;

            /* Report progress on each buffer refill so UI stays responsive */
            odv_report_progress(s, fp);
        }

        unsigned char b = buf[buf_pos++];

        if (step == 1) {
            /* Expecting record header byte */
            switch (b) {
            case 0x00:
                /* End of table data (if between records) */
                return ODV_OK;

            case 0x01: case 0x04:
                /* Normal record */
                is_over255 = 0;
                step = 2;
                data_step = 0;
                col_idx = 0;
                reset_record(&s->record);
                break;

            case 0x08: case 0x09:
                /* LOB record */
                is_over255 = 0;
                step = 2;
                data_step = 0;
                col_idx = 0;
                reset_record(&s->record);
                break;

            case 0x18: case 0x19: case 0x1c: case 0x2c: case 0x3c:
                /* >255 columns record */
                is_over255 = 1;
                over255_count = 0;
                step = 2;
                data_step = 0;
                col_idx = 0;
                reset_record(&s->record);
                break;

            case 0x0c:
                /* Single-chunk LOB */
                is_over255 = 0;
                step = 2;
                data_step = 0;
                col_idx = 0;
                reset_record(&s->record);
                break;

            case 0xff:
                /* End of table data */
                return ODV_OK;

            default:
                /* Unknown header - skip */
                break;
            }

        } else if (step == 2) {
            /* Reading column data */

            if (data_step == 0) {
                /* Read column length marker */
                if (b == 0xff) {
                    /* NULL column */
                    if (col_idx < s->table.col_count) {
                        set_value_null(&s->record.values[col_idx]);
                        s->record.values[col_idx].type = s->table.columns[col_idx].type;
                    }
                    col_idx++;
                } else if (b == 0xfe) {
                    /* 2-byte length follows */
                    data_step = 1;
                    col_len = 0;
                } else if (b == 0x00) {
                    /* Length 0 = empty string */
                    if (col_idx < s->table.col_count) {
                        set_value_string(&s->record.values[col_idx], "", 0);
                        s->record.values[col_idx].type = s->table.columns[col_idx].type;
                    }
                    col_idx++;
                } else {
                    /* Direct length (1-253) */
                    col_len = (int)b;
                    col_remaining = col_len;
                    data_step = 3;  /* read col_len bytes */

                    /* Prepare value buffer */
                    if (col_idx < s->table.col_count) {
                        ensure_value_buf(&s->record.values[col_idx], col_len + 1);
                        s->record.values[col_idx].data_len = 0;
                    }
                }

                /* Check for 255-column boundary filler */
                if (is_over255 && col_idx > 0 && (col_idx % 255) == 0 && data_step == 0) {
                    /* Skip filler byte (already consumed) - handled by next iteration */
                    over255_count++;
                }

                /* Check if record is complete */
                if (data_step == 0 && col_idx >= non_lob_cols) {
                    /* Record complete - deliver */
                    s->record.col_count = col_idx;

                    if (!list_only) {
                        rc = deliver_row(s);
                        if (rc != ODV_OK) return rc;
                        odv_report_progress(s, fp);
                    }

                    record_count++;
                    s->table.record_count++;

                    /* LOB extraction: remaining columns are LOB data */
                    if (s->lob_extract_mode && s->table.lob_col_count > 0 && s->lob_column_index >= 0) {
                        int lob_i;
                        for (lob_i = 0; lob_i < s->table.lob_col_count && !s->cancelled; lob_i++) {
                            /* Read LOB column length */
                            int lb;
                            int lob_len = 0;
                            while (buf_pos >= buf_len) {
                                buf_len = (int)fread(buf, 1, ODV_DUMP_BLOCK_LEN, fp);
                                if (buf_len <= 0) goto END_LOB_PARSE;
                                buf_pos = 0;
                                *address += buf_len;
                            }
                            lb = buf[buf_pos++];
                            if (lb == 0xff) {
                                /* NULL LOB */
                                continue;
                            } else if (lb == 0xfe) {
                                /* 2-byte length */
                                while (buf_pos >= buf_len) {
                                    buf_len = (int)fread(buf, 1, ODV_DUMP_BLOCK_LEN, fp);
                                    if (buf_len <= 0) goto END_LOB_PARSE;
                                    buf_pos = 0; *address += buf_len;
                                }
                                lob_len = buf[buf_pos++];
                                while (buf_pos >= buf_len) {
                                    buf_len = (int)fread(buf, 1, ODV_DUMP_BLOCK_LEN, fp);
                                    if (buf_len <= 0) goto END_LOB_PARSE;
                                    buf_pos = 0; *address += buf_len;
                                }
                                lob_len |= (buf[buf_pos++] << 8);
                            } else if (lb == 0x00) {
                                /* Empty LOB */
                                continue;
                            } else {
                                lob_len = lb;
                            }

                            /* Read lob_len bytes of LOB data */
                            {
                                int lob_read = 0;
                                while (lob_read < lob_len && !s->cancelled) {
                                    int avail = buf_len - buf_pos;
                                    int need = lob_len - lob_read;
                                    int chunk = (avail < need) ? avail : need;
                                    if (chunk > 0) {
                                        rc = odv_lob_accumulate(s, lob_i, buf + buf_pos, chunk);
                                        if (rc != ODV_OK) return rc;
                                        buf_pos += chunk;
                                        lob_read += chunk;
                                    }
                                    if (buf_pos >= buf_len) {
                                        buf_len = (int)fread(buf, 1, ODV_DUMP_BLOCK_LEN, fp);
                                        if (buf_len <= 0) goto END_LOB_PARSE;
                                        buf_pos = 0; *address += buf_len;
                                    }
                                }
                            }
                        }
                        /* Write LOB file and reset buffer */
                        rc = odv_lob_write_file(s);
                        if (rc != ODV_OK) return rc;
                    }
END_LOB_PARSE:
                    step = 1; /* back to expecting header */
                }

            } else if (data_step == 1) {
                /* First byte of 2-byte length */
                col_len = (int)b;
                data_step = 2;

            } else if (data_step == 2) {
                /* Second byte of 2-byte length (little-endian) */
                col_len |= ((int)b << 8);
                col_remaining = col_len;
                data_step = 3;

                if (col_idx < s->table.col_count) {
                    ensure_value_buf(&s->record.values[col_idx], col_len + 1);
                    s->record.values[col_idx].data_len = 0;
                }

            } else if (data_step == 3) {
                /* Reading column data bytes */
                if (col_idx < s->table.col_count) {
                    ODV_VALUE *v = &s->record.values[col_idx];
                    if (v->data && v->data_len < v->buf_size - 1) {
                        v->data[v->data_len++] = b;
                    }
                }
                col_remaining--;

                if (col_remaining <= 0) {
                    /* Column data complete - decode */
                    if (col_idx < s->table.col_count) {
                        ODV_VALUE *v = &s->record.values[col_idx];
                        ODV_COLUMN *col = &s->table.columns[col_idx];
                        v->type = col->type;
                        v->is_null = 0;

                        /* Type-specific decoding */
                        switch (col->type) {
                        case COL_NUMBER:
                        case COL_FLOAT:
                            decode_oracle_number(v->data, v->data_len,
                                                 decode_buf, sizeof(decode_buf));
                            set_value_string(v, decode_buf, (int)strlen(decode_buf));
                            v->type = col->type;
                            break;

                        case COL_DATE:
                            decode_oracle_date(v->data, v->data_len,
                                              decode_buf, sizeof(decode_buf),
                                              s->date_format, s->custom_date_format);
                            set_value_string(v, decode_buf, (int)strlen(decode_buf));
                            v->type = col->type;
                            break;

                        case COL_TIMESTAMP:
                        case COL_TIMESTAMP_TZ:
                        case COL_TIMESTAMP_LTZ:
                            decode_oracle_timestamp(v->data, v->data_len,
                                                    decode_buf, sizeof(decode_buf),
                                                    s->date_format, s->custom_date_format);
                            set_value_string(v, decode_buf, (int)strlen(decode_buf));
                            v->type = col->type;
                            break;

                        case COL_BIN_FLOAT:
                            decode_binary_float(v->data, decode_buf, sizeof(decode_buf));
                            set_value_string(v, decode_buf, (int)strlen(decode_buf));
                            v->type = col->type;
                            break;

                        case COL_BIN_DOUBLE:
                            decode_binary_double(v->data, decode_buf, sizeof(decode_buf));
                            set_value_string(v, decode_buf, (int)strlen(decode_buf));
                            v->type = col->type;
                            break;

                        case COL_RAW:
                        case COL_ROWID: {
                            /* Convert to hex string */
                            int hi;
                            char hex_buf[ODV_VARCHAR_LEN];
                            int hlen = 0;
                            for (hi = 0; hi < v->data_len && hlen < (int)sizeof(hex_buf) - 3; hi++) {
                                snprintf(hex_buf + hlen, 3, "%02X", v->data[hi]);
                                hlen += 2;
                            }
                            hex_buf[hlen] = '\0';
                            set_value_string(v, hex_buf, hlen);
                            v->type = col->type;
                            break;
                        }

                        case COL_CHAR:
                        case COL_VARCHAR:
                        case COL_NCHAR:
                        case COL_NVARCHAR:
                        default:
                            /* String data: ensure null-terminated */
                            if (v->data) v->data[v->data_len] = '\0';
                            /* Charset conversion if needed */
                            if (s->dump_charset != s->out_charset &&
                                s->dump_charset != CHARSET_UNKNOWN) {
                                char conv_buf[ODV_VARCHAR_LEN];
                                int conv_len = 0;
                                if (convert_charset((const char *)v->data, v->data_len,
                                                    s->dump_charset,
                                                    conv_buf, sizeof(conv_buf),
                                                    s->out_charset, &conv_len) == ODV_OK) {
                                    set_value_string(v, conv_buf, conv_len);
                                }
                            }
                            v->type = col->type;
                            break;
                        }
                    }
                    col_idx++;
                    data_step = 0;

                    /* Check for record completion */
                    if (col_idx >= non_lob_cols) {
                        s->record.col_count = col_idx;

                        if (!list_only) {
                            rc = deliver_row(s);
                            if (rc != ODV_OK) return rc;
                            odv_report_progress(s, fp);
                        }

                        record_count++;
                        s->table.record_count++;

                        /* LOB extraction: read remaining LOB columns */
                        if (s->lob_extract_mode && s->table.lob_col_count > 0 && s->lob_column_index >= 0) {
                            int lob_i;
                            for (lob_i = 0; lob_i < s->table.lob_col_count && !s->cancelled; lob_i++) {
                                int lb, lob_len = 0;
                                while (buf_pos >= buf_len) {
                                    buf_len = (int)fread(buf, 1, ODV_DUMP_BLOCK_LEN, fp);
                                    if (buf_len <= 0) goto END_LOB_PARSE2;
                                    buf_pos = 0; *address += buf_len;
                                }
                                lb = buf[buf_pos++];
                                if (lb == 0xff) continue; /* NULL */
                                else if (lb == 0xfe) {
                                    while (buf_pos >= buf_len) { buf_len = (int)fread(buf, 1, ODV_DUMP_BLOCK_LEN, fp); if (buf_len <= 0) goto END_LOB_PARSE2; buf_pos = 0; *address += buf_len; }
                                    lob_len = buf[buf_pos++];
                                    while (buf_pos >= buf_len) { buf_len = (int)fread(buf, 1, ODV_DUMP_BLOCK_LEN, fp); if (buf_len <= 0) goto END_LOB_PARSE2; buf_pos = 0; *address += buf_len; }
                                    lob_len |= (buf[buf_pos++] << 8);
                                } else if (lb == 0x00) continue; /* empty */
                                else lob_len = lb;

                                { int lr = 0;
                                while (lr < lob_len && !s->cancelled) {
                                    int avail = buf_len - buf_pos, need = lob_len - lr, chunk = (avail < need) ? avail : need;
                                    if (chunk > 0) { rc = odv_lob_accumulate(s, lob_i, buf + buf_pos, chunk); if (rc != ODV_OK) return rc; buf_pos += chunk; lr += chunk; }
                                    if (buf_pos >= buf_len) { buf_len = (int)fread(buf, 1, ODV_DUMP_BLOCK_LEN, fp); if (buf_len <= 0) goto END_LOB_PARSE2; buf_pos = 0; *address += buf_len; }
                                } }
                            }
                            rc = odv_lob_write_file(s);
                            if (rc != ODV_OK) return rc;
                        }
END_LOB_PARSE2:
                        step = 1;
                    }
                }
            }
        }
    }

    if (s->cancelled) return ODV_ERROR_CANCELLED;
    return ODV_OK;
}

/*---------------------------------------------------------------------------
    parse_expdp_dump

    Main EXPDP parsing loop:
    1. Open file, read blocks sequentially
    2. Detect XML DDL blocks (<?xml version="1.0"?>)
    3. Parse XML to extract table/column definitions
    4. Parse binary records following DDL
    5. Repeat until end of file

    If list_only=1, only extracts table metadata (no row data).
 ---------------------------------------------------------------------------*/
int parse_expdp_dump(ODV_SESSION *s, int list_only)
{
    FILE *fp;
    unsigned char block[ODV_DUMP_BLOCK_LEN];
    char *ddl_buf = NULL;
    int ddl_len = 0;
    int ddl_alloc = 0;
    int in_ddl = 0;
    int filter_found = 0;   /* 1=filter target table already processed */
    int64_t address = 0;
    int64_t cur_ddl_pos = 0;    /* File position of current XML DDL block */
    int n, rc;

    if (!s) return ODV_ERROR_INVALID_ARG;

    fp = fopen(s->dump_path, "rb");
    if (!fp) {
        snprintf(s->last_error, ODV_MSG_LEN, "Cannot open: %s", s->dump_path);
        return ODV_ERROR_FOPEN;
    }

    /* Set 64KB I/O buffer for improved read throughput */
    setvbuf(fp, NULL, _IOFBF, 65536);

    /* Reset progress tracking */
    s->last_progress_pct = -1;

    /* Allocate DDL accumulation buffer (1MB) */
    ddl_alloc = ODV_DDL_BUF_LEN;
    ddl_buf = (char *)malloc(ddl_alloc);
    if (!ddl_buf) {
        fclose(fp);
        return ODV_ERROR_MALLOC;
    }

    s->table_count = 0;
    s->total_rows = 0;

    /* Fast seek: if seek_offset is set (from previous list_tables),
       jump directly to the target DDL position instead of scanning from top */
    if (s->seek_offset > 0 && s->filter_active) {
        odv_fseek(fp, s->seek_offset, SEEK_SET);
        address = s->seek_offset;
    }

    /* Read blocks sequentially */
    while (!s->cancelled) {
        n = (int)fread(block, 1, ODV_DUMP_BLOCK_LEN, fp);
        if (n <= 0) break;

        /* Report progress during DDL scan so UI stays responsive */
        odv_report_progress(s, fp);

        /* Search for XML marker in this block */
        int xml_pos = find_pattern(block, n, "<?xml", 5);

        if (xml_pos >= 0 && !in_ddl) {
            /* Start of XML DDL block - record file position for caching */
            cur_ddl_pos = odv_ftell(fp) - n + xml_pos;
            in_ddl = 1;
            ddl_len = 0;

            /* Copy from xml_pos to end of block */
            int copy_len = n - xml_pos;
            if (copy_len > ddl_alloc - ddl_len - 1) copy_len = ddl_alloc - ddl_len - 1;
            memcpy(ddl_buf + ddl_len, block + xml_pos, copy_len);
            ddl_len += copy_len;
        } else if (in_ddl) {
            /* Continue accumulating DDL */
            int copy_len = n;
            if (ddl_len + copy_len >= ddl_alloc - 1) {
                /* Grow buffer */
                int new_alloc = ddl_alloc * 2;
                char *new_buf = (char *)realloc(ddl_buf, new_alloc);
                if (!new_buf) { /* Give up on this DDL */ in_ddl = 0; continue; }
                ddl_buf = new_buf;
                ddl_alloc = new_alloc;
            }
            memcpy(ddl_buf + ddl_len, block, copy_len);
            ddl_len += copy_len;
        }

        /* Check if DDL is complete (ends with </ROWSET>) */
        if (in_ddl && ddl_len > 10) {
            ddl_buf[ddl_len] = '\0';
            char *end_tag = strstr(ddl_buf, "</ROWSET>");
            if (end_tag) {
                int end_pos = (int)(end_tag - ddl_buf) + 9; /* +9 for "</ROWSET>" */
                ddl_buf[end_pos] = '\0';

                /* Parse the DDL XML */
                DDL_CONTEXT dc;
                memset(&dc, 0, sizeof(dc));
                dc.session = s;

                /* Reset table for new definition */
                memset(&s->table, 0, sizeof(ODV_TABLE));
                s->table.dump_charset = s->dump_charset;
                s->table.os_charset = s->out_charset;
                invalidate_meta_cache();

                parse_xml_ddl(ddl_buf, end_pos, ddl_xml_callback, &dc);

                /* Record DDL position for fast seek on next parse */
                s->table.ddl_offset = cur_ddl_pos;

                /* Skip dictionary tables */
                if (s->table.name[0] != '\0' && !is_dictionary_table(&s->table)) {
                    /* Calculate file position for record data */
                    int64_t ddl_file_pos = odv_ftell(fp);

                    /* If there was data after </ROWSET> in the block, seek back */
                    int data_offset = end_pos - (ddl_len - n);
                    if (data_offset > 0 && data_offset < n) {
                        /* Seek to start of record data */
                        odv_fseek(fp, ddl_file_pos - n + data_offset, SEEK_SET);
                    }

                    address = odv_ftell(fp);

                    /* Table filter check */
                    if (s->filter_active) {
                        int match = 1;
                        if (s->filter_table[0]) {
                            char ft[ODV_OBJNAME_LEN + 1];
                            int ft_len = 0;
                            odv_strcpy(ft, s->filter_table, ODV_OBJNAME_LEN);
                            ft_len = (int)strlen(ft);
                            /* Reverse-convert filter name: UTF-8 → dump charset */
                            if (s->dump_charset != s->out_charset &&
                                s->dump_charset != CHARSET_UNKNOWN) {
                                char tmp[ODV_OBJNAME_LEN + 1];
                                int tlen = 0;
                                if (convert_charset(ft, ft_len, s->out_charset,
                                                    tmp, ODV_OBJNAME_LEN, s->dump_charset,
                                                    &tlen) == ODV_OK) {
                                    tmp[tlen] = '\0';
                                    odv_strcpy(ft, tmp, ODV_OBJNAME_LEN);
                                }
                            }
                            if (_stricmp(s->table.name, ft) != 0)
                                match = 0;
                        }
                        if (match && s->filter_schema[0]) {
                            char fs[ODV_OBJNAME_LEN + 1];
                            int fs_len = 0;
                            odv_strcpy(fs, s->filter_schema, ODV_OBJNAME_LEN);
                            fs_len = (int)strlen(fs);
                            if (s->dump_charset != s->out_charset &&
                                s->dump_charset != CHARSET_UNKNOWN) {
                                char tmp[ODV_OBJNAME_LEN + 1];
                                int tlen = 0;
                                if (convert_charset(fs, fs_len, s->out_charset,
                                                    tmp, ODV_OBJNAME_LEN, s->dump_charset,
                                                    &tlen) == ODV_OK) {
                                    tmp[tlen] = '\0';
                                    odv_strcpy(fs, tmp, ODV_OBJNAME_LEN);
                                }
                            }
                            if (_stricmp(s->table.schema, fs) != 0)
                                match = 0;
                        }
                        s->pass_flg = match ? 0 : 1;

                        /* Early exit: target table already processed,
                           now a different table appeared → done */
                        if (filter_found && s->pass_flg) {
                            in_ddl = 0;
                            goto expdp_done;
                        }
                        if (match) {
                            filter_found = 1;
                        }
                    }

                    if (list_only && s->filter_active && s->pass_flg) {
                        /* Filtered out in list_only: skip records entirely */
                        notify_table(s, 0);
                    } else if (list_only && !s->filter_active) {
                        /* list_only without filter: count rows */
                        rc = parse_expdp_records(s, fp, &address, list_only);
                        notify_table(s, s->table.record_count);
                        if (rc != ODV_OK && rc != ODV_ERROR_CANCELLED) { /* non-fatal */ }
                    } else if (!s->filter_active || !s->pass_flg) {
                        /* Full parse (no filter or filter matched) */
                        rc = parse_expdp_records(s, fp, &address, list_only);
                        notify_table(s, s->table.record_count);
                        if (rc != ODV_OK && rc != ODV_ERROR_CANCELLED) { /* non-fatal */ }
                    } else {
                        /* Filtered out in full parse: skip records */
                        notify_table(s, 0);
                    }
                }

                in_ddl = 0;
                ddl_len = 0;
            }
        }

        address = odv_ftell(fp);
    }

expdp_done:
    free(ddl_buf);
    fclose(fp);

    if (s->cancelled) return ODV_ERROR_CANCELLED;
    return ODV_OK;
}
