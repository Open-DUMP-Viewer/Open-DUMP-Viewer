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
    case 11:  return COL_ROWID;
    case 12:  return COL_DATE;
    case 23:  return (length > 8000 || flags > 0) ? COL_BLOB : COL_RAW;
    case 24:  return COL_LONG_RAW;
    case 58:  return COL_LONG_RAW;
    case 69:  return COL_ROWID;
    case 96:  return COL_CHAR;
    case 100: return COL_BIN_FLOAT;
    case 101: return COL_BIN_DOUBLE;
    case 109: return COL_USER_DEFINE;   /* XMLTYPE (object) / user-defined types */
    case 111: return COL_RAW;           /* REF (object ref) */
    case 112: return COL_CLOB;
    case 113: return COL_BLOB;
    case 114: return COL_BFILE;
    case 121: return COL_RAW;           /* ROWID-like (internal) */
    case 122: return COL_RAW;
    case 123: return (length > 8000 || flags > 0) ? COL_BLOB : COL_RAW;
    case 180: return COL_TIMESTAMP;
    case 181: return COL_TIMESTAMP_TZ;
    case 182: return COL_INTERVAL_YM;
    case 183: return COL_INTERVAL_DS;
    case 208: return COL_ROWID;         /* UROWID */
    case 231: return COL_TIMESTAMP_LTZ;
    default:  return COL_VARCHAR;       /* fallback for unknown types */
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

    /* Tags with empty value: could be opening OR closing (whitespace-only
       content is trimmed to ""). Use state to distinguish:
       - COL_LIST_ITEM: if in_col_list==0 → opening; if in_col_list==1 → closing
       - ROW: always fall through to bottom for save/reset logic
       - Others: treat as opening (no-op) */
    if (value[0] == '\0') {
        if (strcmp(tag, "COL_LIST_ITEM") == 0) {
            if (!dc->in_col_list) {
                /* Opening: prepare next column slot */
                dc->in_col_list = 1;
                if (dc->col_idx < ODV_MAX_COLUMNS) {
                    memset(&s->table.columns[dc->col_idx], 0, sizeof(ODV_COLUMN));
                }
                return;
            }
            /* Closing: fall through to column finalization at bottom */
        } else if (strcmp(tag, "ROW") != 0) {
            return;
        }
        /* ROW and COL_LIST_ITEM closing fall through */
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
            /* Skip Oracle system-generated internal columns:
               - SYS_NC*****$ : In-Database Archiving / function-based index columns
               - SYS_IME_*    : JSON binary storage (In-Memory Expression) columns */
            if ((strncmp(value, "SYS_NC", 6) == 0 &&
                 value[strlen(value) - 1] == '$') ||
                strncmp(value, "SYS_IME_", 8) == 0) {
                /* Mark to skip */
                col->type = -1;
            } else {
                odv_strcpy(col->name, value, ODV_OBJNAME_LEN);
            }
        }
        else if (strcmp(tag, "TYPE_NUM") == 0) {
            /* Don't overwrite -1 marker for system-generated columns */
            if (col->type != -1) {
                int tn = atoi(value);
                col->type = type_num_to_col_type(tn, col->length, col->flags);
            }
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
        else if (strcmp(tag, "NOT_NULL") == 0) {
            col->not_null = atoi(value);
        }
    }

    /* End of column definition */
    if (strcmp(tag, "COL_LIST_ITEM") == 0 && dc->in_col_list) {
        dc->in_col_list = 0;
        if (dc->col_idx < ODV_MAX_COLUMNS) {
            ODV_COLUMN *col = &s->table.columns[dc->col_idx];
            /* Only count non-system columns */
            if (col->type != -1 && col->name[0] != '\0') {
                /* CHARSETID 2000 = AL16UTF16 (national charset, UTF-16BE).
                 * Upgrade VARCHAR2→NVARCHAR2 and CHAR→NCHAR accordingly. */
                if (col->charset == 2000) {
                    if (col->type == COL_VARCHAR) col->type = COL_NVARCHAR;
                    else if (col->type == COL_CHAR)    col->type = COL_NCHAR;
                }
                /* Build type string */
                switch (col->type) {
                case COL_NVARCHAR:
                    /* length is in bytes (2 bytes/char in UTF-16) */
                    snprintf(col->type_str, sizeof(col->type_str),
                             "NVARCHAR2(%d)", col->length / 2);
                    break;
                case COL_NCHAR:
                    snprintf(col->type_str, sizeof(col->type_str),
                             "NCHAR(%d)", col->length / 2);
                    break;
                case COL_VARCHAR:
                    snprintf(col->type_str, sizeof(col->type_str),
                             "VARCHAR2(%d)", col->length);
                    break;
                case COL_CHAR:
                    snprintf(col->type_str, sizeof(col->type_str),
                             "CHAR(%d)", col->length);
                    break;
                case COL_NUMBER:
                    if (col->precision > 0 && col->scale != 0)
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
                    if (col->precision <= 0) col->precision = 6; /* Oracle default */
                    snprintf(col->type_str, sizeof(col->type_str),
                             "TIMESTAMP(%d)", col->precision);
                    break;
                case COL_TIMESTAMP_TZ:
                    if (col->precision <= 0) col->precision = 6;
                    snprintf(col->type_str, sizeof(col->type_str),
                             "TIMESTAMP(%d) WITH TIME ZONE", col->precision);
                    break;
                case COL_TIMESTAMP_LTZ:
                    if (col->precision <= 0) col->precision = 6;
                    snprintf(col->type_str, sizeof(col->type_str),
                             "TIMESTAMP(%d) WITH LOCAL TIME ZONE", col->precision);
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
                case COL_INTERVAL_YM:
                    snprintf(col->type_str, sizeof(col->type_str),
                             "INTERVAL YEAR TO MONTH");
                    break;
                case COL_INTERVAL_DS:
                    snprintf(col->type_str, sizeof(col->type_str),
                             "INTERVAL DAY TO SECOND");
                    break;
                case COL_BIN_FLOAT:
                    snprintf(col->type_str, sizeof(col->type_str), "BINARY_FLOAT");
                    break;
                case COL_BIN_DOUBLE:
                    snprintf(col->type_str, sizeof(col->type_str), "BINARY_DOUBLE");
                    break;
                case COL_LONG:
                    snprintf(col->type_str, sizeof(col->type_str), "LONG");
                    break;
                case COL_LONG_RAW:
                    snprintf(col->type_str, sizeof(col->type_str), "LONG RAW");
                    break;
                case COL_BFILE:
                    snprintf(col->type_str, sizeof(col->type_str), "BFILE");
                    break;
                case COL_XMLTYPE:
                    snprintf(col->type_str, sizeof(col->type_str), "XMLTYPE");
                    break;
                case COL_NCLOB:
                    snprintf(col->type_str, sizeof(col->type_str), "NCLOB");
                    break;
                case COL_ROWID:
                    snprintf(col->type_str, sizeof(col->type_str), "ROWID");
                    break;
                case COL_USER_DEFINE:
                    snprintf(col->type_str, sizeof(col->type_str), "USER_DEFINED");
                    break;
                default:
                    snprintf(col->type_str, sizeof(col->type_str), "VARCHAR2(%d)", col->length);
                    break;
                }

                /* Count LOB columns */
                if (col->type == COL_BLOB || col->type == COL_CLOB ||
                    col->type == COL_NCLOB || col->type == COL_LONG_RAW ||
                    col->type == COL_BFILE || col->type == COL_USER_DEFINE) {
                    s->table.lob_col_count++;
                }

                dc->col_idx++;
            }
        }
    }

    /* ROW tag: save accumulated table info, then reset for next table.
       Both <ROW> (opening) and </ROW> (closing with empty value) arrive here.
       On first <ROW>, cur_table is empty so nothing is saved — just reset.
       On </ROW>, accumulated table info is saved before reset. */
    if (strcmp(tag, "ROW") == 0) {
        if (dc->cur_table[0] != '\0') {
            odv_strcpy(s->table.schema, dc->cur_schema, ODV_OBJNAME_LEN);
            odv_strcpy(s->table.name, dc->cur_table, ODV_OBJNAME_LEN);
            s->table.col_count = dc->col_idx;
        }
        dc->col_idx = 0;
        dc->in_col_list = 0;
        dc->property = 0;
        dc->cur_schema[0] = '\0';
        dc->cur_table[0] = '\0';
    }
}

