/*****************************************************************************
    OraDB DUMP Viewer - Parser Test Harness
    テスト用ダンプファイルを解析し、結果を検証する
 *****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

/* --- DLL function pointers --- */
typedef struct _odv_session ODV_SESSION;

typedef void (__stdcall *ODV_ROW_CALLBACK)(
    const char *schema, const char *table, int col_count,
    const char **col_names, const char **col_values, void *user_data);

typedef void (__stdcall *ODV_PROGRESS_CALLBACK)(
    __int64 rows_processed, const char *current_table, void *user_data);

typedef void (__stdcall *ODV_TABLE_CALLBACK)(
    const char *schema, const char *table, int col_count,
    const char **col_names, const char **col_types,
    const int *col_not_nulls, const char **col_defaults,
    int constraint_count, const char *constraints_json,
    __int64 row_count, __int64 data_offset, void *user_data);

typedef int (__stdcall *FN_CREATE)(ODV_SESSION **s);
typedef int (__stdcall *FN_DESTROY)(ODV_SESSION *s);
typedef int (__stdcall *FN_SET_FILE)(ODV_SESSION *s, const char *path);
typedef int (__stdcall *FN_SET_ROW_CB)(ODV_SESSION *s, ODV_ROW_CALLBACK cb, void *ud);
typedef int (__stdcall *FN_SET_PROGRESS_CB)(ODV_SESSION *s, ODV_PROGRESS_CALLBACK cb, void *ud);
typedef int (__stdcall *FN_SET_TABLE_CB)(ODV_SESSION *s, ODV_TABLE_CALLBACK cb, void *ud);
typedef int (__stdcall *FN_SET_FILTER)(ODV_SESSION *s, const char *schema, const char *table);
typedef int (__stdcall *FN_SET_OFFSET)(ODV_SESSION *s, __int64 offset);
typedef int (__stdcall *FN_CHECK_KIND)(ODV_SESSION *s, int *dump_type);
typedef int (__stdcall *FN_LIST_TABLES)(ODV_SESSION *s);
typedef int (__stdcall *FN_PARSE_DUMP)(ODV_SESSION *s);
typedef const char* (__stdcall *FN_GET_VERSION)(void);
typedef const char* (__stdcall *FN_GET_ERROR)(ODV_SESSION *s);
typedef int (__stdcall *FN_GET_PCT)(ODV_SESSION *s);

/* --- Globals --- */
static FN_CREATE        fn_create;
static FN_DESTROY       fn_destroy;
static FN_SET_FILE      fn_set_file;
static FN_SET_ROW_CB    fn_set_row_cb;
static FN_SET_PROGRESS_CB fn_set_progress_cb;
static FN_SET_TABLE_CB  fn_set_table_cb;
static FN_SET_FILTER    fn_set_filter;
static FN_SET_OFFSET    fn_set_offset;
static FN_CHECK_KIND    fn_check_kind;
static FN_LIST_TABLES   fn_list_tables;
static FN_PARSE_DUMP    fn_parse_dump;
static FN_GET_VERSION   fn_get_version;
static FN_GET_ERROR     fn_get_error;
static FN_GET_PCT       fn_get_pct;

/* --- Test state --- */
typedef struct {
    int table_count;
    int total_rows;
    int errors;
    int warnings;
    char current_table[256];
    /* Per-table row counts for verification */
    char table_names[200][256];
    int table_rows[200];
    int table_cols[200];
    __int64 table_offsets[200];
} TestState;

static TestState g_state;

/* --- Callbacks --- */
static void __stdcall on_table(const char *schema, const char *table,
    int col_count, const char **col_names, const char **col_types,
    const int *col_not_nulls, const char **col_defaults,
    int constraint_count, const char *constraints_json,
    __int64 row_count, __int64 data_offset, void *ud)
{
    TestState *st = (TestState *)ud;
    int idx = st->table_count;
    if (idx < 200) {
        snprintf(st->table_names[idx], 256, "%s.%s", schema, table);
        st->table_cols[idx] = col_count;
        st->table_rows[idx] = (int)row_count;
        st->table_offsets[idx] = data_offset;
    }
    st->table_count++;

    printf("  TABLE: %s.%s  cols=%d  rows=%lld  offset=%lld  constraints=%d\n",
           schema, table, col_count, row_count, data_offset, constraint_count);

    /* Print column info */
    for (int i = 0; i < col_count && i < 5; i++) {
        printf("    col[%d]: %s (%s)%s%s%s\n", i, col_names[i], col_types[i],
               (col_not_nulls && col_not_nulls[i]) ? " NOT NULL" : "",
               (col_defaults && col_defaults[i][0]) ? " DEFAULT " : "",
               (col_defaults && col_defaults[i][0]) ? col_defaults[i] : "");
    }
    if (col_count > 5) printf("    ... (%d more columns)\n", col_count - 5);

    /* Print constraint summary */
    if (constraint_count > 0 && constraints_json) {
        printf("    constraints_json: %.200s%s\n",
               constraints_json,
               strlen(constraints_json) > 200 ? "..." : "");
    }
}

