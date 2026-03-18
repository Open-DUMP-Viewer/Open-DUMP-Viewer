/*****************************************************************************
    OraDB DUMP Viewer

    odv_exp.c
    Legacy EXP (conventional export) format dump file parsing

    EXP format structure:
      - 256-byte binary header (version, mode, charset)
      - DDL text (CREATE TABLE, INSERT INTO)
      - Binary record data (2-byte LE length + column data)

    Copyright (C) 2026 YANAI Taketo
 *****************************************************************************/

#include "odv_types.h"
#include <stdio.h>
#include <ctype.h>

/*---------------------------------------------------------------------------
    Constants
 ---------------------------------------------------------------------------*/
#define EXP_HEADER_SIZE     0x100
#define EXP_DDL_BUF_SIZE    262144   /* 256KB for single DDL statement */

/*---------------------------------------------------------------------------
    Forward declarations
 ---------------------------------------------------------------------------*/
static int parse_exp_header(ODV_SESSION *s, FILE *fp);
static int parse_exp_ddl_and_data(ODV_SESSION *s, FILE *fp, int list_only);
static int parse_column_type(const char *type_str, ODV_COLUMN *col);
static void trim_right(char *str);

/*---------------------------------------------------------------------------
    Helper: case-insensitive prefix match
 ---------------------------------------------------------------------------*/
static int starts_with_ci(const char *str, const char *prefix)
{
    while (*prefix) {
        if (toupper((unsigned char)*str) != toupper((unsigned char)*prefix))
            return 0;
        str++;
        prefix++;
    }
    return 1;
}

/*---------------------------------------------------------------------------
    Helper: skip whitespace
 ---------------------------------------------------------------------------*/
static const char *skip_ws(const char *p)
{
    while (*p && ((unsigned char)*p <= ' ')) p++;
    return p;
}

/*---------------------------------------------------------------------------
    Helper: extract quoted identifier or bare word
    Returns pointer past the extracted token.
 ---------------------------------------------------------------------------*/
static const char *extract_identifier(const char *p, char *out, int max_len)
{
    int len = 0;
    p = skip_ws(p);

    if (*p == '"') {
        p++; /* skip opening quote */
        while (*p && *p != '"' && len < max_len - 1) {
            out[len++] = *p++;
        }
        if (*p == '"') p++; /* skip closing quote */
    } else {
        while (*p && *p != ' ' && *p != ',' && *p != '(' && *p != ')'
               && *p != '\t' && *p != '\n' && *p != '\r'
               && len < max_len - 1) {
            out[len++] = *p++;
        }
    }
    out[len] = '\0';
    return p;
}

/*---------------------------------------------------------------------------
    byte_to_charset

    Maps a charset indicator byte from the EXP header to an internal constant.
    Byte ranges:
      0x30-0x3f -> JA16EUC
      0x40-0x4f -> JA16SJIS
      0x60-0x6f -> UTF8
      0xd0-0xdf -> UTF16
      other     -> US7ASCII
 ---------------------------------------------------------------------------*/
static int byte_to_charset(unsigned char b)
{
    if (b >= 0x30 && b <= 0x3f) return CHARSET_EUC;
    if (b >= 0x40 && b <= 0x4f) return CHARSET_SJIS;
    if (b >= 0x60 && b <= 0x6f) return CHARSET_UTF8;
    if (b >= 0xd0 && b <= 0xdf) return CHARSET_UTF16LE;
    return CHARSET_US7;
}

/*---------------------------------------------------------------------------
    parse_exp_header

    Reads the 256-byte EXP header and extracts:
    - Oracle version
    - Export mode (TABLE/USER/DATABASE)
    - Character sets (client, database, NLS)

    Header structure: records separated by 0x00 or 0x0A, starting at offset 6.
    Record 0: Oracle version (e.g. "V11.02.00")
    Record 1: Export user/schema
    Record 2: Export mode ("RTABLES"/"RUSERS"/"RENTIRE")
    Records 3-6: (misc)
    Record 7: Charset info - byte[1]=env, byte[3]=tbl, byte[5]=nls
 ---------------------------------------------------------------------------*/
static int parse_exp_header(ODV_SESSION *s, FILE *fp)
{
    unsigned char hdr[EXP_HEADER_SIZE];
    unsigned char word[256];
    int i, ct, step;

    odv_fseek(fp, 0, SEEK_SET);
    if (fread(hdr, 1, EXP_HEADER_SIZE, fp) != EXP_HEADER_SIZE) {
        odv_strcpy(s->last_error, "Cannot read EXP header", ODV_MSG_LEN);
        return ODV_ERROR_FREAD;
    }

    /* Check for direct export mode: look for "D\n" in first 32 bytes */
    for (i = 0; i < 31; i++) {
        if (hdr[i] == 'D' && hdr[i + 1] == 0x0A) {
            s->dump_type = DUMP_EXP_DIRECT;
            break;
        }
    }

    /* Parse header records separated by 0x00 or 0x0A */
    ct = 0;
    step = 0;
    s->exp_state.exp_mode = EXP_MODE_TABLE;

    for (i = 6; i < 0x100; i++) {
        switch (hdr[i]) {
        case 0x00:
        case 0x0A:
            word[ct] = '\0';
            ct = 0;

            switch (step) {
            case 0:
                /* Record 0: Oracle version ("V11.02.00") */
                if (word[0] == 'V' || word[0] == 'v')
                    s->exp_state.oracle_version = atoi((char *)&word[1]);
                else
                    s->exp_state.oracle_version = atoi((char *)&word[2]);
                break;
            case 1:
                /* Record 1: Export user/schema (e.g. "DNO1CCUSER") */
                if (word[0] != '\0')
                    odv_strcpy(s->exp_state.export_user, (char *)word,
                               ODV_OBJNAME_LEN);
                break;
            case 2:
                /* Record 2: Export mode */
                if (memcmp(word, "RTABLES", 7) == 0)
                    s->exp_state.exp_mode = EXP_MODE_TABLE;
                else if (memcmp(word, "RUSERS", 6) == 0)
                    s->exp_state.exp_mode = EXP_MODE_USER;
                else if (memcmp(word, "RENTIRE", 7) == 0)
                    s->exp_state.exp_mode = EXP_MODE_DATABASE;
                break;
            case 7: {
                /* Record 7: Character set info */
                /* word[1]=env charset, word[3]=tbl charset, word[5]=nls charset */
                int tbl_cs;
                if (ct >= 2 || strlen((char*)word) >= 2) {
                    /* env charset = word[1] (informational, not used directly) */
                }
                if (ct >= 4 || strlen((char*)word) >= 4) {
                    /* tbl charset = word[3]: this is the database charset */
                    tbl_cs = byte_to_charset(word[3]);
                    s->dump_charset = tbl_cs;
                } else if (ct >= 2 || strlen((char*)word) >= 2) {
                    /* Fallback: use env charset */
                    s->dump_charset = byte_to_charset(word[1]);
                }
                break;
            }
            default:
                break;
            }
            step++;
            break;

        default:
            if (ct < (int)sizeof(word) - 1) {
                word[ct] = hdr[i];
                ct++;
            }
            break;
        }

        /* Stop after charset record */
        if (step > 7) break;
    }

    s->exp_state.header_size = EXP_HEADER_SIZE;
    return ODV_OK;
}

/*---------------------------------------------------------------------------
    parse_create_table

    Parses a CREATE TABLE DDL statement to extract table name and columns.
    Returns 1 if a table was found, 0 otherwise.
 ---------------------------------------------------------------------------*/