/*---------------------------------------------------------------------------
    Check if a table is an EXPDP system table (should be skipped).
    Returns:
      0 = normal user table
      1 = system table (skip entirely)
      2 = dictionary/master table (parse for partition info, then skip)
 ---------------------------------------------------------------------------*/
static int is_system_table(ODV_TABLE *t, const char *schema)
{
    int i, match;
    static const char *dict_cols[] = {
        "SCN", "SEED", "OPERATION", "BASE_OBJECT_NAME",
        "BASE_OBJECT_SCHEMA", "BASE_OBJECT_TYPE", "PARTITION_NAME",
        "SUBPARTITION_NAME", "COMPLETED_ROWS", "PROCESS_ORDER", NULL
    };

    /* Skip SYS-owned tables */
    if (schema[0] && _stricmp(schema, "SYS") == 0) return 1;

    /* Skip DataPump job tables */
    if (_strnicmp(t->name, "SYS_EXPORT_", 11) == 0) return 1;
    if (_strnicmp(t->name, "SYS_IMPORT_", 11) == 0) return 1;
    if (_strnicmp(t->name, "IMPDP_", 6) == 0) return 1;

    /* Dictionary table check: 10 specific columns */
    if (t->col_count < 10) return 0;

    match = 0;
    for (i = 0; dict_cols[i]; i++) {
        int j;
        for (j = 0; j < t->col_count; j++) {
            if (strcmp(t->columns[j].name, dict_cols[i]) == 0) {
                match++;
                break;
            }
        }
    }
    return (match >= 10);
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
        int col_not_nulls[ODV_MAX_COLUMNS];
        const char *col_defaults[ODV_MAX_COLUMNS];
        int i;

        for (i = 0; i < s->table.col_count && i < ODV_MAX_COLUMNS; i++) {
            convert_name(s->table.columns[i].name, s->dump_charset, s->out_charset,
                         conv_col_names_buf[i], sizeof(conv_col_names_buf[i]));
            col_names[i] = conv_col_names_buf[i];
            col_types[i] = s->table.columns[i].type_str;
            col_not_nulls[i] = s->table.columns[i].not_null;
            col_defaults[i] = s->table.columns[i].default_val;
        }

        s->table_cb(conv_schema, conv_name,
                     s->table.col_count, col_names, col_types,
                     col_not_nulls, col_defaults,
                     0, "[]",
                     row_count, s->table.ddl_offset, s->table_ud);
    }

    /* Add to internal table list (store converted names) */
    if (s->table_count < ODV_MAX_TABLES && s->table.name[0] != '\0') {
        ODV_TABLE_ENTRY *e = &s->table_list[s->table_count];
        odv_strcpy(e->schema, conv_schema, ODV_OBJNAME_LEN);
        odv_strcpy(e->name, conv_name, ODV_OBJNAME_LEN);
        e->partition[0] = '\0';
        e->parent_partition[0] = '\0';
        e->type = TABLE_TYPE_TABLE;
        e->col_count = s->table.col_count;
        e->row_count = row_count;

        /* Detect partitioned tables: if the same schema.table already appeared
           in the table list, this is another partition of that table.
           Mark duplicates as TABLE_TYPE_PARTITION and mark the first occurrence
           retroactively as TABLE_TYPE_PARTITION_TABLE. */
        {
            int k;
            for (k = 0; k < s->table_count; k++) {
                if (strcmp(s->table_list[k].schema, e->schema) == 0 &&
                    strcmp(s->table_list[k].name, e->name) == 0) {
                    /* Found a previous occurrence — this table is partitioned */
                    e->type = TABLE_TYPE_PARTITION;

                    /* Retroactively mark the first occurrence as PARTITION_TABLE
                       (only if it hasn't been marked yet) */
                    if (s->table_list[k].type == TABLE_TYPE_TABLE) {
                        s->table_list[k].type = TABLE_TYPE_PARTITION_TABLE;
                    }
                    break;
                }
            }
        }

        s->table_count++;
    }
}