static void __stdcall on_row(const char *schema, const char *table,
    int col_count, const char **col_names, const char **col_values, void *ud)
{
    TestState *st = (TestState *)ud;
    st->total_rows++;

    /* Print first few rows per table */
    if (strcmp(st->current_table, table) != 0) {
        strncpy(st->current_table, table, 255);
        st->current_table[255] = '\0';
        printf("\n  ROWS for %s.%s:\n", schema, table);
    }

    /* Only print first 3 rows per table */
    static int rows_for_table = 0;
    static char last_table[256] = "";
    if (strcmp(last_table, table) != 0) {
        strncpy(last_table, table, 255);
        last_table[255] = '\0';
        rows_for_table = 0;
    }
    rows_for_table++;

    if (rows_for_table <= 3) {
        printf("    row %d: ", rows_for_table);
        for (int i = 0; i < col_count && i < 5; i++) {
            const char *val = col_values[i] ? col_values[i] : "(NULL)";
            /* Truncate long values */
            if (strlen(val) > 40) {
                printf("[%s]=%.40s... ", col_names[i], val);
            } else {
                printf("[%s]=%s ", col_names[i], val);
            }
        }
        if (col_count > 5) printf("...");
        printf("\n");
    } else if (rows_for_table == 4) {
        printf("    ... (more rows)\n");
    }
}

static void __stdcall on_progress(__int64 rows_processed, const char *current_table, void *ud)
{
    /* Silent progress */
}

/* --- Dump type name --- */
static const char *dump_type_name(int t) {
    switch (t) {
        case 0: return "EXPDP";
        case 1: return "EXPDP_COMPRESS";
        case 10: return "EXP";
        case 11: return "EXP_DIRECT";
        default: return "UNKNOWN";
    }
}

