/*****************************************************************************
    Open DUMP Viewer for Oracle database

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
    int  col_item_depth;    /* XML depth of the current <COL_LIST_ITEM> */
    int  col_idx;           /* current column being defined */
    char cur_schema[ODV_OBJNAME_LEN + 1];
    char cur_table[ODV_OBJNAME_LEN + 1];
    int  property;
    /* Current <COL_LIST_ITEM>: which optional tags were present.  The column
     * slot is zeroed when the item opens, so "absent" and "0" would otherwise
     * look the same, and the type name depends on exactly that distinction:
     * NUMBER(p) has PRECISION_NUM and SCALE=0, FLOAT(p) has PRECISION_NUM
     * only, INTEGER / NUMBER(*,s) have SCALE only, TIMESTAMP(0) has SCALE=0
     * (M-04 / M-06 / M-20). */
    int  col_type_num;      /* TYPE_NUM (69 ROWID / 208 UROWID ...) */
    int  col_has_prec;      /* <PRECISION_NUM> present */
    int  col_has_scale;     /* <SCALE> present */
    int  col_char_len;      /* <CHARLENGTH>: declared length in characters */
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
    Oracle system-generated column name?  These never appear as user columns:
      - SYS_NC*****$     : object OID / ROWINFO, ADT attributes, nested-table
                           SETID, function-based & DESC index hidden columns
      - SYS_IME_*        : JSON binary storage (In-Memory Expression) columns
    The ones that are actually stored in the row data (SEGCOL_NUM >= 1) are read
    and discarded; the rest (SEGCOL_NUM == 0) are not in the data at all (H-18).
 ---------------------------------------------------------------------------*/