static int parse_create_table(ODV_SESSION *s, const char *ddl)
{
    const char *p;
    char schema[ODV_OBJNAME_LEN + 1] = {0};
    char table_name[ODV_OBJNAME_LEN + 1] = {0};
    char col_name[ODV_OBJNAME_LEN + 1];
    char type_str[256];
    char default_str[256];
    int not_null_flag;
    int col_count = 0;
    int paren_depth;

    /* Find "CREATE TABLE" */
    p = ddl;
    while (*p) {
        if (starts_with_ci(p, "CREATE") && ((unsigned char)p[6] <= ' ')) {
            p += 6;
            p = skip_ws(p);
            if (starts_with_ci(p, "TABLE") && ((unsigned char)p[5] <= ' ')) {
                p += 5;
                break;
            }
        }
        p++;
    }
    if (!*p) return 0;

    /* Extract table name: "schema"."table" or "table" */
    p = extract_identifier(p, table_name, ODV_OBJNAME_LEN);
    p = skip_ws(p);

    if (*p == '.') {
        /* schema.table format */
        odv_strcpy(schema, table_name, ODV_OBJNAME_LEN);
        p++;
        p = extract_identifier(p, table_name, ODV_OBJNAME_LEN);
        p = skip_ws(p);
    }

    /* Store table info */
    memset(&s->table, 0, sizeof(ODV_TABLE));
    odv_strcpy(s->table.schema, schema, ODV_OBJNAME_LEN);
    odv_strcpy(s->table.name, table_name, ODV_OBJNAME_LEN);
    s->table.dump_charset = s->dump_charset;
    s->table.col_count = 0;
    s->table.lob_col_count = 0;

    /* Expect opening parenthesis */
    if (*p != '(') return 0;
    p++;

    /* Parse column definitions */
    while (*p && col_count < ODV_MAX_COLUMNS) {
        int type_len = 0;
        not_null_flag = 0;
        default_str[0] = '\0';

        p = skip_ws(p);
        if (*p == ')') break;

        /* Extract column name */
        col_name[0] = '\0';
        p = extract_identifier(p, col_name, ODV_OBJNAME_LEN);
        if (col_name[0] == '\0') break;

        /* Skip constraint keywords that aren't column definitions */
        if (starts_with_ci(col_name, "CONSTRAINT") ||
            starts_with_ci(col_name, "PRIMARY") ||
            starts_with_ci(col_name, "UNIQUE") ||
            starts_with_ci(col_name, "FOREIGN") ||
            starts_with_ci(col_name, "CHECK")) {
            /* Skip to next comma at same depth or closing paren */
            paren_depth = 0;
            while (*p) {
                if (*p == '(') paren_depth++;
                else if (*p == ')') {
                    if (paren_depth == 0) break;
                    paren_depth--;
                }
                else if (*p == ',' && paren_depth == 0) { p++; break; }
                p++;
            }
            continue;
        }

        p = skip_ws(p);

        /* Extract type string (up to comma, closing paren, or constraint) */
        type_str[0] = '\0';
        type_len = 0;
        paren_depth = 0;

        while (*p && type_len < 250) {
            if (*p == '(') {
                paren_depth++;
                type_str[type_len++] = *p++;
            } else if (*p == ')') {
                if (paren_depth > 0) {
                    paren_depth--;
                    type_str[type_len++] = *p++;
                } else {
                    break; /* End of column list */
                }
            } else if (*p == ',' && paren_depth == 0) {
                p++; /* skip comma */
                break;
            } else if (starts_with_ci(p, "NOT ") || starts_with_ci(p, "DEFAULT ") ||
                       starts_with_ci(p, "CONSTRAINT ") || starts_with_ci(p, "ENABLE")) {
                /* Parse constraint keywords before skipping */
                type_str[type_len] = '\0';
                trim_right(type_str);
                /* Parse NOT NULL, DEFAULT, etc. until comma or closing paren */
                while (*p && *p != ',' && *p != ')') {
                    if (starts_with_ci(p, "NOT NULL")) {
                        not_null_flag = 1;
                        p += 8;
                    } else if (starts_with_ci(p, "DEFAULT ")) {
                        int dlen = 0;
                        p += 8;
                        p = skip_ws(p);
                        /* Extract DEFAULT value expression */
                        {
                            int d = 0;
                            while (*p && dlen < 250) {
                                if (*p == '(') { d++; default_str[dlen++] = *p++; }
                                else if (*p == ')') {
                                    if (d > 0) { d--; default_str[dlen++] = *p++; }
                                    else break;
                                }
                                else if ((*p == ',' || *p == ' ') && d == 0) {
                                    /* Check if space is followed by NOT/CONSTRAINT/ENABLE */
                                    if (*p == ' ') {
                                        const char *np = skip_ws(p);
                                        if (starts_with_ci(np, "NOT ") ||
                                            starts_with_ci(np, "CONSTRAINT ") ||
                                            starts_with_ci(np, "ENABLE")) {
                                            p = np;
                                            break;
                                        }
                                        default_str[dlen++] = *p++;
                                    } else {
                                        break; /* comma */
                                    }
                                }
                                else { default_str[dlen++] = *p++; }
                            }
                            default_str[dlen] = '\0';
                            trim_right(default_str);
                        }
                    } else if (*p == '(') {
                        /* Skip nested parens in CONSTRAINT expressions */
                        int d = 1;
                        p++;
                        while (*p && d > 0) {
                            if (*p == '(') d++;
                            else if (*p == ')') d--;
                            p++;
                        }
                    } else {
                        p++;
                    }
                }
                if (*p == ',') p++;
                break;
            } else {
                type_str[type_len++] = *p++;
            }
        }
        type_str[type_len] = '\0';
        trim_right(type_str);

        /* Store column */
        if (col_name[0] != '\0' && type_str[0] != '\0') {
            ODV_COLUMN *col = &s->table.columns[col_count];
            memset(col, 0, sizeof(ODV_COLUMN));
            odv_strcpy(col->name, col_name, ODV_OBJNAME_LEN);
            parse_column_type(type_str, col);
            col->not_null = not_null_flag;
            odv_strcpy(col->default_val, default_str, 255);
            col_count++;
        }
    }

    s->table.col_count = col_count;

    /* Count LOB columns.
       LOB types: BLOB, CLOB, NCLOB, BFILE, USER_DEFINE
       Note: LONG and LONG_RAW are NOT counted as LOB */
    {
        int i;
        for (i = 0; i < col_count; i++) {
            int t = s->table.columns[i].type;
            if (t == COL_BLOB || t == COL_CLOB || t == COL_NCLOB ||
                t == COL_BFILE || t == COL_USER_DEFINE) {
                s->table.lob_col_count++;
            }
        }
    }

    return 1;
}

/*---------------------------------------------------------------------------
    parse_column_type

    Parses a type string like "VARCHAR2(100)" into ODV_COLUMN fields.
 ---------------------------------------------------------------------------*/