/* --- Test a single dump file --- */
static int test_dump(const char *path, int expected_type) {
    ODV_SESSION *s = NULL;
    int rc, dump_type;
    int pass = 1;

    printf("\n========================================\n");
    printf("TEST: %s\n", path);
    printf("Expected type: %s (%d)\n", dump_type_name(expected_type), expected_type);
    printf("========================================\n");

    memset(&g_state, 0, sizeof(g_state));

    rc = fn_create(&s);
    if (rc != 0) { printf("FAIL: create_session returned %d\n", rc); return 0; }

    rc = fn_set_file(s, path);
    if (rc != 0) {
        printf("FAIL: set_dump_file returned %d: %s\n", rc, fn_get_error(s));
        fn_destroy(s);
        return 0;
    }

    /* Phase 1: Check dump kind */
    rc = fn_check_kind(s, &dump_type);
    if (rc != 0) {
        printf("FAIL: check_dump_kind returned %d: %s\n", rc, fn_get_error(s));
        fn_destroy(s);
        return 0;
    }

    printf("Detected type: %s (%d)\n", dump_type_name(dump_type), dump_type);
    if (expected_type >= 0 && dump_type != expected_type) {
        printf("FAIL: Expected type %d, got %d\n", expected_type, dump_type);
        pass = 0;
    } else {
        printf("OK: Type detection correct\n");
    }

    /* Phase 2: List tables */
    fn_set_table_cb(s, on_table, &g_state);
    rc = fn_list_tables(s);
    if (rc != 0) {
        printf("FAIL: list_tables returned %d: %s\n", rc, fn_get_error(s));
        fn_destroy(s);
        return 0;
    }
    printf("\nTables found: %d\n", g_state.table_count);
    if (g_state.table_count == 0 && expected_type != -2) {
        /* expected_type == -2 means metadata-only dump (0 tables is OK) */
        printf("FAIL: No tables found\n");
        pass = 0;
    }

    /* Phase 3: Parse all data */
    fn_set_row_cb(s, on_row, &g_state);
    fn_set_progress_cb(s, on_progress, &g_state);

    g_state.total_rows = 0;
    g_state.current_table[0] = '\0';

    rc = fn_parse_dump(s);
    if (rc != 0) {
        printf("FAIL: parse_dump returned %d: %s\n", rc, fn_get_error(s));
        pass = 0;
    }

    printf("\nTotal rows parsed: %d\n", g_state.total_rows);
    if (g_state.total_rows == 0 && g_state.table_count > 0) {
        /* Check if any table had expected rows */
        int expected_rows = 0;
        for (int i = 0; i < g_state.table_count && i < 200; i++) {
            expected_rows += g_state.table_rows[i];
        }
        if (expected_rows > 0) {
            printf("WARNING: Expected %d rows but parsed 0\n", expected_rows);
            g_state.warnings++;
        }
    }

    /* Phase 4: Test per-table parsing with filter + offset */
    if (g_state.table_count > 0 && g_state.table_offsets[0] > 0) {
        printf("\n--- Testing filtered parse (first table) ---\n");

        /* Extract schema and table from "SCHEMA.TABLE" */
        char schema_buf[256], table_buf[256];
        const char *dot = strchr(g_state.table_names[0], '.');
        if (dot) {
            int slen = (int)(dot - g_state.table_names[0]);
            strncpy(schema_buf, g_state.table_names[0], slen);
            schema_buf[slen] = '\0';
            strcpy(table_buf, dot + 1);

            ODV_SESSION *s2 = NULL;
            fn_create(&s2);
            fn_set_file(s2, path);
            fn_check_kind(s2, &dump_type);
            fn_set_table_cb(s2, on_table, &g_state);

            /* Reset state for filtered test */
            int prev_total = g_state.total_rows;
            g_state.total_rows = 0;
            g_state.current_table[0] = '\0';

            fn_set_row_cb(s2, on_row, &g_state);
            fn_set_filter(s2, schema_buf, table_buf);
            fn_set_offset(s2, g_state.table_offsets[0]);

            rc = fn_parse_dump(s2);
            if (rc != 0) {
                printf("FAIL: filtered parse_dump returned %d: %s\n", rc, fn_get_error(s2));
                pass = 0;
            } else {
                printf("Filtered parse: %d rows for %s.%s\n",
                       g_state.total_rows, schema_buf, table_buf);
                if (g_state.total_rows != g_state.table_rows[0] && g_state.table_rows[0] > 0) {
                    printf("WARNING: Expected %d rows, got %d\n",
                           g_state.table_rows[0], g_state.total_rows);
                    g_state.warnings++;
                }
            }

            g_state.total_rows = prev_total;
            fn_destroy(s2);
        }
    }

    fn_destroy(s);

    printf("\n%s: %s", path, pass ? "PASS" : "FAIL");
    if (g_state.warnings > 0) printf(" (%d warnings)", g_state.warnings);
    printf("\n");

    return pass;
}