/*---------------------------------------------------------------------------
    Decode a completed column value (type-specific conversion)

    Called after all bytes of a column have been accumulated in v->data.
    Converts raw Oracle wire format to display string.
 ---------------------------------------------------------------------------*/
static void decode_column_value(ODV_SESSION *s, int col_idx)
{
    ODV_VALUE  *v;
    ODV_COLUMN *col;
    char decode_buf[ODV_VARCHAR_LEN + 4];

    if (col_idx >= s->table.col_count) return;

    v   = &s->record.values[col_idx];
    col = &s->table.columns[col_idx];
    v->type    = col->type;
    v->is_null = 0;

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
                                s->date_format, s->custom_date_format,
                                col->precision);
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

    case COL_INTERVAL_YM:
        decode_interval_ym(v->data, v->data_len,
                           decode_buf, sizeof(decode_buf));
        set_value_string(v, decode_buf, (int)strlen(decode_buf));
        v->type = col->type;
        break;

    case COL_INTERVAL_DS:
        decode_interval_ds(v->data, v->data_len,
                           decode_buf, sizeof(decode_buf));
        set_value_string(v, decode_buf, (int)strlen(decode_buf));
        v->type = col->type;
        break;

    case COL_RAW:
    case COL_ROWID: {
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

    case COL_NCHAR:
    case COL_NVARCHAR: {
        char conv_buf[ODV_VARCHAR_LEN];
        int conv_len = 0;
        if (v->data && v->data_len > 0 &&
            convert_charset((const char *)v->data, v->data_len,
                            CHARSET_UTF16BE,
                            conv_buf, sizeof(conv_buf),
                            CHARSET_UTF8, &conv_len) == ODV_OK) {
            set_value_string(v, conv_buf, conv_len);
        } else if (v->data) {
            v->data[v->data_len] = '\0';
        }
        v->type = col->type;
        break;
    }

    case COL_CHAR:
    case COL_VARCHAR:
    default:
        if (v->data) v->data[v->data_len] = '\0';
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

/*---------------------------------------------------------------------------
    Accumulate LOB preview data for GUI display.

    For BLOB: hex-encode into the column value (max ODV_LOB_PREVIEW_LEN/2 bytes)
    For CLOB: copy text into the column value (max ODV_LOB_PREVIEW_LEN bytes)
    Only accumulates if the LOB column is within the table's column range.
 ---------------------------------------------------------------------------*/
static void accumulate_lob_preview(ODV_SESSION *s, int lob_col_idx,
                                   const unsigned char *data, int len)
{
    int abs_col;    /* Absolute column index in the table */
    int non_lob_cols;
    int i, lob_i;
    ODV_VALUE *v;
    int col_type;

    if (!data || len <= 0) return;

    non_lob_cols = s->table.col_count - s->table.lob_col_count;
    if (non_lob_cols < 0) non_lob_cols = 0;

    /* Find the absolute column index for this LOB column */
    abs_col = -1;
    lob_i = 0;
    for (i = 0; i < s->table.col_count; i++) {
        int t = s->table.columns[i].type;
        if (t == COL_BLOB || t == COL_CLOB || t == COL_NCLOB ||
            t == COL_LONG || t == COL_LONG_RAW) {
            if (lob_i == lob_col_idx) {
                abs_col = i;
                break;
            }
            lob_i++;
        }
    }
    if (abs_col < 0 || abs_col >= s->table.col_count) return;

    col_type = s->table.columns[abs_col].type;
    v = &s->record.values[abs_col];
    v->type = col_type;
    v->is_null = 0;

    if (col_type == COL_BLOB || col_type == COL_LONG_RAW) {
        /* BLOB: hex-encode (each source byte → 2 hex chars) */
        int max_src = ODV_LOB_PREVIEW_LEN / 2;  /* max source bytes */
        int already = v->data_len / 2;           /* source bytes already encoded */
        int avail = max_src - already;
        int to_encode = (len < avail) ? len : avail;
        int hi;

        if (to_encode <= 0) return;
        ensure_value_buf(v, v->data_len + to_encode * 2 + 1);
        if (!v->data) return;

        for (hi = 0; hi < to_encode; hi++) {
            snprintf((char *)v->data + v->data_len, 3, "%02X", data[hi]);
            v->data_len += 2;
        }
        v->data[v->data_len] = '\0';
    } else if (col_type == COL_CLOB || col_type == COL_NCLOB) {
        /* CLOB/NCLOB: Oracle EXPDP stores LOB data in AL16UTF16 (UTF-16BE).
         * Convert to UTF-8 for display. */
        int max_bytes = ODV_LOB_PREVIEW_LEN;
        int avail = max_bytes - v->data_len;
        char conv_buf[ODV_LOB_PREVIEW_LEN + 4];
        int conv_len = 0;
        int src_len = (len < avail * 2) ? len : avail * 2; /* UTF-16 is ~2x */

        if (avail <= 0 || src_len <= 0) return;

        /* Ensure even byte count for UTF-16 */
        src_len &= ~1;
        if (src_len <= 0) return;

        if (convert_charset((const char *)data, src_len,
                            CHARSET_UTF16BE,
                            conv_buf, sizeof(conv_buf),
                            CHARSET_UTF8, &conv_len) == ODV_OK && conv_len > 0) {
            int to_copy = (conv_len < avail) ? conv_len : avail;
            ensure_value_buf(v, v->data_len + to_copy + 1);
            if (!v->data) return;
            memcpy(v->data + v->data_len, conv_buf, to_copy);
            v->data_len += to_copy;
            v->data[v->data_len] = '\0';
        }
    } else {
        /* LONG: copy text directly (stored in DB charset) */
        int max_bytes = ODV_LOB_PREVIEW_LEN;
        int avail = max_bytes - v->data_len;
        int to_copy = (len < avail) ? len : avail;

        if (to_copy <= 0) return;
        ensure_value_buf(v, v->data_len + to_copy + 1);
        if (!v->data) return;

        memcpy(v->data + v->data_len, data, to_copy);
        v->data_len += to_copy;
        v->data[v->data_len] = '\0';
    }
}

/*---------------------------------------------------------------------------
    Parse EXPDP binary records for one table (ARK-style state machine)

    Record header byte values:
      0x01, 0x04   = normal record (for LOB tables: non-LOB columns only)
      0x08, 0x09   = LOB record (multi-chunk LOB data)
      0x18, 0x19   = >255 columns record
      0x0c         = single-chunk LOB record
      0xff         = end of table data
      0x00         = end of table (when between records)

    Column length encoding:
      0x00         = length 0 (empty)
      0x01-0xfd    = direct length (1-253 bytes)
      0xfe         = 2-byte length follows (LE)
      0xff         = NULL

    LOB chunk marker (data_step = DS_LOB_MARKER):
      0x00         = empty/end of LOB column
      0x01-0x06    = continuation marker (filler)
      0x07-0xfd    = direct chunk size (except 0x08/0x09/0x0c)
      0x08/0x09    = new LOB record header (end current LOB)
      0x0c         = new single-chunk LOB header (end current LOB)
      0xfe         = 2-byte chunk length follows
      0xff         = NULL LOB column
 ---------------------------------------------------------------------------*/
static int parse_expdp_records(ODV_SESSION *s, FILE *fp, int64_t *address,
                               int list_only)
{
    ODV_PARSE_STATE *st = &s->state;
    unsigned char b;
    int non_lob_cols;
    int chunk_size = 0;
    int record_count = 0;
    int progress_counter = 0;
    int rc;

    non_lob_cols = s->table.col_count - s->table.lob_col_count;
    if (non_lob_cols <= 0) non_lob_cols = s->table.col_count;

    /* Initialize parse state */
    st->step             = 1;
    st->data_step        = DS_COL_LENGTH;
    st->col_idx          = 0;
    st->lob_col_idx      = 0;
    st->col_len          = 0;
    st->col_remaining    = 0;
    st->record_header    = 0;
    st->is_lob_record    = 0;
    st->is_between_record = 0;
    st->is_last_chunk    = 0;
    st->is_end_lob       = 0;
    st->lob_length       = 0;
    st->lob_preview_len  = 0;
    st->is_over255       = 0;
    st->over255_count    = 0;
    st->filler_length    = 2;
    st->seg_remaining    = -1;

    /* Build non-LOB column → absolute column index mapping.
     * In EXPDP LOB records, column data is packed without LOB columns.
     * For tables with no LOBs, this is a trivial identity mapping. */
    {
        int mi, mc = 0;
        for (mi = 0; mi < s->table.col_count && mc < ODV_MAX_COLUMNS; mi++) {
            int t = s->table.columns[mi].type;
            if (t != COL_BLOB && t != COL_CLOB && t != COL_NCLOB &&
                t != COL_LONG && t != COL_LONG_RAW) {
                st->non_lob_map[mc++] = mi;
            }
        }
        st->non_lob_count = mc;
    }

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
        /* Segment boundary check: Oracle omits trailing NULL columns.
         * When a 3c-segment is exhausted while we are still reading
         * normal columns, treat unread columns as NULL and deliver. */
        if (st->step == 2 && st->seg_remaining == 0
            && st->data_step == DS_COL_LENGTH) {
            while (st->col_idx < non_lob_cols) {
                int ac = st->non_lob_map[st->col_idx];
                if (ac < s->table.col_count)
                    set_value_null(&s->record.values[ac]);
                st->col_idx++;
            }
            s->record.col_count = s->table.col_count;
            if (!list_only && !st->is_lob_record) {
                rc = deliver_row(s);
                if (rc != ODV_OK) return rc;
                odv_report_progress(s, fp);
                record_count++;
                s->table.record_count++;
            }
            st->step = 1;
            st->seg_remaining = -1;
            continue;
        }

        /* Read one byte */
        if (fread(&b, 1, 1, fp) != 1) break;
        (*address)++;

        /* Safety check: detect XML DDL overrun */
        if (b == '<' && (*address % ODV_DUMP_BLOCK_LEN) == 3) {
            unsigned char peek[4];
            if (fread(peek, 1, 4, fp) == 4) {
                if (memcmp(peek, "?xml", 4) == 0) {
                    odv_fseek(fp, *address - 3, SEEK_SET);
                    *address -= 3;
                    break;
                }
                odv_fseek(fp, -4, SEEK_CUR);
            }
        }

        /* Throttled progress reporting */
        if (++progress_counter >= 1000) {
            progress_counter = 0;
            odv_report_progress(s, fp);
        }

        /* Track bytes consumed inside a 3c segment */
        if (st->step == 2 && st->seg_remaining > 0)
            st->seg_remaining--;

        /* ===== step 1: Expect record header byte ===== */
        if (st->step == 1) {
            switch (b) {
            case 0x00:
                if (st->is_between_record)
                    return ODV_OK;
                /* Padding before first record — skip */
                break;

            case 0x3c: {
                /* DataPump segment wrapper: 3c 00 NN */
                unsigned char nn;
                if (fread(&b, 1, 1, fp) != 1) return ODV_OK;
                (*address)++;
                if (fread(&nn, 1, 1, fp) != 1) return ODV_OK;
                (*address)++;
                st->seg_remaining = (int)nn - 4;
                if (st->seg_remaining < 0) st->seg_remaining = 0;
                break;
            }

            case 0x08: case 0x09:
                /* LOB record: only valid OUTSIDE 3c segments */
                if (st->seg_remaining >= 0) goto normal_record;
                st->is_between_record = 1;
                st->record_header  = b;
                st->is_lob_record  = 1;
                st->is_over255     = 1;
                st->filler_length  = 2;
                st->is_last_chunk  = 0;
                st->is_end_lob     = 0;
                st->lob_length     = 0;
                st->lob_col_idx    = 0;
                st->step           = 2;
                st->data_step      = DS_COL_LENGTH;
                st->col_idx        = 0;
                reset_record(&s->record);
                /* Read non-LOB column count sub-byte */
                if (fread(&b, 1, 1, fp) != 1) return ODV_OK;
                (*address)++;
                break;

            case 0x0c:
                /* Single-chunk LOB record: only valid OUTSIDE 3c segments */
                if (st->seg_remaining >= 0) goto normal_record;
                st->is_between_record = 1;
                st->record_header  = b;
                st->is_lob_record  = 1;
                st->is_over255     = 0;
                st->is_last_chunk  = 0;
                st->is_end_lob     = 0;
                st->lob_length     = 0;
                st->lob_col_idx    = 0;
                st->step           = 2;
                st->data_step      = DS_COL_LENGTH;
                st->col_idx        = 0;
                reset_record(&s->record);
                /* Read column count sub-byte */
                if (fread(&b, 1, 1, fp) != 1) return ODV_OK;
                (*address)++;
                break;

            case 0x18: case 0x19: case 0x1c: case 0x2c:
                /* Over-255 columns record */
                if (st->seg_remaining >= 0) goto normal_record;
                st->is_between_record = 1;
                st->record_header  = b;
                st->is_lob_record  = 0;
                st->is_over255     = 1;
                st->over255_count  = 0;
                st->step           = 2;
                st->data_step      = DS_COL_LENGTH;
                st->col_idx        = 0;
                reset_record(&s->record);
                break;

            case 0xff:
                return ODV_OK;

            default:
                if (st->seg_remaining >= 0 || (b >= 0x01 && b <= 0x07)) {
            normal_record:
                    st->is_between_record = 1;
                    st->record_header  = b;
                    st->is_lob_record  = 0;
                    st->is_over255     = 0;
                    st->step           = 2;
                    st->data_step      = DS_COL_LENGTH;
                    st->col_idx        = 0;
                    reset_record(&s->record);
                }
                break;
            }

        /* ===== step 2: Reading record data ===== */
        } else if (st->step == 2) {

            switch (st->data_step) {

            /* --- Normal column reading states --- */

            case DS_COL_LENGTH: /* 0: Column length marker */
            {
                /* Map non-LOB sequential index to absolute column index */
                int ac = (st->col_idx < st->non_lob_count)
                         ? st->non_lob_map[st->col_idx]
                         : st->col_idx;

                if (b == 0xff) {
                    /* NULL */
                    if (ac < s->table.col_count) {
                        set_value_null(&s->record.values[ac]);
                        s->record.values[ac].type =
                            s->table.columns[ac].type;
                    }
                    st->col_idx++;
                } else if (b == 0xfe) {
                    /* 2-byte length follows */
                    st->data_step = DS_COL_LEN_HI;
                    st->col_len = 0;
                } else if (b == 0x00) {
                    /* Empty string */
                    if (ac < s->table.col_count) {
                        set_value_string(&s->record.values[ac], "", 0);
                        s->record.values[ac].type =
                            s->table.columns[ac].type;
                    }
                    st->col_idx++;
                } else {
                    /* Direct length (1-253) */
                    st->col_len = (int)b;
                    st->col_remaining = st->col_len;
                    st->data_step = DS_COL_DATA;
                    if (ac < s->table.col_count) {
                        ensure_value_buf(&s->record.values[ac],
                                         st->col_len + 1);
                        s->record.values[ac].data_len = 0;
                    }
                }

                /* 255-column boundary filler */
                if (st->is_over255 && st->col_idx > 0
                    && (st->col_idx % 255) == 0
                    && st->data_step == DS_COL_LENGTH) {
                    st->over255_count++;
                }

                /* Check if all non-LOB columns are complete */
                if (st->data_step == DS_COL_LENGTH
                    && st->col_idx >= non_lob_cols) {
                    s->record.col_count = s->table.col_count;

                    /* LOB table? Transition to LOB state machine */
                    if (s->table.lob_col_count > 0 && st->is_lob_record) {
                        /* Do NOT deliver_row yet — LOB data follows.
                         * Transition to LOB marker reading. */
                        st->data_step   = DS_LOB_MARKER;
                        st->lob_col_idx = 0;
                        st->is_end_lob  = 0;
                        st->lob_length  = 0;
                    } else {
                        /* Non-LOB table or normal record in LOB table:
                         * deliver the row immediately. */
                        if (!list_only) {
                            rc = deliver_row(s);
                            if (rc != ODV_OK) return rc;
                            odv_report_progress(s, fp);
                        }
                        record_count++;
                        s->table.record_count++;
                        st->step = 1;
                    }
                }
                break;
            }

            case DS_COL_LEN_HI: /* 1: First byte of 2-byte length */
                st->col_len = (int)b;
                st->data_step = DS_COL_LEN_LO;
                break;

            case DS_COL_LEN_LO: /* 2: Second byte of 2-byte length (LE) */
            {
                int ac = (st->col_idx < st->non_lob_count)
                         ? st->non_lob_map[st->col_idx]
                         : st->col_idx;
                st->col_len |= ((int)b << 8);
                st->col_remaining = st->col_len;
                st->data_step = DS_COL_DATA;
                if (ac < s->table.col_count) {
                    ensure_value_buf(&s->record.values[ac],
                                     st->col_len + 1);
                    s->record.values[ac].data_len = 0;
                }
                break;
            }

            case DS_COL_DATA: /* 3: Reading column data bytes */
            {
                int ac = (st->col_idx < st->non_lob_count)
                         ? st->non_lob_map[st->col_idx]
                         : st->col_idx;
                if (ac < s->table.col_count) {
                    ODV_VALUE *v = &s->record.values[ac];
                    if (v->data && v->data_len < v->buf_size - 1) {
                        v->data[v->data_len++] = b;
                    }
                }
                st->col_remaining--;
                if (st->col_remaining <= 0) {
                    /* Column complete — decode */
                    decode_column_value(s, ac);
                    st->col_idx++;
                    st->data_step = DS_COL_LENGTH;

                    /* Check record completion */
                    if (st->col_idx >= non_lob_cols) {
                        s->record.col_count = s->table.col_count;

                        if (s->table.lob_col_count > 0 && st->is_lob_record) {
                            st->data_step   = DS_LOB_MARKER;
                            st->lob_col_idx = 0;
                            st->is_end_lob  = 0;
                            st->lob_length  = 0;
                        } else {
                            if (!list_only) {
                                rc = deliver_row(s);
                                if (rc != ODV_OK) return rc;
                                odv_report_progress(s, fp);
                            }
                            record_count++;
                            s->table.record_count++;
                            st->step = 1;
                        }
                    }
                }
                break;
            }

            /* --- LOB state machine (negative data_step values) --- */

            case DS_LOB_MARKER: /* -12: LOB column marker byte */
                switch (b) {
                case 0x00:
                    /* LOB end-of-column marker.
                     * Only advance lob_col_idx if we actually read chunk
                     * data (lob_length > 0). If lob_length == 0, this is
                     * a filler/padding byte before LOB data starts. */
                    st->is_last_chunk = 1;
                    if (st->lob_length > 0) {
                        st->lob_col_idx++;
                        st->lob_length = 0;

                        if (st->lob_col_idx >= s->table.lob_col_count) {
                            /* All LOB columns processed — deliver row */
                            if (s->lob_extract_mode &&
                                s->lob_column_index >= 0) {
                                rc = odv_lob_write_file(s);
                                if (rc != ODV_OK) return rc;
                            }
                            if (!list_only) {
                                rc = deliver_row(s);
                                if (rc != ODV_OK) return rc;
                                odv_report_progress(s, fp);
                            }
                            record_count++;
                            s->table.record_count++;
                            st->step = 1;
                            st->data_step = DS_COL_LENGTH;
                            break;
                        }
                    }
                    st->data_step++;   /* → DS_LOB_POST (-11) */
                    break;

                case 0x08: case 0x09: case 0x0c:
                    /* New LOB/record header encountered inside LOB stream.
                     * End current LOB processing, deliver row, then
                     * restart as a new record with this header. */

                    /* Finalize LOB extraction for current record */
                    if (s->lob_extract_mode && s->lob_column_index >= 0) {
                        rc = odv_lob_write_file(s);
                        if (rc != ODV_OK) return rc;
                    }

                    /* Deliver the completed record */
                    if (!list_only) {
                        rc = deliver_row(s);
                        if (rc != ODV_OK) return rc;
                        odv_report_progress(s, fp);
                    }
                    record_count++;
                    s->table.record_count++;

                    /* Set up new record with this header */
                    st->record_header  = b;
                    st->is_lob_record  = 1;
                    st->is_last_chunk  = 0;
                    st->is_end_lob     = 0;
                    st->lob_length     = 0;
                    st->lob_col_idx    = 0;
                    st->col_idx        = 0;
                    st->data_step      = DS_COL_LENGTH;
                    reset_record(&s->record);

                    if (b == 0x0c) {
                        st->is_over255 = 0;
                    } else {
                        st->is_over255    = 1;
                        st->filler_length = 2;
                    }

                    /* Read column count sub-byte */
                    if (fread(&b, 1, 1, fp) != 1) return ODV_OK;
                    (*address)++;
                    break;

                case 0xfe:
                    /* 2-byte chunk length follows */
                    st->data_step = DS_LOB_FE_NEXT;
                    break;

                case 0xff:
                    /* NULL LOB column */
                    if (st->col_idx + st->lob_col_idx < s->table.col_count) {
                        int abs_idx = non_lob_cols + st->lob_col_idx;
                        if (abs_idx < s->table.col_count)
                            set_value_null(&s->record.values[abs_idx]);
                    }
                    st->lob_col_idx++;
                    st->lob_length = 0;

                    /* Check if all LOB columns are done */
                    if (st->lob_col_idx >= s->table.lob_col_count) {
                        /* All LOBs processed — deliver row */
                        if (s->lob_extract_mode && s->lob_column_index >= 0) {
                            rc = odv_lob_write_file(s);
                            if (rc != ODV_OK) return rc;
                        }
                        if (!list_only) {
                            rc = deliver_row(s);
                            if (rc != ODV_OK) return rc;
                            odv_report_progress(s, fp);
                        }
                        record_count++;
                        s->table.record_count++;
                        st->step = 1;
                        st->data_step = DS_COL_LENGTH;
                    }
                    /* else: stay at DS_LOB_MARKER for next LOB column */
                    break;

                case 0x01: case 0x02: case 0x03:
                case 0x04: case 0x05: case 0x06:
                    /* Filler / separator between LOB columns.
                     * If we had data (lob_length > 0) and this was the
                     * last chunk, advance to next LOB column. */
                    if (st->lob_length > 0 && st->is_last_chunk) {
                        st->lob_col_idx++;
                        st->lob_length = 0;
                        st->is_last_chunk = 0;

                        if (st->lob_col_idx >= s->table.lob_col_count) {
                            if (s->lob_extract_mode &&
                                s->lob_column_index >= 0) {
                                rc = odv_lob_write_file(s);
                                if (rc != ODV_OK) return rc;
                            }
                            if (!list_only) {
                                rc = deliver_row(s);
                                if (rc != ODV_OK) return rc;
                                odv_report_progress(s, fp);
                            }
                            record_count++;
                            s->table.record_count++;
                            st->step = 1;
                            st->data_step = DS_COL_LENGTH;
                            break;
                        }
                    }
                    st->data_step++;   /* → DS_LOB_POST (-11) */
                    break;

                default:
                    /* Direct chunk size (0x07-0xFD excluding 0x08/0x09/0x0c) */
                    chunk_size = (int)b;
                    st->is_last_chunk = 1;
                    st->is_end_lob    = 1;
                    st->data_step     = DS_LOB_CHUNK;
                    goto LOB_READ_CHUNK;
                }
                break;

            case DS_LOB_POST: /* -11: Filler/separator after LOB marker */
                switch (b) {
                case 0x00:
                    /* End of table data during LOB */
                    if (s->lob_extract_mode && s->lob_column_index >= 0) {
                        rc = odv_lob_write_file(s);
                        if (rc != ODV_OK) return rc;
                    }
                    if (!list_only) {
                        rc = deliver_row(s);
                        if (rc != ODV_OK) return rc;
                    }
                    record_count++;
                    s->table.record_count++;
                    return ODV_OK;
                default:
                    break;
                }
                st->data_step++;   /* → DS_LOB_FE_NEXT (-10) */
                break;

            case DS_LOB_FE_NEXT: /* -10: After 0xFE or filler sequence */
                switch (b) {
                case 0xfe:
                    /* 2-byte LOB chunk length follows */
                    break;
                case 0xff:
                case 0x00:
                    /* NULL/empty LOB column after filler */
                    st->lob_col_idx++;
                    st->lob_length = 0;

                    if (st->lob_col_idx >= s->table.lob_col_count) {
                        /* All LOBs done */
                        if (s->lob_extract_mode && s->lob_column_index >= 0) {
                            rc = odv_lob_write_file(s);
                            if (rc != ODV_OK) return rc;
                        }
                        if (!list_only) {
                            rc = deliver_row(s);
                            if (rc != ODV_OK) return rc;
                            odv_report_progress(s, fp);
                        }
                        record_count++;
                        s->table.record_count++;
                        st->step = 1;
                        st->data_step = DS_COL_LENGTH;
                        break;
                    }
                    st->data_step = DS_LOB_MARKER;
                    st->is_last_chunk = 0;
                    break;
                default:
                    /* Direct chunk size */
                    chunk_size = (int)b;
                    st->is_last_chunk = 1;
                    st->data_step     = DS_LOB_CHUNK;
                    goto LOB_READ_CHUNK;
                }
                if (st->data_step == DS_LOB_FE_NEXT)
                    st->data_step++;  /* → DS_LOB_LEN_HI (-9) */
                break;

            case DS_LOB_LEN_HI: /* -9: First byte of 2-byte LOB chunk length */
                st->len_buf[0] = b;
                st->data_step++;   /* → DS_LOB_LEN_LO (-8) */
                break;

            case DS_LOB_LEN_LO: /* -8: Second byte of 2-byte LOB chunk length */
                st->len_buf[1] = b;
                chunk_size = (int)st->len_buf[0] * 0x100 + (int)st->len_buf[1];
                st->data_step = DS_LOB_CHUNK;
                goto LOB_READ_CHUNK;

            case DS_LOB_CHUNK: /* 4: Read chunk data (bulk fread) */
            LOB_READ_CHUNK:
            {
                unsigned char lob_tmp[4096];
                int lob_read = 0;
                unsigned char next_buf[2];

                /* Bulk-read chunk_size bytes */
                while (lob_read < chunk_size && !s->cancelled) {
                    int need = chunk_size - lob_read;
                    int blk  = (need < (int)sizeof(lob_tmp))
                                ? need : (int)sizeof(lob_tmp);
                    int got  = (int)fread(lob_tmp, 1, blk, fp);
                    if (got <= 0) goto END_PARSE;
                    *address += got;

                    /* LOB extraction accumulation */
                    if (s->lob_extract_mode && s->lob_column_index >= 0) {
                        rc = odv_lob_accumulate(s, st->lob_col_idx,
                                                lob_tmp, got);
                        if (rc != ODV_OK) return rc;
                    }

                    /* LOB preview accumulation */
                    if (!list_only) {
                        accumulate_lob_preview(s, st->lob_col_idx,
                                               lob_tmp, got);
                    }

                    lob_read += got;
                }
                st->lob_length += chunk_size;

                /* Peek ahead 2 bytes to determine is_last_chunk */
                if (fread(next_buf, 2, 1, fp) != 1) {
                    /* EOF — treat as last chunk */
                    st->is_last_chunk = 1;
                } else {
                    odv_fseek(fp, -2, SEEK_CUR);
                    switch (next_buf[0]) {
                    case 0xfe: case 0xff:
                        st->is_last_chunk = 1;
                        break;
                    case 0x01: case 0x08: case 0x09: case 0x0c:
                        st->is_last_chunk = 1;
                        break;
                    case 0x02: case 0x03: case 0x04:
                    case 0x05: case 0x06:
                        st->is_last_chunk = 0;
                        break;
                    default:
                        /* Keep whatever was set */
                        break;
                    }
                }

                /* Back to LOB marker for next chunk or next LOB column */
                st->data_step = DS_LOB_MARKER;
                break;
            }

            default:
                /* Unexpected data_step — safety fallback */
                st->step = 1;
                st->data_step = DS_COL_LENGTH;
                break;

            } /* end switch(data_step) */
        } /* end step==2 */
    } /* end while */

END_PARSE:
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
    unsigned char skip_blk[ODV_DUMP_BLOCK_LEN];  /* buffer for skipping record data */

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
       jump directly to the target DDL position instead of scanning from top.
       Align to block boundary because the main loop reads full blocks and
       checks block[2] for "<?xml".  ddl_offset stores the <?xml position
       (= block_start + 2), so we round down to the enclosing block. */
    if (s->seek_offset > 0 && s->filter_active) {
        int64_t aligned = (s->seek_offset / ODV_DUMP_BLOCK_LEN) * ODV_DUMP_BLOCK_LEN;
        odv_fseek(fp, aligned, SEEK_SET);
        address = aligned;
    }

    /* Read blocks sequentially */
    while (!s->cancelled) {
        n = (int)fread(block, 1, ODV_DUMP_BLOCK_LEN, fp);
        if (n <= 0) break;

        /* Report progress during DDL scan so UI stays responsive */
        odv_report_progress(s, fp);

        /* Check for XML DDL marker at block offset 2 (per EXPDP format).
           Offset 0 blocks contain statistics/metadata, not table DDL.
           Table DDL blocks always start with 2-byte length prefix + "<?xml". */
        int xml_pos = (n >= 7 && memcmp(block + 2, "<?xml", 5) == 0) ? 2 : -1;

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

                /* Seek to just after </ROWSET> for record data */
                odv_fseek(fp, cur_ddl_pos + end_pos, SEEK_SET);
                address = odv_ftell(fp);

                /* Skip dictionary tables and metadata-only XMLs (0 columns) */
                if (s->table.name[0] != '\0' && s->table.col_count > 0
                    && !is_system_table(&s->table, s->table.schema)) {

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

                        /* When seek_offset was used, we parsed exactly one partition's
                         * data at the target position. Exit now to avoid parsing
                         * subsequent partitions of the same table name. */
                        if (s->filter_active && s->seek_offset > 0 && filter_found) {
                            in_ddl = 0;
                            goto expdp_done;
                        }
                    } else {
                        /* Filtered out in full parse: skip records */
                        notify_table(s, 0);
                    }
                }

                /* After DDL+records processing (or skipping), scan forward
                 * to find the next <?xml DDL block.  This is the safest
                 * approach: even if parse_expdp_records over-reads or
                 * under-reads, we always re-sync to the correct position.
                 * First, align to the next block boundary. */
                {
                    int64_t cur = odv_ftell(fp);
                    int64_t rem = cur % ODV_DUMP_BLOCK_LEN;
                    if (rem != 0)
                        odv_fseek(fp, cur + (ODV_DUMP_BLOCK_LEN - rem), SEEK_SET);
                }
                /* Then scan block-by-block until we find <?xml or EOF */
                while (!s->cancelled) {
                    int nr = (int)fread(skip_blk, 1, ODV_DUMP_BLOCK_LEN, fp);
                    if (nr < 7) break;  /* EOF */
                    if (memcmp(skip_blk + 2, "<?xml", 5) == 0) {
                        /* Found next DDL block — seek back so main loop reads it */
                        odv_fseek(fp, odv_ftell(fp) - nr, SEEK_SET);
                        break;
                    }
                }

                in_ddl = 0;
                ddl_len = 0;
            }
        }

        address = odv_ftell(fp);
    }

