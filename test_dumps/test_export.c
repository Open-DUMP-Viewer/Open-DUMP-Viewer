/*****************************************************************************
    OraDB DUMP Viewer - Export Test Harness
    テスト用ダンプファイルから CSV/SQL を生成し、結果を検証する
 *****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

/* --- DLL function pointers --- */
typedef struct _odv_session ODV_SESSION;

typedef void (__stdcall *ODV_TABLE_CALLBACK)(
    const char *schema, const char *table, int col_count,
    const char **col_names, const char **col_types,
    const int *col_not_nulls, const char **col_defaults,
    int constraint_count, const char *constraints_json,
    __int64 row_count, __int64 data_offset, void *user_data);

typedef int (__stdcall *FN_CREATE)(ODV_SESSION **s);
typedef int (__stdcall *FN_DESTROY)(ODV_SESSION *s);
typedef int (__stdcall *FN_SET_FILE)(ODV_SESSION *s, const char *path);
typedef int (__stdcall *FN_SET_TABLE_CB)(ODV_SESSION *s, ODV_TABLE_CALLBACK cb, void *ud);
typedef int (__stdcall *FN_CHECK_KIND)(ODV_SESSION *s, int *dump_type);
typedef int (__stdcall *FN_LIST_TABLES)(ODV_SESSION *s);
typedef int (__stdcall *FN_EXPORT_CSV)(ODV_SESSION *s, const char *table_name, const char *output_path);
typedef int (__stdcall *FN_EXPORT_SQL)(ODV_SESSION *s, const char *table_name, const char *output_path, int dbms_type);
typedef int (__stdcall *FN_SET_CSV_OPTS)(ODV_SESSION *s, int write_header, int write_types);
typedef int (__stdcall *FN_SET_SQL_OPTS)(ODV_SESSION *s, int create_table);
typedef int (__stdcall *FN_SET_DATE_FMT)(ODV_SESSION *s, int fmt, const char *custom_fmt);
typedef const char* (__stdcall *FN_GET_VERSION)(void);
typedef const char* (__stdcall *FN_GET_ERROR)(ODV_SESSION *s);

static FN_CREATE        fn_create;
static FN_DESTROY       fn_destroy;
static FN_SET_FILE      fn_set_file;
static FN_SET_TABLE_CB  fn_set_table_cb;
static FN_CHECK_KIND    fn_check_kind;
static FN_LIST_TABLES   fn_list_tables;
static FN_EXPORT_CSV    fn_export_csv;
static FN_EXPORT_SQL    fn_export_sql;
static FN_SET_CSV_OPTS  fn_set_csv_opts;
static FN_SET_SQL_OPTS  fn_set_sql_opts;
static FN_SET_DATE_FMT  fn_set_date_fmt;
static FN_GET_VERSION   fn_get_version;
static FN_GET_ERROR     fn_get_error;

/* --- Table list state --- */
#define MAX_TABLES 200
typedef struct {
    int count;
    char names[MAX_TABLES][256];
    char schemas[MAX_TABLES][256];
    __int64 row_counts[MAX_TABLES];
} TableList;

static void __stdcall on_table(const char *schema, const char *table,
    int col_count, const char **col_names, const char **col_types,
    const int *col_not_nulls, const char **col_defaults,
    int constraint_count, const char *constraints_json,
    __int64 row_count, __int64 data_offset, void *ud)
{
    TableList *tl = (TableList *)ud;
    if (tl->count < MAX_TABLES) {
        strncpy(tl->schemas[tl->count], schema, 255);
        strncpy(tl->names[tl->count], table, 255);
        tl->row_counts[tl->count] = row_count;
        tl->count++;
    }
}

/* Count lines in a file */
static int count_lines(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    int count = 0;
    char buf[8192];
    while (fgets(buf, sizeof(buf), f)) count++;
    fclose(f);
    return count;
}

/* Count INSERT statements in a SQL file */
static int count_inserts(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    int count = 0;
    char buf[65536];
    while (fgets(buf, sizeof(buf), f)) {
        if (strncmp(buf, "INSERT INTO", 11) == 0) count++;
    }
    fclose(f);
    return count;
}

