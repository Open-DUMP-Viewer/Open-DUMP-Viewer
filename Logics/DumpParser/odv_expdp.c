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
    int  col_has_charlen;   /* <CHARLENGTH> present */
    int  col_spare3;        /* TABLE_T <SPARE3>: the character length / VECTOR
                               dimension count (TABLE_T has no CHARLENGTH) */
    int  col_charset_form;  /* <CHARSETFORM>: 2 = the national character set */
    /* The column's type (TYPEMD): a VARRAY column is shown with its type name
     * "OWNER"."TYPE" (M-36). */
    char col_type_owner[ODV_OBJNAME_LEN + 1];
    char col_type_name[ODV_OBJNAME_LEN + 1];
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
    case 119: return COL_JSON;          /* JSON (OSON), 21c+ (M-36) */
    case 121: return COL_RAW;           /* ROWID-like (internal) */
    case 122: return COL_RAW;
    case 123: return COL_VARRAY;        /* VARRAY: written after the inline
                                           columns, like a LOB (M-36) */
    case 127: return COL_VECTOR;        /* VECTOR, 23ai (M-36) */
    case 180: return COL_TIMESTAMP;
    case 181: return COL_TIMESTAMP_TZ;
    case 182: return COL_INTERVAL_YM;
    case 183: return COL_INTERVAL_DS;
    case 208: return COL_ROWID;         /* UROWID */
    case 231: return COL_TIMESTAMP_LTZ;
    case 252: return COL_BOOLEAN;       /* BOOLEAN, 23ai (M-36) */
    default:  return COL_VARCHAR;       /* fallback for unknown types */
    }
}

/*---------------------------------------------------------------------------
    Columns that expdp writes AFTER the inline (non-LOB) columns of a row, in
    SEGCOL order (each in a row piece of its own in direct path, inline at the
    end of the row in the external-table format): the LOBs, LONG / LONG RAW,
    and the LOB-stored JSON / VECTOR / VARRAY (M-36).
 ---------------------------------------------------------------------------*/