static int is_system_colname(const char *name)
{
    size_t n = strlen(name);
    if (n == 0) return 0;
    if (strncmp(name, "SYS_NC", 6) == 0 && name[n - 1] == '$') return 1;
    if (strncmp(name, "SYS_IME_", 8) == 0) return 1;
    return 0;
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
                dc->col_item_depth = depth;
                dc->col_type_num  = 0;
                dc->col_has_prec  = 0;
                dc->col_has_scale = 0;
                dc->col_char_len  = 0;
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
    else if (strcmp(tag, "OWNER_NAME") == 0 && !dc->in_col_list) {
        /* Table-level OWNER_NAME only.  A column's type carries its own nested
         * <TYPEMD><OWNER_NAME>…</OWNER_NAME> (SYS for XMLTYPE/ANYDATA, MDSYS for
         * SDO_GEOMETRY, the ADT's schema for object columns); using it as the
         * table schema made XMLTYPE/ANYDATA tables look SYS-owned (then dropped
         * by is_system_table) and SDO_GEOMETRY tables appear under MDSYS (H-19). */
        odv_strcpy(dc->cur_schema, value, ODV_OBJNAME_LEN);
    }
    else if (strcmp(tag, "PROPERTY") == 0 && !dc->in_col_list) {
        dc->property = atoi(value);
    }
    else if (dc->in_col_list && depth == dc->col_item_depth + 1 &&
             dc->col_idx < ODV_MAX_COLUMNS) {
        /* Direct children of <COL_LIST_ITEM> only.  A column's type definition
         * nests a <TYPEMD> whose own <NAME> is the type name (XMLTYPE,
         * SDO_GEOMETRY, the ADT name); without the depth guard it overwrote the
         * column name with the type name (H-19). */
        ODV_COLUMN *col = &s->table.columns[dc->col_idx];

        if (strcmp(tag, "COL_NAME") == 0 || strcmp(tag, "NAME") == 0) {
            /* Store every column's name, including the Oracle system-generated
             * ones (SYS_NC*****$, SYS_IME_*).  Whether a column is shown, read
             * and discarded, or skipped entirely is decided at COL_LIST_ITEM
             * close from its name, COL_NUM and SEGCOL_NUM (H-18). */
            odv_strcpy(col->name, value, ODV_OBJNAME_LEN);
        }
        else if (strcmp(tag, "COL_NUM") == 0) {
            col->col_num = atoi(value);
        }
        else if (strcmp(tag, "SEGCOL_NUM") == 0) {
            col->seg_col = atoi(value);
        }
        else if (strcmp(tag, "TYPE_NUM") == 0) {
            int tn = atoi(value);
            dc->col_type_num = tn;
            col->type = type_num_to_col_type(tn, col->length, col->flags);
        }
        else if (strcmp(tag, "LENGTH") == 0) {
            col->length = atoi(value);
        }
        else if (strcmp(tag, "PRECISION_NUM") == 0) {
            col->precision = atoi(value);
            dc->col_has_prec = 1;
        }
        else if (strcmp(tag, "SCALE") == 0) {
            col->scale = atoi(value);
            dc->col_has_scale = 1;
        }
        else if (strcmp(tag, "CHARLENGTH") == 0) {
            dc->col_char_len = atoi(value);
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
            /* A column is in the row data when SEGCOL_NUM >= 1.  It is a shown
             * (user) column only when it also has a user column id (COL_NUM>=1)
             * and a non-system name; otherwise it is a hidden in-data column
             * (object OID, ADT attribute, nested SETID, invisible column) whose
             * bytes are read and discarded.  Virtual columns (SEGCOL_NUM==0) are
             * neither shown nor in the data (H-18). */
            int in_data    = (col->seg_col >= 1);
            int is_display = in_data && col->col_num >= 1 &&
                             col->name[0] != '\0' &&
                             !is_system_colname(col->name);

            if (in_data && s->table.data_count < ODV_MAX_COLUMNS) {
                s->table.data_disp[s->table.data_count] =
                    is_display ? dc->col_idx : -1;
                s->table.data_type[s->table.data_count] = col->type;
                s->table.data_count++;
            }

            /* Only count non-system columns */
            if (is_display) {
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
                case COL_CHAR: {
                    /* LENGTH is the byte length.  A column declared with
                     * character length semantics (n CHAR) has the PROPERTY bit
                     * 0x800000 and its declared n in CHARLENGTH; showing the
                     * byte length (4n in AL32UTF8) changed the column's
                     * meaning and padded CHAR values on re-creation (M-05). */
                    const char *base = (col->type == COL_CHAR) ? "CHAR" : "VARCHAR2";
                    if ((col->property & 0x800000) && dc->col_char_len > 0)
                        snprintf(col->type_str, sizeof(col->type_str),
                                 "%s(%d CHAR)", base, dc->col_char_len);
                    else
                        snprintf(col->type_str, sizeof(col->type_str),
                                 "%s(%d)", base, col->length);
                    break;
                }
                case COL_NUMBER:
                    /* The type follows from which of PRECISION_NUM / SCALE
                     * are present (M-06):
                     *   both          NUMBER(p) / NUMBER(p,s)
                     *   PRECISION     FLOAT(p)  (binary precision; REAL is
                     *                 FLOAT(63), FLOAT / DOUBLE PRECISION are
                     *                 FLOAT(126))
                     *   SCALE         NUMBER(*,s) (INTEGER etc. are NUMBER(*,0))
                     *   neither       NUMBER */
                    if (dc->col_has_prec && !dc->col_has_scale)
                        snprintf(col->type_str, sizeof(col->type_str),
                                 "FLOAT(%d)", col->precision);
                    else if (dc->col_has_prec && col->scale != 0)
                        snprintf(col->type_str, sizeof(col->type_str),
                                 "NUMBER(%d,%d)", col->precision, col->scale);
                    else if (dc->col_has_prec)
                        snprintf(col->type_str, sizeof(col->type_str),
                                 "NUMBER(%d)", col->precision);
                    else if (dc->col_has_scale)
                        snprintf(col->type_str, sizeof(col->type_str),
                                 "NUMBER(*,%d)", col->scale);
                    else
                        snprintf(col->type_str, sizeof(col->type_str), "NUMBER");
                    break;
                case COL_DATE:
                    snprintf(col->type_str, sizeof(col->type_str), "DATE");
                    break;
                case COL_TIMESTAMP:
                case COL_TIMESTAMP_TZ:
                case COL_TIMESTAMP_LTZ:
                    /* The fractional-second precision is in SCALE (there is no
                     * PRECISION_NUM); 0 is a real precision, the Oracle default
                     * 6 applies only when the tag is absent.  col->precision is
                     * also what decode_oracle_timestamp formats to (M-04). */
                    col->precision = dc->col_has_scale ? col->scale : 6;
                    if (col->precision < 0) col->precision = 0;
                    if (col->precision > 9) col->precision = 9;
                    snprintf(col->type_str, sizeof(col->type_str),
                             "TIMESTAMP(%d)%s", col->precision,
                             col->type == COL_TIMESTAMP_TZ  ? " WITH TIME ZONE" :
                             col->type == COL_TIMESTAMP_LTZ ? " WITH LOCAL TIME ZONE" : "");
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
                    /* Leading precision in PRECISION_NUM (0 is valid; the
                     * Oracle default 2 applies only when absent) (M-20). */
                    snprintf(col->type_str, sizeof(col->type_str),
                             "INTERVAL YEAR(%d) TO MONTH",
                             dc->col_has_prec ? col->precision : 2);
                    break;
                case COL_INTERVAL_DS:
                    /* Leading precision in PRECISION_NUM, fractional-second
                     * precision in SCALE (defaults 2 / 6) (M-20). */
                    snprintf(col->type_str, sizeof(col->type_str),
                             "INTERVAL DAY(%d) TO SECOND(%d)",
                             dc->col_has_prec ? col->precision : 2,
                             dc->col_has_scale ? col->scale : 6);
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
                    /* TYPE_NUM 208 is UROWID (LENGTH = its maximum size, 4000
                     * unless declared); 69 / 11 are ROWID.  decode_column_value
                     * tells the two apart by this name (M-35). */
                    if (dc->col_type_num == 208)
                        snprintf(col->type_str, sizeof(col->type_str),
                                 "UROWID(%d)", col->length > 0 ? col->length : 4000);
                    else
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
    if (schema[0] && odv_stricmp(schema, "SYS") == 0) return 1;

    /* Skip DataPump job tables */
    if (odv_strnicmp(t->name, "SYS_EXPORT_", 11) == 0) return 1;
    if (odv_strnicmp(t->name, "SYS_IMPORT_", 11) == 0) return 1;
    if (odv_strnicmp(t->name, "IMPDP_", 6) == 0) return 1;

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

    /* The product pairs the i-th table callback with odv_get_table_entry(i),
       so report a table only when it also gets its table_list entry. The
       list grows on demand; it used to stop at 1000 entries while the
       callbacks went on, leaving later partitions unnamed (M-29). */
    if (s->table.name[0] == '\0' || !odv_table_list_reserve(s)) return;

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
            col_defaults[i] = ODV_TEXT(s->table.columns[i].default_val,
                                       s->table.columns[i].default_long);
        }

        s->table_cb(conv_schema, conv_name,
                     s->table.col_count, col_names, col_types,
                     col_not_nulls, col_defaults,
                     0, "[]",
                     row_count, s->table.ddl_offset, s->table_ud);
    }

    /* Add to internal table list (store converted names) */
    {
        ODV_TABLE_ENTRY *e = &s->table_list[s->table_count];
        odv_strcpy(e->schema, conv_schema, ODV_OBJNAME_LEN);
        odv_strcpy(e->name, conv_name, ODV_OBJNAME_LEN);
        e->partition[0] = '\0';
        e->parent_partition[0] = '\0';
        e->type = TABLE_TYPE_TABLE;
        e->col_count = s->table.col_count;
        e->row_count = row_count;
        e->meta_constraint_count = 0;

        /* Detect partitioned tables: if the same schema.table already appeared
           in the table list, this is another partition of that table.
           Mark duplicates as TABLE_TYPE_PARTITION and mark the first occurrence
           retroactively as TABLE_TYPE_PARTITION_TABLE. The first occurrence
           is looked up through an index: scanning the list for every entry
           was quadratic once the list stopped being capped at 1000 (M-29). */
        {
            int k = odv_table_list_find(s, e->schema, e->name);
            if (k >= 0) {
                /* Found a previous occurrence — this table is partitioned */
                e->type = TABLE_TYPE_PARTITION;

                /* Retroactively mark the first occurrence as PARTITION_TABLE
                   (only if it hasn't been marked yet) */
                if (s->table_list[k].type == TABLE_TYPE_TABLE) {
                    s->table_list[k].type = TABLE_TYPE_PARTITION_TABLE;
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

    case COL_ROWID:
        /* ROWID (10-byte internal form) / UROWID (01 physical, 02 logical):
           Oracle's notation, not hex (M-35). */
        if (decode_oracle_rowid(v->data, v->data_len,
                                odv_strnicmp(col->type_str, "UROWID", 6) == 0,
                                decode_buf, sizeof(decode_buf)) == ODV_OK) {
            set_value_string(v, decode_buf, (int)strlen(decode_buf));
            v->type = col->type;
            break;
        }
        /* An unrecognised form is shown as hex, like RAW. */
        /* FALLTHROUGH */
    case COL_RAW:
    case COL_BLOB:
    case COL_LONG_RAW: {
        /* BLOB / LONG RAW only reach decode_column_value when read inline,
         * which happens only in the external-table format (direct path sends
         * them as chunk fragments to accumulate_lob_preview).  Latch that so an
         * inline LONG later in the pass is decoded as UTF-16BE (H-21). */
        int hi;
        char hex_buf[ODV_VARCHAR_LEN];
        int hlen = 0;
        if (col->type == COL_BLOB || col->type == COL_LONG_RAW)
            s->state.expdp_external = 1;
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
    case COL_NVARCHAR:
    case COL_CLOB:
    case COL_NCLOB: {
        /* NCHAR/NVARCHAR are always UTF-16BE; CLOB/NCLOB are UTF-16BE too but
         * only ever arrive here inline in the external-table format, so latch
         * external for a later inline LONG (H-21). */
        char conv_buf[ODV_VARCHAR_LEN];
        int conv_len = 0;
        if (col->type == COL_CLOB || col->type == COL_NCLOB)
            s->state.expdp_external = 1;
        if (v->data && v->data_len > 0 &&
            convert_charset((const char *)v->data, v->data_len,
                            CHARSET_UTF16BE,
                            conv_buf, sizeof(conv_buf),
                            CHARSET_UTF8, &conv_len) == ODV_OK) {
            set_value_string(v, conv_buf, conv_len);
        } else if (v->data && v->data_len < v->buf_size) {
            v->data[v->data_len] = '\0';
        }
        v->type = col->type;
        break;
    }

    case COL_CHAR:
    case COL_VARCHAR:
    case COL_LONG:
    default:
        if (v->data && v->data_len < v->buf_size) v->data[v->data_len] = '\0';
        if (col->type == COL_LONG && s->state.expdp_external) {
            /* External-table LONG is stored as UTF-16BE (direct-path LONG is
             * the DB charset, handled by the else branch) (H-21). */
            char conv_buf[ODV_VARCHAR_LEN];
            int conv_len = 0;
            if (v->data && v->data_len > 0 &&
                convert_charset((const char *)v->data, v->data_len,
                                CHARSET_UTF16BE,
                                conv_buf, sizeof(conv_buf),
                                CHARSET_UTF8, &conv_len) == ODV_OK) {
                set_value_string(v, conv_buf, conv_len);
            }
        } else if (s->dump_charset != s->out_charset &&
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
    ODV_PARSE_STATE *st = &s->state;
    int abs_col;    /* Absolute (display) column index in the table */
    int pos;
    ODV_VALUE *v;
    int col_type;

    if (!data || len <= 0) return;

    /* Resolve this LOB column's display index from the row-data map: the LOB-ish
     * columns are the suffix of non_lob_map after the non-LOB prefix, in the
     * same order the LOB chunk machine walks them.  A hidden in-data LOB maps to
     * ODV_MAX_COLUMNS (>= col_count) and its preview is discarded (H-18). */
    pos = st->non_lob_count + lob_col_idx;
    abs_col = (pos >= 0 && pos < st->map_count) ? st->non_lob_map[pos] : -1;
    if (abs_col < 0 || abs_col >= s->table.col_count) return;

    col_type = s->table.columns[abs_col].type;
    v = &s->record.values[abs_col];
    v->type = col_type;

    /* First chunk of this LOB column: reset the UTF-16 carry.  Clear the reused
     * value buffer too, but only when nothing has been written to this column
     * yet in the current row (data_len == 0): otherwise a column whose
     * conversion yields nothing would deliver the previous row's bytes (H-05).
     * When data_len > 0 the bytes are this row's own inline value (a multi-
     * piece 0x09 LONG whose head was read inline and whose tail now arrives as
     * a 0x06 continuation chunk), so the chunk is appended, not discarded. */
    if (st->lob_pv_fresh) {
        if (v->data_len == 0) {
            if (ensure_value_buf(v, 1) == ODV_OK) v->data[0] = '\0';
            v->data_len = 0;
        }
        st->lob_pv_carry_len = 0;
        st->lob_pv_fresh = 0;
    }
    v->is_null = 0;

    if (col_type == COL_BLOB || col_type == COL_LONG_RAW) {
        /* BLOB / LONG RAW: hex-encode (each source byte → 2 hex chars) */
        int max_src = ODV_LOB_PREVIEW_LEN / 2;  /* max source bytes */
        int already = v->data_len / 2;           /* source bytes already encoded */
        int avail = max_src - already;
        int to_encode = (len < avail) ? len : avail;
        int hi;

        if (to_encode <= 0) return;
        if (ensure_value_buf(v, v->data_len + to_encode * 2 + 1) != ODV_OK ||
            !v->data) return;

        for (hi = 0; hi < to_encode; hi++) {
            snprintf((char *)v->data + v->data_len, 3, "%02X", data[hi]);
            v->data_len += 2;
        }
        v->data[v->data_len] = '\0';
    } else if (col_type == COL_CLOB || col_type == COL_NCLOB) {
        /* CLOB/NCLOB: Oracle EXPDP stores LOB data in AL16UTF16 (UTF-16BE).
         * Convert to UTF-8, carrying any trailing incomplete UTF-16 unit or
         * unpaired high surrogate across the 4KB read-block boundary so a
         * character split by the boundary converts intact instead of being
         * dropped or turned into U+FFFD.  The conversion buffer is sized for a
         * whole block's worst-case expansion (3 bytes/unit) so a block that is
         * mostly multi-byte characters is no longer rejected wholesale.  The
         * visible preview is the first ODV_LOB_PREVIEW_LEN UTF-8 bytes, cut on
         * a character boundary (H-05). */
        unsigned char src[ODV_DUMP_BLOCK_LEN + 8];
        char conv_buf[ODV_DUMP_BLOCK_LEN * 2 + 16];
        int src_len = 0, usable, conv_len = 0;
        int avail = ODV_LOB_PREVIEW_LEN - v->data_len;

        if (avail <= 0) return;   /* preview already full for this column */

        /* Carry bytes from the previous block, then this block (clamped). */
        if (st->lob_pv_carry_len > 0 &&
            st->lob_pv_carry_len <= (int)sizeof(st->lob_pv_carry)) {
            memcpy(src, st->lob_pv_carry, st->lob_pv_carry_len);
            src_len = st->lob_pv_carry_len;
        }
        if (len > (int)sizeof(src) - src_len) len = (int)sizeof(src) - src_len;
        memcpy(src + src_len, data, len);
        src_len += len;

        /* Usable = whole UTF-16 units, excluding a trailing lone high
         * surrogate; carry the remainder (≤3 bytes) to the next block. */
        usable = src_len & ~1;
        if (usable >= 2) {
            unsigned int last = ((unsigned int)src[usable - 2] << 8) |
                                 (unsigned int)src[usable - 1];
            if (last >= 0xD800 && last <= 0xDBFF) usable -= 2;
        }
        st->lob_pv_carry_len = src_len - usable;
        if (st->lob_pv_carry_len < 0 ||
            st->lob_pv_carry_len > (int)sizeof(st->lob_pv_carry))
            st->lob_pv_carry_len = 0;   /* paranoia */
        else if (st->lob_pv_carry_len > 0)
            memcpy(st->lob_pv_carry, src + usable, st->lob_pv_carry_len);

        if (usable <= 0) return;

        if (convert_charset((const char *)src, usable, CHARSET_UTF16BE,
                            conv_buf, sizeof(conv_buf),
                            CHARSET_UTF8, &conv_len) == ODV_OK && conv_len > 0) {
            int to_copy = (conv_len < avail) ? conv_len : avail;
            /* Never cut inside a UTF-8 sequence: if the cut lands on a
             * continuation byte (10xxxxxx) back up to the lead byte. */
            if (to_copy < conv_len) {
                while (to_copy > 0 &&
                       ((unsigned char)conv_buf[to_copy] & 0xC0) == 0x80)
                    to_copy--;
            }
            if (to_copy > 0) {
                if (ensure_value_buf(v, v->data_len + to_copy + 1) != ODV_OK ||
                    !v->data) return;
                memcpy(v->data + v->data_len, conv_buf, to_copy);
                v->data_len += to_copy;
                v->data[v->data_len] = '\0';
            }
        }
    } else {
        /* LONG (direct path): copy text directly (stored in DB charset) */
        int avail = ODV_LOB_PREVIEW_LEN - v->data_len;
        int to_copy = (len < avail) ? len : avail;

        if (to_copy <= 0) return;
        if (ensure_value_buf(v, v->data_len + to_copy + 1) != ODV_OK ||
            !v->data) return;

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
/*---------------------------------------------------------------------------
    finish_fragment

    Called from parse_expdp_records once every column stored in the current
    record fragment (its CC columns) has been read.  Three outcomes:

      * LOB record (0x08 / 0x09) that is not the last fragment: the non-LOB
        columns are done and LOB chunk data follows, so hand off to the LOB
        state machine (DS_LOB_MARKER).
      * Not the last fragment of a >255-column row: keep the record and expect
        a continuation fragment header back at step 1 (seg_remaining = -2).
      * Last fragment (L bit set): any columns Oracle omitted as trailing NULLs
        stay NULL (reset_record cleared the record at the start of the row), so
        deliver the row and return to step 1 for the next record.

    record_count is the caller's local row counter for this table.
 ---------------------------------------------------------------------------*/
/*---------------------------------------------------------------------------
    expdp_block_prefix_ok

    A table-definition metadata block is "<?xml" at block offset 2, preceded
    by a 2-byte prefix that is the dump's DB charset id (e.g. 0x0369 for
    AL32UTF8, 0x00b2 for WE8MSWIN1252).  The value is constant within a dump
    but differs between dumps, so it must not be hard-coded.  It is captured
    from the first table-definition block encountered and required to match
    thereafter.  A row value that merely contains "<?xml" at a 4KB boundary
    carries a different leading pair (a column-length marker, not the charset
    id) and is rejected, which is what keeps it from truncating the table and
    dropping every table after it (H-23).

    `pre` is the 2 bytes immediately before the '<'.  When the prefix has not
    been captured yet, the first caller captures it and succeeds; later callers
    must match.  During row parsing the prefix is always already set (the
    table's own definition was read first), so an unset prefix there means the
    bytes are not a genuine block boundary and the match fails.
 ---------------------------------------------------------------------------*/
static int expdp_block_prefix_ok(ODV_SESSION *s, const unsigned char *pre,
                                 int capture)
{
    int prefix = ((int)pre[0] << 8) | (int)pre[1];
    if (s->expdp_xml_prefix < 0) {
        if (!capture) return 0;
        s->expdp_xml_prefix = prefix;
        return 1;
    }
    return prefix == s->expdp_xml_prefix;
}

static int finish_fragment(ODV_SESSION *s, ODV_VFILE *fp, int list_only,
                           int *record_count)
{
    ODV_PARSE_STATE *st = &s->state;
    int rc;
    int is_last = (st->record_header & 0x04) != 0;   /* L bit */

    s->record.col_count = s->table.col_count;

    /* 0x08 / 0x09: real LOB chunk fragments follow the non-LOB columns. */
    if (st->frag_lob_count > 0 && st->is_lob_record && !is_last) {
        st->data_step   = DS_LOB_MARKER;
        st->lob_col_idx = 0;
        st->is_end_lob  = 0;
        st->lob_length  = 0;
        return ODV_OK;
    }

    /* Not the last fragment of a wide (>255-column) row: the remaining
     * columns arrive in a continuation fragment. */
    if (!is_last && st->is_over255) {
        st->step          = 1;
        st->seg_remaining = -2;   /* expect a continuation fragment header */
        return ODV_OK;
    }

    /* Last fragment: deliver the row (trailing unset columns stay NULL). */
    if (!list_only) {
        rc = deliver_row(s);
        if (rc != ODV_OK) return rc;
        odv_report_progress(s, fp);
    }
    (*record_count)++;
    s->table.record_count++;
    st->step          = 1;
    st->data_step     = DS_COL_LENGTH;
    st->seg_remaining = -1;
    return ODV_OK;
}

static int parse_expdp_records(ODV_SESSION *s, ODV_VFILE *fp, int64_t *address,
                               int list_only)
{
    ODV_PARSE_STATE *st = &s->state;
    unsigned char b;
    int chunk_size = 0;
    int record_count = 0;
    int progress_counter = 0;
    int rc;

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
    st->lob_pv_carry_len = 0;
    st->lob_pv_fresh     = 0;
    st->expdp_external   = 0;   /* per-table; set on an inline real-LOB (H-21) */

    /* Build the row's data-stream → display column map from the table's row-
     * data layout (table.data_*, built in SEGCOL order at DDL parse): the
     * non-LOB columns first (the inline prefix, in SEGCOL order), then the
     * LOB-ish columns (BLOB/CLOB/NCLOB/LONG/LONG RAW) in SEGCOL order.  In
     * direct path only the non-LOB prefix is read inline and the real LOB data
     * arrives as separate 08/09 chunk fragments; in the external-table format
     * every column — LOBs included — is inline in exactly this order (H-21).
     * Each entry is the destination display column index, or ODV_MAX_COLUMNS
     * (always >= col_count, so the read loop reads but stores nothing) for a
     * hidden in-data column whose bytes are discarded — object OID, ADT
     * attribute, nested-table SETID, invisible column (H-18).  For a table
     * with no virtual/hidden columns this reproduces the old table-order map. */
    {
        int mi, mc = 0;
        for (mi = 0; mi < s->table.data_count && mc < ODV_MAX_COLUMNS; mi++) {
            int t = s->table.data_type[mi];
            if (t != COL_BLOB && t != COL_CLOB && t != COL_NCLOB &&
                t != COL_LONG && t != COL_LONG_RAW) {
                int d = s->table.data_disp[mi];
                st->non_lob_map[mc++] = (d >= 0) ? d : ODV_MAX_COLUMNS;
            }
        }
        st->non_lob_count = mc;
        for (mi = 0; mi < s->table.data_count && mc < ODV_MAX_COLUMNS; mi++) {
            int t = s->table.data_type[mi];
            if (t == COL_BLOB || t == COL_CLOB || t == COL_NCLOB ||
                t == COL_LONG || t == COL_LONG_RAW) {
                int d = s->table.data_disp[mi];
                st->non_lob_map[mc++] = (d >= 0) ? d : ODV_MAX_COLUMNS;
            }
        }
        st->map_count = mc;
        /* Columns the direct-path LOB chunk machine walks (the appended
         * suffix): BLOB/CLOB/NCLOB/LONG/LONG RAW.  table.lob_col_count is not
         * usable here because it omits plain LONG and includes inline
         * BFILE/object columns (H-06). */
        st->frag_lob_count = st->map_count - st->non_lob_count;
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
        /* Read one byte */
        if (odv_vread(&b, 1, 1, fp) != 1) break;
        (*address)++;

        /* Safety net: a table-definition block begins at a 4KB boundary with
           the two constant bytes 0x03 0x69 followed by "<?xml" at offset 2.
           If record parsing ever runs into such a block, stop so the caller
           can re-sync.  The "03 69" prefix is required so that a column value
           that merely contains "<?xml" at a block boundary (which lands at
           offset 2 about once per 4096 values) is not mistaken for a new
           table definition and does not truncate the table (see H-23). */
        if (b == '<' && (*address % ODV_DUMP_BLOCK_LEN) == 3) {
            unsigned char peek[4];
            if (odv_vread(peek, 1, 4, fp) == 4) {
                int looks_xml = (memcmp(peek, "?xml", 4) == 0);
                odv_vseek(fp, -4, SEEK_CUR);          /* back to just past '<' */
                if (looks_xml) {
                    unsigned char pre[2];
                    int64_t here = *address;          /* file pos just past '<' */
                    int is_block_prefix = 0;
                    odv_vseek(fp, here - 3, SEEK_SET);
                    if (odv_vread(pre, 1, 2, fp) == 2 &&
                        expdp_block_prefix_ok(s, pre, 0))
                        is_block_prefix = 1;
                    odv_vseek(fp, here, SEEK_SET);     /* restore */
                    if (is_block_prefix) {
                        odv_vseek(fp, here - 3, SEEK_SET);
                        *address = here - 3;
                        break;
                    }
                }
            }
        }

        /* Throttled progress reporting */
        if (++progress_counter >= 1000) {
            progress_counter = 0;
            odv_report_progress(s, fp);
        }

        /* ===== step 1: Expect a record-fragment header ===== */
        if (st->step == 1) {
            /* A row is made of one or more fragments.  Each fragment header is
             *     flag [HH LL] CC
             * where HH LL (big-endian) is present only when (flag & 0x10) and
             * is the fragment's total byte length, and CC is the number of
             * columns stored in this fragment (Oracle omits trailing NULLs).
             * Flag bits: 0x08=first(F) 0x04=last(L) 0x01=more(N) 0x10=has-len.
             *   3c / 1c    : single length-prefixed fragment (F|L)
             *   18/10/14   : >255-column row, split across fragments
             *   0c         : single short fragment (F|L), no length
             *   08 / 09    : LOB record (non-LOB columns here, LOB data after)
             * seg_remaining == -2 marks "mid-row, expecting a continuation
             * fragment" so the record is not reset between split fragments. */
            int is_cont = (st->seg_remaining == -2);

            if (b == 0x00) {
                /* End-of-table padding between records, or leading padding. */
                if (st->is_between_record && !is_cont)
                    return ODV_OK;
                /* leading padding — skip and keep scanning */
            } else if (b == 0xff) {
                /* End of this table's row data. */
                return ODV_OK;
            } else {
                /* b is a fragment-header flag byte. */
                unsigned char cc;

                st->record_header = (int)b;

                /* Consume the 2-byte big-endian length when present.  Row
                 * completion is driven by the column count CC, so the byte
                 * length itself only needs to be skipped. */
                if (b & 0x10) {
                    unsigned char lenb[2];
                    if (odv_vread(lenb, 1, 2, fp) != 2) return ODV_OK;
                    (*address) += 2;
                    (void)lenb;
                }

                /* Column count for this fragment. */
                if (odv_vread(&cc, 1, 1, fp) != 1) return ODV_OK;
                (*address)++;
                st->seg_remaining = (int)cc;

                if (!is_cont) {
                    /* Start of a new record. */
                    reset_record(&s->record);
                    st->col_idx       = 0;
                    st->lob_col_idx   = 0;
                    st->is_lob_record = (b == 0x08 || b == 0x09 || b == 0x0c);
                    st->is_over255    = (b == 0x18 || b == 0x19);
                    st->is_end_lob    = 0;
                    st->lob_length    = 0;
                    st->is_between_record = 1;
                }
                st->step      = 2;
                st->data_step = DS_COL_LENGTH;

                /* A fragment that stores zero columns completes at once. */
                if (st->seg_remaining <= 0) {
                    rc = finish_fragment(s, fp, list_only, &record_count);
                    if (rc != ODV_OK) return rc;
                }
            }

        /* ===== step 2: Reading record data ===== */
        } else if (st->step == 2) {

            switch (st->data_step) {

            /* --- Normal column reading states --- */

            case DS_COL_LENGTH: /* 0: Column length marker */
            {
                /* Map non-LOB sequential index to absolute column index */
                int ac = (st->col_idx < st->map_count)
                         ? st->non_lob_map[st->col_idx]
                         : st->col_idx;
                int col_done = 0;

                if (b == 0xff) {
                    /* NULL */
                    if (ac < s->table.col_count) {
                        set_value_null(&s->record.values[ac]);
                        s->record.values[ac].type =
                            s->table.columns[ac].type;
                    }
                    st->col_idx++;
                    col_done = 1;
                } else if (b == 0xfe) {
                    /* 2-byte length follows (big-endian) */
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
                    col_done = 1;
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

                /* A NULL/empty column completes here; a fixed- or 2-byte
                 * length column completes in DS_COL_DATA.  When this
                 * fragment's CC columns are all read, close out the
                 * fragment (deliver, continue to next fragment, or hand
                 * off to the LOB state machine). */
                if (col_done) {
                    st->seg_remaining--;
                    if (st->seg_remaining <= 0) {
                        rc = finish_fragment(s, fp, list_only, &record_count);
                        if (rc != ODV_OK) return rc;
                    }
                }
                break;
            }

            case DS_COL_LEN_HI: /* 1: High byte of 2-byte length (big-endian) */
                st->col_len = (int)b << 8;
                st->data_step = DS_COL_LEN_LO;
                break;

            case DS_COL_LEN_LO: /* 2: Low byte of 2-byte length (big-endian) */
            {
                int ac = (st->col_idx < st->map_count)
                         ? st->non_lob_map[st->col_idx]
                         : st->col_idx;
                st->col_len |= (int)b;
                if (st->col_len <= 0) {
                    /* Degenerate zero-length value — treat as empty. */
                    if (ac < s->table.col_count) {
                        set_value_string(&s->record.values[ac], "", 0);
                        s->record.values[ac].type =
                            s->table.columns[ac].type;
                    }
                    st->col_idx++;
                    st->data_step = DS_COL_LENGTH;
                    st->seg_remaining--;
                    if (st->seg_remaining <= 0) {
                        rc = finish_fragment(s, fp, list_only, &record_count);
                        if (rc != ODV_OK) return rc;
                    }
                    break;
                }
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
                int ac = (st->col_idx < st->map_count)
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
                    /* Column complete — decode.
                       In list_only the decoded value is never delivered to the
                       caller, so skip the NUMBER/DATE/charset conversion work:
                       listing a large dump only needs the row count. */
                    if (!list_only)
                        decode_column_value(s, ac);
                    st->col_idx++;
                    st->data_step = DS_COL_LENGTH;

                    /* Fragment column count reached? Close out the fragment. */
                    st->seg_remaining--;
                    if (st->seg_remaining <= 0) {
                        rc = finish_fragment(s, fp, list_only, &record_count);
                        if (rc != ODV_OK) return rc;
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

                        if (st->lob_col_idx >= st->frag_lob_count) {
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
                    /* A new record-fragment header appeared in the LOB marker
                     * position: the current record's LOB data is finished.
                     * Deliver the current row, then push the header byte back
                     * so step 1 parses the next record uniformly (correct
                     * column count and big-endian column lengths). */
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

                    odv_vseek(fp, -1, SEEK_CUR);   /* unread the header byte */
                    (*address)--;
                    st->step          = 1;
                    st->data_step     = DS_COL_LENGTH;
                    st->seg_remaining = -1;
                    break;

                case 0xfe:
                    /* 2-byte chunk length follows */
                    st->data_step = DS_LOB_FE_NEXT;
                    break;

                case 0xff:
                    /* NULL LOB column.  Resolve its display index from the
                     * row-data map, as accumulate_lob_preview does (H-18). */
                    {
                        int pos = st->non_lob_count + st->lob_col_idx;
                        int abs_idx = (pos < st->map_count)
                                      ? st->non_lob_map[pos] : -1;
                        if (abs_idx >= 0 && abs_idx < s->table.col_count)
                            set_value_null(&s->record.values[abs_idx]);
                    }
                    st->lob_col_idx++;
                    st->lob_length = 0;

                    /* Check if all LOB columns are done */
                    if (st->lob_col_idx >= st->frag_lob_count) {
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

                        if (st->lob_col_idx >= st->frag_lob_count) {
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

                    if (st->lob_col_idx >= st->frag_lob_count) {
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

                /* First chunk of this LOB column (lob_length still 0): tell the
                 * preview accumulator to clear the reused value buffer and the
                 * UTF-16 carry before appending (H-05). */
                if (st->lob_length == 0) st->lob_pv_fresh = 1;

                /* Bulk-read chunk_size bytes */
                while (lob_read < chunk_size && !s->cancelled) {
                    int need = chunk_size - lob_read;
                    int blk  = (need < (int)sizeof(lob_tmp))
                                ? need : (int)sizeof(lob_tmp);
                    int got  = (int)odv_vread(lob_tmp, 1, blk, fp);
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
                if (odv_vread(next_buf, 2, 1, fp) != 1) {
                    /* EOF — treat as last chunk */
                    st->is_last_chunk = 1;
                } else {
                    odv_vseek(fp, -2, SEEK_CUR);
                    switch (next_buf[0]) {
                    case 0xfe: case 0xff:
                        st->is_last_chunk = 1;
                        break;
                    case 0x01: case 0x04: case 0x08: case 0x09: case 0x0c:
                        /* 0x04 is the flag byte (L bit) that opens the row's
                         * LAST LOB-column fragment, so the chunk just read is
                         * the final chunk of the CURRENT column.  Treating it
                         * as a continuation merged the last LOB column's data
                         * onto the previous LOB column and left the real column
                         * empty when a row had two or more LOBs, or a LOB plus
                         * a LONG RAW (H-06). */
                        st->is_last_chunk = 1;
                        break;
                    case 0x02: case 0x03:
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
    scan_master_metadata

    Post-parse scan of the master table area for information that is not in
    the per-table XML DDL:

      - SCHEMA_EXPORT/TABLE/TABLE_DATA            → partition name
      - SCHEMA_EXPORT/TABLE/CONSTRAINT/CONSTRAINT → PK / UNIQUE / CHECK name
      - SCHEMA_EXPORT/TABLE/CONSTRAINT/REF_CONSTRAINT → FK name
      - SCHEMA_EXPORT/TABLE/INDEX/INDEX           → index name

    These four markers used to be searched in four independent passes over the
    whole dump file (one for partitions, then one per constraint path), so a
    plain table listing read a large dump five times end to end.  They are all
    plain byte-pattern searches, so a single pass finds every one of them.

    Ordering is preserved: constraint/index matches are staged in a growable
    array and only written into table_list afterwards, grouped by marker in the
    original path order.  That matters because meta_constraints[] is capped at
    ODV_MAX_META_CONSTRAINTS — appending them in file order instead would drop
    a different subset than before on tables that exceed the cap.
 ---------------------------------------------------------------------------*/
typedef struct {
    int  table_index;                   /* index into s->table_list */
    int  path_index;                    /* 0=CONSTRAINT, 1=REF_CONSTRAINT, 2=INDEX */
    int  type;                          /* CONSTRAINT_PK / _FK / _INDEX */
    char name[ODV_OBJNAME_LEN + 1];
} ODV_META_MATCH;

/* Marker table. Order defines the order meta_constraints[] is filled in. */
static const struct {
    const char *path;
    int         path_len;
    int         constraint_type;
} odv_meta_paths[] = {
    { "SCHEMA_EXPORT/TABLE/CONSTRAINT/CONSTRAINT",     41, CONSTRAINT_PK },     /* PK/UNIQUE/CHECK — type refined later */
    { "SCHEMA_EXPORT/TABLE/CONSTRAINT/REF_CONSTRAINT", 45, CONSTRAINT_FK },
    { "SCHEMA_EXPORT/TABLE/INDEX/INDEX\xff",           31, CONSTRAINT_INDEX }
};
#define ODV_META_PATH_COUNT 3

/* One SCHEMA_EXPORT/TABLE/TABLE_DATA marker: the table it belongs to and the
   partition name it carries (empty when none was extractable).  Markers are
   collected across the whole master-table area first, then grouped by table so
   a table's partition names can be assigned in the order expdp writes the data
   rather than the scrambled order the markers appear in (H-15). */
typedef struct {
    char schema[260];   /* holds convert_name output (may expand past 128) */
    char table[260];
    char part[ODV_OBJNAME_LEN + 1];
} ODV_PART_MARK;

/* Bytes re-scanned at each block boundary so a marker (and the ~230-byte
   structure it reads after itself) straddling an 8KB read is never skipped.
   A dropped marker shifted every later partition name of its table by one
   (cascading), which was the dominant cause of wrong partition names (H-15). */
#define ODV_META_SCAN_OVERLAP  512

static void scan_master_metadata(ODV_SESSION *s)
{
    static const char path_marker[] = "SCHEMA_EXPORT/TABLE/TABLE_DATA";
    static const int  path_len = 30;

    /* part_occ[i] = which occurrence (0-based) of this schema.table the
       table_list entry i is (e.g. 4 partitions → occ 0,1,2,3).  Table_list
       entries (one per partition that has data) are in the order expdp wrote
       the data, which is the partitions sorted by name. */
    int *part_occ = NULL;

    ODV_META_MATCH *matches = NULL;
    int match_count = 0, match_alloc = 0;

    /* Collected partition-name markers (grouped and assigned after the scan). */
    ODV_PART_MARK *pmarks = NULL;
    int pmark_count = 0, pmark_alloc = 0;

    ODV_VFILE vsf;
    ODV_VFILE *sfp = &vsf;
    int sfp_open = 0;
    unsigned char blk[8192];
    int carry = 0;
    int ti, k, mi, pass;

    if (s->table_count <= 0) return;

    part_occ = (int *)malloc((size_t)s->table_count * sizeof(int));
    if (!part_occ) goto scan_cleanup;

    for (ti = 0; ti < s->table_count; ti++) {
        part_occ[ti] = 0;
        for (k = 0; k < ti; k++) {
            if (strcmp(s->table_list[k].schema, s->table_list[ti].schema) == 0 &&
                strcmp(s->table_list[k].name,   s->table_list[ti].name)   == 0) {
                part_occ[ti]++;
            }
        }
    }

    if (odv_vopen(&vsf, s) != ODV_OK) goto scan_cleanup;
    sfp_open = 1;

    for (;;) {
        int got = (int)odv_vread(blk + carry, 1, sizeof(blk) - carry, sfp);
        int avail = carry + got;
        int at_eof = (got < (int)sizeof(blk) - carry);
        int scan_end = at_eof ? avail : (avail - ODV_META_SCAN_OVERLAP);
        int si;

        if (s->cancelled || avail <= 0) break;
        if (scan_end < 0) scan_end = 0;

        /* Scan every offset up to scan_end; a marker there can still read its
           whole structure within [si, avail).  Markers past scan_end are
           re-examined in the next window via the carried-over tail. */
        for (si = 0; si < scan_end; si++) {

            /* ---- SCHEMA_EXPORT/TABLE/TABLE_DATA → partition name ---- */
            if (si <= avail - path_len - 50 &&
                memcmp(blk + si, path_marker, path_len) == 0) {
                /* Structure after the path:
                 * [ff]...[len]TABLE[len]SCHEMA[ff][02]...[ff][len]PART[ff] */
                int p = si + path_len;
                char tn[64] = {0}, sn[64] = {0}, pn[64] = {0};
                int tl, sl;

                while (p < avail && blk[p] == 0xff) p++;
                if (p + 3 >= avail) continue;

                tl = blk[p++];
                if (tl <= 0 || tl > 60 || p + tl + 3 >= avail) continue;
                memcpy(tn, blk + p, tl); p += tl;

                sl = blk[p++];
                if (sl <= 0 || sl > 60 || p + sl >= avail) continue;
                memcpy(sn, blk + p, sl); p += sl;

                /* After schema: skip binary pattern [ff][02][c1][02][ff][ff][ff]
                 * and find partition name [len]NAME (printable ASCII).
                 * Scan forward looking for a valid len+name pattern. */
                {
                    int limit = ODV_MIN(p + 20, avail - 2);
                    while (p < limit && !pn[0]) {
                        int pl = blk[p];
                        if (pl >= 4 && pl <= 60 && p + 1 + pl <= avail) {
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

                /* Collect the marker (table/schema converted; the partition name
                 * is printable ASCII and needs no conversion).  Names are matched
                 * to entries only after every marker is in, so the scrambled
                 * marker order no longer matters (H-15). */
                {
                    char ct[260], cs2[260];
                    convert_name(tn, s->dump_charset, s->out_charset, ct, sizeof(ct));
                    convert_name(sn, s->dump_charset, s->out_charset, cs2, sizeof(cs2));

                    if (pmark_count >= pmark_alloc) {
                        int new_alloc = pmark_alloc ? pmark_alloc * 2 : 256;
                        ODV_PART_MARK *np = (ODV_PART_MARK *)realloc(
                            pmarks, (size_t)new_alloc * sizeof(ODV_PART_MARK));
                        if (!np) goto scan_apply;   /* keep what we have */
                        pmarks = np;
                        pmark_alloc = new_alloc;
                    }
                    odv_strcpy(pmarks[pmark_count].schema, cs2, 259);
                    odv_strcpy(pmarks[pmark_count].table,  ct,  259);
                    odv_strcpy(pmarks[pmark_count].part,   pn,  ODV_OBJNAME_LEN);
                    pmark_count++;
                }
                continue;   /* markers cannot overlap */
            }

            /* ---- constraint / index markers ---- */
            for (mi = 0; mi < ODV_META_PATH_COUNT; mi++) {
                int mlen = odv_meta_paths[mi].path_len;
                int p, tl, sl, cl;
                char tn[64] = {0}, sn[64] = {0}, cn[64] = {0};

                if (si > avail - mlen - 30) continue;
                if (memcmp(blk + si, odv_meta_paths[mi].path, mlen) != 0) continue;

                p = si + mlen;
                while (p < avail && blk[p] == 0xff) p++;
                if (p + 4 >= avail) break;

                /* [len]TABLE_NAME */
                tl = blk[p++];
                if (tl <= 0 || tl > 60 || p + tl + 3 >= avail) break;
                memcpy(tn, blk + p, tl); p += tl;

                /* [len]SCHEMA */
                sl = blk[p++];
                if (sl <= 0 || sl > 60 || p + sl + 2 >= avail) break;
                memcpy(sn, blk + p, sl); p += sl;

                /* [len]CONSTRAINT/INDEX_NAME */
                cl = blk[p++];
                if (cl <= 0 || cl > 60 || p + cl > avail) break;
                memcpy(cn, blk + p, cl); p += cl;

                /* Validate: all printable */
                {
                    int valid = 1, q;
                    for (q = 0; q < tl; q++) if (tn[q] < 0x20) { valid = 0; break; }
                    if (!valid) break;
                    for (q = 0; q < cl; q++) if (cn[q] < 0x20) { valid = 0; break; }
                    if (!valid) break;
                }

                {
                    char ct[260], cs2[260], cc[260];

                    convert_name(tn, s->dump_charset, s->out_charset, ct, sizeof(ct));
                    convert_name(sn, s->dump_charset, s->out_charset, cs2, sizeof(cs2));
                    convert_name(cn, s->dump_charset, s->out_charset, cc, sizeof(cc));

                    /* Find matching table (first occurrence for partitioned
                     * tables) and stage the name for later insertion. */
                    for (ti = 0; ti < s->table_count; ti++) {
                        if (strcmp(s->table_list[ti].schema, cs2) != 0) continue;
                        if (strcmp(s->table_list[ti].name,   ct)  != 0) continue;

                        if (match_count >= match_alloc) {
                            int new_alloc = match_alloc ? match_alloc * 2 : 256;
                            ODV_META_MATCH *nm = (ODV_META_MATCH *)realloc(
                                matches, (size_t)new_alloc * sizeof(ODV_META_MATCH));
                            if (!nm) goto scan_apply;   /* keep what we have */
                            matches = nm;
                            match_alloc = new_alloc;
                        }
                        matches[match_count].table_index = ti;
                        matches[match_count].path_index  = mi;
                        matches[match_count].type        = odv_meta_paths[mi].constraint_type;
                        odv_strcpy(matches[match_count].name, cc, ODV_OBJNAME_LEN);
                        match_count++;
                        break;
                    }
                }
                break;      /* one marker can only match one path */
            }
        }

        if (at_eof) break;
        /* Carry the tail forward so a marker straddling this read boundary is
         * seen whole next window; scan_end excluded it here, so nothing is
         * scanned twice. */
        memmove(blk, blk + avail - ODV_META_SCAN_OVERLAP, ODV_META_SCAN_OVERLAP);
        carry = ODV_META_SCAN_OVERLAP;
    }

scan_apply:
    /* Fill meta_constraints[] grouped by marker, in odv_meta_paths order, so
       the result is byte-for-byte what the old four-pass scan produced. */
    for (pass = 0; pass < ODV_META_PATH_COUNT; pass++) {
        for (k = 0; k < match_count; k++) {
            ODV_TABLE_ENTRY *te;
            ODV_CONSTRAINT_NAME *mc;

            if (matches[k].path_index != pass) continue;

            te = &s->table_list[matches[k].table_index];
            if (te->meta_constraint_count >= ODV_MAX_META_CONSTRAINTS) continue;

            mc = &te->meta_constraints[te->meta_constraint_count];
            odv_strcpy(mc->name, matches[k].name, ODV_OBJNAME_LEN);
            mc->type = matches[k].type;
            te->meta_constraint_count++;
        }
    }

    /* Assign partition names.  A table's markers are matched to its data
     * entries only when it is safe: every marker carries a name, the names are
     * distinct (a composite table repeats the parent name once per subpartition,
     * and the subpartition name is not in the marker — left empty), and the
     * marker count equals the entry count (a 0-row partition has a marker but no
     * entry — left empty).  When safe, the names are sorted and handed out in
     * that order, because expdp writes a table's partition data sorted by
     * partition name, i.e. in part_occ order.  Leaving a name empty (L-01) is
     * deliberately preferred over a wrong name, which would misattribute the
     * entry's rows to another partition (H-15). */
    {
        int *idx = (int *)malloc((size_t)(pmark_count > 0 ? pmark_count : 1) *
                                 sizeof(int));
        if (idx) {
            for (k = 0; k < pmark_count; k++) {
                int cnt = 0, entry_cnt = 0, i, a, b, ok = 1, seen = 0;

                for (i = 0; i < k; i++)
                    if (strcmp(pmarks[i].schema, pmarks[k].schema) == 0 &&
                        strcmp(pmarks[i].table,  pmarks[k].table)  == 0) { seen = 1; break; }
                if (seen) continue;   /* table already handled */

                for (i = 0; i < pmark_count; i++)
                    if (strcmp(pmarks[i].schema, pmarks[k].schema) == 0 &&
                        strcmp(pmarks[i].table,  pmarks[k].table)  == 0) {
                        if (pmarks[i].part[0] == '\0') ok = 0;
                        idx[cnt++] = i;
                    }
                for (ti = 0; ti < s->table_count; ti++)
                    if (strcmp(s->table_list[ti].schema, pmarks[k].schema) == 0 &&
                        strcmp(s->table_list[ti].name,   pmarks[k].table)  == 0)
                        entry_cnt++;

                if (!ok || entry_cnt < 2 || cnt != entry_cnt) continue;

                /* insertion-sort idx[] by partition name */
                for (a = 1; a < cnt; a++) {
                    int t = idx[a];
                    b = a - 1;
                    while (b >= 0 &&
                           strcmp(pmarks[idx[b]].part, pmarks[t].part) > 0) {
                        idx[b + 1] = idx[b]; b--;
                    }
                    idx[b + 1] = t;
                }
                for (a = 1; a < cnt && ok; a++)
                    if (strcmp(pmarks[idx[a - 1]].part, pmarks[idx[a]].part) == 0)
                        ok = 0;   /* duplicate name → composite → leave empty */
                if (!ok) continue;

                /* entry with part_occ==i is the i-th partition in data order
                 * (= sorted by name), so it gets the i-th sorted name. */
                for (i = 0; i < cnt; i++)
                    for (ti = 0; ti < s->table_count; ti++)
                        if (part_occ[ti] == i &&
                            strcmp(s->table_list[ti].schema, pmarks[k].schema) == 0 &&
                            strcmp(s->table_list[ti].name,   pmarks[k].table)  == 0) {
                            odv_strcpy(s->table_list[ti].partition,
                                       pmarks[idx[i]].part, ODV_OBJNAME_LEN);
                            break;
                        }
            }
            free(idx);
        }
    }

scan_cleanup:
    if (sfp_open) odv_vclose(&vsf);
    free(matches);
    free(pmarks);
    free(part_occ);
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
    ODV_VFILE vf;
    ODV_VFILE *fp = &vf;
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

    /* Open the dump as one logical stream (joins dumpfile=%U split volumes). */
    rc = odv_vopen(&vf, s);
    if (rc != ODV_OK) {
        if (s->last_error[0] == '\0')
            snprintf(s->last_error, ODV_MSG_LEN, "Cannot open: %s", s->dump_path);
        return rc;
    }

    /* Reset progress tracking */
    s->last_progress_pct = -1;

    /* Allocate DDL accumulation buffer (1MB) */
    ddl_alloc = ODV_DDL_BUF_LEN;
    ddl_buf = (char *)malloc(ddl_alloc);
    if (!ddl_buf) {
        odv_vclose(&vf);
        return ODV_ERROR_MALLOC;
    }

    s->table_count = 0;
    s->total_rows = 0;
    s->expdp_xml_prefix = -1;   /* captured from the first table-def block */

    /* Fast seek: if seek_offset is set (from previous list_tables),
       jump directly to the target DDL position instead of scanning from top.
       Align to block boundary because the main loop reads full blocks and
       checks block[2] for "<?xml".  ddl_offset stores the <?xml position
       (= block_start + 2), so we round down to the enclosing block. */
    if (s->seek_offset > 0 && s->filter_active) {
        int64_t aligned = (s->seek_offset / ODV_DUMP_BLOCK_LEN) * ODV_DUMP_BLOCK_LEN;
        odv_vseek(fp, aligned, SEEK_SET);
        address = aligned;
    }

    /* Read blocks sequentially */
    while (!s->cancelled) {
        n = (int)odv_vread(block, 1, ODV_DUMP_BLOCK_LEN, fp);
        if (n <= 0) break;

        /* Report progress during DDL scan so UI stays responsive */
        odv_report_progress(s, fp);

        /* Check for XML DDL marker at block offset 2 (per EXPDP format).
           Offset 0 blocks contain statistics/metadata, not table DDL.
           A table DDL block begins with the dump's 2-byte charset-id prefix
           then "<?xml" at offset 2.  Matching that prefix (captured from the
           first such block) keeps a row value that merely contains "<?xml" at
           a 4KB boundary from being mistaken for a table definition (H-23). */
        int xml_pos = (n >= 7 && memcmp(block + 2, "<?xml", 5) == 0 &&
                       expdp_block_prefix_ok(s, block, 1)) ? 2 : -1;

        if (xml_pos >= 0 && !in_ddl) {
            /* Start of XML DDL block - record file position for caching */
            cur_ddl_pos = odv_vtell(fp) - n + xml_pos;
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
                odv_vseek(fp, cur_ddl_pos + end_pos, SEEK_SET);
                address = odv_vtell(fp);

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
                            if (odv_stricmp(s->table.name, ft) != 0)
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
                            if (odv_stricmp(s->table.schema, fs) != 0)
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
                    int64_t cur = odv_vtell(fp);
                    int64_t rem = cur % ODV_DUMP_BLOCK_LEN;
                    if (rem != 0)
                        odv_vseek(fp, cur + (ODV_DUMP_BLOCK_LEN - rem), SEEK_SET);
                }
                /* Then scan block-by-block until we find <?xml or EOF.
                   Require the dump's charset-id block prefix so a "<?xml" that
                   is really row-data text is not taken for a table (H-23). */
                while (!s->cancelled) {
                    int nr = (int)odv_vread(skip_blk, 1, ODV_DUMP_BLOCK_LEN, fp);
                    if (nr < 7) break;  /* EOF */
                    if (memcmp(skip_blk + 2, "<?xml", 5) == 0 &&
                        expdp_block_prefix_ok(s, skip_blk, 1)) {
                        /* Found next DDL block — seek back so main loop reads it */
                        odv_vseek(fp, odv_vtell(fp) - nr, SEEK_SET);
                        break;
                    }
                }

                in_ddl = 0;
                ddl_len = 0;
            }
        }

        address = odv_vtell(fp);
    }

expdp_done:
    /* Post-parse: pull partition names, constraint names and index names out
     * of the master table area. All four byte markers are found in one pass
     * over the file (see scan_master_metadata). */
    if (list_only)
        scan_master_metadata(s);

    free(ddl_buf);
    odv_vclose(&vf);

    if (s->cancelled) return ODV_ERROR_CANCELLED;
    return ODV_OK;
}