static int parse_column_type(const char *type_str, ODV_COLUMN *col)
{
    char upper[256];
    int i;
    const char *p;

    /* Copy and uppercase for matching */
    for (i = 0; type_str[i] && i < 255; i++) {
        upper[i] = (char)toupper((unsigned char)type_str[i]);
    }
    upper[i] = '\0';

    /* Store original type string */
    odv_strcpy(col->type_str, type_str, 63);

    /* Match type and extract parameters */
    if (starts_with_ci(upper, "VARCHAR2") || starts_with_ci(upper, "VARCHAR")) {
        col->type = COL_VARCHAR;
    } else if (starts_with_ci(upper, "NVARCHAR2") || starts_with_ci(upper, "NVARCHAR")) {
        col->type = COL_NVARCHAR;
    } else if (starts_with_ci(upper, "NCHAR")) {
        col->type = COL_NCHAR;
    } else if (starts_with_ci(upper, "CHAR")) {
        col->type = COL_CHAR;
    } else if (starts_with_ci(upper, "NUMBER")) {
        col->type = COL_NUMBER;
    } else if (starts_with_ci(upper, "FLOAT")) {
        col->type = COL_FLOAT;
    } else if (starts_with_ci(upper, "BINARY_FLOAT")) {
        col->type = COL_BIN_FLOAT;
    } else if (starts_with_ci(upper, "BINARY_DOUBLE")) {
        col->type = COL_BIN_DOUBLE;
    } else if (starts_with_ci(upper, "TIMESTAMP") && strstr(upper, "LOCAL")) {
        col->type = COL_TIMESTAMP_LTZ;
    } else if (starts_with_ci(upper, "TIMESTAMP") && strstr(upper, "TIME ZONE")) {
        col->type = COL_TIMESTAMP_TZ;
    } else if (starts_with_ci(upper, "TIMESTAMP")) {
        col->type = COL_TIMESTAMP;
    } else if (starts_with_ci(upper, "DATE")) {
        col->type = COL_DATE;
    } else if (starts_with_ci(upper, "INTERVAL") && strstr(upper, "YEAR")) {
        col->type = COL_INTERVAL_YM;
    } else if (starts_with_ci(upper, "INTERVAL") && strstr(upper, "DAY")) {
        col->type = COL_INTERVAL_DS;
    } else if (starts_with_ci(upper, "LONG RAW")) {
        col->type = COL_LONG_RAW;
    } else if (starts_with_ci(upper, "LONG")) {
        col->type = COL_LONG;
    } else if (starts_with_ci(upper, "RAW")) {
        col->type = COL_RAW;
    } else if (starts_with_ci(upper, "BLOB")) {
        col->type = COL_BLOB;
    } else if (starts_with_ci(upper, "NCLOB")) {
        col->type = COL_NCLOB;
    } else if (starts_with_ci(upper, "CLOB")) {
        col->type = COL_CLOB;
    } else if (starts_with_ci(upper, "BFILE")) {
        col->type = COL_BFILE;
    } else if (starts_with_ci(upper, "XMLTYPE")) {
        col->type = COL_XMLTYPE;
    } else if (starts_with_ci(upper, "ROWID") || starts_with_ci(upper, "UROWID")) {
        col->type = COL_ROWID;
    } else {
        col->type = COL_VARCHAR;  /* default fallback */
    }

    /* Extract length/precision/scale from parentheses */
    {
        int has_parens = 0;
        p = strchr(upper, '(');
        if (p) {
            has_parens = 1;
            p++;
            col->length = atoi(p);

            /* For NUMBER(p,s) */
            p = strchr(p, ',');
            if (p) {
                p++;
                col->scale = atoi(p);
                col->precision = col->length;
            }
        }

        /* For TIMESTAMP types, parenthesized value is fractional seconds precision */
        switch (col->type) {
        case COL_TIMESTAMP:
        case COL_TIMESTAMP_TZ:
        case COL_TIMESTAMP_LTZ:
            if (has_parens) {
                col->precision = col->length;
                col->length = 0; /* Reset to get default byte length below */
            } else {
                col->precision = 6; /* Oracle default */
            }
            break;
        }
    }

    /* Default lengths for types without explicit size */
    if (col->length == 0) {
        switch (col->type) {
        case COL_DATE:          col->length = 7; break;
        case COL_TIMESTAMP:     col->length = 11; break;
        case COL_TIMESTAMP_TZ:  col->length = 13; break;
        case COL_TIMESTAMP_LTZ: col->length = 11; break;
        case COL_BIN_FLOAT:     col->length = 4; break;
        case COL_BIN_DOUBLE:    col->length = 8; break;
        case COL_ROWID:         col->length = 18; break;
        default: break;
        }
    }

    return ODV_OK;
}

/*---------------------------------------------------------------------------
    notify_exp_table

    Notifies via table callback and adds to table list.
 ---------------------------------------------------------------------------*/