static int is_tail_type(int t)
{
    return t == COL_BLOB || t == COL_CLOB || t == COL_NCLOB ||
           t == COL_LONG || t == COL_LONG_RAW ||
           t == COL_JSON || t == COL_VECTOR || t == COL_VARRAY;
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
    Column definition helpers shared by the two table-definition documents:
    the STRMTABLE_T at the head of a table's data, and the TABLE_T that the
    master table keeps for every exported table (M-02).
 ---------------------------------------------------------------------------*/

/* <COL_LIST_ITEM> opened: prepare the next column slot. */
static void ddl_col_open(DDL_CONTEXT *dc, int depth)
{
    ODV_SESSION *s = dc->session;
    dc->in_col_list = 1;
    dc->col_item_depth = depth;
    dc->col_type_num  = 0;
    dc->col_has_prec  = 0;
    dc->col_has_scale = 0;
    dc->col_char_len  = 0;
    dc->col_has_charlen = 0;
    dc->col_spare3    = 0;
    dc->col_charset_form = 0;
    dc->col_type_owner[0] = '\0';
    dc->col_type_name[0]  = '\0';
    if (dc->col_idx < ODV_MAX_COLUMNS) {
        memset(&s->table.columns[dc->col_idx], 0, sizeof(ODV_COLUMN));
    }
}

/* A direct child of <COL_LIST_ITEM> closed with its value. */
static void ddl_col_attr(DDL_CONTEXT *dc, const char *tag, const char *value)
{
    ODV_COLUMN *col;

    if (dc->col_idx >= ODV_MAX_COLUMNS) return;
    col = &dc->session->table.columns[dc->col_idx];

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
        dc->col_has_charlen = 1;
    }
    else if (strcmp(tag, "SPARE3") == 0) {
        dc->col_spare3 = atoi(value);
    }
    else if (strcmp(tag, "CHARSET") == 0 || strcmp(tag, "CHARSETID") == 0) {
        col->charset = atoi(value);
    }
    else if (strcmp(tag, "CHARSETFORM") == 0) {
        dc->col_charset_form = atoi(value);
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

/* VECTOR type string from the column attributes (M-36).  Measured on 23ai
 * (STRMTABLE_T and TABLE_T alike): PRECISION_NUM = element format (2 FLOAT32,
 * 3 FLOAT64, 4 INT8, 5 BINARY; absent = *), the dimension count is CHARLENGTH
 * (STRMTABLE_T) / SPARE3 (TABLE_T) with 0 = *, SCALE = storage (4 dense, 5
 * dense with * dimensions, 2 sparse).  Anything else is shown as plain
 * VECTOR rather than guessed. */
static void ddl_vector_type(DDL_CONTEXT *dc, ODV_COLUMN *col)
{
    const char *fmt = NULL, *store = NULL;
    int dims = dc->col_has_charlen ? dc->col_char_len : dc->col_spare3;
    char dbuf[16];

    if (!dc->col_has_prec) fmt = "*";
    else switch (col->precision) {
        case 2: fmt = "FLOAT32"; break;
        case 3: fmt = "FLOAT64"; break;
        case 4: fmt = "INT8";    break;
        case 5: fmt = "BINARY";  break;
        default: fmt = NULL;     break;
    }
    if (dc->col_has_scale) {
        if (col->scale == 4 || col->scale == 5) store = "";
        else if (col->scale == 2) store = ", SPARSE";
    }
    if (dims > 0) snprintf(dbuf, sizeof(dbuf), "%d", dims);
    else          snprintf(dbuf, sizeof(dbuf), "*");
    if (fmt && store)
        snprintf(col->type_str, sizeof(col->type_str), "VECTOR(%s, %s%s)",
                 dbuf, fmt, store);
    else
        snprintf(col->type_str, sizeof(col->type_str), "VECTOR");
}

/* </COL_LIST_ITEM>: register the column in the row-data layout and, when it
 * is a shown column, build its type string and advance to the next slot. */
static void ddl_col_finish(DDL_CONTEXT *dc)
{
    ODV_SESSION *s = dc->session;
    ODV_COLUMN *col;
    int in_data, is_display;

    dc->in_col_list = 0;
    if (dc->col_idx >= ODV_MAX_COLUMNS) return;
    col = &s->table.columns[dc->col_idx];

    /* TABLE_T has no CHARLENGTH; its SPARE3 is the same number. */
    if (!dc->col_has_charlen && dc->col_spare3 > 0)
        dc->col_char_len = dc->col_spare3;

    /* A column is in the row data when SEGCOL_NUM >= 1.  It is a shown
     * (user) column only when it also has a user column id (COL_NUM>=1)
     * and a non-system name; otherwise it is a hidden in-data column
     * (object OID, ADT attribute, nested SETID, invisible column) whose
     * bytes are read and discarded.  Virtual columns (SEGCOL_NUM==0) are
     * neither shown nor in the data (H-18). */
    in_data    = (col->seg_col >= 1);
    is_display = in_data && col->col_num >= 1 &&
                 col->name[0] != '\0' &&
                 !is_system_colname(col->name);

    if (in_data && s->table.data_count < ODV_MAX_COLUMNS) {
        s->table.data_disp[s->table.data_count] =
            is_display ? dc->col_idx : -1;
        s->table.data_type[s->table.data_count] = col->type;
        s->table.data_count++;
    }

    if (!is_display) return;

    /* CHARSETID 2000 = AL16UTF16 (national charset, UTF-16BE).
     * Upgrade VARCHAR2→NVARCHAR2 and CHAR→NCHAR accordingly. */
    if (col->charset == 2000) {
        if (col->type == COL_VARCHAR) col->type = COL_NVARCHAR;
        else if (col->type == COL_CHAR)    col->type = COL_NCHAR;
    }
    /* An NCLOB is a CLOB (TYPE_NUM 112) in the national character set
     * (CHARSETFORM 2; CHARSETID 2000 / 871).  Its data is UTF-16BE like any
     * CLOB in the dump; only the type differs. */
    if (col->type == COL_CLOB && (dc->col_charset_form == 2 || col->charset == 2000))
        col->type = COL_NCLOB;
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
    case COL_JSON:
        snprintf(col->type_str, sizeof(col->type_str), "JSON");
        break;
    case COL_BOOLEAN:
        snprintf(col->type_str, sizeof(col->type_str), "BOOLEAN");
        break;
    case COL_VECTOR:
        ddl_vector_type(dc, col);
        break;
    case COL_VARRAY:
        /* The collection type's own name (TYPEMD), as Oracle writes
         * it in DDL.  The element type is not in the dump (M-36). */
        {
            size_t lo = strlen(dc->col_type_owner), ln = strlen(dc->col_type_name);
            char *p = col->type_str;
            if (ln > 0 && lo + ln + 5 < sizeof(col->type_str)) {
                if (lo > 0) {
                    *p++ = '"'; memcpy(p, dc->col_type_owner, lo); p += lo;
                    *p++ = '"'; *p++ = '.';
                }
                *p++ = '"'; memcpy(p, dc->col_type_name, ln); p += ln;
                *p++ = '"'; *p = '\0';
            } else {
                snprintf(col->type_str, sizeof(col->type_str), "VARRAY");
            }
        }
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

/*---------------------------------------------------------------------------
    order_columns_by_colnum

    The definition lists the columns in storage (SEGCOL) order, which differs
    from the table's own column order (COL_NUM) for a table with LONG / LONG
    RAW or a column added later: T_LC (ID, L LONG, C CLOB, Z) came out as
    ID, Z, C, L in the grid, CSV and SQL.  Sort the shown columns by COL_NUM;
    the row-data layout (data_disp, in storage order) is remapped so values
    still land in the right column.
 ---------------------------------------------------------------------------*/
static void order_columns_by_colnum(ODV_TABLE *t)
{
    int n = t->col_count, i, j;
    int *order, *newpos;
    ODV_COLUMN *tmp;

    if (n <= 1 || n > ODV_MAX_COLUMNS) return;
    for (i = 1; i < n; i++)
        if (t->columns[i].col_num < t->columns[i - 1].col_num) break;
    if (i == n) return;   /* already in COL_NUM order */

    order  = (int *)malloc((size_t)n * sizeof(int));
    newpos = (int *)malloc((size_t)n * sizeof(int));
    tmp    = (ODV_COLUMN *)malloc((size_t)n * sizeof(ODV_COLUMN));
    if (!order || !newpos || !tmp) { free(order); free(newpos); free(tmp); return; }

    /* stable insertion sort of the slot indexes by COL_NUM */
    for (i = 0; i < n; i++) {
        int v = i;
        j = i - 1;
        while (j >= 0 && t->columns[order[j]].col_num > t->columns[v].col_num) {
            order[j + 1] = order[j];
            j--;
        }
        order[j + 1] = v;
    }
    for (i = 0; i < n; i++) {
        tmp[i] = t->columns[order[i]];
        newpos[order[i]] = i;
    }
    memcpy(t->columns, tmp, (size_t)n * sizeof(ODV_COLUMN));
    for (i = 0; i < t->data_count; i++)
        if (t->data_disp[i] >= 0 && t->data_disp[i] < n)
            t->data_disp[i] = newpos[t->data_disp[i]];
    free(order);
    free(newpos);
    free(tmp);
}

/*---------------------------------------------------------------------------
    XML callback: processes each tag from the EXPDP DDL (STRMTABLE_T)
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
                ddl_col_open(dc, depth);
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
        ddl_col_attr(dc, tag, value);
    }
    else if (dc->in_col_list && depth == dc->col_item_depth + 2) {
        /* <TYPEMD><OWNER_NAME>/<NAME>: the column's type, used for the type
         * name of a VARRAY column (M-36). */
        if (strcmp(tag, "NAME") == 0)
            odv_strcpy(dc->col_type_name, value, ODV_OBJNAME_LEN);
        else if (strcmp(tag, "OWNER_NAME") == 0)
            odv_strcpy(dc->col_type_owner, value, ODV_OBJNAME_LEN);
    }

    /* End of column definition */
    if (strcmp(tag, "COL_LIST_ITEM") == 0 && dc->in_col_list) {
        ddl_col_finish(dc);
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
            /* tab$.property 0x20: a partitioned table (all its data
             * segments carry it; measured on every partitioned table of the
             * 21c / 23ai test dumps) */
            s->table.is_partition = (dc->property & 0x20) ? 1 : 0;
            order_columns_by_colnum(&s->table);
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

/*===========================================================================
    Values of the types expdp writes after the inline columns (M-36) and of
    BOOLEAN / BFILE (M-11 / M-36).

    The binary forms below are not publicly specified.  Every rule was taken
    from dumps of values created on 21c / 23ai and checked against what the
    database itself returns (JSON_SERIALIZE / VECTOR_SERIALIZE / the column);
    the case files are cases/meta/g9_types*, see NOTES_g9.md.  A form that was
    not seen there is not guessed: the whole value is shown as %JSON% /
    %VECTOR% / %VARRAY% / %BOOLEAN% / %BFILE% so it cannot be mistaken for a
    real value.
 ===========================================================================*/

/* Growable output text */
typedef struct {
    char *p;
    int   len;
    int   cap;
    int   err;      /* 1 = out of memory / over the size cap */
} ODV_SBUF;

#define ODV_SBUF_MAX (64 * 1024 * 1024)

static void sb_reserve(ODV_SBUF *sb, int add)
{
    int need;
    if (sb->err) return;
    if (add < 0 || sb->len > ODV_SBUF_MAX - add - 1) { sb->err = 1; return; }
    need = sb->len + add + 1;
    if (need > sb->cap) {
        int nc = sb->cap ? sb->cap : 256;
        char *np;
        while (nc < need) nc = (nc > ODV_SBUF_MAX / 2) ? ODV_SBUF_MAX : nc * 2;
        np = (char *)realloc(sb->p, (size_t)nc);
        if (!np) { sb->err = 1; return; }
        sb->p = np;
        sb->cap = nc;
    }
}

static void sb_putn(ODV_SBUF *sb, const char *s, int n)
{
    if (n <= 0) return;
    sb_reserve(sb, n);
    if (sb->err) return;
    memcpy(sb->p + sb->len, s, (size_t)n);
    sb->len += n;
    sb->p[sb->len] = '\0';
}

static void sb_puts(ODV_SBUF *sb, const char *s) { sb_putn(sb, s, (int)strlen(s)); }
static void sb_putc(ODV_SBUF *sb, char c)        { sb_putn(sb, &c, 1); }

static void sb_free(ODV_SBUF *sb) { free(sb->p); sb->p = NULL; sb->len = sb->cap = 0; }

static unsigned int rd_u16(const unsigned char *b) { return ((unsigned int)b[0] << 8) | b[1]; }
static uint32_t rd_u32(const unsigned char *b)
{
    return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) |
           ((uint32_t)b[2] << 8) | (uint32_t)b[3];
}

/* Oracle BINARY_FLOAT / BINARY_DOUBLE byte forms (also the VECTOR elements):
 * a value with the sign bit set is stored with the sign bit cleared, a
 * negative one with every bit inverted. */
static float ora_bin_float(const unsigned char *b)
{
    unsigned char t[4];
    uint32_t u;
    float f;
    int i;
    for (i = 0; i < 4; i++) t[i] = b[i];
    if (t[0] & 0x80) t[0] &= 0x7f;
    else for (i = 0; i < 4; i++) t[i] = (unsigned char)~t[i];
    u = rd_u32(t);
    memcpy(&f, &u, sizeof(f));
    return f;
}

static double ora_bin_double(const unsigned char *b)
{
    unsigned char t[8];
    uint64_t u = 0;
    double d;
    int i;
    for (i = 0; i < 8; i++) t[i] = b[i];
    if (t[0] & 0x80) t[0] &= 0x7f;
    else for (i = 0; i < 8; i++) t[i] = (unsigned char)~t[i];
    for (i = 0; i < 8; i++) u = (u << 8) | t[i];
    memcpy(&d, &u, sizeof(d));
    return d;
}

/*---------------------------------------------------------------------------
    Number text as JSON_SERIALIZE writes it (measured, cases/meta/g9_types3):
    plain decimal while the integer part has at most 40 digits and the
    fraction at most 40 digits, otherwise d.dddE+n / d.dddE-n (no exponent
    padding).  A value below 1 gets a leading 0.  digs = significant digits
    (no leading / trailing zeros), x = decimal exponent of the first digit.
 ---------------------------------------------------------------------------*/
static void json_number_text(ODV_SBUF *sb, int neg, const char *digs, int n, int x)
{
    int intd = (x >= 0) ? x + 1 : 0;
    int frac = n - 1 - x;
    char eb[16];
    if (frac < 0) frac = 0;
    if (neg) sb_putc(sb, '-');
    if (intd <= 40 && frac <= 40) {
        if (x < 0) {
            int z;
            sb_puts(sb, "0.");
            for (z = 0; z < -x - 1; z++) sb_putc(sb, '0');
            sb_putn(sb, digs, n);
        } else if (n <= x + 1) {
            int z;
            sb_putn(sb, digs, n);
            for (z = 0; z < x + 1 - n; z++) sb_putc(sb, '0');
        } else {
            sb_putn(sb, digs, x + 1);
            sb_putc(sb, '.');
            sb_putn(sb, digs + x + 1, n - x - 1);
        }
    } else {
        sb_putc(sb, digs[0]);
        if (n > 1) { sb_putc(sb, '.'); sb_putn(sb, digs + 1, n - 1); }
        snprintf(eb, sizeof(eb), "E%c%d", x >= 0 ? '+' : '-', x >= 0 ? x : -x);
        sb_puts(sb, eb);
    }
}

/* Oracle NUMBER bytes -> JSON number text.  Returns ODV_OK, or an error for
 * a byte form that is not a valid NUMBER. */
static int json_put_oranum(ODV_SBUF *sb, const unsigned char *b, int len)
{
    char digs[96];
    int n = 0, i, e, neg, pairs, lead = -1, last = -1, x;
    int dv[24];

    if (len < 1 || len > 22) return ODV_ERROR_FORMAT;
    e = b[0];
    if (len == 1) {
        if (e != 0x80) return ODV_ERROR_FORMAT;
        sb_putc(sb, '0');
        return ODV_OK;
    }
    if (e & 0x80) {
        neg = 0;
        pairs = (e & 0x7f) - 64;
        for (i = 1; i < len; i++) dv[i - 1] = b[i] - 1;
        n = len - 1;
    } else {
        int m = len - 1;
        neg = 1;
        pairs = ((~e) & 0x7f) - 64;
        if (m > 0 && b[len - 1] == 102) m--;
        for (i = 0; i < m; i++) dv[i] = 101 - b[i + 1];
        n = m;
    }
    for (i = 0; i < n; i++)
        if (dv[i] < 0 || dv[i] > 99) return ODV_ERROR_FORMAT;
    /* two decimal digits per pair; the point sits after `pairs` pairs */
    {
        char all[64];
        int al = 0;
        for (i = 0; i < n; i++) {
            all[al++] = (char)('0' + dv[i] / 10);
            all[al++] = (char)('0' + dv[i] % 10);
        }
        for (i = 0; i < al; i++) if (all[i] != '0') { lead = i; break; }
        if (lead < 0) { sb_putc(sb, '0'); return ODV_OK; }
        for (i = al - 1; i >= 0; i--) if (all[i] != '0') { last = i; break; }
        n = last - lead + 1;
        memcpy(digs, all + lead, (size_t)n);
        x = pairs * 2 - lead - 1;
    }
    json_number_text(sb, neg, digs, n, x);
    return ODV_OK;
}

/* Shortest digits that read back to the same binary value (BINARY_FLOAT /
 * BINARY_DOUBLE inside JSON).  Measured: 0.1 -> 0.1, 1/3 -> 0.3333333333333333,
 * (float)1/3 -> 0.33333334, then the same plain / E layout as NUMBER. */
static int json_put_binfloat(ODV_SBUF *sb, double v, int is_single)
{
    char buf[48], digs[24];
    int p, n = 0, x = 0, neg = 0, i;
    double a;

    if (v != v || v > 1.7976931348623157e308 || v < -1.7976931348623157e308)
        return ODV_ERROR_FORMAT;                 /* NaN / Inf: not seen */
    if (v == 0) { sb_putc(sb, '0'); return ODV_OK; }
    if (v < 0) { neg = 1; a = -v; } else a = v;
    for (p = 1; p <= 17; p++) {
        snprintf(buf, sizeof(buf), "%.*e", p - 1, a);
        if (is_single) {
            if (strtof(buf, NULL) == (float)a) break;
        } else {
            if (strtod(buf, NULL) == a) break;
        }
    }
    if (p > 17) snprintf(buf, sizeof(buf), "%.16e", a);
    /* buf = d[.ddd]e[+-]xx */
    for (i = 0; buf[i] && buf[i] != 'e' && buf[i] != 'E'; i++)
        if (buf[i] >= '0' && buf[i] <= '9' && n < (int)sizeof(digs)) digs[n++] = buf[i];
    if (buf[i]) x = atoi(buf + i + 1);
    while (n > 1 && digs[n - 1] == '0') n--;
    json_number_text(sb, neg, digs, n, x);
    return ODV_OK;
}

/* JSON string literal (JSON_SERIALIZE escaping, measured on 23ai): \" \\ \b
 * \f \n \r \t, other C0 controls / DEL / U+2028 / U+2029 as \u00XX (upper
 * case hex), everything else as is.  21c writes U+2028 / U+2029 as is; both
 * are the same JSON string. */
static int json_put_string(ODV_SBUF *sb, const unsigned char *s, int n)
{
    int i;
    char eb[8];
    sb_putc(sb, '"');
    for (i = 0; i < n; i++) {
        unsigned char c = s[i];
        switch (c) {
        case '"':  sb_puts(sb, "\\\""); continue;
        case '\\': sb_puts(sb, "\\\\"); continue;
        case '\b': sb_puts(sb, "\\b");  continue;
        case '\f': sb_puts(sb, "\\f");  continue;
        case '\n': sb_puts(sb, "\\n");  continue;
        case '\r': sb_puts(sb, "\\r");  continue;
        case '\t': sb_puts(sb, "\\t");  continue;
        default: break;
        }
        if (c < 0x20 || c == 0x7f) {
            snprintf(eb, sizeof(eb), "\\u%04X", (unsigned int)c);
            sb_puts(sb, eb);
        } else if (c == 0xE2 && i + 2 < n && s[i + 1] == 0x80 &&
                   (s[i + 2] == 0xA8 || s[i + 2] == 0xA9)) {
            sb_puts(sb, s[i + 2] == 0xA8 ? "\\u2028" : "\\u2029");
            i += 2;
        } else {
            sb_putc(sb, (char)c);
        }
    }
    sb_putc(sb, '"');
    return ODV_OK;
}

/* DATE / TIMESTAMP bytes (century+100, year+100, month, day, hour+1, min+1,
 * sec+1) -> "YYYY-MM-DDTHH:MI:SS".  BC dates were not measured. */
static int json_datetime(char *out, int out_size, const unsigned char *b)
{
    int year, mon = b[2], day = b[3], hh = b[4] - 1, mi = b[5] - 1, ss = b[6] - 1;
    if (b[0] < 100 || b[1] < 100) return ODV_ERROR_FORMAT;
    year = (b[0] - 100) * 100 + (b[1] - 100);
    if (mon < 1 || mon > 12 || day < 1 || day > 31 || hh < 0 || hh > 23 ||
        mi < 0 || mi > 59 || ss < 0 || ss > 59) return ODV_ERROR_FORMAT;
    snprintf(out, (size_t)out_size, "%04d-%02d-%02dT%02d:%02d:%02d", year, mon, day, hh, mi, ss);
    return ODV_OK;
}

/* Fraction of a second: none when 0 (unless always), 6 digits when the
 * nanoseconds are whole microseconds, else 9 (measured). */
static void json_fraction(ODV_SBUF *sb, uint32_t ns, int always)
{
    char fb[16];
    if (ns == 0 && !always) return;
    if (ns % 1000 == 0) snprintf(fb, sizeof(fb), ".%06u", (unsigned int)(ns / 1000));
    else                snprintf(fb, sizeof(fb), ".%09u", (unsigned int)ns);
    sb_puts(sb, fb);
}

static int days_in_month(int y, int m)
{
    static const int dm[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) return 29;
    return dm[m - 1];
}

/* TIMESTAMP WITH TIME ZONE (UTC date-time, nanoseconds, tz hour+20, minute+60).
 * An offset zone is written as local time + offset ("Z" for 0); a region
 * zone (high bit) is written in UTC with "Z" (21c does so; 23ai resolves the
 * region, which needs the time-zone file this program does not have). */
static int json_tstz(ODV_SBUF *sb, const unsigned char *b)
{
    char dt[40];
    uint32_t ns = rd_u32(b + 7);
    int th = b[11], tm = b[12], off, y, mo, d, h, mi, s2;
    if (ns > 999999999u) return ODV_ERROR_FORMAT;
    if (json_datetime(dt, sizeof(dt), b) != ODV_OK) return ODV_ERROR_FORMAT;
    if (th & 0x80) {
        sb_putc(sb, '"'); sb_puts(sb, dt); json_fraction(sb, ns, 1); sb_puts(sb, "Z\"");
        return ODV_OK;
    }
    off = (th - 20) * 60 + (tm - 60);
    if (off < -15 * 60 || off > 15 * 60) return ODV_ERROR_FORMAT;
    if (off == 0) {
        sb_putc(sb, '"'); sb_puts(sb, dt); json_fraction(sb, ns, 1); sb_puts(sb, "Z\"");
        return ODV_OK;
    }
    y = (b[0] - 100) * 100 + (b[1] - 100); mo = b[2]; d = b[3];
    h = b[4] - 1; mi = b[5] - 1; s2 = b[6] - 1;
    mi += off;                       /* to local time */
    while (mi < 0)   { mi += 60; h--; }
    while (mi >= 60) { mi -= 60; h++; }
    while (h < 0)    { h += 24; d--; }
    while (h >= 24)  { h -= 24; d++; }
    if (d < 1) { if (--mo < 1) { mo = 12; y--; } d = days_in_month(y, mo); }
    else if (d > days_in_month(y, mo)) { d = 1; if (++mo > 12) { mo = 1; y++; } }
    snprintf(dt, sizeof(dt), "%04d-%02d-%02dT%02d:%02d:%02d", y, mo, d, h, mi, s2);
    sb_putc(sb, '"'); sb_puts(sb, dt); json_fraction(sb, ns, 1);
    {
        char ob[16];
        int a = off < 0 ? -off : off;
        snprintf(ob, sizeof(ob), "%c%02d:%02d\"", off < 0 ? '-' : '+', a / 60, a % 60);
        sb_puts(sb, ob);
    }
    return ODV_OK;
}

/* INTERVAL DAY TO SECOND (days+2^31, hour+60, min+60, sec+60, ns+2^31) and
 * YEAR TO MONTH (years+2^31, months+60) in ISO 8601 as measured:
 * P1DT2H3M4.500000S, -PT0.500000S, P0D, P1Y2M, -P5M, P0Y. */
static int json_interval_ds(ODV_SBUF *sb, const unsigned char *b)
{
    long long days = (long long)rd_u32(b) - 0x80000000LL;
    int h = b[4] - 60, mi = b[5] - 60, sc = b[6] - 60;
    long long ns = (long long)rd_u32(b + 7) - 0x80000000LL;
    int neg = (days < 0 || h < 0 || mi < 0 || sc < 0 || ns < 0);
    char nb[32];
    int any = 0;
    if (days < 0) days = -days;
    if (h < 0) h = -h;
    if (mi < 0) mi = -mi;
    if (sc < 0) sc = -sc;
    if (ns < 0) ns = -ns;
    if (h > 23 || mi > 59 || sc > 59 || ns > 999999999LL) return ODV_ERROR_FORMAT;
    sb_puts(sb, neg ? "\"-P" : "\"P");
    if (days) { snprintf(nb, sizeof(nb), "%lldD", days); sb_puts(sb, nb); any = 1; }
    if (h || mi || sc || ns) {
        sb_putc(sb, 'T');
        if (h)  { snprintf(nb, sizeof(nb), "%dH", h);  sb_puts(sb, nb); }
        if (mi) { snprintf(nb, sizeof(nb), "%dM", mi); sb_puts(sb, nb); }
        if (sc || ns) {
            snprintf(nb, sizeof(nb), "%d", sc); sb_puts(sb, nb);
            json_fraction(sb, (uint32_t)ns, 0);
            sb_putc(sb, 'S');
        }
        any = 1;
    }
    if (!any) sb_puts(sb, "0D");
    sb_putc(sb, '"');
    return ODV_OK;
}

static int json_interval_ym(ODV_SBUF *sb, const unsigned char *b)
{
    long long y = (long long)rd_u32(b) - 0x80000000LL;
    int m = b[4] - 60;
    int neg = (y < 0 || m < 0);
    char nb[32];
    if (y < 0) y = -y;
    if (m < 0) m = -m;
    if (m > 11) return ODV_ERROR_FORMAT;
    sb_puts(sb, neg ? "\"-P" : "\"P");
    if (y) { snprintf(nb, sizeof(nb), "%lldY", y); sb_puts(sb, nb); }
    if (m) { snprintf(nb, sizeof(nb), "%dM", m); sb_puts(sb, nb); }
    if (!y && !m) sb_puts(sb, "0Y");
    sb_putc(sb, '"');
    return ODV_OK;
}

/*---------------------------------------------------------------------------
    OSON (the binary JSON of the JSON type) -> JSON text.

    Header: ff 4a 5a, version (1, or 3 when a field name is over 255 bytes),
    flags (16 bits).  Seen flags: 0x0010 scalar document, 0x0100 1-byte hash
    ids, 0x0400 2-byte field-id / name count, 0x1000 4-byte tree size,
    0x2000 / 0x4000 / 0x0002 / 0x0004 / 0x0020 (no effect on decoding).
    Not seen (the value is not decoded): 0x0001 relative offsets, 0x0008
    4-byte name count, 0x0200 2-byte hash ids, 0x0800 4-byte name segment.
    Then the field names (hash ids, offsets, length-prefixed names), and the
    tree of nodes:
      object 0x80 / array 0xC0 (+0x20 4-byte child offsets, +0x08 / +0x10
        2- / 4-byte child count, 0x18 = object that reuses another object's
        field-id list), children by offset into the tree
      0x00-0x1f string of that length, 0x33 / 0x37 / 0x38 string with 1- / 2- /
        4-byte length, 0x20-0x2f NUMBER of (n & 0x0f)+1 bytes, 0x34 NUMBER
        with 1-byte length, 0x30 null, 0x31 true, 0x32 false, 0x36 BINARY_DOUBLE,
        0x7f BINARY_FLOAT, 0x3c DATE, 0x7d TIMESTAMP (7 bytes), 0x39 TIMESTAMP,
        0x7c TIMESTAMP WITH TIME ZONE, 0x3e / 0x3d INTERVAL DS / YM, 0x3a RAW
        with 2-byte length.
 ---------------------------------------------------------------------------*/
typedef struct {
    const unsigned char *b;
    int len;
    int tree;           /* offset of the tree segment */
    int fid_len;        /* field id width: 1 / 2 */
    int nnames;
    int *name_off;      /* offset of each field name's bytes */
    int *name_len;
    int depth;
} OSON_CTX;

#define OSON_MAX_DEPTH 200

static int oson_node(OSON_CTX *o, int pos, ODV_SBUF *sb);

static int oson_children(OSON_CTX *o, int is_obj, uint32_t n, int fids,
                         int offs, int offsz, ODV_SBUF *sb)
{
    uint32_t i;
    int rc;
    sb_putc(sb, is_obj ? '{' : '[');
    for (i = 0; i < n; i++) {
        int op = offs + (int)i * offsz;
        uint32_t off;
        if (op < 0 || op + offsz > o->len) return ODV_ERROR_FORMAT;
        off = (offsz == 2) ? rd_u16(o->b + op) : rd_u32(o->b + op);
        if (i) sb_putc(sb, ',');
        if (is_obj) {
            int fp = fids + (int)i * o->fid_len;
            uint32_t fid;
            if (fp < 0 || fp + o->fid_len > o->len) return ODV_ERROR_FORMAT;
            fid = (o->fid_len == 1) ? o->b[fp] : rd_u16(o->b + fp);
            if (fid < 1 || (int)fid > o->nnames) return ODV_ERROR_FORMAT;
            json_put_string(sb, o->b + o->name_off[fid - 1], o->name_len[fid - 1]);
            sb_putc(sb, ':');
        }
        if (off > (uint32_t)(o->len - o->tree)) return ODV_ERROR_FORMAT;
        rc = oson_node(o, o->tree + (int)off, sb);
        if (rc != ODV_OK) return rc;
        if (sb->err) return ODV_ERROR_MALLOC;
    }
    sb_putc(sb, is_obj ? '}' : ']');
    return ODV_OK;
}

static int oson_count(OSON_CTX *o, int t, int *pos, uint32_t *n)
{
    int cb = t & 0x18;
    if (cb == 0x00) { if (*pos + 1 > o->len) return ODV_ERROR_FORMAT; *n = o->b[*pos]; *pos += 1; }
    else if (cb == 0x08) { if (*pos + 2 > o->len) return ODV_ERROR_FORMAT; *n = rd_u16(o->b + *pos); *pos += 2; }
    else if (cb == 0x10) { if (*pos + 4 > o->len) return ODV_ERROR_FORMAT; *n = rd_u32(o->b + *pos); *pos += 4; }
    else return ODV_ERROR_FORMAT;
    if (*n > (uint32_t)o->len) return ODV_ERROR_FORMAT;   /* cannot fit */
    return ODV_OK;
}

static int oson_container(OSON_CTX *o, int t, int pos, ODV_SBUF *sb)
{
    int is_obj = (t & 0x40) == 0;
    int offsz = (t & 0x20) ? 4 : 2;
    uint32_t n;
    int rc;

    if ((t & 0x18) == 0x18) {
        /* Object whose field-id list is that of another object node. */
        uint32_t ro;
        int rp, t2;
        if (!is_obj || pos + offsz > o->len) return ODV_ERROR_FORMAT;
        ro = (offsz == 2) ? rd_u16(o->b + pos) : rd_u32(o->b + pos);
        pos += offsz;
        if (ro >= (uint32_t)(o->len - o->tree)) return ODV_ERROR_FORMAT;
        rp = o->tree + (int)ro;
        t2 = o->b[rp];
        if ((t2 & 0xC0) != 0x80 || (t2 & 0x18) == 0x18) return ODV_ERROR_FORMAT;
        rp++;
        rc = oson_count(o, t2, &rp, &n);
        if (rc != ODV_OK) return rc;
        return oson_children(o, 1, n, rp, pos, offsz, sb);
    }
    rc = oson_count(o, t, &pos, &n);
    if (rc != ODV_OK) return rc;
    if (is_obj) {
        int fids = pos;
        if ((int64_t)pos + (int64_t)n * o->fid_len > o->len) return ODV_ERROR_FORMAT;
        return oson_children(o, 1, n, fids, pos + (int)n * o->fid_len, offsz, sb);
    }
    return oson_children(o, 0, n, 0, pos, offsz, sb);
}

static int oson_node(OSON_CTX *o, int pos, ODV_SBUF *sb)
{
    int t, rc = ODV_OK;
    char dt[40];
    if (pos < 0 || pos >= o->len) return ODV_ERROR_FORMAT;
    if (++o->depth > OSON_MAX_DEPTH) return ODV_ERROR_FORMAT;
    t = o->b[pos++];

#define NEED(k) do { if (pos + (k) > o->len) { o->depth--; return ODV_ERROR_FORMAT; } } while (0)
    if (t & 0x80) {
        rc = oson_container(o, t, pos, sb);
    } else if (t <= 0x1f) {
        NEED(t);
        rc = json_put_string(sb, o->b + pos, t);
    } else if (t >= 0x20 && t <= 0x2f) {
        NEED((t & 0x0f) + 1);
        rc = json_put_oranum(sb, o->b + pos, (t & 0x0f) + 1);
    } else switch (t) {
    case 0x30: sb_puts(sb, "null");  break;
    case 0x31: sb_puts(sb, "true");  break;
    case 0x32: sb_puts(sb, "false"); break;
    case 0x33: NEED(1); NEED(1 + o->b[pos]);
               rc = json_put_string(sb, o->b + pos + 1, o->b[pos]); break;
    case 0x37: { unsigned int n; NEED(2); n = rd_u16(o->b + pos); NEED(2 + (int)n);
               rc = json_put_string(sb, o->b + pos + 2, (int)n); break; }
    case 0x38: { uint32_t n; NEED(4); n = rd_u32(o->b + pos);
               if (n > (uint32_t)(o->len - pos - 4)) { rc = ODV_ERROR_FORMAT; break; }
               rc = json_put_string(sb, o->b + pos + 4, (int)n); break; }
    case 0x34: NEED(1); NEED(1 + o->b[pos]);
               rc = json_put_oranum(sb, o->b + pos + 1, o->b[pos]); break;
    case 0x36: NEED(8); rc = json_put_binfloat(sb, ora_bin_double(o->b + pos), 0); break;
    case 0x7f: NEED(4); rc = json_put_binfloat(sb, (double)ora_bin_float(o->b + pos), 1); break;
    case 0x3c: case 0x7d:
        NEED(7);
        rc = json_datetime(dt, sizeof(dt), o->b + pos);
        if (rc == ODV_OK) { sb_putc(sb, '"'); sb_puts(sb, dt); sb_putc(sb, '"'); }
        break;
    case 0x39: {
        uint32_t ns;
        NEED(11);
        ns = rd_u32(o->b + pos + 7);
        rc = (ns <= 999999999u) ? json_datetime(dt, sizeof(dt), o->b + pos) : ODV_ERROR_FORMAT;
        if (rc == ODV_OK) {
            sb_putc(sb, '"'); sb_puts(sb, dt); json_fraction(sb, ns, 0); sb_putc(sb, '"');
        }
        break;
    }
    case 0x7c: NEED(13); rc = json_tstz(sb, o->b + pos); break;
    case 0x3e: NEED(11); rc = json_interval_ds(sb, o->b + pos); break;
    case 0x3d: NEED(5);  rc = json_interval_ym(sb, o->b + pos); break;
    case 0x3a: {
        unsigned int n, i;
        char hx[3];
        NEED(2); n = rd_u16(o->b + pos); NEED(2 + (int)n);
        sb_putc(sb, '"');
        for (i = 0; i < n; i++) {
            snprintf(hx, sizeof(hx), "%02X", o->b[pos + 2 + i]);
            sb_putn(sb, hx, 2);
        }
        sb_putc(sb, '"');
        break;
    }
    default:
        rc = ODV_ERROR_FORMAT;      /* node type not seen in the measurements */
        break;
    }
#undef NEED
    o->depth--;
    if (rc == ODV_OK && sb->err) rc = ODV_ERROR_MALLOC;
    return rc;
}

static int oson_to_json(const unsigned char *b, int len, ODV_SBUF *sb)
{
    OSON_CTX o;
    int ver, flags, pos = 6, rc;
    uint32_t nshort = 0, nlong = 0, segsz = 0, lsegsz = 0;
    int loffsz = 2;

    memset(&o, 0, sizeof(o));
    o.b = b;
    o.len = len;
    o.fid_len = 1;
    if (len < 7 || b[0] != 0xff || b[1] != 0x4a || b[2] != 0x5a) return ODV_ERROR_FORMAT;
    ver = b[3];
    if (ver != 1 && ver != 3) return ODV_ERROR_FORMAT;
    flags = (int)rd_u16(b + 4);
    if (flags & (0x0001 | 0x0008 | 0x0200 | 0x0800)) return ODV_ERROR_FORMAT;   /* not seen */

    if (flags & 0x0010) {                      /* scalar document */
        pos += (flags & 0x1000) ? 4 : 2;       /* tree size */
        o.tree = pos;
        return oson_node(&o, pos, sb);
    }
    if (flags & 0x0400) {
        if (pos + 2 > len) return ODV_ERROR_FORMAT;
        nshort = rd_u16(b + pos); pos += 2; o.fid_len = 2;
    } else {
        if (pos + 1 > len) return ODV_ERROR_FORMAT;
        nshort = b[pos]; pos += 1;
    }
    if (pos + 2 > len) return ODV_ERROR_FORMAT;
    segsz = rd_u16(b + pos); pos += 2;
    if (ver == 3) {
        int fl2;
        if (pos + 10 > len) return ODV_ERROR_FORMAT;
        fl2 = (int)rd_u16(b + pos); pos += 2;
        loffsz = (fl2 & 0x0100) ? 2 : 4;
        if (loffsz != 2) return ODV_ERROR_FORMAT;                 /* not seen */
        nlong  = rd_u32(b + pos); pos += 4;
        lsegsz = rd_u32(b + pos); pos += 4;
    }
    pos += (flags & 0x1000) ? 4 : 2;                              /* tree size */
    pos += 2;                                                     /* tiny nodes */
    if (pos > len || nshort > (uint32_t)len || nlong > (uint32_t)len) return ODV_ERROR_FORMAT;

    o.nnames = (int)(nshort + nlong);
    if (o.nnames > 0) {
        o.name_off = (int *)malloc((size_t)o.nnames * sizeof(int));
        o.name_len = (int *)malloc((size_t)o.nnames * sizeof(int));
        if (!o.name_off || !o.name_len) { free(o.name_off); free(o.name_len); return ODV_ERROR_MALLOC; }
    }
    rc = ODV_OK;
    if (nshort > 0 && !(flags & 0x0100)) rc = ODV_ERROR_FORMAT;   /* hash id width not seen */
    if (rc == ODV_OK && nshort > 0) {
        /* 1-byte hash ids, 2-byte offsets, then the names (1-byte length) */
        int offs = pos + (int)nshort, seg = offs + (int)nshort * 2;
        uint32_t i;
        if (seg + (int)segsz > len) rc = ODV_ERROR_FORMAT;
        for (i = 0; rc == ODV_OK && i < nshort; i++) {
            unsigned int no = rd_u16(b + offs + 2 * i);
            if (no >= segsz || (int)(no + 1 + b[seg + no]) > (int)segsz) { rc = ODV_ERROR_FORMAT; break; }
            o.name_off[i] = seg + (int)no + 1;
            o.name_len[i] = b[seg + no];
        }
        pos = seg + (int)segsz;
    }
    if (rc == ODV_OK && nlong > 0) {
        /* 2-byte hash ids, 2-byte offsets, names with a 2-byte length */
        int offs = pos + (int)nlong * 2, seg = offs + (int)nlong * loffsz;
        uint32_t i;
        if ((int64_t)seg + (int64_t)lsegsz > len) rc = ODV_ERROR_FORMAT;
        for (i = 0; rc == ODV_OK && i < nlong; i++) {
            unsigned int no = rd_u16(b + offs + 2 * i);
            unsigned int nl;
            if ((uint32_t)no + 2 > lsegsz) { rc = ODV_ERROR_FORMAT; break; }
            nl = rd_u16(b + seg + no);
            if ((uint32_t)no + 2 + nl > lsegsz) { rc = ODV_ERROR_FORMAT; break; }
            o.name_off[nshort + i] = seg + (int)no + 2;
            o.name_len[nshort + i] = (int)nl;
        }
        pos = seg + (int)lsegsz;
    }
    if (rc == ODV_OK) {
        o.tree = pos;
        rc = oson_node(&o, pos, sb);
    }
    free(o.name_off);
    free(o.name_len);
    return rc;
}

/*---------------------------------------------------------------------------
    VECTOR -> text as VECTOR_SERIALIZE writes it (measured on 23ai):
      db, version (0 dense, 1 BINARY, 2 sparse), flags (16 bits: 0x0002 norm,
      0x0010 norm area present, 0x0020 sparse), format (2 FLOAT32, 3 FLOAT64,
      4 INT8, 5 BINARY), dimension count (4), the 8-byte norm when flags&0x10,
      then dense: the elements; sparse: 2-byte count, 4-byte indexes, values.
    FLOAT32 / FLOAT64 elements: 9 / 17 significant digits with trailing zeros
    dropped (one kept), exponent sign + 3 digits, 0 as "0" (1.5E+000,
    -2.5E-300, 1.00000001E-001).  INT8 and BINARY bytes as integers.
 ---------------------------------------------------------------------------*/
static void vec_put_float(ODV_SBUF *sb, double v, int sig)
{
    char buf[64], out[64];
    int i, n = 0, x = 0, neg = 0, k;
    char digs[24];
    if (v == 0) { sb_putc(sb, '0'); return; }
    if (v < 0) { neg = 1; v = -v; }
    snprintf(buf, sizeof(buf), "%.*E", sig - 1, v);
    for (i = 0; buf[i] && buf[i] != 'E' && buf[i] != 'e'; i++)
        if (buf[i] >= '0' && buf[i] <= '9' && n < (int)sizeof(digs)) digs[n++] = buf[i];
    if (buf[i]) x = atoi(buf + i + 1);
    while (n > 2 && digs[n - 1] == '0') n--;
    if (n < 2) digs[n++] = '0';
    k = 0;
    if (neg) out[k++] = '-';
    out[k++] = digs[0];
    out[k++] = '.';
    for (i = 1; i < n; i++) out[k++] = digs[i];
    snprintf(out + k, sizeof(out) - (size_t)k, "E%c%03d", x < 0 ? '-' : '+', x < 0 ? -x : x);
    sb_puts(sb, out);
}

static int vec_put_elem(ODV_SBUF *sb, int fmt, const unsigned char *p)
{
    char nb[16];
    switch (fmt) {
    case 2: vec_put_float(sb, (double)ora_bin_float(p), 9); return ODV_OK;
    case 3: vec_put_float(sb, ora_bin_double(p), 17);       return ODV_OK;
    case 4: snprintf(nb, sizeof(nb), "%d", (int)(signed char)p[0]); sb_puts(sb, nb); return ODV_OK;
    default: return ODV_ERROR_FORMAT;
    }
}

static int vector_to_text(const unsigned char *b, int len, ODV_SBUF *sb)
{
    int ver, flags, fmt, pos, esz;
    uint32_t n, i;
    char nb[24];

    if (len < 9 || b[0] != 0xdb) return ODV_ERROR_FORMAT;
    ver = b[1];
    flags = (int)rd_u16(b + 2);
    fmt = b[4];
    n = rd_u32(b + 5);
    pos = 9;
    if (flags & ~(0x0002 | 0x0010 | 0x0020)) return ODV_ERROR_FORMAT;     /* not seen */
    if (flags & 0x0010) pos += 8;
    if (pos > len || n > 65535) return ODV_ERROR_FORMAT;
    esz = (fmt == 2) ? 4 : (fmt == 3) ? 8 : (fmt == 4) ? 1 : (fmt == 5) ? 1 : 0;
    if (!esz) return ODV_ERROR_FORMAT;

    if (ver == 0 || ver == 1) {
        if (flags & 0x0020) return ODV_ERROR_FORMAT;
        if (fmt == 5) {                      /* BINARY: n bits, one integer per byte */
            uint32_t nbytes = n / 8;
            if (ver != 1 || n % 8 || pos + (int)nbytes != len) return ODV_ERROR_FORMAT;
            sb_putc(sb, '[');
            for (i = 0; i < nbytes; i++) {
                if (i) sb_putc(sb, ',');
                snprintf(nb, sizeof(nb), "%u", (unsigned int)b[pos + i]);
                sb_puts(sb, nb);
            }
            sb_putc(sb, ']');
            return sb->err ? ODV_ERROR_MALLOC : ODV_OK;
        }
        if (ver != 0 || (int64_t)pos + (int64_t)n * esz != len) return ODV_ERROR_FORMAT;
        sb_putc(sb, '[');
        for (i = 0; i < n; i++) {
            if (i) sb_putc(sb, ',');
            if (vec_put_elem(sb, fmt, b + pos + (int)i * esz) != ODV_OK) return ODV_ERROR_FORMAT;
        }
        sb_putc(sb, ']');
        return sb->err ? ODV_ERROR_MALLOC : ODV_OK;
    }
    if (ver == 2 && (flags & 0x0020) && fmt != 5) {      /* sparse */
        unsigned int nnz;
        int ip, vp;
        if (pos + 2 > len) return ODV_ERROR_FORMAT;
        nnz = rd_u16(b + pos);
        ip = pos + 2;
        vp = ip + (int)nnz * 4;
        if ((int64_t)vp + (int64_t)nnz * esz != len) return ODV_ERROR_FORMAT;
        snprintf(nb, sizeof(nb), "[%u,[", (unsigned int)n);
        sb_puts(sb, nb);
        for (i = 0; i < nnz; i++) {
            if (i) sb_putc(sb, ',');
            snprintf(nb, sizeof(nb), "%u", (unsigned int)rd_u32(b + ip + (int)i * 4));
            sb_puts(sb, nb);
        }
        sb_puts(sb, "],[");
        for (i = 0; i < nnz; i++) {
            if (i) sb_putc(sb, ',');
            if (vec_put_elem(sb, fmt, b + vp + (int)i * esz) != ODV_OK) return ODV_ERROR_FORMAT;
        }
        sb_puts(sb, "]]");
        return sb->err ? ODV_ERROR_MALLOC : ODV_OK;
    }
    return ODV_ERROR_FORMAT;
}

/* UTF-16BE -> UTF-8 (portable; the TABLE_T documents of the master table).
 * An unpaired surrogate becomes U+FFFD.  Returns a malloc'd NUL-terminated
 * string (or NULL) and its length in *out_len. */
static char *utf16be_to_utf8(const unsigned char *s, int n, int *out_len)
{
    char *o;
    int i, k = 0;
    if (n < 0) n = 0;
    o = (char *)malloc((size_t)(n / 2) * 3 + 4);
    if (!o) return NULL;
    for (i = 0; i + 1 < n; i += 2) {
        uint32_t c = ((uint32_t)s[i] << 8) | s[i + 1];
        if (c >= 0xD800 && c <= 0xDBFF && i + 3 < n) {
            uint32_t c2 = ((uint32_t)s[i + 2] << 8) | s[i + 3];
            if (c2 >= 0xDC00 && c2 <= 0xDFFF) {
                c = 0x10000 + ((c - 0xD800) << 10) + (c2 - 0xDC00);
                i += 2;
            } else c = 0xFFFD;
        } else if (c >= 0xD800 && c <= 0xDFFF) c = 0xFFFD;
        if (c < 0x80) o[k++] = (char)c;
        else if (c < 0x800) { o[k++] = (char)(0xC0 | (c >> 6)); o[k++] = (char)(0x80 | (c & 0x3F)); }
        else if (c < 0x10000) {
            o[k++] = (char)(0xE0 | (c >> 12)); o[k++] = (char)(0x80 | ((c >> 6) & 0x3F));
            o[k++] = (char)(0x80 | (c & 0x3F));
        } else {
            o[k++] = (char)(0xF0 | (c >> 18)); o[k++] = (char)(0x80 | ((c >> 12) & 0x3F));
            o[k++] = (char)(0x80 | ((c >> 6) & 0x3F)); o[k++] = (char)(0x80 | (c & 0x3F));
        }
    }
    o[k] = '\0';
    if (out_len) *out_len = k;
    return o;
}

/* BFILE locator, measured: [dir length (2)][dir][file length (2)][file]
 * -> BFILENAME('dir', 'file') (the database's own notation; help: "path
 * information only"). */
static int bfile_to_text(ODV_SESSION *s, const unsigned char *b, int len, ODV_SBUF *sb)
{
    unsigned int dl, fl;
    int i, part;
    if (len < 4) return ODV_ERROR_FORMAT;
    dl = rd_u16(b);
    if (2 + (int)dl + 2 > len) return ODV_ERROR_FORMAT;
    fl = rd_u16(b + 2 + dl);
    if (2 + (int)dl + 2 + (int)fl != len || dl == 0) return ODV_ERROR_FORMAT;
    sb_puts(sb, "BFILENAME('");
    for (part = 0; part < 2; part++) {
        const unsigned char *p = part ? b + 4 + dl : b + 2;
        int pl = part ? (int)fl : (int)dl;
        char conv[ODV_OBJNAME_LEN * 8 + 16];
        int cl = 0;
        const char *src = (const char *)p;
        if (s->dump_charset != s->out_charset && s->dump_charset != CHARSET_UNKNOWN &&
            pl < (int)sizeof(conv) / 4 &&
            convert_charset((const char *)p, pl, s->dump_charset, conv, sizeof(conv),
                            s->out_charset, &cl) == ODV_OK) {
            src = conv;
            pl = cl;
        }
        for (i = 0; i < pl; i++) {
            if (src[i] == '\'') sb_putc(sb, '\'');
            sb_putc(sb, src[i]);
        }
        sb_puts(sb, part ? "')" : "', '");
    }
    return sb->err ? ODV_ERROR_MALLOC : ODV_OK;
}

static int long_form(ODV_SESSION *s, const unsigned char *p, int n);

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
        /* TSTZ: local time + offset / region; SQL export gets the raw bytes (odv_tz.c, M-09) */
        odv_decode_timestamp_value(s, col->type, v->data, v->data_len,
                                   decode_buf, sizeof(decode_buf), col->precision);
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

    case COL_BOOLEAN:
        /* 23ai BOOLEAN: one byte, 01 = TRUE, 00 = FALSE (measured); it was
         * shown as the raw byte (U+0001 / empty) (M-36). */
        if (v->data_len == 1 && v->data && (v->data[0] == 0 || v->data[0] == 1))
            set_value_string(v, v->data[0] ? "TRUE" : "FALSE", v->data[0] ? 4 : 5);
        else
            set_value_string(v, "%BOOLEAN%", 9);
        v->type = col->type;
        break;

    case COL_BFILE: {
        /* The BFILE locator is inline; show its directory and file name
         * (help: "path information only").  It was shown empty (M-11). */
        ODV_SBUF sb;
        memset(&sb, 0, sizeof(sb));
        if (v->data && bfile_to_text(s, v->data, v->data_len, &sb) == ODV_OK && sb.p)
            set_value_string(v, sb.p, sb.len);
        else
            set_value_string(v, "%BFILE%", 7);
        sb_free(&sb);
        v->type = col->type;
        break;
    }

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
        if (col->type == COL_LONG && v->data && v->data_len > 0 &&
            long_form(s, v->data, v->data_len) == 0) {
            set_value_string(v, "%LONG%", 6);
        } else if (col->type == COL_LONG && v->data &&
                   long_form(s, v->data, v->data_len) == 2) {
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

/*===========================================================================
    The master table (M-02)

    Data Pump writes the head of a table's data (STRMTABLE_T) only for a
    table or partition that has rows, so a 0-row table, a 0-row partition and
    every table of a CONTENT=METADATA_ONLY dump never appear there.  The
    master table, unloaded last as an ordinary table, lists them all:

      OBJECT_TYPE 'TABLE'      XML_CLOB = the TABLE_T document (UTF-16BE):
                               the table's full definition, partitions included
      OBJECT_TYPE 'TABLE_DATA' one row per exported segment (table, partition
                               or subpartition): PARTITION_NAME,
                               SUBPARTITION_NAME, COMPLETED_ROWS,
                               DUMP_LENGTH (0 = no data written), SIZE_ESTIMATE

    Its rows are read with the same row-piece reader (st->master), and after
    the scan the missing tables / partitions are added to the list with 0
    rows, and the partitions that do have data get their names.  Measured on
    the 282 expdp dumps of odvtest: the data of a table's segments is written
    in (SIZE_ESTIMATE descending, partition name, subpartition name) order, so
    the i-th data entry of a table is the i-th TABLE_DATA row with data in that
    order; a name is given only when COMPLETED_ROWS agrees with every entry's
    row count (otherwise the names are left to scan_master_metadata as before).
 ===========================================================================*/

typedef struct {
    char    schema[ODV_OBJNAME_LEN + 1];
    char    name[ODV_OBJNAME_LEN + 1];
    char    part[ODV_OBJNAME_LEN + 1];
    char    sub[ODV_OBJNAME_LEN + 1];
    int64_t rows;
    int64_t dlen;
    int64_t size;
    int     umeth;          /* UNLOAD_METHOD: 1 direct path, 2 external table */
    int     lix;            /* scratch: position among the TABLE_T leaves */
} MASTER_TD;

typedef struct {
    char    schema[ODV_OBJNAME_LEN + 1];
    char    name[ODV_OBJNAME_LEN + 1];
    char   *xml;            /* TABLE_T document, UTF-8 */
    int     xml_len;
} MASTER_TT;

typedef struct {
    int        c_type, c_schema, c_name, c_part, c_sub, c_rows, c_dlen, c_size, c_xml;
    int        c_umeth;
    int        td_only;     /* keep only the TABLE_DATA rows (unload method lookup) */
    MASTER_TD *td;
    int        ntd, ctd;
    MASTER_TT *tt;
    int        ntt, ctt;
    int64_t    ddl_pos;     /* position of the master table's definition block */
    int       *enext;       /* per table_list entry: the next entry of the
                               same table, or -1 (master_link_entries) */
    int        nenext;
} MASTER_CTX;

/* Link the table_list entries that share a schema and name, in list order:
 * mc->enext[i] = the next entry of the same table, or -1.  The first entry
 * of each table comes from the session's own index (odv_table_list_find,
 * odv_api.c); this adds only the links, kept as indices because the list is
 * reallocated as entries are added. */
static void master_link_entries(ODV_SESSION *s, MASTER_CTX *mc)
{
    int i, *last;
    free(mc->enext);
    mc->enext = NULL;
    mc->nenext = 0;
    if (s->table_count <= 0) return;
    mc->enext = (int *)malloc((size_t)s->table_count * sizeof(int));
    last = (int *)malloc((size_t)s->table_count * sizeof(int));
    if (!mc->enext || !last) {
        free(mc->enext);
        free(last);
        mc->enext = NULL;
        return;
    }
    for (i = 0; i < s->table_count; i++) {
        int f = odv_table_list_find(s, s->table_list[i].schema, s->table_list[i].name);
        mc->enext[i] = -1;
        if (f < 0 || f >= i) {
            last[i] = i;                 /* the first entry of its table */
        } else {
            mc->enext[last[f]] = i;
            last[f] = i;
        }
    }
    mc->nenext = s->table_count;
    free(last);
}

/* The listed entries of (schema, name): *first = the first one (follow
 * mc->enext from it), returns the count. */
static int master_find_entries(ODV_SESSION *s, const MASTER_CTX *mc,
                               const char *schema, const char *name, int *first)
{
    int k, n = 0;
    *first = -1;
    if (!mc->enext) return 0;
    k = odv_table_list_find(s, schema, name);
    if (k < 0 || k >= mc->nenext) return 0;
    *first = k;
    for (; k >= 0; k = mc->enext[k]) n++;
    return n;
}

static int find_column(ODV_TABLE *t, const char *name)
{
    int i;
    for (i = 0; i < t->col_count; i++)
        if (strcmp(t->columns[i].name, name) == 0) return i;
    return -1;
}

/* The master table: the columns impdp itself relies on.  (A user table with
 * the same column set is not plausible: it has about 100 columns.) */
static int master_columns(ODV_TABLE *t, MASTER_CTX *mc)
{
    mc->c_type   = find_column(t, "OBJECT_TYPE");
    mc->c_schema = find_column(t, "OBJECT_SCHEMA");
    mc->c_name   = find_column(t, "OBJECT_NAME");
    mc->c_part   = find_column(t, "PARTITION_NAME");
    mc->c_sub    = find_column(t, "SUBPARTITION_NAME");
    mc->c_rows   = find_column(t, "COMPLETED_ROWS");
    mc->c_dlen   = find_column(t, "DUMP_LENGTH");
    mc->c_size   = find_column(t, "SIZE_ESTIMATE");
    mc->c_xml    = find_column(t, "XML_CLOB");
    mc->c_umeth  = find_column(t, "UNLOAD_METHOD");
    return mc->c_type >= 0 && mc->c_schema >= 0 && mc->c_name >= 0 &&
           mc->c_part >= 0 && mc->c_rows >= 0 && mc->c_dlen >= 0 &&
           find_column(t, "PROCESS_ORDER") >= 0 && mc->c_xml >= 0 &&
           t->columns[mc->c_xml].type == COL_CLOB;
}

static const char *mval(ODV_SESSION *s, int c)
{
    ODV_VALUE *v;
    if (c < 0 || c >= s->table.col_count) return "";
    v = &s->record.values[c];
    if (v->is_null || !v->data || v->data_len <= 0) return "";
    return (const char *)v->data;
}

static int64_t mnum(ODV_SESSION *s, int c, int64_t dflt)
{
    const char *t = mval(s, c);
    if (!t[0]) return dflt;
    return (int64_t)strtoll(t, NULL, 10);
}

/* One master-table row (called by parse_expdp_records). */
static void master_row(ODV_SESSION *s)
{
    MASTER_CTX *mc = (MASTER_CTX *)s->state.master;
    const char *type = mval(s, mc->c_type);
    const char *name = mval(s, mc->c_name);

    if (!name[0]) return;
    if (strcmp(type, "TABLE_DATA") == 0) {
        MASTER_TD *d;
        if (mc->ntd >= mc->ctd) {
            int nc = mc->ctd ? mc->ctd * 2 : 64;
            MASTER_TD *np = (MASTER_TD *)realloc(mc->td, (size_t)nc * sizeof(MASTER_TD));
            if (!np) return;
            mc->td = np;
            mc->ctd = nc;
        }
        d = &mc->td[mc->ntd++];
        odv_strcpy(d->schema, mval(s, mc->c_schema), ODV_OBJNAME_LEN);
        odv_strcpy(d->name, name, ODV_OBJNAME_LEN);
        odv_strcpy(d->part, mval(s, mc->c_part), ODV_OBJNAME_LEN);
        odv_strcpy(d->sub, mval(s, mc->c_sub), ODV_OBJNAME_LEN);
        d->rows = mnum(s, mc->c_rows, 0);
        d->dlen = mnum(s, mc->c_dlen, 0);
        d->size = mnum(s, mc->c_size, 0);
        d->umeth = (int)mnum(s, mc->c_umeth, 0);
    } else if (strcmp(type, "TABLE") == 0 && !mc->td_only) {
        ODV_VALUE *v = &s->record.values[mc->c_xml];
        char schema[ODV_OBJNAME_LEN + 1];
        char tname[ODV_OBJNAME_LEN + 1];
        char *xml;
        int xl = 0;
        MASTER_TT *t;

        if (v->is_null || !v->data || v->data_len < 16) return;
        odv_strcpy(schema, mval(s, mc->c_schema), ODV_OBJNAME_LEN);
        odv_strcpy(tname, name, ODV_OBJNAME_LEN);
        xml = utf16be_to_utf8(v->data, v->data_len, &xl);
        if (!xml) return;
        /* Keep only what can be needed: a table with no data entry, or a
         * partitioned table (its empty partitions). */
        if (odv_table_list_find(s, schema, tname) >= 0 && !strstr(xml, "<PART_OBJ>")) {
            free(xml);
            return;
        }
        if (mc->ntt >= mc->ctt) {
            int nc = mc->ctt ? mc->ctt * 2 : 32;
            MASTER_TT *np = (MASTER_TT *)realloc(mc->tt, (size_t)nc * sizeof(MASTER_TT));
            if (!np) { free(xml); return; }
            mc->tt = np;
            mc->ctt = nc;
        }
        t = &mc->tt[mc->ntt++];
        odv_strcpy(t->schema, schema, ODV_OBJNAME_LEN);
        odv_strcpy(t->name, tname, ODV_OBJNAME_LEN);
        t->xml = xml;
        t->xml_len = xl;
    }
}

static void master_free(MASTER_CTX *mc)
{
    int i;
    for (i = 0; i < mc->ntt; i++) free(mc->tt[i].xml);
    free(mc->tt);
    free(mc->td);
    free(mc->enext);
    memset(mc, 0, sizeof(*mc));
}

/*---------------------------------------------------------------------------
    TABLE_T document -> s->table (and its partition / subpartition leaves)

    ROWSET/ROW/TABLE_T/SCHEMA_OBJ/{OWNER_NAME,NAME}   the table
    TABLE_T/COL_LIST/COL_LIST_ITEM/<attr>              a column (same
        attributes as STRMTABLE_T; SPARE3 = character length / VECTOR
        dimensions); its TYPEMD/SCHEMA_OBJ/{OWNER_NAME,NAME} = the column type
    TABLE_T/PART_OBJ/PART_LIST/PART_LIST_ITEM/SCHEMA_OBJ/SUBNAME     partition
        (composite: COMPART_LIST/COMPART_LIST_ITEM, measured on 21c)
        .../PART_LIST_ITEM/SUBPARTS/SUBPARTS_ITEM/SCHEMA_OBJ/SUBNAME  subpartition
    Constraint and index column lists (CON*_LIST/.../COL_LIST) are deeper and
    are not columns of the table.
 ---------------------------------------------------------------------------*/
typedef struct {
    char part[ODV_OBJNAME_LEN + 1];
    char sub[ODV_OBJNAME_LEN + 1];
} TT_LEAF;

#define TT_PATH_DEPTH 12

typedef struct {
    DDL_CONTEXT dc;
    char    path[TT_PATH_DEPTH][24];
    char    cur_part[ODV_OBJNAME_LEN + 1];
    int     part_subs;
    TT_LEAF *leaves;
    int     nleaves, cleaves;
    int     done;
} TABLET_CTX;

static void tt_add_leaf(TABLET_CTX *t, const char *part, const char *sub)
{
    if (t->nleaves >= t->cleaves) {
        int nc = t->cleaves ? t->cleaves * 2 : 16;
        TT_LEAF *np = (TT_LEAF *)realloc(t->leaves, (size_t)nc * sizeof(TT_LEAF));
        if (!np) return;
        t->leaves = np;
        t->cleaves = nc;
    }
    odv_strcpy(t->leaves[t->nleaves].part, part, ODV_OBJNAME_LEN);
    odv_strcpy(t->leaves[t->nleaves].sub, sub, ODV_OBJNAME_LEN);
    t->nleaves++;
}

#define TT_AT(d, name) (strcmp(t->path[d], name) == 0)
/* PART_LIST/PART_LIST_ITEM (partitioned) or COMPART_LIST/COMPART_LIST_ITEM
 * (composite: the items carry SUBPARTS) at depths d / d+1 */
#define TT_PARTS(d, item) \
    ((TT_AT(d, "PART_LIST") && strcmp(item, "PART_LIST_ITEM") == 0) || \
     (TT_AT(d, "COMPART_LIST") && strcmp(item, "COMPART_LIST_ITEM") == 0))

static void tablet_xml_event(int kind, const char *tag, const char *value,
                             int depth, void *ctx)
{
    TABLET_CTX *t = (TABLET_CTX *)ctx;
    DDL_CONTEXT *dc = &t->dc;

    if (depth < 1 || depth >= TT_PATH_DEPTH) return;
    if (kind == XML_EV_OPEN) {
        odv_strcpy(t->path[depth], tag, sizeof(t->path[depth]) - 1);
        if (depth == 5 && TT_AT(3, "TABLE_T") && TT_AT(4, "COL_LIST") &&
            strcmp(tag, "COL_LIST_ITEM") == 0)
            ddl_col_open(dc, depth);
        else if (depth == 6 && TT_AT(3, "TABLE_T") && TT_AT(4, "PART_OBJ") &&
                 TT_PARTS(5, tag)) {
            t->cur_part[0] = '\0';
            t->part_subs = 0;
        }
        return;
    }
    if (kind != XML_EV_CLOSE || !TT_AT(3, "TABLE_T") || strcmp(t->path[depth], tag) != 0)
        return;

    if (depth == 3) {
        ODV_SESSION *s = dc->session;
        odv_strcpy(s->table.schema, dc->cur_schema, ODV_OBJNAME_LEN);
        odv_strcpy(s->table.name, dc->cur_table, ODV_OBJNAME_LEN);
        s->table.col_count = dc->col_idx;
        order_columns_by_colnum(&s->table);
        t->done = 1;
    } else if (depth == 4 && strcmp(tag, "PROPERTY") == 0) {
        dc->property = atoi(value);
    } else if (depth == 5 && TT_AT(4, "SCHEMA_OBJ")) {
        if (strcmp(tag, "NAME") == 0)            odv_strcpy(dc->cur_table, value, ODV_OBJNAME_LEN);
        else if (strcmp(tag, "OWNER_NAME") == 0) odv_strcpy(dc->cur_schema, value, ODV_OBJNAME_LEN);
    } else if (TT_AT(4, "COL_LIST") && depth >= 5 && TT_AT(5, "COL_LIST_ITEM") && dc->in_col_list) {
        if (depth == 5)
            ddl_col_finish(dc);
        else if (depth == 6)
            ddl_col_attr(dc, tag, value);
        else if (depth == 8 && TT_AT(6, "TYPEMD") && TT_AT(7, "SCHEMA_OBJ")) {
            if (strcmp(tag, "NAME") == 0)            odv_strcpy(dc->col_type_name, value, ODV_OBJNAME_LEN);
            else if (strcmp(tag, "OWNER_NAME") == 0) odv_strcpy(dc->col_type_owner, value, ODV_OBJNAME_LEN);
        }
    } else if (TT_AT(4, "PART_OBJ") && depth >= 6 && TT_PARTS(5, t->path[6])) {
        if (depth == 6) {
            if (t->part_subs == 0 && t->cur_part[0]) tt_add_leaf(t, t->cur_part, "");
        } else if (depth == 8 && TT_AT(7, "SCHEMA_OBJ") && strcmp(tag, "SUBNAME") == 0) {
            odv_strcpy(t->cur_part, value, ODV_OBJNAME_LEN);
        } else if (depth == 10 && TT_AT(7, "SUBPARTS") && TT_AT(8, "SUBPARTS_ITEM") &&
                   TT_AT(9, "SCHEMA_OBJ") && strcmp(tag, "SUBNAME") == 0) {
            tt_add_leaf(t, t->cur_part, value);
            t->part_subs++;
        }
    }
}

#undef TT_PARTS
#undef TT_AT

/* UTF-8 name (TABLE_T) -> the dump character set the session keeps names in. */
static void tt_name_to_dump_cs(ODV_SESSION *s, char *name)
{
    char tmp[ODV_OBJNAME_LEN * 4 + 1];
    int ol = 0;
    if (s->dump_charset == CHARSET_UTF8 || s->dump_charset == CHARSET_UNKNOWN || !name[0])
        return;
    if (convert_charset(name, (int)strlen(name), CHARSET_UTF8, tmp, sizeof(tmp),
                        s->dump_charset, &ol) == ODV_OK && ol >= 0 && ol <= ODV_OBJNAME_LEN) {
        memcpy(name, tmp, (size_t)ol);
        name[ol] = '\0';
    }
}

static int parse_tablet(ODV_SESSION *s, const MASTER_TT *tt, TABLET_CTX *t)
{
    int i;
    memset(t, 0, sizeof(*t));
    odv_table_free_text(&s->table);
    memset(&s->table, 0, sizeof(ODV_TABLE));
    s->table.dump_charset = s->dump_charset;
    s->table.os_charset = s->out_charset;
    invalidate_meta_cache();
    t->dc.session = s;
    parse_xml_events(tt->xml, tt->xml_len, tablet_xml_event, t);
    if (!t->done || !s->table.name[0]) return ODV_ERROR_FORMAT;
    tt_name_to_dump_cs(s, s->table.schema);
    tt_name_to_dump_cs(s, s->table.name);
    for (i = 0; i < s->table.col_count; i++)
        tt_name_to_dump_cs(s, s->table.columns[i].name);
    return ODV_OK;
}

/* qsort keys: (schema, name) for the TABLE_DATA array; for the rows of one
 * table: data order (SIZE_ESTIMATE desc, partition, subpartition), and the
 * partitions' definition order (lix = position among the TABLE_T leaves,
 * then name) */
static int td_qcmp_key(const void *a, const void *b)
{
    const MASTER_TD *x = (const MASTER_TD *)a, *y = (const MASTER_TD *)b;
    int c = strcmp(x->schema, y->schema);
    if (c) return c;
    return strcmp(x->name, y->name);
}

static int td_cmp_name(const MASTER_TD *a, const MASTER_TD *b)
{
    int c = strcmp(a->part, b->part);
    if (c) return c;
    return strcmp(a->sub, b->sub);
}

static int td_qcmp_data(const void *a, const void *b)
{
    const MASTER_TD *x = *(const MASTER_TD * const *)a, *y = *(const MASTER_TD * const *)b;
    if (x->size != y->size) return (x->size > y->size) ? -1 : 1;
    return td_cmp_name(x, y);
}

static int td_qcmp_leaf(const void *a, const void *b)
{
    const MASTER_TD *x = *(const MASTER_TD * const *)a, *y = *(const MASTER_TD * const *)b;
    if (x->lix != y->lix) return (x->lix < y->lix) ? -1 : 1;
    return td_cmp_name(x, y);
}

/* The TABLE_DATA rows of (schema, name) in the key-sorted array: *first,
 * returns the count. */
static int master_find_td(const MASTER_CTX *mc, const char *schema, const char *name,
                          int *first)
{
    int lo = 0, hi = mc->ntd, n = 0;
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        int c = strcmp(mc->td[mid].schema, schema);
        if (!c) c = strcmp(mc->td[mid].name, name);
        if (c < 0) lo = mid + 1; else hi = mid;
    }
    *first = lo;
    while (lo + n < mc->ntd && strcmp(mc->td[lo + n].schema, schema) == 0 &&
           strcmp(mc->td[lo + n].name, name) == 0) n++;
    return n;
}

typedef struct {
    const char *part;
    const char *sub;
    int         k;
} TT_LKEY;

static int lkey_cmp(const void *a, const void *b)
{
    const TT_LKEY *x = (const TT_LKEY *)a, *y = (const TT_LKEY *)b;
    int c = strcmp(x->part, y->part);
    if (c) return c;
    c = strcmp(x->sub, y->sub);
    if (c) return c;
    return (x->k > y->k) - (x->k < y->k);
}

/* dv[0..n): set lix = the segment's position among the TABLE_T leaves (the
 * partitions / subpartitions in definition order; not found = after all). */
static void tt_set_leaf_index(const TABLET_CTX *tc, MASTER_TD **dv, int n)
{
    TT_LKEY *lk;
    int i;
    for (i = 0; i < n; i++) dv[i]->lix = tc->nleaves;
    if (tc->nleaves <= 0 || n <= 0) return;
    lk = (TT_LKEY *)malloc((size_t)tc->nleaves * sizeof(TT_LKEY));
    if (!lk) return;
    for (i = 0; i < tc->nleaves; i++) {
        lk[i].part = tc->leaves[i].part;
        lk[i].sub = tc->leaves[i].sub;
        lk[i].k = i;
    }
    qsort(lk, (size_t)tc->nleaves, sizeof(TT_LKEY), lkey_cmp);
    for (i = 0; i < n; i++) {
        int lo = 0, hi = tc->nleaves;
        while (lo < hi) {
            int mid = lo + (hi - lo) / 2;
            int c = strcmp(lk[mid].part, dv[i]->part);
            if (!c) c = strcmp(lk[mid].sub, dv[i]->sub);
            if (c < 0) lo = mid + 1; else hi = mid;
        }
        if (lo < tc->nleaves && strcmp(lk[lo].part, dv[i]->part) == 0 &&
            strcmp(lk[lo].sub, dv[i]->sub) == 0)
            dv[i]->lix = lk[lo].k;
    }
    free(lk);
}

/* Partition naming of an entry (the first entry of a partitioned table is
 * its PARTITION_TABLE row, as notify_table marks it). */
static void entry_set_part(ODV_TABLE_ENTRY *e, const char *part, const char *sub)
{
    if (sub && sub[0]) {
        odv_strcpy(e->partition, sub, ODV_OBJNAME_LEN);
        odv_strcpy(e->parent_partition, part, ODV_OBJNAME_LEN);
        if (e->type == TABLE_TYPE_PARTITION) e->type = TABLE_TYPE_SUBPARTITION;
    } else {
        odv_strcpy(e->partition, part ? part : "", ODV_OBJNAME_LEN);
        e->parent_partition[0] = '\0';
    }
}

/*---------------------------------------------------------------------------
    master_apply

    After the scan: name the partitions of the data entries and add the
    tables / partitions that have no data.  list_only: every missing entry
    (0 rows, data_offset = the master table's block, which holds no rows of
    them).  Full parse (SQL export, which needs a CREATE TABLE for a 0-row
    table): one table callback per wholly missing table, honouring the table
    filter.  The lookups are sorted / binary-searched, so a dump of many
    thousands of tables or partitions stays O(n log n).
 ---------------------------------------------------------------------------*/
static void master_apply(ODV_SESSION *s, MASTER_CTX *mc, int list_only)
{
    MASTER_TD **dv = NULL;
    int *todo = NULL;     /* per TABLE object: add it (1) or not (0) */
    int i, j, t;

    if (mc->ntd > 0) {
        dv = (MASTER_TD **)malloc((size_t)mc->ntd * sizeof(MASTER_TD *));
        if (!dv) return;
        qsort(mc->td, (size_t)mc->ntd, sizeof(MASTER_TD), td_qcmp_key);
    }
    todo = (int *)calloc((size_t)(mc->ntt > 0 ? mc->ntt : 1), sizeof(int));
    if (!todo) { free(dv); return; }
    master_link_entries(s, mc);

    /* 1. Partition names of the data entries. */
    if (list_only) {
        for (i = 0; i < mc->ntd; i = j) {
            const MASTER_TD *key = &mc->td[i];
            int nd = 0, ne, e0 = 0, e, ok = 1, haspart = 0, k;
            for (j = i; j < mc->ntd && strcmp(mc->td[j].schema, key->schema) == 0 &&
                        strcmp(mc->td[j].name, key->name) == 0; j++) {
                if (mc->td[j].part[0]) haspart = 1;
                if (mc->td[j].dlen > 0) dv[nd++] = &mc->td[j];
            }
            if (!haspart) continue;
            ne = master_find_entries(s, mc, key->schema, key->name, &e0);
            if (ne == 0 || nd != ne) continue;
            qsort(dv, (size_t)nd, sizeof(MASTER_TD *), td_qcmp_data);
            for (k = 0, e = e0; k < ne; k++, e = mc->enext[e])
                if (s->table_list[e].row_count != dv[k]->rows) { ok = 0; break; }
            if (!ok) continue;
            for (k = 0, e = e0; k < ne; k++, e = mc->enext[e])
                entry_set_part(&s->table_list[e], dv[k]->part, dv[k]->sub);
        }
    }

    /* 2a. Which TABLE objects to add, decided on the list as it is now
     * (before any entry is added: the links cover the list as it is). */
    for (t = 0; t < mc->ntt; t++) {
        const MASTER_TT *tt = &mc->tt[t];
        int nd, ndata = 0, ne, e0 = 0, d0 = 0, named = 0, k;
        nd = master_find_td(mc, tt->schema, tt->name, &d0);
        for (k = 0; k < nd; k++) if (mc->td[d0 + k].dlen > 0) ndata++;
        ne = master_find_entries(s, mc, tt->schema, tt->name, &e0);
        for (k = e0; k >= 0; k = mc->enext[k])
            if (s->table_list[k].partition[0]) named = 1;
        if (ne > 0) {
            /* Data entries exist: add the empty partitions only when the
             * entries were matched to the TABLE_DATA rows (named above). */
            todo[t] = list_only && named && ndata == ne;
        } else {
            todo[t] = (ndata == 0);  /* has data, yet none was found: do not claim 0 rows */
        }
    }
    free(mc->enext);
    mc->enext = NULL;
    mc->nenext = 0;

    /* 2b. Add them, in the order of the TABLE objects. */
    for (t = 0; t < mc->ntt; t++) {
        const MASTER_TT *tt = &mc->tt[t];
        TABLET_CTX tc;
        int nd, d0 = 0, k;

        if (!todo[t]) continue;
        nd = master_find_td(mc, tt->schema, tt->name, &d0);
        if (parse_tablet(s, tt, &tc) != ODV_OK) { free(tc.leaves); continue; }
        if (is_system_table(&s->table, s->table.schema)) { free(tc.leaves); continue; }
        if (!list_only && s->filter_active) {
            int match = 1;
            char ft[ODV_OBJNAME_LEN * 4 + 1], fs[ODV_OBJNAME_LEN * 4 + 1];
            convert_name(s->table.name, s->dump_charset, s->out_charset, ft, sizeof(ft));
            convert_name(s->table.schema, s->dump_charset, s->out_charset, fs, sizeof(fs));
            if (s->filter_table[0] && odv_stricmp(ft, s->filter_table) != 0) match = 0;
            if (s->filter_schema[0] && odv_stricmp(fs, s->filter_schema) != 0) match = 0;
            if (!match) { free(tc.leaves); continue; }
        }
        s->table.ddl_offset = mc->ddl_pos;
        s->table.record_count = 0;

        if (!list_only) {
            notify_table(s, 0);
            free(tc.leaves);
            continue;
        }
        if (nd > 0) {
            /* TABLE_DATA rows: the empty ones (all of them for a table
             * without data), in the partitions' definition order (TABLE_T),
             * the same order as a METADATA_ONLY dump lists them */
            int ne2 = 0;
            for (j = 0; j < nd; j++) if (mc->td[d0 + j].dlen <= 0) dv[ne2++] = &mc->td[d0 + j];
            tt_set_leaf_index(&tc, dv, ne2);
            qsort(dv, (size_t)ne2, sizeof(MASTER_TD *), td_qcmp_leaf);
            for (k = 0; k < ne2; k++) {
                int before = s->table_count;
                notify_table(s, 0);
                if (s->table_count > before && dv[k]->part[0])
                    entry_set_part(&s->table_list[s->table_count - 1], dv[k]->part, dv[k]->sub);
            }
        } else if (tc.nleaves > 0) {
            /* no TABLE_DATA (CONTENT=METADATA_ONLY): the partitions of
             * TABLE_T in their own order */
            for (k = 0; k < tc.nleaves; k++) {
                int before = s->table_count;
                notify_table(s, 0);
                if (s->table_count > before)
                    entry_set_part(&s->table_list[s->table_count - 1],
                                   tc.leaves[k].part, tc.leaves[k].sub);
            }
        } else {
            notify_table(s, 0);
        }
        free(tc.leaves);
    }
    free(dv);
    free(todo);
}

/*---------------------------------------------------------------------------
    Accumulate LOB preview data for GUI display.

    For BLOB: hex-encode into the column value (max ODV_LOB_PREVIEW_LEN/2 bytes)
    For CLOB: copy text into the column value (max ODV_LOB_PREVIEW_LEN bytes)
    abs_col is the column's display index; the caller clears the value and
    sets st->lob_pv_fresh at the start of each LOB value (not at a
    continuation piece of the same value).
 ---------------------------------------------------------------------------*/
static void accumulate_lob_preview(ODV_SESSION *s, int abs_col,
                                   const unsigned char *data, int len)
{
    ODV_PARSE_STATE *st = &s->state;
    ODV_VALUE *v;
    int col_type;

    if (!data || len <= 0) return;
    if (abs_col < 0 || abs_col >= s->table.col_count) return;

    col_type = s->table.columns[abs_col].type;
    v = &s->record.values[abs_col];
    v->type = col_type;

    /* First chunk of this LOB value: reset the UTF-16 carry (H-05). */
    if (st->lob_pv_fresh) {
        st->lob_pv_carry_len = 0;
        st->lob_pv_fresh = 0;
    }
    v->is_null = 0;

    if (col_type == COL_BLOB) {
        /* BLOB: hex-encode (each source byte → 2 hex chars) */
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
    } else {
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
    }
}

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

/*===========================================================================
    Row data
 ===========================================================================*/

/* How a row-data column's bytes are kept while the row is read. */
#define DCLS_PLAIN 0    /* inline column: v->data, decoded at the end */
#define DCLS_LOB   1    /* CLOB / NCLOB / BLOB: preview + LOB extraction */
#define DCLS_LONG  2    /* LONG / LONG RAW: the whole value (capped) */
#define DCLS_BODY  3    /* JSON / VECTOR / VARRAY: the raw body, decoded at the end */

/* A LONG / LONG RAW value is kept whole up to the exp limit of one column,
 * 6,144,000 bytes (M-42: it used to be cut at the first 63,616-byte piece).
 * A JSON / VECTOR / VARRAY body above 16 MiB is not decoded. */
#define EXPDP_LONG_MAX   ODV_EXP_RECORD_LEN
#define EXPDP_BODY_MAX   (16 * 1024 * 1024)
#define EXPDP_MASTER_LOB_MAX (64 * 1024 * 1024)

static int data_class(int t)
{
    switch (t) {
    case COL_BLOB: case COL_CLOB: case COL_NCLOB: return DCLS_LOB;
    case COL_LONG: case COL_LONG_RAW:             return DCLS_LONG;
    case COL_JSON: case COL_VECTOR: case COL_VARRAY: return DCLS_BODY;
    default: return DCLS_PLAIN;
    }
}

/* Replace a value with a heap text of n bytes (the value buffer is grown). */
static void set_value_text(ODV_VALUE *v, const char *t, int n, int type)
{
    if (n > 0) set_value_string(v, t, n);
    else { v->is_null = 1; v->data_len = 0; }
    v->type = type;
}

static int utf8_ok(const unsigned char *p, int n)
{
    int i = 0;
    while (i < n) {
        unsigned c = p[i++];
        int k;
        if (c < 0x80) continue;
        if (c >= 0xC2 && c <= 0xDF)      k = 1;
        else if (c >= 0xE0 && c <= 0xEF) k = 2;
        else if (c >= 0xF0 && c <= 0xF4) k = 3;
        else return 0;
        if (i + k > n) return 0;
        for (; k > 0; k--, i++)
            if ((p[i] & 0xC0) != 0x80) return 0;
    }
    return 1;
}

/* How a LONG value's bytes are encoded: 1 = the DB character set (direct
 * path), 2 = UTF-16BE (external-table format), 0 = cannot tell.  Known from
 * the table's unload method (master table, see master_read_aside) or from an
 * inline LOB (H-21).  When neither is known (no master table found), a value
 * is decided only when one reading is impossible: an odd length, a U+0000 or
 * an unpaired surrogate rules out UTF-16; a NUL byte or invalid UTF-8 (an
 * AL32UTF8 dump) rules out the DB character set.  Both readings possible ->
 * 0, shown as a marker rather than guessed. */
static int long_form(ODV_SESSION *s, const unsigned char *p, int n)
{
    int db_ok = 1, u16_ok = (n % 2) == 0, i;
    if (s->state.expdp_method == 1) return 1;
    if (s->state.expdp_method == 2 || s->state.expdp_external) return 2;
    for (i = 0; i < n && db_ok; i++) if (p[i] == 0) db_ok = 0;
    if (db_ok && s->dump_charset == CHARSET_UTF8) db_ok = utf8_ok(p, n);
    for (i = 0; u16_ok && i + 1 < n; i += 2) {
        unsigned u = ((unsigned)p[i] << 8) | p[i + 1];
        if (u == 0 || (u >= 0xDC00 && u <= 0xDFFF)) u16_ok = 0;
        else if (u >= 0xD800 && u <= 0xDBFF) {
            unsigned w = (i + 3 < n) ? (((unsigned)p[i + 2] << 8) | p[i + 3]) : 0;
            if (w < 0xDC00 || w > 0xDFFF) u16_ok = 0;
            else i += 2;
        }
    }
    if (db_ok && !u16_ok) return 1;
    if (u16_ok && !db_ok) return 2;
    return 0;
}

/* LONG / LONG RAW, kept whole in v->data: LONG RAW to hex; LONG converted
 * from the DB character set (direct path) or UTF-16BE (external-table format,
 * H-21) to the output character set (long_form). */
static void decode_long_value(ODV_SESSION *s, int ac)
{
    ODV_VALUE *v = &s->record.values[ac];
    ODV_COLUMN *col = &s->table.columns[ac];
    int n = v->data_len;
    int form;

    if (!v->data || n <= 0) { v->is_null = 1; v->data_len = 0; return; }
    if (col->type == COL_LONG_RAW) {
        char *hx = (char *)malloc((size_t)n * 2 + 1);
        int i;
        if (!hx) return;
        for (i = 0; i < n; i++) {
            static const char HX[] = "0123456789ABCDEF";
            hx[i * 2]     = HX[v->data[i] >> 4];
            hx[i * 2 + 1] = HX[v->data[i] & 0x0f];
        }
        hx[n * 2] = '\0';
        set_value_text(v, hx, n * 2, col->type);
        free(hx);
        return;
    }
    form = long_form(s, v->data, n);
    if (form == 0) {
        set_value_text(v, "%LONG%", 6, col->type);
        return;
    }
    if (form == 2 ||
        (s->dump_charset != s->out_charset && s->dump_charset != CHARSET_UNKNOWN)) {
        int src_cs = (form == 2) ? CHARSET_UTF16BE : s->dump_charset;
        int cap = n * 3 + 16, out_len = 0;
        char *cb = (char *)malloc((size_t)cap);
        if (cb && convert_charset((const char *)v->data, n, src_cs, cb, cap,
                                  s->out_charset, &out_len) == ODV_OK) {
            set_value_text(v, cb, out_len, col->type);
        }
        free(cb);
    } else {
        if (n < v->buf_size) v->data[n] = '\0';
    }
    v->type = col->type;
}

/* JSON / VECTOR / VARRAY body (M-36) -> display text.  A form that was not
 * measured is shown as a marker, never as a guessed value. */
static void decode_body_value(ODV_SESSION *s, int ac, int truncated)
{
    ODV_VALUE *v = &s->record.values[ac];
    ODV_COLUMN *col = &s->table.columns[ac];
    ODV_SBUF sb;
    int rc = ODV_ERROR_FORMAT;
    const char *marker;

    if (!v->data || v->data_len <= 0) { v->is_null = 1; v->data_len = 0; v->type = col->type; return; }
    memset(&sb, 0, sizeof(sb));
    switch (col->type) {
    case COL_JSON:
        marker = "%JSON%";
        if (!truncated) rc = oson_to_json(v->data, v->data_len, &sb);
        break;
    case COL_VECTOR:
        marker = "%VECTOR%";
        if (!truncated) rc = vector_to_text(v->data, v->data_len, &sb);
        break;
    default:
        /* VARRAY: the collection image (88 01 <len> <prefix> <count> ...) is
         * readable, but the element type is not in the dump (the TYPE
         * definition is only in the compressed metadata), so the elements
         * cannot be decoded without guessing. */
        marker = "%VARRAY%";
        break;
    }
    if (rc == ODV_OK && !sb.err && sb.p)
        set_value_text(v, sb.p, sb.len, col->type);
    else
        set_value_text(v, marker, (int)strlen(marker), col->type);
    sb_free(&sb);
}

/* Append raw bytes of a LONG / BODY value (or a master-table CLOB) up to cap;
 * *trunc is set when bytes had to be dropped. */
static void append_raw(ODV_VALUE *v, const unsigned char *d, int n, int cap, int *trunc)
{
    int room = cap - v->data_len;
    int need;
    if (n > room) { *trunc = 1; n = room; }
    if (n <= 0) return;
    need = v->data_len + n + 1;
    if (need > v->buf_size) {
        /* Grow geometrically: the value arrives in 4 KB reads, and growing
         * to the exact size each time copies the whole value again on every
         * read (quadratic for a multi-MB LONG or master-table CLOB). */
        int64_t grow = (int64_t)v->buf_size * 2;
        if (grow < need) grow = need;
        if (grow > (int64_t)cap + 1) grow = (int64_t)cap + 1;
        if (grow < need) grow = need;
        need = (int)grow;
    }
    if (ensure_value_buf(v, need) != ODV_OK || !v->data) { *trunc = 1; return; }
    memcpy(v->data + v->data_len, d, (size_t)n);
    v->data_len += n;
    v->data[v->data_len] = '\0';
    v->is_null = 0;
}

static void master_row(ODV_SESSION *s);

/* A column's value is complete (all its pieces read): decode it. */
static void close_column(ODV_SESSION *s, int k, int is_master, int decode, const int *trunc)
{
    ODV_PARSE_STATE *st = &s->state;
    int ac = (k < st->map_count) ? st->non_lob_map[k] : ODV_MAX_COLUMNS;
    int cls;
    if (!decode || ac < 0 || ac >= s->table.col_count) return;
    cls = data_class(st->map_type[k]);
    if (cls == DCLS_PLAIN) {
        /* NULL (ff) / empty (00) stay as they are, as before */
        if (s->record.values[ac].data_len > 0)
            decode_column_value(s, ac);
    }
    else if (cls == DCLS_BODY && !is_master)
        decode_body_value(s, ac, trunc[ac]);
    else if (cls == DCLS_LONG && !is_master)
        decode_long_value(s, ac);
    else
        s->record.values[ac].type = s->table.columns[ac].type;   /* LOB preview / raw master CLOB */
}

/* Progress for the expdp reader.  The main loop reads a table definition a
 * block ahead and then seeks back to its end for the rows, so the position
 * can step back by up to a block (most visibly before the master table's
 * rows, M-02): never report a lower percentage than already reported. */
static void expdp_progress(ODV_SESSION *s, ODV_VFILE *fp)
{
    if (s->dump_size > 0 && s->last_progress_pct >= 0 &&
        odv_vtell(fp) * 100 / s->dump_size < s->last_progress_pct)
        return;
    odv_report_progress(s, fp);
}

/*---------------------------------------------------------------------------
    parse_expdp_records

    Reads one table's row data.  A row is one or more row pieces:

        piece  = flag [HH LL] CC  column*CC
        column = ff (NULL) | 00 (empty) | n data (n = 1..fd) | fe HH LL data

    HH LL (big-endian) is present when flag & 0x10.  CC is the number of
    columns stored in the piece (Oracle omits trailing NULLs).  Flag bits:
    0x08 first piece (F), 0x04 last piece (L), 0x02 the first column continues
    the previous piece's last column (P), 0x01 the last column continues in
    the next piece (N).  The columns are, in order, the inline columns in
    SEGCOL order, then the LOBs / LONG / LONG RAW / JSON / VECTOR / VARRAY in
    SEGCOL order.  Direct path puts each of the latter in a piece of its own;
    the external-table format keeps them inline; a value over 64 KB spans
    pieces (LONG 09 / 03 / 06, M-42); a wide row is split into several pieces
    (18 / 10 / 14, also the 11g master table 08 / 00 / 04).  This one grammar
    covers all of them (checked on the 270 expdp dumps of odvtest: every
    table's row count equals the export log), replacing the old LOB marker
    state machine that read LONG RAW / LONG / BFILE / JSON / VECTOR / VARRAY
    by type guesses (M-11 / M-36 / M-42).

    In list_only mode values are not decoded (only rows are counted), except
    for the master table (st->master), whose rows are handed to master_row.
 ---------------------------------------------------------------------------*/
static int parse_expdp_records(ODV_SESSION *s, ODV_VFILE *fp, int64_t *address,
                               int list_only)
{
    ODV_PARSE_STATE *st = &s->state;
    int is_master = (st->master != NULL);
    int decode = !list_only || is_master;
    unsigned char b;
    int record_count = 0;
    int progress_counter = 0;
    int rc;

    int in_row = 0;          /* inside a row: the next header is a continuation piece */
    int step = 1;            /* 1 = expect a piece header, 2 = reading columns */
    int data_step = DS_COL_LENGTH;
    int flag = 0;            /* current piece flag */
    int seg_rem = 0;         /* columns left in the current piece */
    int k = 0;               /* row-data stream position of the current column */
    int appending = 0;       /* current column continues a value from the previous piece */
    int col_len = 0, col_rem = 0;
    int trunc[ODV_MAX_COLUMNS];   /* per display column: LONG / BODY bytes dropped */

    /* Initialize parse state */
    st->step = 1;
    st->data_step = DS_COL_LENGTH;
    st->col_idx = 0;
    st->is_between_record = 0;
    st->lob_pv_carry_len = 0;
    st->lob_pv_fresh = 0;
    st->col_open = 0;
    st->piece_no = 0;
    st->lob_target_abs = -1;
    st->expdp_external = 0;   /* per-table; set on an inline real-LOB (H-21) */

    /* Row-data stream → display column map from the table's row-data layout
     * (table.data_*, built in SEGCOL order at DDL parse): the inline columns
     * first, then the tail columns (is_tail_type).  Each entry is the
     * destination display column index, or ODV_MAX_COLUMNS (always >=
     * col_count, so the bytes are read and discarded) for a hidden in-data
     * column — object OID, ADT attribute, nested-table SETID, invisible
     * column (H-18). */
    {
        int mi, mc = 0, pass;
        for (pass = 0; pass < 2; pass++) {
            for (mi = 0; mi < s->table.data_count && mc < ODV_MAX_COLUMNS; mi++) {
                int t = s->table.data_type[mi];
                if (is_tail_type(t) != pass) continue;
                st->non_lob_map[mc] = (s->table.data_disp[mi] >= 0)
                                      ? s->table.data_disp[mi] : ODV_MAX_COLUMNS;
                st->map_type[mc] = t;
                mc++;
            }
            if (pass == 0) st->non_lob_count = mc;
        }
        st->map_count = mc;
        st->frag_lob_count = st->map_count - st->non_lob_count;
    }

    /* In LOB extraction mode, validate the target column exists and find its
     * display index (odv_lob_check_column numbers the BLOB/CLOB/NCLOB columns
     * in display order). */
    if (s->lob_extract_mode) {
        rc = odv_lob_check_column(s);
        if (rc != ODV_OK) return rc;
        if (s->lob_column_index >= 0) {
            int i;
            for (i = 0; i < s->table.col_count; i++) {
                int t = s->table.columns[i].type;
                if ((t == COL_BLOB || t == COL_CLOB || t == COL_NCLOB) &&
                    odv_stricmp(s->table.columns[i].name, s->lob_column) == 0) {
                    st->lob_target_abs = i;
                    break;
                }
            }
        }
    }

    /* Ensure record has enough columns */
    if (s->record.max_columns < s->table.col_count) {
        free_record(&s->record);
        rc = init_record(&s->record, s->table.col_count + 16);
        if (rc != ODV_OK) return rc;
    }

    while (!s->cancelled) {
        int ac, cls, col_done = 0;

        /* Read one byte */
        if (odv_vread(&b, 1, 1, fp) != 1) break;
        (*address)++;

        /* Safety net: a table-definition block begins at a 4KB boundary with
           the dump's 2-byte charset id followed by "<?xml" at offset 2.
           If record parsing ever runs into such a block, stop so the caller
           can re-sync.  The prefix is required so that a column value
           that merely contains "<?xml" at a block boundary (which lands at
           offset 2 about once per 4096 values) is not mistaken for a new
           table definition and does not truncate the table (see H-23).
           LOB / LONG / body bytes are read in bulk and never come here. */
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
            expdp_progress(s, fp);
        }

        /* ===== Piece header ===== */
        if (step == 1) {
            unsigned char cc;
            if (!in_row && b == 0x00) {
                /* End-of-table padding between records, or leading padding. */
                if (st->is_between_record) break;
                continue;
            }
            if (b == 0xff) break;          /* end of this table's row data */
            flag = (int)b;
            if (flag & 0x10) {             /* 2-byte piece length: skipped */
                unsigned char lenb[2];
                if (odv_vread(lenb, 1, 2, fp) != 2) break;
                (*address) += 2;
            }
            if (odv_vread(&cc, 1, 1, fp) != 1) break;
            (*address)++;
            seg_rem = (int)cc;
            st->record_header = flag;

            if (!in_row) {
                /* Start of a new row. */
                reset_record(&s->record);
                s->record.col_count = s->table.col_count;
                k = 0;
                st->piece_no = 0;
                st->col_open = 0;
                st->is_between_record = 1;
                in_row = 1;
                if (decode) memset(trunc, 0, sizeof(int) * (size_t)(s->table.col_count < ODV_MAX_COLUMNS ? s->table.col_count : ODV_MAX_COLUMNS));
            } else {
                st->piece_no++;
            }
            /* P: this piece's first column continues the open column. */
            appending = 0;
            if (st->col_open) {
                if (flag & 0x02) {
                    appending = 1;
                } else {
                    /* The previous piece said the column goes on (N) but this
                     * one does not continue it: close it as it is. */
                    st->col_open = 0;
                    close_column(s, k, is_master, decode, trunc);
                    k++;
                }
            }
            step = 2;
            data_step = DS_COL_LENGTH;
            if (seg_rem > 0) continue;
            /* A piece that stores zero columns: go to its end. */
            goto PIECE_END;
        }

        /* ===== Columns ===== */
        ac  = (k < st->map_count) ? st->non_lob_map[k] : ODV_MAX_COLUMNS;
        cls = (k < st->map_count) ? data_class(st->map_type[k]) : DCLS_PLAIN;
        if (is_master && cls == DCLS_LOB) cls = DCLS_LONG;   /* keep the whole CLOB */

        switch (data_step) {
        case DS_COL_LENGTH:
            if (b == 0xff || b == 0x00) {
                /* NULL / empty (the API does not tell them apart) */
                if (!appending && ac < s->table.col_count) {
                    if (b == 0xff) set_value_null(&s->record.values[ac]);
                    else set_value_string(&s->record.values[ac], "", 0);
                    s->record.values[ac].type = s->table.columns[ac].type;
                }
                col_done = 1;
                break;
            }
            if (b == 0xfe) { data_step = DS_COL_LEN_HI; break; }
            col_len = (int)b;
            goto START_DATA;

        case DS_COL_LEN_HI:
            col_len = (int)b << 8;
            data_step = DS_COL_LEN_LO;
            break;

        case DS_COL_LEN_LO:
            col_len |= (int)b;
            if (col_len <= 0) {
                if (!appending && ac < s->table.col_count) {
                    set_value_string(&s->record.values[ac], "", 0);
                    s->record.values[ac].type = s->table.columns[ac].type;
                }
                col_done = 1;
                break;
            }
        START_DATA:
            if (cls == DCLS_PLAIN) {
                /* Inline column: read byte by byte (the block-boundary safety
                 * net above stays active for these). */
                col_rem = col_len;
                data_step = DS_COL_DATA;
                if (decode && ac < s->table.col_count) {
                    ODV_VALUE *v = &s->record.values[ac];
                    int base = appending ? v->data_len : 0;
                    if (base + col_len + 1 > ODV_VARCHAR_LEN) base = 0;
                    ensure_value_buf(v, base + col_len + 1);
                    v->data_len = base;
                }
                break;
            } else {
                /* LOB / LONG / body: read the bytes in bulk. */
                unsigned char tmp[4096];
                int got_all = 0;
                ODV_VALUE *v = (ac < s->table.col_count) ? &s->record.values[ac] : NULL;

                if (!appending && v) {
                    v->data_len = 0;
                    v->is_null = 1;
                    v->type = s->table.columns[ac].type;
                    if (cls == DCLS_LOB) st->lob_pv_fresh = 1;
                }
                /* External-table format: a real LOB inline in the row's first
                 * piece (direct path never does that) — latch it for this
                 * table so an inline LONG is read as UTF-16BE (H-21). */
                if (st->piece_no == 0 && !is_master) {
                    int t = st->map_type[k];
                    if (t == COL_BLOB || t == COL_CLOB || t == COL_NCLOB || t == COL_LONG_RAW)
                        st->expdp_external = 1;
                }
                if (!decode && !(s->lob_extract_mode && ac == st->lob_target_abs)) {
                    /* list only: skip the bytes */
                    if (odv_vseek(fp, col_len, SEEK_CUR) != 0) goto END_PARSE;
                    *address += col_len;
                } else {
                    while (got_all < col_len && !s->cancelled) {
                        int need = col_len - got_all;
                        int blk = need < (int)sizeof(tmp) ? need : (int)sizeof(tmp);
                        int got = (int)odv_vread(tmp, 1, blk, fp);
                        if (got <= 0) goto END_PARSE;
                        *address += got;
                        got_all += got;
                        if (s->lob_extract_mode && s->lob_column_index >= 0 &&
                            ac == st->lob_target_abs) {
                            rc = odv_lob_accumulate(s, s->lob_column_index, tmp, got);
                            if (rc != ODV_OK) return rc;
                        }
                        if (!decode || !v) continue;
                        if (cls == DCLS_LOB)
                            accumulate_lob_preview(s, ac, tmp, got);
                        else
                            append_raw(v, tmp, got,
                                       is_master ? EXPDP_MASTER_LOB_MAX :
                                       cls == DCLS_LONG ? EXPDP_LONG_MAX : EXPDP_BODY_MAX,
                                       &trunc[ac]);
                    }
                }
                col_done = 1;
            }
            break;

        case DS_COL_DATA:
            if (decode && ac < s->table.col_count) {
                ODV_VALUE *v = &s->record.values[ac];
                if (v->data && v->data_len < v->buf_size - 1) {
                    v->data[v->data_len++] = b;
                }
            }
            if (--col_rem <= 0) col_done = 1;
            break;

        default:
            data_step = DS_COL_LENGTH;
            break;
        }

        if (!col_done) continue;

        /* ----- one column (or one piece of it) read ----- */
        data_step = DS_COL_LENGTH;
        if (seg_rem == 1 && (flag & 0x01)) {
            /* N: the value goes on in the next piece */
            st->col_open = 1;
        } else {
            st->col_open = 0;
            close_column(s, k, is_master, decode, trunc);
            k++;
        }
        appending = 0;
        seg_rem--;
        if (seg_rem > 0) continue;

    PIECE_END:
        step = 1;
        if (!(flag & 0x04)) continue;      /* not the last piece: more follow */

        /* Last piece (L): the row is complete; columns Oracle omitted as
         * trailing NULLs stay NULL (reset_record cleared the record). */
        if (st->col_open) {                /* L together with N: close it */
            st->col_open = 0;
            close_column(s, k, is_master, decode, trunc);
        }
        in_row = 0;
        s->record.col_count = s->table.col_count;
        if (s->lob_extract_mode && s->lob_column_index >= 0) {
            rc = odv_lob_write_file(s);
            if (rc != ODV_OK) return rc;
        }
        if (is_master) {
            master_row(s);
        } else if (!list_only) {
            rc = deliver_row(s);
            if (rc != ODV_OK) return rc;
            expdp_progress(s, fp);
        }
        record_count++;
        if (!is_master) s->table.record_count++;
    }

END_PARSE:
    (void)record_count;
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
                /* already named from the master table's TABLE_DATA rows */
                for (ti = 0; ti < s->table_count; ti++)
                    if (s->table_list[ti].partition[0] &&
                        strcmp(s->table_list[ti].schema, pmarks[k].schema) == 0 &&
                        strcmp(s->table_list[ti].name,   pmarks[k].table)  == 0) { ok = 0; break; }
                if (!ok) continue;

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
    The unload method of a table (external-table LONG)

    A LONG of a table without LOB columns is written alike by both access
    methods except for its bytes: direct path keeps the DB character set, the
    external-table format UTF-16BE ('x' = 01 78 / 02 00 78).  The row pieces,
    the STRMTABLE_T and the bytes around them are otherwise identical
    (measured on 11.2 / 21c / 23ai, ACCESS_METHOD=DIRECT_PATH vs
    EXTERNAL_TABLE); the master table's TABLE_DATA row says which was used:
    UNLOAD_METHOD 1 = direct path, 2 = external table.

    The master is often not on the way (a table opened at its data_offset; a
    master written after the data), so it is read out of turn.  Its block
    number is in the dump file header (big-endian at offset 0x36): 11.2 / 21c
    in the first block, 23ai in the trailer block (the first header keeps 0
    there); in a split set, in the file that holds the master's start.
 ---------------------------------------------------------------------------*/

/* Logical position of the master table's definition block, or -1: the
 * first file of the (split) set whose header says it holds the master's
 * start (odv_expdp_master_offset, odv_api.c). */
static int64_t master_locate(const ODV_VFILE *vf)
{
    int i;
    for (i = 0; i < vf->nfiles; i++) {
        const ODV_VSEG *sg = &vf->seg[i];
        int64_t p = odv_expdp_master_offset(sg->path);
        if (p >= sg->skip && p - sg->skip < sg->avail)
            return sg->logstart + (p - sg->skip);
    }
    return -1;
}

/* Read the master table's TABLE_DATA rows into lk (td_only) out of turn.
 * The current table definition, the file position and the progress shown
 * are kept.  Returns 1 when the master was read. */
static int master_read_aside(ODV_SESSION *s, ODV_VFILE *fp, MASTER_CTX *lk)
{
    int64_t pos = odv_vtell(fp), at = master_locate(fp);
    ODV_TABLE *saved;
    char *buf = NULL;
    int len = 0, end = -1, ok = 0;
    int cut = s->stream_cut;    /* a cut met here is not the operation's (M-21) */

    if (at < 0) return 0;
    saved = (ODV_TABLE *)malloc(sizeof(ODV_TABLE));
    if (!saved) return 0;
    memcpy(saved, &s->table, sizeof(ODV_TABLE));

    /* The definition: from the block start (2-byte prefix, then <?xml) to
     * </ROWSET>, at most 16 MB. */
    odv_vseek(fp, at, SEEK_SET);
    while (end < 0 && len < 16 * 1024 * 1024 && !s->cancelled) {
        char *nb = (char *)realloc(buf, (size_t)len + ODV_DUMP_BLOCK_LEN + 1);
        int n, i, from;
        if (!nb) break;
        buf = nb;
        n = (int)odv_vread(buf + len, 1, ODV_DUMP_BLOCK_LEN, fp);
        if (n <= 0) break;
        from = (len > 10) ? len - 8 : 2;
        len += n;
        for (i = from; i + 9 <= len; i++)
            if (memcmp(buf + i, "</ROWSET>", 9) == 0) { end = i + 9; break; }
    }
    if (end > 0) {
        DDL_CONTEXT dc;
        memset(&dc, 0, sizeof(dc));
        dc.session = s;
        memset(&s->table, 0, sizeof(ODV_TABLE));   /* saved keeps the pointers */
        s->table.dump_charset = s->dump_charset;
        s->table.os_charset = s->out_charset;
        invalidate_meta_cache();
        parse_xml_ddl(buf + 2, end - 2, ddl_xml_callback, &dc);
        if (s->table.name[0] && master_columns(&s->table, lk)) {
            ODV_PROGRESS_CALLBACK cb = s->progress_cb;
            int pct = s->last_progress_pct;
            int lobx = s->lob_extract_mode;   /* the master is not the LOB's table */
            int64_t addr = at + end;
            odv_vseek(fp, addr, SEEK_SET);
            lk->td_only = 1;
            s->progress_cb = NULL;
            s->lob_extract_mode = 0;
            s->state.master = lk;
            parse_expdp_records(s, fp, &addr, 1);
            s->state.master = NULL;
            s->lob_extract_mode = lobx;
            s->progress_cb = cb;
            s->last_progress_pct = pct;
            ok = 1;
        }
        odv_table_free_text(&s->table);
    }
    memcpy(&s->table, saved, sizeof(ODV_TABLE));
    free(saved);
    free(buf);
    invalidate_meta_cache();
    odv_vseek(fp, pos, SEEK_SET);
    s->stream_cut = cut;
    return ok;
}

/* The table has a LONG column (whose bytes depend on the unload method). */
static int table_has_long(const ODV_TABLE *t)
{
    int i;
    for (i = 0; i < t->data_count; i++)
        if (t->data_type[i] == COL_LONG) return 1;
    return 0;
}

/* The unload method of the current table from the master's TABLE_DATA rows:
 * 1 / 2 when every segment of the table agrees, else 0. */
static int master_unload_method(ODV_SESSION *s, const MASTER_CTX *mc)
{
    char cs[ODV_OBJNAME_LEN * 4 + 1], cn[ODV_OBJNAME_LEN * 4 + 1];
    int i, m = 0;
    convert_name(s->table.schema, s->dump_charset, s->out_charset, cs, sizeof(cs));
    convert_name(s->table.name, s->dump_charset, s->out_charset, cn, sizeof(cn));
    for (i = 0; i < mc->ntd; i++) {
        const MASTER_TD *d = &mc->td[i];
        if (strcmp(d->name, cn) != 0 || strcmp(d->schema, cs) != 0) continue;
        if ((d->umeth != 1 && d->umeth != 2) || (m && m != d->umeth)) return 0;
        m = d->umeth;
    }
    return m;
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
    MASTER_CTX mc;              /* master-table rows (M-02) */
    int master_done = 0;
    MASTER_CTX mlk;             /* master rows read out of turn (unload method) */
    int mlk_state = 0;          /* 0 = not tried, 1 = read, -1 = not found */
    int multi = 0;              /* reading the entries of s->seek_list in turn */
    int seg_next = 0;           /* next s->seek_list entry */
    int target_part = 0;        /* the filtered table is partitioned */
    int n, rc;
    unsigned char skip_blk[ODV_DUMP_BLOCK_LEN];  /* buffer for skipping record data */

    if (!s) return ODV_ERROR_INVALID_ARG;
    memset(&mc, 0, sizeof(mc));
    memset(&mlk, 0, sizeof(mlk));
    s->state.master = NULL;
    s->state.expdp_method = 0;

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
    if (s->filter_active && s->seek_list && s->seek_list_count > 0) {
        /* The whole of a partitioned table (odv_set_data_offsets): the entry
         * at each offset in turn, one table definition each. */
        int64_t aligned = (s->seek_list[0] / ODV_DUMP_BLOCK_LEN) * ODV_DUMP_BLOCK_LEN;
        multi = 1;
        seg_next = 1;
        odv_vseek(fp, aligned, SEEK_SET);
        address = aligned;
    } else if (s->seek_offset > 0 && s->filter_active) {
        int64_t aligned = (s->seek_offset / ODV_DUMP_BLOCK_LEN) * ODV_DUMP_BLOCK_LEN;
        odv_vseek(fp, aligned, SEEK_SET);
        address = aligned;
    }

    /* Read blocks sequentially */
    while (!s->cancelled) {
        n = (int)odv_vread(block, 1, ODV_DUMP_BLOCK_LEN, fp);
        if (n <= 0) break;

        /* Report progress during DDL scan so UI stays responsive */
        expdp_progress(s, fp);

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
                odv_tz_capture_dbtz_xml(s, ddl_buf, end_pos);   /* <DBTIMEZONE> for TSLTZ SQL (M-09) */

                /* Record DDL position for fast seek on next parse */
                s->table.ddl_offset = cur_ddl_pos;
                s->state.expdp_method = 0;

                /* Seek to just after </ROWSET> for record data */
                odv_vseek(fp, cur_ddl_pos + end_pos, SEEK_SET);
                address = odv_vtell(fp);

                /* The master table: read its rows for the tables and
                 * partitions that have no data (M-02).  A listing always needs
                 * them; a full parse only for a table callback (the SQL
                 * export's CREATE TABLE of a 0-row table). */
                if (!master_done && s->table.name[0] != '\0' &&
                    (list_only || (s->table_cb && !(s->filter_active && filter_found))) &&
                    master_columns(&s->table, &mc)) {
                    mc.ddl_pos = cur_ddl_pos;
                    s->state.master = &mc;
                    rc = parse_expdp_records(s, fp, &address, list_only);
                    s->state.master = NULL;
                    master_done = 1;
                    (void)rc;
                }
                /* Skip dictionary tables and metadata-only XMLs (0 columns) */
                else if (s->table.name[0] != '\0' && s->table.col_count > 0
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
                           now a different table appeared → done.  Not for
                           a partitioned table: EXPDP writes its partitions
                           in data order (by size), other tables in between,
                           so without the entries' offsets (multi) its
                           partitions are looked for to the end of the dump. */
                        if (filter_found && s->pass_flg && !multi && !target_part) {
                            in_ddl = 0;
                            goto expdp_done;
                        }
                        if (match) {
                            filter_found = 1;
                            if (s->table.is_partition) target_part = 1;
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
                        /* Full parse (no filter or filter matched).  A LONG
                         * needs the unload method (master table). */
                        if (table_has_long(&s->table)) {
                            if (master_done) {
                                s->state.expdp_method = master_unload_method(s, &mc);
                            } else {
                                if (mlk_state == 0)
                                    mlk_state = master_read_aside(s, fp, &mlk) ? 1 : -1;
                                if (mlk_state > 0)
                                    s->state.expdp_method = master_unload_method(s, &mlk);
                            }
                        }
                        rc = parse_expdp_records(s, fp, &address, list_only);
                        notify_table(s, s->table.record_count);
                        if (rc != ODV_OK && rc != ODV_ERROR_CANCELLED) { /* non-fatal */ }

                        /* When seek_offset was used, we parsed exactly one partition's
                         * data at the target position. Exit now to avoid parsing
                         * subsequent partitions of the same table name. */
                        if (s->filter_active && s->seek_offset > 0 && filter_found && !multi) {
                            in_ddl = 0;
                            goto expdp_done;
                        }
                    } else {
                        /* Filtered out in full parse: skip records */
                        notify_table(s, 0);
                    }
                }

                /* The whole of a partitioned table: on to the next entry. */
                if (multi) {
                    in_ddl = 0;
                    ddl_len = 0;
                    if (seg_next >= s->seek_list_count) goto expdp_done;
                    odv_vseek(fp, (s->seek_list[seg_next] / ODV_DUMP_BLOCK_LEN) *
                                  ODV_DUMP_BLOCK_LEN, SEEK_SET);
                    seg_next++;
                    address = odv_vtell(fp);
                    continue;
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
    /* Tables / partitions without data, and the partition names of the data
     * entries, from the master table (M-02). */
    if (master_done && !s->cancelled)
        master_apply(s, &mc, list_only);
    master_free(&mc);
    master_free(&mlk);
    s->state.expdp_method = 0;

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