int main(int argc, char *argv[]) {
    /* Set console to UTF-8 */
    SetConsoleOutputCP(65001);

    printf("OraDB DUMP Viewer - Parser Test Harness\n");
    printf("========================================\n");

    /* Load DLL */
    HMODULE dll = LoadLibraryA("OraDB_DumpParser.dll");
    if (!dll) {
        printf("ERROR: Cannot load OraDB_DumpParser.dll (error %lu)\n", GetLastError());
        printf("Make sure the DLL is in the same directory or PATH.\n");
        return 1;
    }

    /* Resolve functions */
    fn_create       = (FN_CREATE)GetProcAddress(dll, "_odv_create_session@4");
    fn_destroy      = (FN_DESTROY)GetProcAddress(dll, "_odv_destroy_session@4");
    fn_set_file     = (FN_SET_FILE)GetProcAddress(dll, "_odv_set_dump_file@8");
    fn_set_row_cb   = (FN_SET_ROW_CB)GetProcAddress(dll, "_odv_set_row_callback@12");
    fn_set_progress_cb = (FN_SET_PROGRESS_CB)GetProcAddress(dll, "_odv_set_progress_callback@12");
    fn_set_table_cb = (FN_SET_TABLE_CB)GetProcAddress(dll, "_odv_set_table_callback@12");
    fn_set_filter   = (FN_SET_FILTER)GetProcAddress(dll, "_odv_set_table_filter@12");
    fn_set_offset   = (FN_SET_OFFSET)GetProcAddress(dll, "_odv_set_data_offset@12");
    fn_check_kind   = (FN_CHECK_KIND)GetProcAddress(dll, "_odv_check_dump_kind@8");
    fn_list_tables  = (FN_LIST_TABLES)GetProcAddress(dll, "_odv_list_tables@4");
    fn_parse_dump   = (FN_PARSE_DUMP)GetProcAddress(dll, "_odv_parse_dump@4");
    fn_get_version  = (FN_GET_VERSION)GetProcAddress(dll, "_odv_get_version@0");
    fn_get_error    = (FN_GET_ERROR)GetProcAddress(dll, "_odv_get_last_error@4");
    fn_get_pct      = (FN_GET_PCT)GetProcAddress(dll, "_odv_get_progress_pct@4");

    /* Try undecorated names (x64 doesn't use __stdcall decoration) */
    if (!fn_create) {
        fn_create       = (FN_CREATE)GetProcAddress(dll, "odv_create_session");
        fn_destroy      = (FN_DESTROY)GetProcAddress(dll, "odv_destroy_session");
        fn_set_file     = (FN_SET_FILE)GetProcAddress(dll, "odv_set_dump_file");
        fn_set_row_cb   = (FN_SET_ROW_CB)GetProcAddress(dll, "odv_set_row_callback");
        fn_set_progress_cb = (FN_SET_PROGRESS_CB)GetProcAddress(dll, "odv_set_progress_callback");
        fn_set_table_cb = (FN_SET_TABLE_CB)GetProcAddress(dll, "odv_set_table_callback");
        fn_set_filter   = (FN_SET_FILTER)GetProcAddress(dll, "odv_set_table_filter");
        fn_set_offset   = (FN_SET_OFFSET)GetProcAddress(dll, "odv_set_data_offset");
        fn_check_kind   = (FN_CHECK_KIND)GetProcAddress(dll, "odv_check_dump_kind");
        fn_list_tables  = (FN_LIST_TABLES)GetProcAddress(dll, "odv_list_tables");
        fn_parse_dump   = (FN_PARSE_DUMP)GetProcAddress(dll, "odv_parse_dump");
        fn_get_version  = (FN_GET_VERSION)GetProcAddress(dll, "odv_get_version");
        fn_get_error    = (FN_GET_ERROR)GetProcAddress(dll, "odv_get_last_error");
        fn_get_pct      = (FN_GET_PCT)GetProcAddress(dll, "odv_get_progress_pct");
    }

    if (!fn_create || !fn_destroy || !fn_set_file || !fn_check_kind ||
        !fn_list_tables || !fn_parse_dump) {
        printf("ERROR: Cannot resolve DLL functions\n");
        FreeLibrary(dll);
        return 1;
    }

    printf("DLL version: %s\n", fn_get_version ? fn_get_version() : "unknown");

    /* Run tests */
    int pass = 0, fail = 0;
    const char *base = ".";
    if (argc > 1) base = argv[1];

    char path[512];

    /* EXPDP tests */
    snprintf(path, 512, "%s/23ai/expdp_schema_odv_test.dmp", base);
    if (test_dump(path, 0)) pass++; else fail++;

    snprintf(path, 512, "%s/23ai/expdp_tables.dmp", base);
    if (test_dump(path, 0)) pass++; else fail++;

    snprintf(path, 512, "%s/23ai/expdp_multi_schema.dmp", base);
    if (test_dump(path, 0)) pass++; else fail++;

    snprintf(path, 512, "%s/23ai/expdp_data_only.dmp", base);
    if (test_dump(path, 0)) pass++; else fail++;

    snprintf(path, 512, "%s/23ai/expdp_metadata_only.dmp", base);
    if (test_dump(path, -2)) pass++; else fail++;  /* metadata-only: 0 tables OK */

    /* EXP tests */
    snprintf(path, 512, "%s/11g/exp_tables.dmp", base);
    if (test_dump(path, 10)) pass++; else fail++;

    snprintf(path, 512, "%s/11g/exp_user.dmp", base);
    if (test_dump(path, 10)) pass++; else fail++;

    snprintf(path, 512, "%s/11g/exp_direct.dmp", base);
    if (test_dump(path, 10)) pass++; else fail++;  /* DIRECT=Y is detected as EXP (functionally identical) */

    snprintf(path, 512, "%s/11g/exp_index_test.dmp", base);
    if (test_dump(path, 10)) pass++; else fail++;  /* INDEX test: PK + 3 indexes */

    snprintf(path, 512, "%s/11g/exp_comment_test.dmp", base);
    if (test_dump(path, 10)) pass++; else fail++;  /* COMMENT test: table + 4 column comments */

    /* Summary */
    printf("\n========================================\n");
    printf("RESULTS: %d passed, %d failed (total %d)\n", pass, fail, pass + fail);
    printf("========================================\n");

    FreeLibrary(dll);
    return fail > 0 ? 1 : 0;
}