/* Check if file contains CREATE TABLE */
static int has_create_table(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    char buf[65536];
    while (fgets(buf, sizeof(buf), f)) {
        if (strstr(buf, "CREATE TABLE") != NULL) {
            fclose(f);
            return 1;
        }
    }
    fclose(f);
    return 0;
}

/* Count COMMENT ON statements in a SQL file */
static int count_comment_on(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    int count = 0;
    char buf[65536];
    while (fgets(buf, sizeof(buf), f)) {
        if (strncmp(buf, "COMMENT ON", 10) == 0) count++;
    }
    fclose(f);
    return count;
}

/* Count CREATE INDEX statements in a SQL file */
static int count_create_indexes(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    int count = 0;
    char buf[65536];
    while (fgets(buf, sizeof(buf), f)) {
        if (strncmp(buf, "CREATE INDEX", 12) == 0) count++;
    }
    fclose(f);
    return count;
}

/* Get file size */
static long get_file_size(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fclose(f);
    return size;
}

/* DBMS type names */
static const char *dbms_name(int t) {
    switch (t) {
        case 0: return "Oracle";
        case 4: return "PostgreSQL";
        case 5: return "MySQL";
        case 6: return "SQLServer";
        default: return "Unknown";
    }
}

int main(int argc, char *argv[]) {
    SetConsoleOutputCP(65001);

    printf("OraDB DUMP Viewer - Export Test Harness\n");
    printf("========================================\n");

    /* Load DLL */
    HMODULE dll = LoadLibraryA("OraDB_DumpParser.dll");
    if (!dll) {
        printf("ERROR: Cannot load OraDB_DumpParser.dll (error %lu)\n", GetLastError());
        return 1;
    }

    /* Resolve functions (x64 undecorated names) */
    fn_create       = (FN_CREATE)GetProcAddress(dll, "odv_create_session");
    fn_destroy      = (FN_DESTROY)GetProcAddress(dll, "odv_destroy_session");
    fn_set_file     = (FN_SET_FILE)GetProcAddress(dll, "odv_set_dump_file");
    fn_set_table_cb = (FN_SET_TABLE_CB)GetProcAddress(dll, "odv_set_table_callback");
    fn_check_kind   = (FN_CHECK_KIND)GetProcAddress(dll, "odv_check_dump_kind");
    fn_list_tables  = (FN_LIST_TABLES)GetProcAddress(dll, "odv_list_tables");
    fn_export_csv   = (FN_EXPORT_CSV)GetProcAddress(dll, "odv_export_csv");
    fn_export_sql   = (FN_EXPORT_SQL)GetProcAddress(dll, "odv_export_sql");
    fn_set_csv_opts = (FN_SET_CSV_OPTS)GetProcAddress(dll, "odv_set_csv_options");
    fn_set_sql_opts = (FN_SET_SQL_OPTS)GetProcAddress(dll, "odv_set_sql_options");
    fn_set_date_fmt = (FN_SET_DATE_FMT)GetProcAddress(dll, "odv_set_date_format");
    fn_get_version  = (FN_GET_VERSION)GetProcAddress(dll, "odv_get_version");
    fn_get_error    = (FN_GET_ERROR)GetProcAddress(dll, "odv_get_last_error");

    if (!fn_create || !fn_export_csv || !fn_export_sql) {
        printf("ERROR: Cannot resolve DLL export functions\n");
        if (!fn_export_csv) printf("  Missing: odv_export_csv\n");
        if (!fn_export_sql) printf("  Missing: odv_export_sql\n");
        FreeLibrary(dll);
        return 1;
    }

    printf("DLL version: %s\n\n", fn_get_version ? fn_get_version() : "unknown");

    /* Create output directory */
    CreateDirectoryA("export_output", NULL);

    /* Test dump file */
    const char *dump_file = "./23ai/expdp_schema_odv_test.dmp";

    /* Step 1: List tables */
    printf("Phase 1: Listing tables in %s\n", dump_file);
    printf("----------------------------------------\n");

    ODV_SESSION *s = NULL;
    TableList tables;
    memset(&tables, 0, sizeof(tables));

    fn_create(&s);
    fn_set_file(s, dump_file);
    int dump_type;
    fn_check_kind(s, &dump_type);
    fn_set_table_cb(s, on_table, &tables);
    fn_list_tables(s);
    fn_destroy(s);

    printf("Found %d tables\n\n", tables.count);

    /* Select test tables (mix of types) */
    const char *test_tables[] = {
        "T_BASIC_TYPES",
        "T_NUMERIC_TYPES",
        "T_DATETIME_TYPES",
        "T_NCHAR_TYPES",
        "T_TRAILING_NULLS",
        "T_SPECIAL_CHARS",
        "T_SINGLE_ROW",
        "T_EMPTY",
        "T_WIDE_TABLE",
        NULL
    };

    /* Also test Japanese tables */
    const char *jp_test_tables[] = {
        "\xe7\xa4\xbe\xe5\x93\xa1\xe3\x83\x9e\xe3\x82\xb9\xe3\x82\xbf",  /* 社員マスタ */
        "\xe9\x83\xa8\xe7\xbd\xb2\xe3\x83\x9e\xe3\x82\xb9\xe3\x82\xbf",  /* 部署マスタ */
        "\xe5\x8f\x97\xe6\xb3\xa8\xe3\x83\x87\xe3\x83\xbc\xe3\x82\xbf",  /* 受注データ */
        NULL
    };

    int pass = 0, fail = 0;

    /* ================================================================
       Phase 2: CSV Export Tests
       ================================================================ */
    printf("Phase 2: CSV Export Tests\n");
    printf("========================================\n");

    for (int i = 0; test_tables[i]; i++) {
        char out_path[512];
        snprintf(out_path, 512, "export_output/%s.csv", test_tables[i]);

        s = NULL;
        fn_create(&s);
        fn_set_file(s, dump_file);
        fn_check_kind(s, &dump_type);
        fn_set_table_cb(s, on_table, &tables);
        fn_list_tables(s);
        if (fn_set_csv_opts) fn_set_csv_opts(s, 1, 1);  /* header + types */

        int rc = fn_export_csv(s, test_tables[i], out_path);
        if (rc == 0) {
            long fsize = get_file_size(out_path);
            int lines = count_lines(out_path);
            printf("  CSV %-25s -> %s (%ld bytes, %d lines) OK\n",
                   test_tables[i], out_path, fsize, lines);
            pass++;
        } else {
            printf("  CSV %-25s -> FAIL: %s\n", test_tables[i],
                   fn_get_error ? fn_get_error(s) : "unknown error");
            fail++;
        }
        fn_destroy(s);
    }

    /* Japanese table CSV */
    for (int i = 0; jp_test_tables[i]; i++) {
        char out_path[512];
        snprintf(out_path, 512, "export_output/jp_%d.csv", i);

        s = NULL;
        fn_create(&s);
        fn_set_file(s, dump_file);
        fn_check_kind(s, &dump_type);
        fn_set_table_cb(s, on_table, &tables);
        fn_list_tables(s);
        if (fn_set_csv_opts) fn_set_csv_opts(s, 1, 0);

        int rc = fn_export_csv(s, jp_test_tables[i], out_path);
        if (rc == 0) {
            long fsize = get_file_size(out_path);
            printf("  CSV %-25s -> %s (%ld bytes) OK\n",
                   jp_test_tables[i], out_path, fsize);
            pass++;
        } else {
            printf("  CSV %-25s -> FAIL: %s\n", jp_test_tables[i],
                   fn_get_error ? fn_get_error(s) : "unknown error");
            fail++;
        }
        fn_destroy(s);
    }

    printf("\n");

    /* ================================================================
       Phase 3: SQL Export Tests (All 4 DBMS types)
       ================================================================ */
    int dbms_types[] = {0, 4, 5, 6};  /* Oracle, PostgreSQL, MySQL, SQL Server */

    for (int d = 0; d < 4; d++) {
        int dbms = dbms_types[d];
        printf("Phase 3.%d: SQL Export - %s\n", d+1, dbms_name(dbms));
        printf("----------------------------------------\n");

        for (int i = 0; test_tables[i]; i++) {
            char out_path[512];
            snprintf(out_path, 512, "export_output/%s_%s.sql",
                     test_tables[i], dbms_name(dbms));

            s = NULL;
            fn_create(&s);
            fn_set_file(s, dump_file);
            fn_check_kind(s, &dump_type);
            fn_set_table_cb(s, on_table, &tables);
            fn_list_tables(s);
            if (fn_set_sql_opts) fn_set_sql_opts(s, 1);  /* include CREATE TABLE */

            int rc = fn_export_sql(s, test_tables[i], out_path, dbms);
            if (rc == 0) {
                long fsize = get_file_size(out_path);
                int inserts = count_inserts(out_path);
                int has_ddl = has_create_table(out_path);
                printf("  SQL %-25s -> %s (%ld bytes, %d INSERTs, DDL=%s) OK\n",
                       test_tables[i], out_path, fsize, inserts,
                       has_ddl ? "YES" : "NO");
                pass++;
            } else {
                printf("  SQL %-25s -> FAIL: %s\n", test_tables[i],
                       fn_get_error ? fn_get_error(s) : "unknown error");
                fail++;
            }
            fn_destroy(s);
        }

        /* Japanese table SQL */
        for (int i = 0; jp_test_tables[i]; i++) {
            char out_path[512];
            snprintf(out_path, 512, "export_output/jp_%d_%s.sql", i, dbms_name(dbms));

            s = NULL;
            fn_create(&s);
            fn_set_file(s, dump_file);
            fn_check_kind(s, &dump_type);
            fn_set_table_cb(s, on_table, &tables);
            fn_list_tables(s);
            if (fn_set_sql_opts) fn_set_sql_opts(s, 1);

            int rc = fn_export_sql(s, jp_test_tables[i], out_path, dbms);
            if (rc == 0) {
                long fsize = get_file_size(out_path);
                int inserts = count_inserts(out_path);
                printf("  SQL %-25s -> %s (%ld bytes, %d INSERTs) OK\n",
                       jp_test_tables[i], out_path, fsize, inserts);
                pass++;
            } else {
                printf("  SQL %-25s -> FAIL: %s\n", jp_test_tables[i],
                       fn_get_error ? fn_get_error(s) : "unknown error");
                fail++;
            }
            fn_destroy(s);
        }
        printf("\n");
    }

    /* ================================================================
       Phase 4: EXP format test
       ================================================================ */
    const char *exp_file = "./11g/exp_user.dmp";
    printf("Phase 4: EXP Format Export Tests (%s)\n", exp_file);
    printf("========================================\n");

    const char *exp_tables[] = {"T_BASIC_TYPES", "T_NUMERIC_TYPES", "T_DATETIME_TYPES", NULL};
    for (int i = 0; exp_tables[i]; i++) {
        for (int d = 0; d < 4; d++) {
            int dbms = dbms_types[d];
            char out_path[512];
            snprintf(out_path, 512, "export_output/exp_%s_%s.sql",
                     exp_tables[i], dbms_name(dbms));

            s = NULL;
            fn_create(&s);
            fn_set_file(s, exp_file);
            fn_check_kind(s, &dump_type);
            fn_set_table_cb(s, on_table, &tables);
            fn_list_tables(s);
            if (fn_set_sql_opts) fn_set_sql_opts(s, 1);

            int rc = fn_export_sql(s, exp_tables[i], out_path, dbms);
            if (rc == 0) {
                int inserts = count_inserts(out_path);
                printf("  EXP SQL %-20s [%s] -> %d INSERTs OK\n",
                       exp_tables[i], dbms_name(dbms), inserts);
                pass++;
            } else {
                printf("  EXP SQL %-20s [%s] -> FAIL: %s\n",
                       exp_tables[i], dbms_name(dbms),
                       fn_get_error ? fn_get_error(s) : "unknown error");
                fail++;
            }
            fn_destroy(s);
        }
    }

    /* ================================================================
       Phase 5: EXP INDEX Export Test (exp_index_test.dmp)
       ================================================================ */
    const char *idx_file = "./11g/exp_index_test.dmp";
    printf("\nPhase 5: INDEX Export Test (%s)\n", idx_file);
    printf("========================================\n");

    for (int d = 0; d < 4; d++) {
        int dbms = dbms_types[d];
        char out_path[512];
        snprintf(out_path, 512, "export_output/idx_T_INDEX_TEST_%s.sql",
                 dbms_name(dbms));

        s = NULL;
        fn_create(&s);
        fn_set_file(s, idx_file);
        fn_check_kind(s, &dump_type);
        fn_set_table_cb(s, on_table, &tables);
        fn_list_tables(s);
        if (fn_set_sql_opts) fn_set_sql_opts(s, 1);

        int rc = fn_export_sql(s, "T_INDEX_TEST", out_path, dbms);
        if (rc == 0) {
            int inserts = count_inserts(out_path);
            int indexes = count_create_indexes(out_path);
            int has_ddl = has_create_table(out_path);
            printf("  IDX SQL [%-10s] -> %d INSERTs, DDL=%s, %d CREATE INDEX",
                   dbms_name(dbms), inserts, has_ddl ? "YES" : "NO", indexes);
            if (indexes == 3) {
                printf(" OK\n");
                pass++;
            } else {
                printf(" FAIL (expected 3 CREATE INDEX)\n");
                fail++;
            }
        } else {
            printf("  IDX SQL [%-10s] -> FAIL: %s\n", dbms_name(dbms),
                   fn_get_error ? fn_get_error(s) : "unknown error");
            fail++;
        }
        fn_destroy(s);
    }

    /* ================================================================
       Phase 6: EXP COMMENT Export Test (exp_comment_test.dmp)
       ================================================================ */
    const char *cmt_file = "./11g/exp_comment_test.dmp";
    printf("\nPhase 6: COMMENT Export Test (%s)\n", cmt_file);
    printf("========================================\n");

    /* Oracle and PostgreSQL should have COMMENT ON statements */
    for (int d = 0; d < 4; d++) {
        int dbms = dbms_types[d];
        char out_path[512];
        snprintf(out_path, 512, "export_output/cmt_T_COMMENT_TEST_%s.sql",
                 dbms_name(dbms));

        s = NULL;
        fn_create(&s);
        fn_set_file(s, cmt_file);
        fn_check_kind(s, &dump_type);
        fn_set_table_cb(s, on_table, &tables);
        fn_list_tables(s);
        if (fn_set_sql_opts) fn_set_sql_opts(s, 1);

        int rc = fn_export_sql(s, "T_COMMENT_TEST", out_path, dbms);
        if (rc == 0) {
            int inserts = count_inserts(out_path);
            int comments = count_comment_on(out_path);
            int has_ddl = has_create_table(out_path);
            printf("  CMT SQL [%-10s] -> %d INSERTs, DDL=%s, %d COMMENT ON",
                   dbms_name(dbms), inserts, has_ddl ? "YES" : "NO", comments);
            /* Oracle/PostgreSQL: 1 table comment + 4 column comments = 5 */
            if ((dbms == 0 || dbms == 4) && comments == 5) {
                printf(" OK\n");
                pass++;
            } else if ((dbms == 5 || dbms == 6) && comments == 0) {
                /* MySQL/SQL Server: comments as SQL comments, not COMMENT ON */
                printf(" OK (as SQL comments)\n");
                pass++;
            } else {
                printf(" FAIL (expected %d COMMENT ON)\n",
                       (dbms == 0 || dbms == 4) ? 5 : 0);
                fail++;
            }
        } else {
            printf("  CMT SQL [%-10s] -> FAIL: %s\n", dbms_name(dbms),
                   fn_get_error ? fn_get_error(s) : "unknown error");
            fail++;
        }
        fn_destroy(s);
    }

    printf("\n========================================\n");
    printf("EXPORT TEST RESULTS: %d passed, %d failed (total %d)\n", pass, fail, pass + fail);
    printf("========================================\n");

    FreeLibrary(dll);
    return fail > 0 ? 1 : 0;
}