expdp_done:
    /* Post-parse: populate partition names from master table area.
     * The master table binary records contain TABLE_DATA entries with
     * the pattern: SCHEMA_EXPORT/TABLE/TABLE_DATA [ff]...[len]TABLE_NAME
     * [len]SCHEMA [ff][02][c1][02][ff][ff][ff][len]PARTITION_NAME[ff]
     * For non-partitioned tables, PARTITION_NAME is absent (just [ff]s). */
    if (list_only && s->table_count > 0) {
        /* Build per-table occurrence counters for partition matching.
         * part_occ[i] = which occurrence (0-based) of this schema.table
         * in the table_list (e.g., 4 partitions → occ 0,1,2,3). */
        int part_occ[ODV_MAX_TABLES];
        {
            int ti;
            for (ti = 0; ti < s->table_count; ti++) {
                part_occ[ti] = 0;
                int k;
                for (k = 0; k < ti; k++) {
                    if (strcmp(s->table_list[k].schema, s->table_list[ti].schema) == 0 &&
                        strcmp(s->table_list[k].name, s->table_list[ti].name) == 0) {
                        part_occ[ti]++;
                    }
                }
            }
        }

        /* Scan file for SCHEMA_EXPORT/TABLE/TABLE_DATA entries.
         * Simple approach: search byte-by-byte, then seek+read the structure. */
        {
            static const char path_marker[] = "SCHEMA_EXPORT/TABLE/TABLE_DATA";
            static const int path_len = 30;
            /* Track per-table occurrence in master table */
            char scan_keys[ODV_MAX_TABLES][ODV_OBJNAME_LEN * 2 + 2];
            int scan_count = 0;

            /* Use the already-parsed file — re-open for scanning */
            FILE *sfp = fopen(s->dump_path, "rb");
            if (sfp) {
                unsigned char blk[8192];
                int64_t blk_offset = 0;
                int blk_len = 0;

                while (!s->cancelled) {
                    blk_len = (int)fread(blk, 1, sizeof(blk), sfp);
                    if (blk_len < path_len + 60) break;

                    int si;
                    for (si = 0; si <= blk_len - path_len - 50; si++) {
                        if (memcmp(blk + si, path_marker, path_len) != 0) continue;

                        /* Found path. Structure after:
                         * [ff]...[len]TABLE[len]SCHEMA[ff][02]...[ff][len]PART[ff] */
                        int p = si + path_len;

                        /* Skip ff */
                        while (p < blk_len && blk[p] == 0xff) p++;
                        if (p + 3 >= blk_len) continue;

                        /* [len]TABLE */
                        int tl = blk[p++];
                        if (tl <= 0 || tl > 60 || p + tl + 3 >= blk_len) continue;
                        char tn[64] = {0};
                        memcpy(tn, blk + p, tl); p += tl;

                        /* [len]SCHEMA */
                        int sl = blk[p++];
                        if (sl <= 0 || sl > 60 || p + sl >= blk_len) continue;
                        char sn[64] = {0};
                        memcpy(sn, blk + p, sl); p += sl;

                        /* After schema: skip binary pattern [ff][02][c1][02][ff][ff][ff]
                         * and find partition name [len]NAME (printable ASCII).
                         * Scan forward looking for a valid len+name pattern. */
                        char pn[64] = {0};
                        {
                            int limit = ODV_MIN(p + 20, blk_len - 2);
                            while (p < limit && !pn[0]) {
                                int pl = blk[p];
                                if (pl >= 4 && pl <= 60 && p + 1 + pl <= blk_len) {
                                    /* Check if next pl bytes are all printable ASCII */
                                    int ok = 1, q;
                                    for (q = 0; q < pl; q++) {
                                        if (blk[p+1+q] < 0x20 || blk[p+1+q] > 0x7e) { ok = 0; break; }
                                    }
                                    if (ok) {
                                        memcpy(pn, blk + p + 1, pl);
                                        pn[pl] = '\0';
                                    }
                                }
                                p++;
                            }
                        }

                        /* Convert charset for matching */
                        char ct[260], cs2[260];
                        convert_name(tn, s->dump_charset, s->out_charset, ct, sizeof(ct));
                        convert_name(sn, s->dump_charset, s->out_charset, cs2, sizeof(cs2));

                        /* Count occurrence */
                        int occ = 0;
                        {
                            char key[260];
                            snprintf(key, sizeof(key), "%s.%s", cs2, ct);
                            int pi;
                            for (pi = 0; pi < scan_count; pi++) {
                                if (strcmp(scan_keys[pi], key) == 0) occ++;
                            }
                            if (scan_count < ODV_MAX_TABLES)
                                odv_strcpy(scan_keys[scan_count++], key, 259);
                        }

                        /* Assign partition name to matching table_list entry */
                        if (pn[0]) {
                            int ti;
                            for (ti = 0; ti < s->table_count; ti++) {
                                if (strcmp(s->table_list[ti].schema, cs2) == 0 &&
                                    strcmp(s->table_list[ti].name, ct) == 0 &&
                                    part_occ[ti] == occ) {
                                    odv_strcpy(s->table_list[ti].partition, pn,
                                               ODV_OBJNAME_LEN);
                                    break;
                                }
                            }
                        }
                    }

                    /* No seek-back: 8KB blocks are large enough that boundary
                     * splits of ~80-byte structures are extremely rare. */
                }
                fclose(sfp);
            }
        }
    }

    free(ddl_buf);
    fclose(fp);

    if (s->cancelled) return ODV_ERROR_CANCELLED;
    return ODV_OK;
}
