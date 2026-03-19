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
            for (k = 0; k < s->table_count - 1; k++) {
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
    unsigned char b;
    int step = 1;       /* 1=expect header, 2=reading columns */
    int data_step = 0;
    int col_idx = 0;
    int col_len = 0;
    int col_remaining = 0;
    int is_over255 = 0;
    int over255_count = 0;
    int record_count = 0;
    int is_between_record = 0; /* 0=before first record, 1=after at least one record header */
    int seg_remaining = -1;   /* bytes left in current 3c-segment (-1 = no limit) */
    int non_lob_cols;
    int rc;
    int progress_counter = 0; /* throttle progress reporting */
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
        /* Segment boundary check: Oracle omits trailing NULL columns.
         * When a 3c-segment is exhausted (seg_remaining==0) while we are
         * still in step 2, treat every unread column as NULL and deliver
         * the record.  The next byte belongs to the following segment. */
        if (step == 2 && seg_remaining == 0 && data_step == 0) {
            while (col_idx < non_lob_cols) {
                if (col_idx < s->table.col_count)
                    set_value_null(&s->record.values[col_idx]);
                col_idx++;
            }
            s->record.col_count = col_idx;
            if (!list_only) {
                rc = deliver_row(s);
                if (rc != ODV_OK) return rc;
                odv_report_progress(s, fp);
            }
            record_count++;
            s->table.record_count++;
            step = 1;
            seg_remaining = -1;
            continue; /* next byte is the start of a new segment or padding */
        }

        /* Read one byte */
        if (fread(&b, 1, 1, fp) != 1) break;
        (*address)++;

        /* Safety check: if we've just read byte at block offset 2 and
         * it's '<', peek ahead to see if this is "<?xml" — the start of
         * the next table's DDL.  If so, seek back and stop.  This prevents
         * record parsing bugs from overrunning into adjacent DDL blocks. */
        if (b == '<' && (*address % ODV_DUMP_BLOCK_LEN) == 3) {
            unsigned char peek[4];
            if (fread(peek, 1, 4, fp) == 4) {
                if (memcmp(peek, "?xml", 4) == 0) {
                    /* Overrun detected — rewind to block offset 0 */
                    odv_fseek(fp, *address - 3, SEEK_SET);
                    *address -= 3;
                    break;
                }
                /* Not XML — rewind the peek bytes */
                odv_fseek(fp, -4, SEEK_CUR);
            }
        }

        /* Throttled progress reporting */
        if (++progress_counter >= 1000) {
            progress_counter = 0;
            odv_report_progress(s, fp);
        }

        /* Track bytes consumed inside a 3c segment */
        if (step == 2 && seg_remaining > 0)
            seg_remaining--;

        if (step == 1) {
            /* Expecting record header byte */
            switch (b) {
            case 0x00:
                /* End of table data only if we've already seen records.
                   Before the first record, 0x00 bytes are padding after </ROWSET>. */
                if (is_between_record) {
                    return ODV_OK;
                }
                /* Padding byte before first record - skip */
                break;

            case 0x3c: {
                /* DataPump segment wrapper: 3c 00 NN
                 * NN = total segment length (including 3-byte header).
                 * The actual column data is (NN - 4) bytes:
                 *   NN - 3 (header) - 1 (record header byte that follows).
                 * Track this in seg_remaining so that trailing NULL columns
                 * (which Oracle omits) are handled correctly. */
                unsigned char _nn = 0;
                /* skip the 0x00 byte */
                if (fread(&b, 1, 1, fp) != 1) return ODV_OK;
                (*address)++;
                /* read NN */
                if (fread(&b, 1, 1, fp) != 1) return ODV_OK;
                (*address)++;
                _nn = b;
                /* column data bytes = NN - 3 (header) - 1 (record hdr) */
                seg_remaining = (int)_nn - 4;
                if (seg_remaining < 0) seg_remaining = 0;
                /* step stays 1 — next byte is the real record header */
                break;
            }

            case 0x08: case 0x09:
                /* LOB record (BLOB/CLOB): only valid OUTSIDE 3c segments.
                 * Inside segments, 0x08/0x09 = column count (8 or 9).
                 * A sub-byte (non-LOB column count) follows the header. */
                if (seg_remaining >= 0) goto normal_record;
                is_between_record = 1;
                is_over255 = 0;
                step = 2;
                data_step = 0;
                col_idx = 0;
                reset_record(&s->record);
                /* Skip the non-LOB column count sub-byte */
                if (fread(&b, 1, 1, fp) != 1) return ODV_OK;
                (*address)++;
                break;

            case 0x0c:
                /* LONG column record: only valid OUTSIDE 3c segments.
                 * Inside segments, 0x0c = column count (12).
                 * A sub-byte (column count) follows the header. */
                if (seg_remaining >= 0) goto normal_record;
                is_between_record = 1;
                is_over255 = 0;
                step = 2;
                data_step = 0;
                col_idx = 0;
                reset_record(&s->record);
                /* Skip the column count sub-byte */
                if (fread(&b, 1, 1, fp) != 1) return ODV_OK;
                (*address)++;
                break;

            case 0x18: case 0x19: case 0x1c: case 0x2c:
                /* Special record format (cluster/IOT/over-255 columns).
                 * Inside 3c segments, these are column counts. */
                if (seg_remaining >= 0) goto normal_record;
                is_between_record = 1;
                is_over255 = 1;
                over255_count = 0;
                step = 2;
                data_step = 0;
                col_idx = 0;
                reset_record(&s->record);
                break;

            case 0xff:
                /* End of table data */
                return ODV_OK;

            default:
                /* Inside a 3c segment, any byte value is a valid column
                 * count for a normal record.  Outside segments, bytes
                 * 0x01-0x07 are also normal records. */
                if (seg_remaining >= 0 || (b >= 0x01 && b <= 0x07)) {
            normal_record:
                    if (seg_remaining < 0 && b >= 0x01 && b <= 0x07) {
                        /* Outside segment: only 0x01-0x07 are valid.
                         * Other values may be LOB payload — skip. */
                    }
                    is_between_record = 1;
                    is_over255 = 0;
                    step = 2;
                    data_step = 0;
                    col_idx = 0;
                    reset_record(&s->record);
                }
                /* else: unknown header outside segment - skip */
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
                            /* Read LOB column length byte */
                            int lb;
                            int lob_len = 0;
                            if (fread(&b, 1, 1, fp) != 1) goto END_LOB_PARSE;
                            (*address)++;
                            lb = b;
                            if (lb == 0xff) {
                                /* NULL LOB */
                                continue;
                            } else if (lb == 0xfe) {
                                /* 2-byte length */
                                if (fread(&b, 1, 1, fp) != 1) goto END_LOB_PARSE;
                                (*address)++;
                                lob_len = b;
                                if (fread(&b, 1, 1, fp) != 1) goto END_LOB_PARSE;
                                (*address)++;
                                lob_len |= (b << 8);
                            } else if (lb == 0x00) {
                                /* Empty LOB */
                                continue;
                            } else {
                                lob_len = lb;
                            }

                            /* Read lob_len bytes of LOB data (bulk fread) */
                            {
                                unsigned char lob_tmp[4096];
                                int lob_read = 0;
                                while (lob_read < lob_len && !s->cancelled) {
                                    int need = lob_len - lob_read;
                                    int chunk = (need < (int)sizeof(lob_tmp)) ? need : (int)sizeof(lob_tmp);
                                    int got = (int)fread(lob_tmp, 1, chunk, fp);
                                    if (got <= 0) goto END_LOB_PARSE;
                                    *address += got;
                                    rc = odv_lob_accumulate(s, lob_i, lob_tmp, got);
                                    if (rc != ODV_OK) return rc;
                                    lob_read += got;
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

                        case COL_NCHAR:
                        case COL_NVARCHAR: {
                            /* NCHAR/NVARCHAR2: Oracle stores in national charset
                             * (AL16UTF16, big-endian). Convert UTF-16BE → UTF-8. */
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
                                if (fread(&b, 1, 1, fp) != 1) goto END_LOB_PARSE2;
                                (*address)++;
                                lb = b;
                                if (lb == 0xff) continue; /* NULL */
                                else if (lb == 0xfe) {
                                    if (fread(&b, 1, 1, fp) != 1) goto END_LOB_PARSE2;
                                    (*address)++;
                                    lob_len = b;
                                    if (fread(&b, 1, 1, fp) != 1) goto END_LOB_PARSE2;
                                    (*address)++;
                                    lob_len |= (b << 8);
                                } else if (lb == 0x00) continue; /* empty */
                                else lob_len = lb;

                                { unsigned char lob_tmp[4096];
                                int lr = 0;
                                while (lr < lob_len && !s->cancelled) {
                                    int need = lob_len - lr;
                                    int chunk = (need < (int)sizeof(lob_tmp)) ? need : (int)sizeof(lob_tmp);
                                    int got = (int)fread(lob_tmp, 1, chunk, fp);
                                    if (got <= 0) goto END_LOB_PARSE2;
                                    *address += got;
                                    rc = odv_lob_accumulate(s, lob_i, lob_tmp, got);
                                    if (rc != ODV_OK) return rc;
                                    lr += got;
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
    free(ddl_buf);
    fclose(fp);

    if (s->cancelled) return ODV_ERROR_CANCELLED;
    return ODV_OK;
}