static void conv_name(const char *src, int src_cs, int dst_cs,
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

static void notify_exp_table(ODV_SESSION *s, int64_t row_count)
{
    const char *col_names[ODV_MAX_COLUMNS];
    const char *col_types[ODV_MAX_COLUMNS];
    int col_not_nulls[ODV_MAX_COLUMNS];
    const char *col_defaults[ODV_MAX_COLUMNS];
    char conv_schema[ODV_OBJNAME_LEN * 4 + 1];
    char conv_name_buf[ODV_OBJNAME_LEN * 4 + 1];
    char conv_col_names[ODV_MAX_COLUMNS][ODV_OBJNAME_LEN * 4 + 1];
    int i;

    /* Convert names to output charset */
    conv_name(s->table.schema, s->dump_charset, s->out_charset,
              conv_schema, sizeof(conv_schema));
    conv_name(s->table.name, s->dump_charset, s->out_charset,
              conv_name_buf, sizeof(conv_name_buf));

    if (s->table_count < ODV_MAX_TABLES) {
        ODV_TABLE_ENTRY *e = &s->table_list[s->table_count];
        odv_strcpy(e->schema, conv_schema, ODV_OBJNAME_LEN);
        odv_strcpy(e->name, conv_name_buf, ODV_OBJNAME_LEN);
        e->type = 0;
        e->col_count = s->table.col_count;
        e->row_count = row_count;
        s->table_count++;
    }

    if (s->table_cb) {
        for (i = 0; i < s->table.col_count && i < ODV_MAX_COLUMNS; i++) {
            conv_name(s->table.columns[i].name, s->dump_charset, s->out_charset,
                      conv_col_names[i], sizeof(conv_col_names[i]));
            col_names[i] = conv_col_names[i];
            col_types[i] = s->table.columns[i].type_str;
            col_not_nulls[i] = s->table.columns[i].not_null;
            col_defaults[i] = s->table.columns[i].default_val;
        }
        s->table_cb(
            conv_schema,
            conv_name_buf,
            s->table.col_count,
            col_names,
            col_types,
            col_not_nulls,
            col_defaults,
            row_count,
            s->table.ddl_offset,
            s->table_ud
        );
    }
}

/*---------------------------------------------------------------------------
    decode_exp_column

    Decodes a single EXP column value from binary data.
    Stores result as string in the record value.
 ---------------------------------------------------------------------------*/
static int decode_exp_column(ODV_SESSION *s, int col_idx,
                             const unsigned char *data, int data_len)
{
    ODV_COLUMN *col;
    ODV_VALUE *val;
    char tmp[1024];
    int rc;

    if (col_idx >= s->table.col_count) return ODV_OK;

    col = &s->table.columns[col_idx];
    val = &s->record.values[col_idx];

    switch (col->type) {
    case COL_NUMBER:
    case COL_FLOAT:
        rc = decode_oracle_number(data, data_len, tmp, sizeof(tmp));
        if (rc == ODV_OK) {
            set_value_string(val, tmp, (int)strlen(tmp));
        } else {
            set_value_null(val);
        }
        break;

    case COL_DATE:
        rc = decode_oracle_date(data, data_len, tmp, sizeof(tmp), s->date_format, s->custom_date_format);
        if (rc == ODV_OK) {
            set_value_string(val, tmp, (int)strlen(tmp));
        } else {
            set_value_null(val);
        }
        break;

    case COL_TIMESTAMP:
    case COL_TIMESTAMP_TZ:
    case COL_TIMESTAMP_LTZ:
        rc = decode_oracle_timestamp(data, data_len, tmp, sizeof(tmp), s->date_format, s->custom_date_format, col->precision);
        if (rc == ODV_OK) {
            set_value_string(val, tmp, (int)strlen(tmp));
        } else {
            set_value_null(val);
        }
        break;

    case COL_BIN_FLOAT:
        rc = decode_binary_float(data, tmp, sizeof(tmp));
        if (rc == ODV_OK) {
            set_value_string(val, tmp, (int)strlen(tmp));
        } else {
            set_value_null(val);
        }
        break;

    case COL_BIN_DOUBLE:
        rc = decode_binary_double(data, tmp, sizeof(tmp));
        if (rc == ODV_OK) {
            set_value_string(val, tmp, (int)strlen(tmp));
        } else {
            set_value_null(val);
        }
        break;

    case COL_INTERVAL_YM:
        rc = decode_interval_ym(data, data_len, tmp, sizeof(tmp));
        if (rc == ODV_OK) {
            set_value_string(val, tmp, (int)strlen(tmp));
        } else {
            set_value_null(val);
        }
        break;

    case COL_INTERVAL_DS:
        rc = decode_interval_ds(data, data_len, tmp, sizeof(tmp));
        if (rc == ODV_OK) {
            set_value_string(val, tmp, (int)strlen(tmp));
        } else {
            set_value_null(val);
        }
        break;

    case COL_RAW:
    case COL_ROWID: {
        /* Hex output: "0x" + hex bytes */
        int needed = data_len * 2 + 3;
        rc = ensure_value_buf(val, needed);
        if (rc == ODV_OK) {
            int j, pos = 0;
            val->data[pos++] = '0';
            val->data[pos++] = 'x';
            for (j = 0; j < data_len; j++) {
                static const char hex[] = "0123456789ABCDEF";
                val->data[pos++] = hex[(data[j] >> 4) & 0x0F];
                val->data[pos++] = hex[data[j] & 0x0F];
            }
            val->data[pos] = '\0';
            val->data_len = pos;
            val->is_null = 0;
        }
        break;
    }

    case COL_BLOB:
    case COL_LONG_RAW: {
        /* LOB extraction mode: accumulate binary data */
        if (s->lob_extract_mode && s->lob_column_index >= 0) {
            /* Find which LOB column this is (LOB-only index) */
            int li, lob_idx = 0;
            for (li = 0; li < col_idx; li++) {
                int ct = s->table.columns[li].type;
                if (ct == COL_BLOB || ct == COL_CLOB || ct == COL_NCLOB)
                    lob_idx++;
            }
            if (lob_idx < s->table.lob_col_count)
                odv_lob_accumulate(s, lob_idx, data, data_len);
        }
        /* Always set placeholder for row callback display */
        {
            const char *ph = "%BLOB%";
            set_value_string(val, ph, (int)strlen(ph));
        }
        break;
    }

    case COL_CLOB:
    case COL_LONG: {
        /* LOB extraction mode: accumulate text data */
        if (s->lob_extract_mode && s->lob_column_index >= 0) {
            int li, lob_idx = 0;
            for (li = 0; li < col_idx; li++) {
                int ct = s->table.columns[li].type;
                if (ct == COL_BLOB || ct == COL_CLOB || ct == COL_NCLOB)
                    lob_idx++;
            }
            if (lob_idx < s->table.lob_col_count)
                odv_lob_accumulate(s, lob_idx, data, data_len);
        }
        /* CLOB: store as string with charset conversion if needed */
        if (s->table.dump_charset != s->out_charset &&
            s->table.dump_charset != CHARSET_UNKNOWN) {
            int out_len = 0;
            rc = ensure_value_buf(val, data_len * 4 + 1);
            if (rc == ODV_OK) {
                rc = convert_charset((const char *)data, data_len,
                                     s->table.dump_charset,
                                     (char *)val->data, val->buf_size,
                                     s->out_charset, &out_len);
                if (rc == ODV_OK) {
                    val->data[out_len] = '\0';
                    val->data_len = out_len;
                    val->is_null = 0;
                } else {
                    /* Fallback: copy raw */
                    set_value_string(val, (const char *)data, data_len);
                }
            }
        } else {
            set_value_string(val, (const char *)data, data_len);
        }
        break;
    }

    case COL_NCLOB: {
        if (s->lob_extract_mode && s->lob_column_index >= 0) {
            int li, lob_idx = 0;
            for (li = 0; li < col_idx; li++) {
                int ct = s->table.columns[li].type;
                if (ct == COL_BLOB || ct == COL_CLOB || ct == COL_NCLOB)
                    lob_idx++;
            }
            if (lob_idx < s->table.lob_col_count)
                odv_lob_accumulate(s, lob_idx, data, data_len);
        }
        {
            const char *ph = "%NCLOB%";
            set_value_string(val, ph, (int)strlen(ph));
        }
        break;
    }

    case COL_BFILE: {
        const char *ph = "%BFILE%";
        set_value_string(val, ph, (int)strlen(ph));
        break;
    }

    case COL_NCHAR:
    case COL_NVARCHAR: {
        /* EXP stores NCHAR/NVARCHAR2 data in national charset (AL16UTF16 = UTF-16BE).
           Convert UTF-16BE → UTF-8 for display. */
        char conv_buf[ODV_VARCHAR_LEN];
        int conv_len = 0;
        if (data_len > 0 &&
            convert_charset((const char *)data, data_len,
                            CHARSET_UTF16BE,
                            conv_buf, sizeof(conv_buf),
                            CHARSET_UTF8, &conv_len) == ODV_OK) {
            set_value_string(val, conv_buf, conv_len);
            /* Trim trailing spaces for NCHAR */
            if (col->type == COL_NCHAR && val->data && val->data_len > 0) {
                while (val->data_len > 0 && val->data[val->data_len - 1] == ' ') {
                    val->data_len--;
                }
                val->data[val->data_len] = '\0';
            }
        } else {
            /* Fallback: copy raw */
            set_value_string(val, (const char *)data, data_len);
        }
        break;
    }

    default:
        /* String types: CHAR, VARCHAR, etc. */
        if (s->table.dump_charset != s->out_charset &&
            s->table.dump_charset != CHARSET_UNKNOWN) {
            int out_len = 0;
            rc = ensure_value_buf(val, data_len * 4 + 1);
            if (rc == ODV_OK) {
                rc = convert_charset((const char *)data, data_len,
                                     s->table.dump_charset,
                                     (char *)val->data, val->buf_size,
                                     s->out_charset, &out_len);
                if (rc == ODV_OK) {
                    val->data[out_len] = '\0';
                    val->data_len = out_len;
                    val->is_null = 0;
                } else {
                    set_value_string(val, (const char *)data, data_len);
                }
            }
        } else {
            set_value_string(val, (const char *)data, data_len);
        }

        /* Trim trailing spaces for CHAR types */
        if ((col->type == COL_CHAR || col->type == COL_NCHAR) &&
            val->data && val->data_len > 0) {
            while (val->data_len > 0 && val->data[val->data_len - 1] == ' ') {
                val->data_len--;
            }
            val->data[val->data_len] = '\0';
        }
        break;
    }

    return ODV_OK;
}

/*---------------------------------------------------------------------------
    parse_exp_records

    Reads binary records from EXP dump.
    Each column: 2-byte LE length + data
    Record end: 0x0000
    Table end: 0xFFFF
    NULL: 0xFFFE
 ---------------------------------------------------------------------------*/
static int parse_exp_records(ODV_SESSION *s, FILE *fp, int64_t data_start,
                             int list_only)
{
    unsigned char len_buf[2];
    unsigned char *col_buf = NULL;
    int col_buf_size = 0;
    int col_idx = 0;
    int col_len;
    int rc = ODV_OK;
    int64_t row_count = 0;

    /* Seek to data start */
    odv_fseek(fp, data_start, SEEK_SET);

    /* Ensure record can hold all columns */
    if (s->table.col_count > s->record.max_columns) {
        /* grow_record is static in odv_record.c, use realloc approach */
        ODV_VALUE *new_vals;
        int new_max = s->table.col_count + 64;
        new_vals = (ODV_VALUE *)realloc(s->record.values,
                                        new_max * sizeof(ODV_VALUE));
        if (!new_vals) return ODV_ERROR_MALLOC;
        memset(new_vals + s->record.max_columns, 0,
               (new_max - s->record.max_columns) * sizeof(ODV_VALUE));
        s->record.values = new_vals;
        s->record.max_columns = new_max;
    }

    /* In LOB extraction mode, validate the target column exists */
    if (s->lob_extract_mode) {
        rc = odv_lob_check_column(s);
        if (rc != ODV_OK) return rc;
    }

    /* Allocate column data buffer */
    col_buf_size = ODV_EXP_READ_BUF_LEN;
    col_buf = (unsigned char *)malloc(col_buf_size);
    if (!col_buf) return ODV_ERROR_MALLOC;

    reset_record(&s->record);
    col_idx = 0;


    while (!s->cancelled) {
        /* Read 2-byte length prefix */
        if (fread(len_buf, 1, 2, fp) != 2) {
            break; /* EOF */
        }

        col_len = (int)((unsigned int)len_buf[0] | ((unsigned int)len_buf[1] << 8));


        /* Special markers */
        if (col_len == 0x0000) {
            /* Record end */
            if (col_idx > 0) {
                s->record.col_count = col_idx;
                if (!list_only) {
                    deliver_row(s);
                    odv_report_progress(s, fp);
                }
                row_count++;
                s->table.record_count++;

                /* LOB extraction: write accumulated data */
                if (s->lob_extract_mode && s->lob_column_index >= 0) {
                    int lrc = odv_lob_write_file(s);
                    if (lrc != ODV_OK) return lrc;
                }
            }
            reset_record(&s->record);
            col_idx = 0;
            continue;
        }

        if (col_len == 0xFFFF) {
            /* Table data end */
            break;
        }

        if (col_len == 0xFFFE) {
            /* NULL column */
            if (col_idx < s->table.col_count) {
                set_value_null(&s->record.values[col_idx]);
            }
            col_idx++;
            /* DIRECT=Y: check row completion after NULL column too */
            if (s->dump_type == DUMP_EXP_DIRECT && col_idx == s->table.col_count) {
                s->record.col_count = col_idx;
                if (!list_only) {
                    deliver_row(s);
                    odv_report_progress(s, fp);
                }
                row_count++;
                s->table.record_count++;
                if (s->lob_extract_mode && s->lob_column_index >= 0) {
                    int lrc = odv_lob_write_file(s);
                    if (lrc != ODV_OK) { rc = lrc; goto rec_done; }
                }
                reset_record(&s->record);
                col_idx = 0;
            }
            continue;
        }

            /* Handle large data: 0xFF00 means 4-byte length follows */
        if (col_len == 0xFF00) {
            unsigned char big_len[4];
            unsigned int ulen;
            if (fread(big_len, 1, 4, fp) != 4) break;
            ulen = (unsigned int)big_len[0]
                 | ((unsigned int)big_len[1] << 8)
                 | ((unsigned int)big_len[2] << 16)
                 | ((unsigned int)big_len[3] << 24);
            if (ulen > (unsigned int)ODV_EXP_RECORD_LEN) {
                break; /* Invalid length — corrupted record */
            }
            col_len = (int)ulen;
        }

        /* Type-specific length validation (ref: check_column_length) */
        if (col_idx < s->table.col_count) {
            int ctype = s->table.columns[col_idx].type;
            int bad = 0;
            switch (ctype) {
            case COL_NUMBER: case COL_FLOAT:
                if (col_len > 32) bad = 1; break;
            case COL_DATE:
                if (col_len > 7) bad = 1; break;
            case COL_TIMESTAMP: case COL_TIMESTAMP_TZ: case COL_TIMESTAMP_LTZ:
                if (col_len > 13) bad = 1; break;
            case COL_INTERVAL_YM: case COL_INTERVAL_DS:
                if (col_len > 11) bad = 1; break;
            case COL_BFILE:
                if (col_len > 1000) bad = 1; break;
            case COL_ROWID:
                if (col_len > 100) bad = 1; break;
            case COL_CHAR: case COL_NCHAR: case COL_VARCHAR: case COL_NVARCHAR:
                if (col_len > ODV_VARCHAR_LEN * 3) bad = 1; break;
            default:
                break; /* BLOB, CLOB, RAW, LONG, LONG_RAW: no upper limit */
            }
            if (bad) {
                /* Corrupt data — skip to next table */
                while (!s->cancelled) {
                    unsigned char scan[2];
                    if (fread(scan, 1, 2, fp) != 2) goto rec_done;
                    if (scan[0] == 0xFF && scan[1] == 0xFF) break;
                    odv_fseek(fp, -1, SEEK_CUR);
                }
                break;
            }
        }

        /* Sanity check: reject absurdly large column lengths */
        if (col_len < 0 || col_len > ODV_EXP_RECORD_LEN) {
            /* Corrupt data — try to find 0xFFFF end marker */
            while (!s->cancelled) {
                unsigned char scan[2];
                if (fread(scan, 1, 2, fp) != 2) goto rec_done;
                if (scan[0] == 0xFF && scan[1] == 0xFF) break;
                /* Seek back 1 byte (sliding window) */
                odv_fseek(fp, -1, SEEK_CUR);
            }
            break;
        }

        /* Ensure buffer is large enough */
        if (col_len > col_buf_size) {
            unsigned char *new_buf = (unsigned char *)realloc(col_buf, col_len + 1);
            if (!new_buf) { rc = ODV_ERROR_MALLOC; break; }
            col_buf = new_buf;
            col_buf_size = col_len + 1;
        }

        /* Read column data */
        if (col_len > 0) {
            if ((int)fread(col_buf, 1, col_len, fp) != col_len) {
                break; /* Truncated */
            }
        }

        /* Decode and store */
        if (col_idx < s->table.col_count) {
            decode_exp_column(s, col_idx, col_buf, col_len);
        }
        col_idx++;

        /* DIRECT=Y: rows are concatenated without 00 00 end-of-row separator.
           Deliver the row as soon as all columns have been read. */
        if (s->dump_type == DUMP_EXP_DIRECT && col_idx == s->table.col_count) {
            s->record.col_count = col_idx;
            if (!list_only) {
                deliver_row(s);
                odv_report_progress(s, fp);
            }
            row_count++;
            s->table.record_count++;
            if (s->lob_extract_mode && s->lob_column_index >= 0) {
                int lrc = odv_lob_write_file(s);
                if (lrc != ODV_OK) { rc = lrc; break; }
            }
            reset_record(&s->record);
            col_idx = 0;
            continue;
        }

        /* Safety check — too many columns means record structure is corrupt.
           Allow extra for LOB columns beyond regular col_count */
        if (col_idx > s->table.col_count + s->table.lob_col_count + 1) {
            /* Scan forward to find 0xFFFF table end marker */
            while (!s->cancelled) {
                unsigned char scan[2];
                if (fread(scan, 1, 2, fp) != 2) goto rec_done;
                if (scan[0] == 0xFF && scan[1] == 0xFF) break;
                odv_fseek(fp, -1, SEEK_CUR);
            }
            break;
        }
    }

rec_done:
    free(col_buf);

    if (s->cancelled) return ODV_ERROR_CANCELLED;
    return rc;
}

/*---------------------------------------------------------------------------
    parse_exp_ddl_and_data

    Byte-by-byte state machine for EXP dump parsing.

    EXP files interleave ASCII DDL text with binary data:
      step 0: Skip 256-byte header
      step 1: Export mode recognition (METRICST, INTERPRETED, etc.)
      step 2: DDL parsing (CREATE TABLE, INSERT INTO, CONNECT)
      step 3: Binary metadata (column count, types, lengths, charset)
      After metadata: call parse_exp_records() for binary record data
      On 0xFFFF end-of-table: back to step 2 for next table

    DDL statements are terminated by \0 or \n.
 ---------------------------------------------------------------------------*/
static int parse_exp_ddl_and_data(ODV_SESSION *s, FILE *fp, int list_only)
{
    int step;               /* 0=header, 1=mode, 2=ddl, 3=metadata */
    int data_step;          /* sub-state within step 3 */
    char *word;
    int wlen = 0;
    char current_schema[ODV_OBJNAME_LEN + 1] = {0};
    unsigned char c;
    int64_t address = 0;
    int rc = ODV_OK;

    /* Step 3 (metadata) state */
    int meta_col_count = 0;
    int meta_col_idx = 0;
    int meta_col_type = 0;
    int is_char_type = 0;
    int meta_lob_idx = 0;
    int lob_total = 0;
    int lob_name_len = 0;
    int lob_name_read = 0;
    unsigned char meta_buf[4];
    int null_count = 0;
    int pending_table = 0;  /* 1=table parsed but not yet notified */
    int filter_found = 0;   /* 1=filter target table already processed */

    word = (char *)malloc(EXP_DDL_BUF_SIZE);
    if (!word) return ODV_ERROR_MALLOC;

    /* Fast seek: if seek_offset is set (from previous list_tables),
       jump directly to the target table's DDL position.
       Header must already be parsed (charset info needed). */
    if (s->seek_offset > EXP_HEADER_SIZE && s->filter_active) {
        odv_fseek(fp, s->seek_offset, SEEK_SET);
        address = s->seek_offset;
        step = 2;   /* Start in DDL scan mode */
        wlen = 0;

        /* Pre-set current_schema from filter (we skipped past CONNECT) */
        if (s->filter_schema[0]) {
            char tmp[ODV_OBJNAME_LEN + 1];
            int tlen = 0;
            if (s->dump_charset != s->out_charset &&
                s->dump_charset != CHARSET_UNKNOWN) {
                if (convert_charset(s->filter_schema, (int)strlen(s->filter_schema),
                                    s->out_charset, tmp, ODV_OBJNAME_LEN,
                                    s->dump_charset, &tlen) == ODV_OK) {
                    tmp[tlen] = '\0';
                    odv_strcpy(current_schema, tmp, ODV_OBJNAME_LEN);
                }
            } else {
                odv_strcpy(current_schema, s->filter_schema, ODV_OBJNAME_LEN);
            }
        }
    } else {
        /* Normal: start from beginning of file */
        odv_fseek(fp, 0, SEEK_SET);
        step = 0;

        /* Pre-set current_schema from export user in the header.
           RTABLES (single-table export) has no CONNECT statement.
           RUSERS (schema export) may also have tables before the
           first CONNECT statement.  The CONNECT statement will
           override this when encountered. */
        if (s->exp_state.export_user[0] != '\0') {
            odv_strcpy(current_schema, s->exp_state.export_user,
                       ODV_OBJNAME_LEN);
        }
    }

    data_step = 0;

    while (!s->cancelled) {
        if (fread(&c, 1, 1, fp) != 1) break;
        address++;

        /* Report progress periodically during DDL scan / metadata parse.
           Check every 64KB to keep overhead negligible.
           (odv_report_progress fires callback only when % changes) */
        if ((address & 0xFFFF) == 0) {
            odv_report_progress(s, fp);
        }

        switch (step) {

        case 0: /* Skip 256-byte header */
            if (address >= EXP_HEADER_SIZE) {
                step = 1;
                wlen = 0;
            }
            break;

        case 1: /* Export mode recognition */
            /* Accumulate chars until \0 or \n terminator.
               After the 256-byte header, RTABLES dumps have ~1.75KB of 0x00
               padding followed by binary metadata, then METRIC markers,
               then DDL text.  We need to recognize METRIC markers to
               properly transition to step 2 (DDL parsing). */
            if (c == 0x00 || c == 0x0a) {
                if (wlen > 0) {
                    word[wlen] = '\0';
                    /* DDL keyword → immediate transition */
                    if (starts_with_ci(word, "CREATE ") ||
                        starts_with_ci(word, "CONNECT ") ||
                        starts_with_ci(word, "ALTER ") ||
                        starts_with_ci(word, "GRANT ") ||
                        starts_with_ci(word, "INSERT ")) {
                        step = 2;
                        goto handle_ddl;
                    }
                    /* METRIC marker or INTERPRETED → transition to step 2.
                       METRICST = table mode, METRICSU/METRICEU = user mode,
                       METRICEE = database mode, INTERPRETED = direct export. */
                    if (strcmp(word, "METRICST") == 0 ||
                        strcmp(word, "METRICSU") == 0 ||
                        strcmp(word, "METRICEU") == 0 ||
                        strcmp(word, "METRICEE") == 0 ||
                        strcmp(word, "INTERPRETED") == 0) {
                        step = 2;
                        wlen = 0;
                        break;
                    }
                    wlen = 0;
                }
            } else {
                /* Only accumulate printable ASCII to avoid binary noise
                   from metadata blocks between header and DDL area */
                if (c >= 0x20 && c <= 0x7e) {
                    if (wlen < EXP_DDL_BUF_SIZE - 2)
                        word[wlen++] = (char)c;
                }
            }
            break;

        case 2: /* DDL text parsing */
            if (c == 0x00 || c == 0x0a) {
                if (wlen == 0) break;
                word[wlen] = '\0';

            handle_ddl:
                if (starts_with_ci(word, "CONNECT ")) {
                    /* Extract schema name */
                    char sch[ODV_OBJNAME_LEN + 1] = {0};
                    extract_identifier(word + 8, sch, ODV_OBJNAME_LEN);
                    if (sch[0] != '\0')
                        odv_strcpy(current_schema, sch, ODV_OBJNAME_LEN);

                } else if (starts_with_ci(word, "CREATE ")) {
                    const char *after = skip_ws(word + 7);
                    /* Match "TABLE" but NOT "TABLESPACE" etc. */
                    if (starts_with_ci(after, "TABLE") &&
                        (after[5] == ' ' || after[5] == '\t' ||
                         after[5] == '"' || after[5] == '\0')) {
                        /* Notify previous pending table if it had no INSERT INTO */
                        if (pending_table && s->table.name[0] != '\0') {
                            notify_exp_table(s, 0);
                            pending_table = 0;
                        }
                        if (parse_create_table(s, word)) {
                            /* Record file position of this CREATE TABLE
                               for fast seeking on subsequent parse_dump calls */
                            s->table.ddl_offset = address - wlen - 1;

                            if (s->table.schema[0] == '\0' &&
                                current_schema[0] != '\0')
                                odv_strcpy(s->table.schema, current_schema,
                                           ODV_OBJNAME_LEN);
                            s->table.record_count = 0;
                            invalidate_meta_cache();
                            pending_table = 1;

                            /* Table filter check */
                            if (s->filter_active) {
                                int match = 1;
                                if (s->filter_table[0]) {
                                    char ft[ODV_OBJNAME_LEN + 1];
                                    int ft_len;
                                    odv_strcpy(ft, s->filter_table, ODV_OBJNAME_LEN);
                                    ft_len = (int)strlen(ft);
                                    /* Reverse-convert: UTF-8 -> dump charset */
                                    if (s->dump_charset != s->out_charset &&
                                        s->dump_charset != CHARSET_UNKNOWN) {
                                        char tmp[ODV_OBJNAME_LEN + 1];
                                        int tlen = 0;
                                        int cvt_rc = convert_charset(ft, ft_len, s->out_charset,
                                                            tmp, ODV_OBJNAME_LEN, s->dump_charset,
                                                            &tlen);
                                        if (cvt_rc == ODV_OK) {
                                            tmp[tlen] = '\0';
                                            odv_strcpy(ft, tmp, ODV_OBJNAME_LEN);
                                        }
                                    }
                                    if (_stricmp(s->table.name, ft) != 0)
                                        match = 0;
                                }
                                if (match && s->filter_schema[0]) {
                                    char fs[ODV_OBJNAME_LEN + 1];
                                    int fs_len;
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
                                    goto done;
                                }
                                if (match) {
                                    filter_found = 1;
                                }
                            }

                            /* notify_exp_table is deferred to after record counting */
                        }
                    }
                    /* Other CREATE types (TRIGGER, INDEX...) are ignored */

                } else if (starts_with_ci(word, "ALTER ") && pending_table) {
                    /* Parse ALTER TABLE ... MODIFY ("col" DEFAULT value)
                       to capture DEFAULT values for columns */
                    const char *ap = word;
                    if (starts_with_ci(ap, "ALTER TABLE") ||
                        starts_with_ci(ap, "ALTER TABLE")) {
                        /* Skip to MODIFY keyword */
                        while (*ap && !starts_with_ci(ap, "MODIFY")) ap++;
                        if (*ap) {
                            ap += 6; /* skip "MODIFY" */
                            while (*ap == ' ' || *ap == '\t' || *ap == '\n' || *ap == '\r') ap++;
                            /* Skip optional "DEFAULT\n" preamble line */
                            if (starts_with_ci(ap, "DEFAULT") && (ap[7] == '\0' || ap[7] <= ' ')) {
                                /* This is just the preamble line, skip */
                            } else if (*ap == '(') {
                                /* ("col_name" DEFAULT value) */
                                char acol[ODV_OBJNAME_LEN + 1] = {0};
                                char adef[256] = {0};
                                int alen = 0;
                                ap++; /* skip ( */
                                while (*ap == ' ') ap++;
                                if (*ap == '"') {
                                    ap++;
                                    while (*ap && *ap != '"' && alen < ODV_OBJNAME_LEN) {
                                        acol[alen++] = *ap++;
                                    }
                                    acol[alen] = '\0';
                                    if (*ap == '"') ap++;
                                }
                                while (*ap == ' ') ap++;
                                if (starts_with_ci(ap, "DEFAULT")) {
                                    ap += 7;
                                    while (*ap == ' ') ap++;
                                    alen = 0;
                                    while (*ap && *ap != ')' && alen < 250) {
                                        adef[alen++] = *ap++;
                                    }
                                    adef[alen] = '\0';
                                    /* trim trailing spaces */
                                    while (alen > 0 && adef[alen-1] == ' ') adef[--alen] = '\0';
                                }
                                if (acol[0] && adef[0]) {
                                    int ci;
                                    for (ci = 0; ci < s->table.col_count; ci++) {
                                        if (strcmp(s->table.columns[ci].name, acol) == 0) {
                                            odv_strcpy(s->table.columns[ci].default_val, adef, 255);
                                            break;
                                        }
                                    }
                                }
                            }
                        }
                    }

                } else if (starts_with_ci(word, "INSERT INTO ")) {
                    /* Extract table name from INSERT INTO and compare with
                       the table from the most recent CREATE TABLE.
                       Format: INSERT INTO "schema"."table" or INSERT INTO "table"
                       Only transition to binary metadata (step 3) if they
                       match; otherwise this is a DDL INSERT (e.g. PL/SQL)
                       and should be ignored. */
                    char ins_table[ODV_OBJNAME_LEN + 1] = {0};
                    {
                        const char *ip = extract_identifier(word + 12, ins_table, ODV_OBJNAME_LEN);
                        ip = skip_ws(ip);
                        if (*ip == '.') {
                            /* schema.table format — extract table part */
                            ip++;
                            extract_identifier(ip, ins_table, ODV_OBJNAME_LEN);
                        }
                    }

                    if (s->table.name[0] != '\0' &&
                        strcmp(s->table.name, ins_table) == 0) {
                        /* Table name matches — transition to binary metadata */
                        step = 3;
                        data_step = 0;
                        meta_col_count = 0;
                        meta_col_idx = 0;
                        meta_lob_idx = 0;
                        null_count = 0;
                    }
                    /* else: DDL INSERT, ignore and stay in step 2 */
                }

                wlen = 0;
            } else {
                if (wlen < EXP_DDL_BUF_SIZE - 2)
                    word[wlen++] = (char)c;
            }
            break;

        case 3: /* Binary metadata after INSERT INTO */
            /*
             * Binary metadata structure:
             *
             * Structure (field order):
             *   2 bytes: column count (LE)         [data_step 0-1]
             *   For each column:
             *     1 byte: Oracle internal type code [data_step 2]
             *     1 byte: null flag                 [data_step 3]
             *     2 bytes: byte length (LE)         [data_step 4-5]
             *     [4 bytes: charset if char type]   [data_step 6-9]
             *   If LOB columns exist:
             *     2 bytes: LOB column count (LE)    [data_step 10-11]
             *     LOB column names (len-prefixed)   [data_step 12-13]
             *   Null padding (0x00 bytes)           [data_step 20]
             *   Record data starts at first non-0x00 byte
             *
             * Char types (need 4 charset bytes):
             *   0x01=VARCHAR2, 0x60=CHAR, 0x70=CLOB, 0xD0=NCLOB
             *   (per reference: 0x40=LONG_RAW also flagged)
             *
             * XMLTYPE (0x3A) triggers abort — back to DDL.
             */
            switch (data_step) {
            case 0: /* Column count byte 0 */
                meta_buf[0] = c;
                data_step = 1;
                break;

            case 1: /* Column count byte 1 */
                meta_col_count = (int)meta_buf[0] | ((int)c << 8);
                meta_col_idx = 0;
                is_char_type = 0;
                if (meta_col_count <= 0 || meta_col_count > ODV_MAX_COLUMNS) {
                    s->table.name[0] = '\0';
                    if (s->table_count > 0) s->table_count--;
                    step = 2;
                    wlen = 0;
                } else {
                    data_step = 2;
                }
                break;

            case 2: /* Column type byte */
                meta_col_type = (int)c;

                /* Check type code against known Oracle internal types */
                switch (c) {
                case 0x3A: /* XMLTYPE — unsupported */
                    s->table.name[0] = '\0';
                    if (s->table_count > 0) s->table_count--;
                    step = 2;
                    wlen = 0;
                    goto meta_done;

                /* Char types: need 4 charset bytes after length */
                case 0x01: /* VARCHAR2 */
                case 0x40: /* LONG RAW (ref flags as char) */
                case 0x60: /* CHAR */
                case 0x70: /* CLOB */
                case 0xD0: /* NCLOB */
                    is_char_type = 1;
                    break;

                /* Non-char types: no charset bytes */
                case 0x02: /* NUMBER */
                case 0x08: /* LONG */
                case 0x09: /* VARCHAR (alternate) */
                case 0x0C: /* DATE */
                case 0x17: /* RAW */
                case 0x18: /* LONG RAW (alt) */
                case 0x45: /* ROWID */
                case 0x71: /* BLOB */
                case 0x72: /* BFILE */
                case 0xB4: /* TIMESTAMP */
                case 0xB5: /* TIMESTAMP WITH TIMEZONE */
                case 0xB6: /* INTERVAL YEAR TO MONTH */
                case 0xB7: /* INTERVAL DAY TO SECOND */
                case 0xE7: /* TIMESTAMP WITH LOCAL TIMEZONE */
                    is_char_type = 0;
                    break;

                default:
                    /* Unknown type — WARNING and continue parsing */
                    is_char_type = 0;
                    break;
                }

                data_step = 3;
            meta_done:
                break;

            case 3: /* Null flag byte */
                data_step = 4;
                break;

            case 4: /* Length byte 0 */
                meta_buf[0] = c;
                data_step = 5;
                break;

            case 5: /* Length byte 1 — advance */
                meta_col_idx++;
                if (is_char_type) {
                    data_step = 6; /* read 4 charset bytes */
                } else {
                    if (meta_col_idx >= meta_col_count) {
                        /* Use DDL-parsed LOB count (ref: tbl_lob_num from
                           table->lob_column_ct, not from metadata bytes) */
                        if (s->table.lob_col_count > 0) {
                            data_step = 10; /* LOB metadata */
                        } else {
                            data_step = 20; /* null padding */
                            null_count = 0;
                        }
                    } else {
                        data_step = 2; /* next column */
                    }
                }
                break;

            case 6: case 7: case 8: /* Charset bytes 0-2 */
                data_step++;
                break;

            case 9: /* Charset byte 3 — advance to next col or finish */
                if (meta_col_idx >= meta_col_count) {
                    /* Use DDL-parsed LOB count (ref: tbl_lob_num) */
                    if (s->table.lob_col_count > 0) {
                        data_step = 10; /* LOB metadata */
                    } else {
                        data_step = 20; /* null padding */
                        null_count = 0;
                    }
                } else {
                    data_step = 2; /* next column */
                }
                break;

            case 10: /* LOB column count byte 0 */
                meta_buf[0] = c;
                data_step = 11;
                break;

            case 11: /* LOB column count byte 1 */
                lob_total = (int)meta_buf[0] | ((int)c << 8);
                meta_lob_idx = 0;
                /* Do NOT reset null_count here — ref does not reset null_ct
                   in data_step 11. The prior null_ct is used in data_step 12. */
                data_step = 12;
                break;

            case 12: /* LOB metadata: null skip / name length */
                if (c == 0xFF) {
                    /* End marker within LOB section */
                    step = 2;
                    wlen = 0;
                } else if (c != 0x00) {
                    if (null_count > 0) {
                        /* Non-zero after nulls = LOB column name length */
                        lob_name_len = (int)c;
                        lob_name_read = 0;
                        data_step = 13;
                    }
                    null_count++;
                } else {
                    null_count++;
                }
                break;

            case 13: /* LOB column name bytes */
                if (c < 0x04) {
                    /* End of LOB column name */
                    meta_lob_idx++;
                    if (meta_lob_idx >= s->table.lob_col_count) {
                        /* All LOB names read — go to null padding */
                        null_count = 0;
                        meta_lob_idx = 0;
                        data_step = 20; /* final null padding */
                    } else {
                        /* Next LOB — do NOT reset null_count (ref behavior) */
                        data_step = 12; /* next LOB */
                    }
                }
                /* else: accumulate (we don't need the actual name) */
                break;

            case 20: /* Null padding before record data */
                /* Null padding before record data begins */
                if (c == 0x00) {
                    null_count++;
                } else if (c == 0xFF) {
                    /* No record data for this table — back to DDL */
                    notify_exp_table(s, 0);
                    pending_table = 0;
                    step = 2;
                    wlen = 0;
                } else {
                    /* For DUMP_EXP_DIRECT, need at least 3 null bytes */
                    if (s->dump_type == DUMP_EXP_DIRECT && null_count < 3) {
                        break;
                    }
                    /* First byte of record data detected.
                       This byte is len_buff[0]; seek back 1 so
                       parse_exp_records reads the full 2-byte length. */
                    {
                        int64_t rec_start = odv_ftell(fp) - 1;

                        if (s->table.lob_col_count > 0) {
                            /* EXP LOB tables use chunk-based binary format
                               that is incompatible with parse_exp_records.
                               Scan past the record data to the 0xFFFF table-end
                               marker so that subsequent tables in the same dump
                               are still correctly parsed. */
                            {
                                int skip_ct = 0;
                                while (!s->cancelled) {
                                    unsigned char scan[2];
                                    if (fread(scan, 1, 2, fp) != 2) goto done;
                                    if (scan[0] == 0xFF && scan[1] == 0xFF) break;
                                    odv_fseek(fp, -1, SEEK_CUR);
                                    if ((++skip_ct & 0x7FFF) == 0)
                                        odv_report_progress(s, fp);
                                }
                            }
                            notify_exp_table(s, 0);
                            pending_table = 0;
                        } else if (list_only && s->filter_active && s->pass_flg) {
                            /* Filtered out in list_only: skip records entirely */
                            /* Scan forward to find 0xFFFF end marker */
                            {
                                int skip_ct = 0;
                                while (!s->cancelled) {
                                    unsigned char scan[2];
                                    if (fread(scan, 1, 2, fp) != 2) goto done;
                                    if (scan[0] == 0xFF && scan[1] == 0xFF) break;
                                    odv_fseek(fp, -1, SEEK_CUR);
                                    if ((++skip_ct & 0x7FFF) == 0)
                                        odv_report_progress(s, fp);
                                }
                            }
                            notify_exp_table(s, 0);
                            pending_table = 0;
                        } else if (!s->filter_active || !s->pass_flg) {
                            /* Parse records (list_only=count only, full=deliver) */
                            rc = parse_exp_records(s, fp, rec_start, list_only);
                            notify_exp_table(s, s->table.record_count);
                            pending_table = 0;
                        } else {
                            /* Filtered out in full parse: skip */
                            {
                                int skip_ct = 0;
                                while (!s->cancelled) {
                                    unsigned char scan[2];
                                    if (fread(scan, 1, 2, fp) != 2) goto done;
                                    if (scan[0] == 0xFF && scan[1] == 0xFF) break;
                                    odv_fseek(fp, -1, SEEK_CUR);
                                    if ((++skip_ct & 0x7FFF) == 0)
                                        odv_report_progress(s, fp);
                                }
                            }
                            notify_exp_table(s, 0);
                            pending_table = 0;
                        }
                        /* Update address to match new file position */
                        address = odv_ftell(fp);
                    }
                    step = 2;
                    wlen = 0;
                    if (rc == ODV_ERROR_CANCELLED) goto done;
                    rc = ODV_OK;
                }
                break;
            } /* switch data_step */
            break;

        } /* switch step */
    }

done:
    /* Notify last pending table if not yet notified */
    if (pending_table && s->table.name[0] != '\0') {
        notify_exp_table(s, s->table.record_count);
    }

    free(word);
    if (s->cancelled) return ODV_ERROR_CANCELLED;
    return rc;
}

/*---------------------------------------------------------------------------
    parse_exp_dump

    Main entry point for EXP dump parsing.
 ---------------------------------------------------------------------------*/
int parse_exp_dump(ODV_SESSION *s, int list_only)
{
    FILE *fp;
    int rc;

    if (!s) return ODV_ERROR_INVALID_ARG;

    fp = fopen(s->dump_path, "rb");
    if (!fp) {
        odv_strcpy(s->last_error, "Cannot open dump file", ODV_MSG_LEN);
        return ODV_ERROR_FOPEN;
    }

    /* Set 64KB I/O buffer for improved read throughput */
    setvbuf(fp, NULL, _IOFBF, 65536);

    /* Reset progress tracking */
    s->last_progress_pct = -1;

    /* Parse header */
    rc = parse_exp_header(s, fp);
    if (rc != ODV_OK) {
        fclose(fp);
        return rc;
    }

    /* Parse DDL and data */
    rc = parse_exp_ddl_and_data(s, fp, list_only);

    fclose(fp);
    return rc;
}

/*---------------------------------------------------------------------------
    trim_right - trim trailing whitespace
 ---------------------------------------------------------------------------*/
static void trim_right(char *str)
{
    int len;
    if (!str) return;
    len = (int)strlen(str);
    while (len > 0 && ((unsigned char)str[len - 1] <= ' ')) {
        len--;
    }
    str[len] = '\0';
}
