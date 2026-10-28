/*****************************************************************************
    Open DUMP Viewer for Oracle database

    odv_api.c
    DLL exported functions - session management and operation dispatch

    Copyright (C) 2026 YANAI Taketo
 *****************************************************************************/

#include "odv_types.h"
#include "odv_api.h"

/*---------------------------------------------------------------------------
    Version
 ---------------------------------------------------------------------------*/
#define ODV_VERSION_STRING "4.7.0"

/*---------------------------------------------------------------------------
    Internal helpers
 ---------------------------------------------------------------------------*/

static void set_error(ODV_SESSION *s, const char *msg)
{
    if (s && msg) {
        odv_strcpy(s->last_error, msg, ODV_MSG_LEN);
    }
}

/* Start of a public operation that reads the dump: forget how far the
   previous one read (M-21). */
static void op_begin(ODV_SESSION *s)
{
    s->stream_cut = ODV_TAIL_COMPLETE;
}

/* End of such an operation (M-21). When it read up to the early end of a
   dump (see "Where a dump ends"), a successful result is turned into the
   matching warning, with the reason in last_error; everything read before
   that point has been delivered. Errors and cancellation pass unchanged. */
static int op_finish(ODV_SESSION *s, int rc)
{
    if (rc != ODV_OK) return rc;
    if (s->stream_cut == ODV_TAIL_CUT) {
        set_error(s, "The dump file is truncated: it ends before the end of the "
                     "export. Tables and rows after that point are missing, and "
                     "the last table read may be incomplete.");
        return ODV_WARN_TRUNCATED;
    }
    if (s->stream_cut == ODV_TAIL_VOLMISS) {
        set_error(s, "The dump continues in a further file of a split dump set "
                     "(FILESIZE / dumpfile=%U) that was not found next to this "
                     "one. Only the tables and rows in the files found are read.");
        return ODV_WARN_VOLUME_MISSING;
    }
    return rc;
}

/*---------------------------------------------------------------------------
    odv_fopen_path (M-26)

    The application hands every path to the DLL in UTF-8 (LPUTF8Str). The
    Windows CRT's fopen() reads a char path in the ANSI code page, so a path
    with Japanese or any other non-ASCII character named a different, non-
    existent file: LOB extraction into C:\temp\出力 wrote nothing at all.
    A path that is valid UTF-8 is therefore converted to UTF-16 and opened
    with _wfopen(); one of MAX_PATH (260) or more units gets the \\?\ prefix
    (after GetFullPathNameW, which that prefix requires), so a long path does
    not fail either. Bytes that are not UTF-8 can only be an ANSI path from an
    older caller and go to fopen() as before; so does a read-only open whose
    UTF-8 reading names no file, for the same reason.
 ---------------------------------------------------------------------------*/
#ifdef WINDOWS
static wchar_t *odv_utf8_to_wpath(const char *path)
{
    int wn;
    wchar_t *w;

    wn = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, NULL, 0);
    if (wn <= 0) return NULL;                       /* not UTF-8 */
    w = (wchar_t *)malloc((size_t)wn * sizeof(wchar_t));
    if (!w) return NULL;
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, w, wn) != wn) {
        free(w);
        return NULL;
    }
    if (wn - 1 >= MAX_PATH && wcsncmp(w, L"\\\\?\\", 4) != 0) {
        DWORD fn = GetFullPathNameW(w, 0, NULL, NULL);
        wchar_t *full, *lp;
        size_t fl;
        if (fn == 0) return w;                      /* let _wfopen report it */
        full = (wchar_t *)malloc((size_t)fn * sizeof(wchar_t));
        if (!full) return w;
        if (GetFullPathNameW(w, fn, full, NULL) == 0) { free(full); return w; }
        fl = wcslen(full);
        lp = (wchar_t *)malloc((fl + 8) * sizeof(wchar_t));
        if (!lp) { free(full); return w; }
        if (full[0] == L'\\' && full[1] == L'\\') {  /* \\server\share\... */
            wcscpy(lp, L"\\\\?\\UNC\\");
            wcscat(lp, full + 2);
        } else {
            wcscpy(lp, L"\\\\?\\");
            wcscat(lp, full);
        }
        free(full);
        free(w);
        w = lp;
    }
    return w;
}
#endif

FILE *odv_fopen_path(const char *path, const char *mode)
{
#ifdef WINDOWS
    wchar_t wmode[8];
    wchar_t *w;
    FILE *fp;
    int i;

    if (!path || !mode) return NULL;
    w = odv_utf8_to_wpath(path);
    if (!w) return fopen(path, mode);               /* ANSI path */
    for (i = 0; mode[i] && i < 7; i++) wmode[i] = (wchar_t)(unsigned char)mode[i];
    wmode[i] = L'\0';
    fp = _wfopen(w, wmode);
    free(w);
    if (!fp && mode[0] == 'r') {
        for (i = 0; path[i]; i++)
            if ((unsigned char)path[i] >= 0x80) return fopen(path, mode);
    }
    return fp;
#else
    if (!path || !mode) return NULL;
    return fopen(path, mode);
#endif
}

static void clear_session(ODV_SESSION *s)
{
    memset(s, 0, sizeof(ODV_SESSION));
    s->dump_type = DUMP_UNKNOWN;
    s->dump_charset = CHARSET_UTF8;
    s->out_charset = CHARSET_UTF8;
    s->last_progress_pct = -1;

    /* Export option defaults */
    s->date_format = DATE_FMT_SLASH;
    s->custom_date_format[0] = '\0';
    s->csv_write_header = 1;
    s->csv_write_types = 0;
    s->csv_delimiter = ',';
    s->sql_flags = ODV_SQL_ALL;
}

/*---------------------------------------------------------------------------
    Session Lifecycle
 ---------------------------------------------------------------------------*/

ODV_API int ODV_CALL odv_create_session(ODV_SESSION **session)
{
    ODV_SESSION *s;

    if (!session) return ODV_ERROR_INVALID_ARG;

    s = (ODV_SESSION *)calloc(1, sizeof(ODV_SESSION));
    if (!s) return ODV_ERROR_MALLOC;

    clear_session(s);

    /* Pre-allocate record buffer */
    if (init_record(&s->record, 256) != ODV_OK) {
        free(s);
        return ODV_ERROR_MALLOC;
    }

    *session = s;
    return ODV_OK;
}

ODV_API int ODV_CALL odv_destroy_session(ODV_SESSION *session)
{
    if (!session) return ODV_ERROR_INVALID_ARG;

    free_record(&session->record);
    odv_expdp_meta_release(session);

    odv_table_free_text(&session->table);
    free(session->table_list);
    session->table_list = NULL;
    free(session->tl_bucket);
    session->tl_bucket = NULL;
    free(session->tl_next);
    session->tl_next = NULL;
    free(session->partition_list);
    session->partition_list = NULL;
    free(session->seek_list);
    session->seek_list = NULL;
    odv_ddl_hints_clear(session);
    free(session->ddl_hint_in);
    session->ddl_hint_in = NULL;
    free_meta_cache(session);

    /* Free LOB buffer if allocated */
    if (session->state.lob_buf) {
        free(session->state.lob_buf);
        session->state.lob_buf = NULL;
    }

    free(session);
    return ODV_OK;
}

/*---------------------------------------------------------------------------
    odv_table_list_reserve

    table_list used to be a fixed array of 1000 entries shared by tables and
    their partitions. Past it the parsers stopped adding entries but kept
    calling table_cb, so the product (which pairs callback i with
    odv_get_table_entry(i)) showed the overflow partitions as unnamed plain
    tables and opened the wrong partition (M-29). It now grows on demand up
    to ODV_MAX_TABLES. On reaching the cap, or if memory runs out, a warning
    is left in last_error and 0 is returned; callers then skip that table
    entirely (entry and callback) so the pairing stays intact.
 ---------------------------------------------------------------------------*/
int odv_table_list_reserve(ODV_SESSION *s)
{
    int new_cap;
    ODV_TABLE_ENTRY *p;

    if (!s) return 0;
    if (s->table_count < s->table_cap) return 1;
    if (s->table_cap >= ODV_MAX_TABLES) {
        set_error(s, "Too many tables and partitions in the dump; "
                     "the rest are not listed");
        return 0;
    }
    new_cap = (s->table_cap > 0) ? s->table_cap * 2 : 256;
    if (new_cap > ODV_MAX_TABLES) new_cap = ODV_MAX_TABLES;
    p = (ODV_TABLE_ENTRY *)realloc(s->table_list,
                                   (size_t)new_cap * sizeof(ODV_TABLE_ENTRY));
    if (!p) {
        set_error(s, "Out of memory while listing tables; "
                     "the rest are not listed");
        return 0;
    }
    memset(p + s->table_cap, 0,
           (size_t)(new_cap - s->table_cap) * sizeof(ODV_TABLE_ENTRY));
    s->table_list = p;
    s->table_cap = new_cap;
    return 1;
}

/*---------------------------------------------------------------------------
    odv_table_list_find

    Both parsers mark a table's partitions by finding the first entry with
    the same schema and name. That was a scan of the whole list for every
    new entry: harmless at the old 1000-entry cap, quadratic now that the
    list grows (M-29). The index holds only the first entry of each name;
    it is brought up to date with table_list[0, table_count) on each call
    (rebuilt when the list was cleared or the bucket array grows), so it
    needs no hook in the code that appends or resets entries.
 ---------------------------------------------------------------------------*/
static unsigned int tl_hash(const char *schema, const char *name)
{
    unsigned int h = 2166136261u;                 /* FNV-1a */
    while (*schema) { h ^= (unsigned char)*schema++; h *= 16777619u; }
    h ^= 0x01u;
    h *= 16777619u;
    while (*name) { h ^= (unsigned char)*name++; h *= 16777619u; }
    return h;
}

static int tl_lookup(const ODV_SESSION *s, const char *schema, const char *name,
                     unsigned int h)
{
    int k = s->tl_bucket[h & (unsigned int)(s->tl_nbucket - 1)];
    while (k >= 0) {
        if (strcmp(s->table_list[k].name, name) == 0 &&
            strcmp(s->table_list[k].schema, schema) == 0)
            return k;
        k = s->tl_next[k];
    }
    return -1;
}

static int tl_sync(ODV_SESSION *s)
{
    int j;

    if (s->table_count < s->tl_count) s->tl_count = 0;   /* list was cleared */
    if (s->tl_next_cap < s->table_cap) {
        int *nn = (int *)realloc(s->tl_next, (size_t)s->table_cap * sizeof(int));
        if (!nn) return 0;
        s->tl_next = nn;
        s->tl_next_cap = s->table_cap;
    }
    if (!s->tl_bucket || s->tl_nbucket < s->table_count || s->tl_count == 0) {
        int nb = (s->tl_nbucket > 0) ? s->tl_nbucket : 1024;
        int *bk;
        while (nb < s->table_count) nb *= 2;
        bk = (s->tl_bucket && nb == s->tl_nbucket)
             ? s->tl_bucket : (int *)malloc((size_t)nb * sizeof(int));
        if (!bk) return 0;
        if (bk != s->tl_bucket) free(s->tl_bucket);
        for (j = 0; j < nb; j++) bk[j] = -1;
        s->tl_bucket = bk;
        s->tl_nbucket = nb;
        s->tl_count = 0;                                  /* index again */
    }
    for (j = s->tl_count; j < s->table_count; j++) {
        const ODV_TABLE_ENTRY *e = &s->table_list[j];
        unsigned int h = tl_hash(e->schema, e->name);
        if (tl_lookup(s, e->schema, e->name, h) < 0) {
            int b = (int)(h & (unsigned int)(s->tl_nbucket - 1));
            s->tl_next[j] = s->tl_bucket[b];
            s->tl_bucket[b] = j;
        }
    }
    s->tl_count = s->table_count;
    return 1;
}

int odv_table_list_find(ODV_SESSION *s, const char *schema, const char *name)
{
    int k;
    if (!s || !schema || !name) return -1;
    if (tl_sync(s)) return tl_lookup(s, schema, name, tl_hash(schema, name));
    /* no memory for the index: the plain scan gives the same answer */
    for (k = 0; k < s->table_count; k++)
        if (strcmp(s->table_list[k].schema, schema) == 0 &&
            strcmp(s->table_list[k].name, name) == 0)
            return k;
    return -1;
}

/*---------------------------------------------------------------------------
    DDL text fields of any length (see ODV_TEXT in odv_types.h)
 ---------------------------------------------------------------------------*/
int odv_text_set(char *fixed, size_t cap, char **longp, const char *src, size_t len)
{
    free(*longp);
    *longp = NULL;
    if (len < cap) {
        memcpy(fixed, src, len);
        fixed[len] = '\0';
        return 1;
    }
    fixed[0] = '\0';
    *longp = (char *)malloc(len + 1);
    if (!*longp) return 0;
    memcpy(*longp, src, len);
    (*longp)[len] = '\0';
    return 1;
}

ODV_CONSTRAINT *odv_table_add_constraint(ODV_TABLE *t, int type)
{
    ODV_CONSTRAINT *c;
    if (!t) return NULL;
    if (t->constraint_count >= t->constraint_cap) {
        int nc = t->constraint_cap ? t->constraint_cap * 2 : 16;
        ODV_CONSTRAINT *na;
        if (nc > ODV_MAX_CONSTRAINTS) nc = ODV_MAX_CONSTRAINTS;
        if (nc <= t->constraint_cap) return NULL;
        na = (ODV_CONSTRAINT *)realloc(t->constraints, (size_t)nc * sizeof(ODV_CONSTRAINT));
        if (!na) return NULL;
        t->constraints = na;
        t->constraint_cap = nc;
    }
    c = &t->constraints[t->constraint_count++];
    memset(c, 0, sizeof(*c));
    c->type = type;
    return c;
}

void odv_table_free_text(ODV_TABLE *t)
{
    int i;
    if (!t) return;
    for (i = 0; i < t->col_count && i < ODV_MAX_COLUMNS; i++) {
        free(t->columns[i].default_long);
        t->columns[i].default_long = NULL;
        free(t->columns[i].comment_long);
        t->columns[i].comment_long = NULL;
    }
    for (i = 0; i < t->constraint_count; i++) {
        free(t->constraints[i].condition_long);
        free(t->constraints[i].index_expr_long);
    }
    free(t->constraints);
    t->constraints = NULL;
    t->constraint_count = 0;
    t->constraint_cap = 0;
    free(t->comment_long);
    t->comment_long = NULL;
    free(t->notes);
    t->notes = NULL;
}

/*---------------------------------------------------------------------------
    Configuration
 ---------------------------------------------------------------------------*/

ODV_API int ODV_CALL odv_set_dump_file(ODV_SESSION *s, const char *path)
{
    FILE *fp;

    if (!s || !path) return ODV_ERROR_INVALID_ARG;

    /* A cut path would name another file (or none): refuse it (M-26). */
    if (strlen(path) > ODV_PATH_LEN) {
        set_error(s, "Dump file path is too long");
        return ODV_ERROR_INVALID_ARG;
    }
    odv_strcpy(s->dump_path, path, ODV_PATH_LEN);
    odv_expdp_meta_release(s);      /* another dump: its own DDL metadata */
    odv_ddl_hints_clear(s);         /* ... and its own DDL hints */
    free(s->ddl_hint_in);
    s->ddl_hint_in = NULL;

    /* Verify file is accessible and get size */
    fp = odv_fopen_path(path, "rb");
    if (!fp) {
        set_error(s, "Cannot open dump file");
        return ODV_ERROR_FOPEN;
    }
    odv_fseek(fp, 0, SEEK_END);
    s->dump_size = odv_ftell(fp);
    fclose(fp);

    /* Reset state for new file */
    s->dump_type = DUMP_UNKNOWN;
    s->table_count = 0;
    s->total_rows = 0;
    s->cancelled = 0;
    invalidate_meta_cache(s);   /* names are converted with this dump's charset */

    return ODV_OK;
}

ODV_API int ODV_CALL odv_set_row_callback(ODV_SESSION *s, ODV_ROW_CALLBACK cb, void *user_data)
{
    if (!s) return ODV_ERROR_INVALID_ARG;
    s->row_cb = cb;
    s->row_cb_ex = NULL;
    s->row_ud = user_data;
    return ODV_OK;
}

/* Values with their lengths and NULL as -1 (M-22). Replaces odv_set_row_callback. */
ODV_API int ODV_CALL odv_set_row_callback_ex(ODV_SESSION *s, ODV_ROW_CALLBACK_EX cb, void *user_data)
{
    if (!s) return ODV_ERROR_INVALID_ARG;
    s->row_cb_ex = cb;
    s->row_cb = NULL;
    s->row_ud = user_data;
    return ODV_OK;
}

ODV_API int ODV_CALL odv_set_progress_callback(ODV_SESSION *s, ODV_PROGRESS_CALLBACK cb, void *user_data)
{
    if (!s) return ODV_ERROR_INVALID_ARG;
    s->progress_cb = cb;
    s->progress_ud = user_data;
    return ODV_OK;
}

ODV_API int ODV_CALL odv_set_table_callback(ODV_SESSION *s, ODV_TABLE_CALLBACK cb, void *user_data)
{
    if (!s) return ODV_ERROR_INVALID_ARG;
    s->table_cb = cb;
    s->table_ud = user_data;
    return ODV_OK;
}

ODV_API int ODV_CALL odv_set_table_filter(ODV_SESSION *s, const char *schema, const char *table)
{
    if (!s) return ODV_ERROR_INVALID_ARG;

    if (!table || table[0] == '\0') {
        /* Clear filter */
        s->filter_active = 0;
        s->filter_schema[0] = '\0';
        s->filter_table[0] = '\0';
        s->filter_partition[0] = '\0';
        return ODV_OK;
    }

    /* Store filter names in UTF-8 (will be reverse-converted after charset detection) */
    if (schema && schema[0]) {
        odv_strcpy(s->filter_schema, schema, ODV_OBJNAME_LEN);
    } else {
        s->filter_schema[0] = '\0';
    }
    odv_strcpy(s->filter_table, table, ODV_OBJNAME_LEN);
    s->filter_partition[0] = '\0';
    s->filter_active = 1;
    s->pass_flg = 0;

    return ODV_OK;
}

ODV_API int ODV_CALL odv_set_partition_filter(ODV_SESSION *s, const char *partition)
{
    if (!s) return ODV_ERROR_INVALID_ARG;
    if (partition && partition[0]) {
        odv_strcpy(s->filter_partition, partition, ODV_OBJNAME_LEN);
    } else {
        s->filter_partition[0] = '\0';
    }
    return ODV_OK;
}

ODV_API int ODV_CALL odv_set_data_offset(ODV_SESSION *s, int64_t offset)
{
    if (!s) return ODV_ERROR_INVALID_ARG;
    s->seek_offset = offset;
    return ODV_OK;
}

static int cmp_i64(const void *a, const void *b)
{
    int64_t x = *(const int64_t *)a, y = *(const int64_t *)b;
    return (x > y) - (x < y);
}

/*---------------------------------------------------------------------------
    odv_set_data_offsets

    The whole of a partitioned table: every data_offset of its entries (from
    the table callbacks / odv_get_table_entry), so that a filtered parse reads
    each entry where it is instead of the one entry odv_set_data_offset names.
    EXPDP writes a table's partitions wherever their data fell (ordered by
    size, other tables in between), so neither one offset nor a scan that stops
    at the next table finds all of them.  The offsets are kept ascending and
    distinct (file order: the rows come out as a full parse gives them);
    0 / negative ones are dropped.  The smallest also becomes the single
    seek_offset, which is what an EXP dump needs (its partitions follow the
    one CREATE TABLE they all share).  count <= 0 or NULL clears the list.
 ---------------------------------------------------------------------------*/
ODV_API int ODV_CALL odv_set_data_offsets(ODV_SESSION *s, const int64_t *offsets, int count)
{
    int i, n = 0;
    int64_t *v;

    if (!s) return ODV_ERROR_INVALID_ARG;
    free(s->seek_list);
    s->seek_list = NULL;
    s->seek_list_count = 0;
    if (!offsets || count <= 0) return ODV_OK;

    v = (int64_t *)malloc((size_t)count * sizeof(int64_t));
    if (!v) return ODV_ERROR_MALLOC;
    for (i = 0; i < count; i++)
        if (offsets[i] > 0) v[n++] = offsets[i];
    if (n == 0) { free(v); return ODV_OK; }
    qsort(v, (size_t)n, sizeof(int64_t), cmp_i64);
    for (i = 1, count = 1; i < n; i++)
        if (v[i] != v[count - 1]) v[count++] = v[i];
    s->seek_list = v;
    s->seek_list_count = count;
    s->seek_offset = v[0];
    return ODV_OK;
}

ODV_API int ODV_CALL odv_set_date_format(ODV_SESSION *s, int fmt, const char *custom_fmt)
{
    if (!s) return ODV_ERROR_INVALID_ARG;
    s->date_format = fmt;
    if (fmt == DATE_FMT_CUSTOM && custom_fmt) {
        odv_strcpy(s->custom_date_format, custom_fmt, sizeof(s->custom_date_format) - 1);
    } else {
        s->custom_date_format[0] = '\0';
    }
    return ODV_OK;
}

ODV_API int ODV_CALL odv_set_csv_options(ODV_SESSION *s, int write_header, int write_types)
{
    if (!s) return ODV_ERROR_INVALID_ARG;
    s->csv_write_header = write_header;
    s->csv_write_types = write_types;
    return ODV_OK;
}

ODV_API int ODV_CALL odv_set_sql_flags(ODV_SESSION *s, uint32_t flags)
{
    if (!s) return ODV_ERROR_INVALID_ARG;
    s->sql_flags = flags & ODV_SQL_ALL;  /* mask to known bits */
    return ODV_OK;
}

ODV_API int ODV_CALL odv_set_app_version(ODV_SESSION *s, const char *ver)
{
    if (!s) return ODV_ERROR_INVALID_ARG;
    if (ver) {
        odv_strcpy(s->app_version, ver, sizeof(s->app_version) - 1);
    } else {
        s->app_version[0] = '\0';
    }
    return ODV_OK;
}

/*---------------------------------------------------------------------------
    Virtual multi-file read stream (H-28)

    FILESIZE= (exp) / dumpfile=%U (expdp) split a single logical dump into
    <stem>_01, _02, ... .  The parsers want one continuous byte stream; this
    layer presents the ordered set as that stream.  Discovery is driven from
    the opened file: only when its trailing number is 1 do we look for _02,
    _03, ... in the same directory (opening a middle file never auto-joins).
    Each later file is accepted only after a signature check against the first,
    so an unrelated same-named neighbour is not concatenated.  For a single,
    non-split dump the stream is one segment with skip 0 and behaves byte-for-
    byte like the one FILE.
 ---------------------------------------------------------------------------*/

static int64_t vf_filesize(const char *path)
{
    FILE *fp = odv_fopen_path(path, "rb");
    int64_t sz;
    if (!fp) return -1;
    if (odv_fseek(fp, 0, SEEK_END) != 0) { fclose(fp); return -1; }
    sz = odv_ftell(fp);
    fclose(fp);
    return sz;
}

static int vf_read_head(const char *path, unsigned char *buf, int n)
{
    FILE *fp = odv_fopen_path(path, "rb");
    int got;
    if (!fp) return -1;
    got = (int)fread(buf, 1, (size_t)n, fp);
    fclose(fp);
    return got;
}

/* Offset of the "EXPORT:" signature in an exp header, looked for in the first
   0x20 bytes as odv_detect does; -1 when absent.  A direct-path export puts
   "D\n" in front of it (03 <cs hi> <cs lo> 'D' 0x0A "EXPORT:..."), so the
   header's fields are counted from here, never from a fixed offset. */
static int exp_sig_pos(const unsigned char *hdr, int len)
{
    int i;
    for (i = 0; i + 7 <= len && i + 7 <= 0x20; i++) {
        if (memcmp(hdr + i, "EXPORT:", 7) == 0) return i;
    }
    return -1;
}

/* The 0x0A/0x00-delimited fields of an exp header, counted from the
   "EXPORT:Vnn.nn.nn" signature (field 0): field 3 is the block size, and the
   charset region (record 7) starts just past the 7th delimiter.  The 2-byte
   big-endian volume number sits at cs_start+6 (right after the 6-byte charset
   region) and the FILESIZE (8 bytes big-endian) at cs_start+8.
   *block_size / *cs_start are set to 0 / -1 when not found. */
static void exp_header_fields(const unsigned char *hdr, int len,
                              int *block_size, int *cs_start)
{
    int i, field = 0, fs, bs = 0, cs = -1;
    int sig = exp_sig_pos(hdr, len);
    *block_size = 0;
    *cs_start = -1;
    if (sig < 0) return;
    fs = sig;
    for (i = sig; i < len; i++) {
        if (hdr[i] == 0x0A || hdr[i] == 0x00) {
            if (field == 3) {            /* field 3 text [fs,i) = block size */
                int j, v = 0, any = 0;
                for (j = fs; j < i; j++) {
                    if (hdr[j] < '0' || hdr[j] > '9') { any = 0; break; }
                    v = v * 10 + (hdr[j] - '0');
                    any = 1;
                    if (v > (1 << 24)) break;
                }
                if (any) bs = v;
            }
            field++;
            fs = i + 1;
            if (field == 7) { cs = fs; break; }
        }
    }
    *block_size = bs;
    *cs_start = cs;
}

/* exp_header_fields for a file that starts like the exp dumps the split
   layout was measured on (first byte 0x03; conventional and direct alike). */
static void exp_header_info(const unsigned char *hdr, int len,
                            int *block_size, int *cs_start)
{
    *block_size = 0;
    *cs_start = -1;
    if (len < 4 || hdr[0] != 0x03) return;
    exp_header_fields(hdr, len, block_size, cs_start);
}

/* 1 if `cand_head` looks like volume `seq` of the same split set as
   `f0_head`.  exp: EXP signature at the same place as in the first file (both
   direct or both conventional), the same block size (each later volume is cut
   at it) and volume number == seq.  expdp: same leading DataPump block type
   and job-id region [6,34) as the first file. */
static int vf_candidate_ok(const unsigned char *f0_head, int f0_len,
                           const unsigned char *cand_head, int cand_len,
                           int dump_type, int seq)
{
    if (dump_type == DUMP_EXP || dump_type == DUMP_EXP_DIRECT) {
        int bs, cs, f0_bs, f0_cs;
        if (cand_len < 64) return 0;
        if (cand_head[0] < 0x01 || cand_head[0] > 0x05) return 0;
        if (exp_sig_pos(cand_head, cand_len) < 0 ||
            exp_sig_pos(cand_head, cand_len) != exp_sig_pos(f0_head, f0_len))
            return 0;
        exp_header_info(cand_head, cand_len, &bs, &cs);
        exp_header_info(f0_head, f0_len, &f0_bs, &f0_cs);
        if (cs < 0 || cs + 8 > cand_len || bs != f0_bs) return 0;
        {
            int vol = ((int)cand_head[cs + 6] << 8) | (int)cand_head[cs + 7];
            return (vol == seq);
        }
    } else {
        /* expdp: DataPump file-header block.  First two bytes are the block
           type, bytes [6,34) are the per-job id shared by every volume. */
        if (f0_len < 34 || cand_len < 34) return 0;
        if (cand_head[0] != f0_head[0] || cand_head[1] != f0_head[1]) return 0;
        return (memcmp(cand_head + 6, f0_head + 6, 28) == 0);
    }
}

static int vf_add_seg(ODV_VFILE *vf, const char *path, int64_t skip, int64_t size)
{
    ODV_VSEG *sg;
    int64_t avail = size - skip;
    if (avail <= 0) return 0;            /* nothing to contribute */
    if (vf->nfiles >= vf->cap) {
        int ncap = vf->cap ? vf->cap * 2 : 4;
        ODV_VSEG *ns = (ODV_VSEG *)realloc(vf->seg, (size_t)ncap * sizeof(ODV_VSEG));
        if (!ns) return ODV_ERROR_MALLOC;
        vf->seg = ns;
        vf->cap = ncap;
    }
    sg = &vf->seg[vf->nfiles];
    sg->path = (char *)malloc(strlen(path) + 1);
    if (!sg->path) return ODV_ERROR_MALLOC;
    strcpy(sg->path, path);
    sg->skip = skip;
    sg->avail = avail;
    sg->logstart = vf->logsize;
    vf->logsize += avail;
    vf->nfiles++;
    return ODV_OK;
}

/*---------------------------------------------------------------------------
    Where a dump ends (M-21)

    A dump whose end is missing (a copy or transfer cut short, a full disk,
    an export that failed) used to read as a smaller, complete dump: the
    parsers stop at end of file without a word, so the tables after the cut
    vanished and the table being read lost rows, with rc 0 and no message.
    Rather than teach every read in the parsers to tell "end of file" from
    "end of dump", the end is checked once against what the dump itself says,
    in the last file of the (possibly split) set:

    exp    The export closes with its EXIT records ("EXIT\n" twice), then
           zeros up to the block size. All 417 complete exp dumps at hand end
           so (11.2 / 21c / 23ai, conventional and direct). A volume of a
           FILESIZE= set that is not the last one ends in row data instead; its
           header carries the FILESIZE (8 bytes BE, after the volume number,
           at cs+8) and the volume is exactly that long.
    expdp  The file header block gives the master table's first block
           (BE32 at 0x36, 1-based) and its length (BE64 at 0x3a); the file
           holds it whole: (start - 1) * block size + length <= file size.
           From 23ai (0x40 in the flags byte 0x32) the header leaves these 0
           and the last block of the file is a trailer, a copy of the header
           (same 2-byte type and job id [6,34), flag 0x80) that holds them.
           Only the file(s) at the end of a dumpfile=%U set carry the master;
           a volume without it, or one whose "a:b:<pieces>:<piece>" field at
           0x395 (21c and later) has more pieces to come, goes on in the next
           volume.

    odv_vopen records the verdict in vf->tail. odv_vread reports it to the
    session only when a read actually reaches that early end, so an
    operation that finished before it (a table opened through its data
    offset, ahead of the cut) is not flagged, and a dump that is damaged in
    the middle but ends properly is never called truncated.
 ---------------------------------------------------------------------------*/
#define VF_TAIL_CHUNK    65536
#define VF_TAIL_MAXZERO  (16 * 1024 * 1024)   /* exp pads less than a block */

static int vf_pread(FILE *fp, int64_t off, unsigned char *buf, int n)
{
    if (off < 0 || odv_fseek(fp, off, SEEK_SET) != 0) return -1;
    return (int)fread(buf, 1, (size_t)n, fp);
}

/* Big-endian unsigned field (header bytes are untrusted: no signed shifts). */
static uint64_t vf_be(const unsigned char *p, int n)
{
    uint64_t v = 0;
    int i;
    for (i = 0; i < n; i++) v = (v << 8) | p[i];
    return v;
}

static int exp_tail_state(const char *path, int64_t size)
{
    unsigned char hdr[256], tail[5];
    unsigned char *buf;
    FILE *fp;
    int hn, i, bs = 0, cs = -1, state = ODV_TAIL_COMPLETE;
    uint64_t filesize = 0;
    int64_t end = size, last = -1;

    fp = odv_fopen_path(path, "rb");
    if (!fp) return ODV_TAIL_COMPLETE;            /* cannot tell: say nothing */
    hn = vf_pread(fp, 0, hdr, (int)sizeof(hdr));
    if (hn > 0)
        exp_header_fields(hdr, hn, &bs, &cs);     /* fields from "EXPORT:" */
    if (cs >= 0 && cs + 16 <= hn)
        filesize = vf_be(hdr + cs + 8, 8);

    /* The last non-zero byte of the file */
    buf = (unsigned char *)malloc(VF_TAIL_CHUNK);
    if (!buf) { fclose(fp); return ODV_TAIL_COMPLETE; }
    while (end > 0 && size - end < VF_TAIL_MAXZERO) {
        int64_t st = (end > VF_TAIL_CHUNK) ? end - VF_TAIL_CHUNK : 0;
        int n = vf_pread(fp, st, buf, (int)(end - st));
        if (n != (int)(end - st)) { last = -2; break; }
        for (i = n - 1; i >= 0 && buf[i] == 0; i--)
            ;
        if (i >= 0) { last = st + i; break; }
        end = st;
    }
    free(buf);

    if (last == -2) {
        state = ODV_TAIL_COMPLETE;                 /* read error: say nothing */
    } else if (last >= 4 && vf_pread(fp, last - 4, tail, 5) == 5 &&
               memcmp(tail, "EXIT\n", 5) == 0) {
        state = ODV_TAIL_COMPLETE;
    } else if (filesize > 0 && (uint64_t)size >= filesize) {
        state = ODV_TAIL_VOLMISS;                  /* a full FILESIZE volume */
    } else {
        state = ODV_TAIL_CUT;
    }
    fclose(fp);
    return state;
}

/* "a:b:<pieces>:<piece>" at 0x395 of a DataPump header (21c and later):
   1 when the master table goes on in a later volume. */
static int expdp_master_continues(const unsigned char *b)
{
    int64_t v[4] = { 0, 0, 0, 0 };
    int i = 0x395, k = 0, digits = 0;

    for (; i < 0x3B0 && k < 4; i++) {
        if (b[i] >= '0' && b[i] <= '9') {
            if (v[k] < 1000000000) v[k] = v[k] * 10 + (b[i] - '0');
            digits++;
        } else if (b[i] == ':' && digits > 0) {
            k++;
            digits = 0;
        } else {
            break;
        }
    }
    if (k == 3 && digits > 0) k = 4;
    return (k == 4 && v[3] > 0 && v[2] > v[3]);
}

static int expdp_tail_state(const char *path, int64_t size)
{
    unsigned char h[ODV_DUMP_BLOCK_LEN], t[ODV_DUMP_BLOCK_LEN];
    const unsigned char *m = h;
    FILE *fp;
    uint64_t bs, mstart, mlen;
    int state;

    fp = odv_fopen_path(path, "rb");
    if (!fp) return ODV_TAIL_COMPLETE;            /* cannot tell: say nothing */
    if (vf_pread(fp, 0, h, (int)sizeof(h)) != (int)sizeof(h)) {
        fclose(fp);
        return ODV_TAIL_CUT;                       /* not even a header block */
    }
    bs = vf_be(h + 0x23, 4);
    if (bs < 512 || bs > (1u << 20) || (bs & (bs - 1)) != 0)
        bs = ODV_DUMP_BLOCK_LEN;
    mstart = vf_be(h + 0x36, 4);
    mlen = vf_be(h + 0x3a, 8);

    if (mstart == 0 && (h[0x32] & 0x40)) {
        /* 23ai and later: the master's place is in the trailer block. */
        if (size < (int64_t)(2 * bs) ||
            vf_pread(fp, size - (int64_t)bs, t, (int)sizeof(t)) != (int)sizeof(t) ||
            t[0] != h[0] || t[1] != h[1] || memcmp(t + 6, h + 6, 28) != 0 ||
            !(t[0x32] & 0x80)) {
            fclose(fp);
            return ODV_TAIL_CUT;                   /* the trailer is gone */
        }
        m = t;
        mstart = vf_be(t + 0x36, 4);
        mlen = vf_be(t + 0x3a, 8);
    }
    fclose(fp);

    if (mstart > 0 && mlen > 0) {
        /* mstart < 2^32 and bs <= 2^20, so the sum cannot wrap once
           mlen <= size has been checked. */
        if (mlen > (uint64_t)size || (mstart - 1) * bs + mlen > (uint64_t)size)
            state = ODV_TAIL_CUT;                  /* master table cut off */
        else if (expdp_master_continues(m))
            state = ODV_TAIL_VOLMISS;
        else
            state = ODV_TAIL_COMPLETE;
    } else {
        state = ODV_TAIL_VOLMISS;                  /* master is in a later file */
    }
    return state;
}

static int vf_tail_state(const ODV_VFILE *vf, const ODV_SESSION *s)
{
    const ODV_VSEG *sg;
    if (vf->nfiles <= 0) return ODV_TAIL_COMPLETE;
    sg = &vf->seg[vf->nfiles - 1];
    if (s->dump_type == DUMP_EXP || s->dump_type == DUMP_EXP_DIRECT)
        return exp_tail_state(sg->path, sg->skip + sg->avail);
    if (s->dump_type == DUMP_EXPDP)
        return expdp_tail_state(sg->path, sg->skip + sg->avail);
    return ODV_TAIL_COMPLETE;
}

/* Where the master table's definition block is in one expdp file, by the
   same header / 23ai-trailer fields as expdp_tail_state: the byte offset of
   the block whose +2 holds its "<?xml" (block number mstart, measured on
   11.2 / 21c / 23ai), or -1 when the file does not say or that block is not
   a table definition (a volume without the master). */
int64_t odv_expdp_master_offset(const char *path)
{
    unsigned char h[ODV_DUMP_BLOCK_LEN], t[ODV_DUMP_BLOCK_LEN];
    FILE *fp;
    int64_t size, at = -1;
    uint64_t bs, mstart;

    size = vf_filesize(path);
    if (size < 2 * ODV_DUMP_BLOCK_LEN) return -1;
    fp = odv_fopen_path(path, "rb");
    if (!fp) return -1;
    if (vf_pread(fp, 0, h, (int)sizeof(h)) == (int)sizeof(h)) {
        bs = vf_be(h + 0x23, 4);
        if (bs < 512 || bs > (1u << 20) || (bs & (bs - 1)) != 0)
            bs = ODV_DUMP_BLOCK_LEN;
        mstart = vf_be(h + 0x36, 4);
        if (mstart == 0 && (h[0x32] & 0x40) && size >= (int64_t)(2 * bs) &&
            vf_pread(fp, size - (int64_t)bs, t, (int)sizeof(t)) == (int)sizeof(t) &&
            t[0] == h[0] && t[1] == h[1] && memcmp(t + 6, h + 6, 28) == 0 &&
            (t[0x32] & 0x80))
            mstart = vf_be(t + 0x36, 4);       /* 23ai: from the trailer */
        if (mstart > 0 && mstart * bs + 8 <= (uint64_t)size) {
            unsigned char x[5];
            if (vf_pread(fp, (int64_t)(mstart * bs) + 2, x, 5) == 5 &&
                memcmp(x, "<?xml", 5) == 0)
                at = (int64_t)(mstart * bs);
        }
    }
    fclose(fp);
    return at;
}

int odv_expdp_cut_short(ODV_SESSION *s)
{
    int64_t size;
    if (!s || s->dump_path[0] == '\0') return 0;
    size = vf_filesize(s->dump_path);
    if (size < 0) return 0;
    return expdp_tail_state(s->dump_path, size) == ODV_TAIL_CUT;
}

int odv_vopen(ODV_VFILE *vf, ODV_SESSION *s)
{
    unsigned char f0_head[256];
    int f0_len, block_size = 0, cs = -1, split_skip = 0;
    int64_t size0;
    int num_start, num_end, width, ext_pos, plen, k;
    char cand[ODV_PATH_LEN * 2 + 1];

    if (!vf || !s) return ODV_ERROR_INVALID_ARG;
    memset(vf, 0, sizeof(*vf));
    vf->open_idx = -1;
    vf->phys_pos = -1;

    size0 = vf_filesize(s->dump_path);
    if (size0 < 0) {
        set_error(s, "Cannot open dump file");
        return ODV_ERROR_FOPEN;
    }

    /* First file is always the whole file (skip 0). */
    {
        int rc = vf_add_seg(vf, s->dump_path, 0, size0);
        if (rc != ODV_OK) return rc;
    }

    /* Per-format leading-header size to drop from each later file. */
    f0_len = vf_read_head(s->dump_path, f0_head, (int)sizeof(f0_head));
    if (f0_len < 0) f0_len = 0;
    if (s->dump_type == DUMP_EXP || s->dump_type == DUMP_EXP_DIRECT) {
        exp_header_info(f0_head, f0_len, &block_size, &cs);
        if (block_size >= 512 && block_size <= (1 << 20))
            split_skip = block_size;       /* data starts at the block boundary */
    } else if (s->dump_type == DUMP_EXPDP) {
        split_skip = ODV_DUMP_BLOCK_LEN;    /* one 4KB file-header block */
    }

    /* Locate the trailing decimal run just before the extension. */
    plen = (int)strlen(s->dump_path);
    ext_pos = plen;
    for (k = plen - 1; k >= 0; k--) {
        if (s->dump_path[k] == '.') { ext_pos = (int)k; break; }
        if (s->dump_path[k] == '/' || s->dump_path[k] == '\\') break;
    }
    num_end = ext_pos;
    num_start = ext_pos;
    while (num_start > 0 &&
           s->dump_path[num_start - 1] >= '0' && s->dump_path[num_start - 1] <= '9')
        num_start--;

    /* Auto-join only when a positive per-file skip was determined, the name
       ends in digits, and that number is 1 (the first volume). */
    if (split_skip > 0 && num_end > num_start) {
        int64_t v = 0;
        int j;
        for (j = num_start; j < num_end; j++) v = v * 10 + (s->dump_path[j] - '0');
        width = num_end - num_start;
        if (v == 1) {
            for (k = 2; k < ODV_MAX_SPLIT; k++) {
                unsigned char ch[256];
                int cl;
                int64_t csz;
                /* Build <prefix><k zero-padded to width><suffix>. */
                int pre = num_start;
                int n = snprintf(cand, sizeof(cand), "%.*s%0*lld%s",
                                 pre, s->dump_path, width, (long long)k,
                                 s->dump_path + num_end);
                if (n < 0 || n >= (int)sizeof(cand)) break;
                csz = vf_filesize(cand);
                if (csz < 0) break;                 /* no more files */
                cl = vf_read_head(cand, ch, (int)sizeof(ch));
                if (cl < 0) break;
                if (!vf_candidate_ok(f0_head, f0_len, ch, cl,
                                     s->dump_type, (int)k))
                    break;                          /* not a continuation */
                if (vf_add_seg(vf, cand, split_skip, csz) != ODV_OK)
                    break;
            }
        }
    }

    /* Progress percentage is against the whole logical stream. */
    s->dump_size = vf->logsize;

    /* How the stream ends (M-21); reported when a read gets there. */
    vf->owner = s;
    vf->tail = vf_tail_state(vf, s);
    return ODV_OK;
}

static FILE *vf_ensure_open(ODV_VFILE *vf, int idx)
{
    if (vf->open_idx == idx && vf->fp) return vf->fp;
    if (vf->fp) { fclose(vf->fp); vf->fp = NULL; }
    vf->fp = odv_fopen_path(vf->seg[idx].path, "rb");
    vf->open_idx = vf->fp ? idx : -1;
    vf->phys_pos = -1;
    if (vf->fp) setvbuf(vf->fp, NULL, _IOFBF, 65536);
    return vf->fp;
}

static int vf_find_seg(ODV_VFILE *vf, int64_t logpos)
{
    int i;
    /* Common case: still inside the currently open segment. */
    if (vf->open_idx >= 0) {
        ODV_VSEG *sg = &vf->seg[vf->open_idx];
        if (logpos >= sg->logstart && logpos < sg->logstart + sg->avail)
            return vf->open_idx;
    }
    for (i = 0; i < vf->nfiles; i++) {
        ODV_VSEG *sg = &vf->seg[i];
        if (logpos >= sg->logstart && logpos < sg->logstart + sg->avail)
            return i;
    }
    return -1;
}

size_t odv_vread(void *ptr, size_t size, size_t nmemb, ODV_VFILE *vf)
{
    int64_t want, done = 0;
    unsigned char *out = (unsigned char *)ptr;

    if (!vf || !ptr || size == 0) return 0;
    want = (int64_t)size * (int64_t)nmemb;

    while (done < want) {
        int i;
        ODV_VSEG *sg;
        int64_t seg_off, seg_rem, chunk, pofs;
        FILE *fp;
        size_t got;

        if (vf->logpos >= vf->logsize) break;          /* logical EOF */
        i = vf_find_seg(vf, vf->logpos);
        if (i < 0) break;
        sg = &vf->seg[i];
        seg_off = vf->logpos - sg->logstart;
        seg_rem = sg->avail - seg_off;
        chunk = want - done;
        if (chunk > seg_rem) chunk = seg_rem;

        fp = vf_ensure_open(vf, i);
        if (!fp) break;
        pofs = sg->skip + seg_off;
        if (vf->phys_pos != pofs) {
            if (odv_fseek(fp, pofs, SEEK_SET) != 0) break;
            vf->phys_pos = pofs;
        }
        got = fread(out + done, 1, (size_t)chunk, fp);
        vf->phys_pos += (int64_t)got;
        vf->logpos   += (int64_t)got;
        done         += (int64_t)got;
        if (got < (size_t)chunk) break;                 /* short read / error */
    }
    /* The reader wanted bytes past the end of a dump that ends early: what
       it is reading (and all that follows) is cut off (M-21). */
    if (done < want && vf->logpos >= vf->logsize &&
        vf->tail != ODV_TAIL_COMPLETE && vf->owner)
        vf->owner->stream_cut = vf->tail;
    return (size_t)(done / (int64_t)size);
}

int odv_vseek(ODV_VFILE *vf, int64_t offset, int whence)
{
    int64_t np;
    if (!vf) return -1;
    switch (whence) {
    case SEEK_SET: np = offset; break;
    case SEEK_CUR: np = vf->logpos + offset; break;
    case SEEK_END: np = vf->logsize + offset; break;
    default: return -1;
    }
    if (np < 0) return -1;
    vf->logpos = np;        /* physical reposition is deferred to the next read */
    return 0;
}

int64_t odv_vtell(ODV_VFILE *vf)
{
    return vf ? vf->logpos : -1;
}

void odv_vclose(ODV_VFILE *vf)
{
    int i;
    if (!vf) return;
    if (vf->fp) { fclose(vf->fp); vf->fp = NULL; }
    for (i = 0; i < vf->nfiles; i++)
        free(vf->seg[i].path);
    free(vf->seg);
    vf->seg = NULL;
    vf->nfiles = 0;
    vf->cap = 0;
    vf->open_idx = -1;
}

/*---------------------------------------------------------------------------
    Operations
 ---------------------------------------------------------------------------*/

ODV_API int ODV_CALL odv_check_dump_kind(ODV_SESSION *s, int *dump_type)
{
    int rc;

    if (!s || !dump_type) return ODV_ERROR_INVALID_ARG;
    if (s->dump_path[0] == '\0') {
        set_error(s, "Dump file path not set");
        return ODV_ERROR_INVALID_ARG;
    }

    rc = detect_dump_kind(s);
    if (rc == ODV_OK) {
        *dump_type = s->dump_type;
    }
    return rc;
}

ODV_API int ODV_CALL odv_list_tables(ODV_SESSION *s)
{
    int rc;

    if (!s) return ODV_ERROR_INVALID_ARG;

    /* Auto-detect dump kind if not done */
    if (s->dump_type == DUMP_UNKNOWN) {
        rc = detect_dump_kind(s);
        if (rc != ODV_OK) return rc;
    }

    s->cancelled = 0;
    s->table_count = 0;
    s->partition_count = 0;
    odv_ddl_hints_clear(s);
    op_begin(s);

    switch (s->dump_type) {
    case DUMP_EXPDP:
        return op_finish(s, parse_expdp_dump(s, 1 /* list_only */));
    case DUMP_EXPDP_COMPRESS:
        set_error(s, "Compressed EXPDP dumps (COMPRESSION=ALL) are not supported. "
                     "Please re-export with COMPRESSION=NONE.");
        return ODV_ERROR_UNSUPPORTED;
    case DUMP_EXP:
    case DUMP_EXP_DIRECT:
        return op_finish(s, parse_exp_dump(s, 1 /* list_only */));
    default:
        set_error(s, "Unknown or unsupported dump format");
        return ODV_ERROR_FORMAT;
    }
}

ODV_API int ODV_CALL odv_get_partition_count(ODV_SESSION *s)
{
    if (!s) return 0;
    return s->partition_count;
}

ODV_API int ODV_CALL odv_get_table_entry(ODV_SESSION *s, int index,
    const char **schema, const char **name, const char **partition,
    const char **parent_partition, int *type, int64_t *row_count)
{
    if (!s || index < 0 || index >= s->table_count) return ODV_ERROR;

    ODV_TABLE_ENTRY *e = &s->table_list[index];
    if (schema) *schema = e->schema;
    if (name) *name = e->name;
    if (partition) *partition = e->partition;
    if (parent_partition) *parent_partition = e->parent_partition;
    if (type) *type = e->type;
    if (row_count) *row_count = e->row_count;

    return ODV_OK;
}

/* Build constraints JSON for a table_list entry (EXPDP metadata).
 * Returns a buffer of the session, overwritten by the next call on it. It
 * used to be one static buffer for every session, so a call in another
 * thread could overwrite the result being read (M-25). */
ODV_API const char * ODV_CALL odv_get_table_constraints_json(ODV_SESSION *s, int index)
{
    static const char empty_json[] = "[]";
    char *json_buf;
    int pos = 0, i;

    if (!s || index < 0 || index >= s->table_count)
        return empty_json;

    ODV_TABLE_ENTRY *e = &s->table_list[index];
    if (e->meta_constraint_count == 0)
        return empty_json;
    json_buf = s->constraints_json;

    /* Reserve one byte for the closing ']' so the loop can never consume the
       space it needs. snprintf() returns the length it *would* have written,
       so `pos` is only advanced when the write actually fit — advancing it by
       an unclamped return value would push `pos` past the buffer and make the
       next call's `sizeof(json_buf) - pos` wrap to a huge size_t. Constraint
       names come from the dump file, so this is reachable input, not theory. */
    json_buf[pos++] = '[';
    for (i = 0; i < e->meta_constraint_count; i++) {
        ODV_CONSTRAINT_NAME *mc = &e->meta_constraints[i];
        char esc_name[ODV_OBJNAME_LEN * 2 + 1];
        int ei = 0, n, remain;
        const char *cp = mc->name;
        while (*cp && ei < (int)sizeof(esc_name) - 2) {
            if (*cp == '"' || *cp == '\\') esc_name[ei++] = '\\';
            esc_name[ei++] = *cp++;
        }
        esc_name[ei] = '\0';

        remain = (int)sizeof(s->constraints_json) - pos - 2;  /* keep room for "]\0" */
        n = snprintf(json_buf + pos, (size_t)(remain + 1),
            "%s{\"type\":%d,\"name\":\"%s\",\"columns\":[]}",
            (i > 0) ? "," : "", mc->type, esc_name);
        if (n < 0 || n > remain) {
            /* Would not fit: drop this and the remaining entries rather than
               emit a half-written object that breaks the JSON. */
            json_buf[pos] = '\0';
            break;
        }
        pos += n;
    }
    json_buf[pos++] = ']';
    json_buf[pos] = '\0';
    return json_buf;
}

ODV_API int ODV_CALL odv_parse_dump(ODV_SESSION *s)
{
    int rc;

    if (!s) return ODV_ERROR_INVALID_ARG;

    /* Auto-detect dump kind if not done */
    if (s->dump_type == DUMP_UNKNOWN) {
        rc = detect_dump_kind(s);
        if (rc != ODV_OK) return rc;
    }

    s->cancelled = 0;
    s->total_rows = 0;
    op_begin(s);

    /* ODV_VALUES_EXPORT: the values as the export writers get them (whole
       LOB / LONG; a marker told apart by its length, see odv_api.h) */
    s->full_values = (s->value_mode == ODV_VALUES_EXPORT) ? 1 : 0;
    s->export_lens = s->full_values;
    switch (s->dump_type) {
    case DUMP_EXPDP:
        rc = parse_expdp_dump(s, 0 /* full parse */);
        break;
    case DUMP_EXPDP_COMPRESS:
        set_error(s, "Compressed EXPDP dumps (COMPRESSION=ALL) are not supported. "
                     "Please re-export with COMPRESSION=NONE.");
        rc = ODV_ERROR_UNSUPPORTED;
        break;
    case DUMP_EXP:
    case DUMP_EXP_DIRECT:
        rc = parse_exp_dump(s, 0 /* full parse */);
        break;
    default:
        set_error(s, "Unknown or unsupported dump format");
        rc = ODV_ERROR_FORMAT;
        break;
    }
    s->full_values = 0;
    s->export_lens = 0;
    return op_finish(s, rc);    /* passes an error through unchanged */
}

ODV_API int ODV_CALL odv_set_value_mode(ODV_SESSION *s, int mode)
{
    if (!s) return ODV_ERROR_INVALID_ARG;
    if (mode != ODV_VALUES_PREVIEW && mode != ODV_VALUES_EXPORT) return ODV_ERROR_INVALID_ARG;
    s->value_mode = mode;
    return ODV_OK;
}

/*---------------------------------------------------------------------------
    DDL hints (odv_get_table_ddl_hint / odv_set_ddl_hint)

    A list records, for each entry, where the DDL that the dump keeps apart
    from the table is: exp writes foreign keys and function-based / DESC
    indexes after the last table, expdp keeps constraints, DEFAULTs, comments
    and indexes in the master table. A single-table export given the hint
    reads only there instead of the rest of the dump / the whole master
    table. The hints are kept per distinct string; the entries of one table
    share one.
 ---------------------------------------------------------------------------*/
void odv_ddl_hints_clear(ODV_SESSION *s)
{
    int i;
    if (!s) return;
    for (i = 0; i < s->ddl_hint_nstr; i++) free(s->ddl_hint_str[i]);
    free(s->ddl_hint_str);
    free(s->ddl_hint_of);
    s->ddl_hint_str = NULL;
    s->ddl_hint_of = NULL;
    s->ddl_hint_nstr = 0;
    s->ddl_hint_nof = 0;
}

int odv_ddl_hint_add(ODV_SESSION *s, const char *hint)
{
    char **np, *c;
    size_t n;
    if (!s || !hint) return -1;
    n = strlen(hint);
    c = (char *)malloc(n + 1);
    if (!c) return -1;
    memcpy(c, hint, n + 1);
    np = (char **)realloc(s->ddl_hint_str, (size_t)(s->ddl_hint_nstr + 1) * sizeof(char *));
    if (!np) { free(c); return -1; }
    s->ddl_hint_str = np;
    s->ddl_hint_str[s->ddl_hint_nstr] = c;
    return s->ddl_hint_nstr++;
}

void odv_ddl_hint_set_entry(ODV_SESSION *s, int entry, int idx)
{
    if (!s || entry < 0) return;
    if (entry >= s->ddl_hint_nof) {
        int nc = s->ddl_hint_nof ? s->ddl_hint_nof : 64, i;
        int *np;
        while (nc <= entry) nc *= 2;
        np = (int *)realloc(s->ddl_hint_of, (size_t)nc * sizeof(int));
        if (!np) return;
        for (i = s->ddl_hint_nof; i < nc; i++) np[i] = -1;
        s->ddl_hint_of = np;
        s->ddl_hint_nof = nc;
    }
    s->ddl_hint_of[entry] = idx;
}

ODV_API const char * ODV_CALL odv_get_table_ddl_hint(ODV_SESSION *s, int index)
{
    int k;
    if (!s || index < 0 || index >= s->table_count || index >= s->ddl_hint_nof) return "";
    k = s->ddl_hint_of[index];
    if (k < 0 || k >= s->ddl_hint_nstr) return "";
    return s->ddl_hint_str[k];
}

ODV_API int ODV_CALL odv_set_ddl_hint(ODV_SESSION *s, const char *hint)
{
    char *c = NULL;
    if (!s) return ODV_ERROR_INVALID_ARG;
    if (hint && hint[0]) {
        size_t n = strlen(hint);
        if (n > 1024 * 1024) return ODV_ERROR_INVALID_ARG;
        c = (char *)malloc(n + 1);
        if (!c) return ODV_ERROR_MALLOC;
        memcpy(c, hint, n + 1);
    }
    free(s->ddl_hint_in);
    s->ddl_hint_in = c;
    return ODV_OK;
}

/*---------------------------------------------------------------------------
    odv_read_table_ddl

    The table callback for the filtered table, with its DDL as the SQL export
    takes it: for an expdp dump the constraints, NOT NULL, DEFAULTs, comments
    and indexes of the master table (the list does not read them; it gives
    their position, odv_get_table_ddl_hint). No rows are read.
 ---------------------------------------------------------------------------*/
ODV_API int ODV_CALL odv_read_table_ddl(ODV_SESSION *s)
{
    int rc;

    if (!s) return ODV_ERROR_INVALID_ARG;
    if (!s->filter_active || !s->filter_table[0]) {
        set_error(s, "odv_read_table_ddl needs a table filter (odv_set_table_filter)");
        return ODV_ERROR_INVALID_ARG;
    }
    if (s->dump_type == DUMP_UNKNOWN) {
        rc = detect_dump_kind(s);
        if (rc != ODV_OK) return rc;
    }
    if (s->dump_type != DUMP_EXPDP) {
        set_error(s, "odv_read_table_ddl reads expdp dumps; the list of an exp dump "
                     "already gives the DDL (table callback)");
        return ODV_ERROR_UNSUPPORTED;
    }

    s->cancelled = 0;
    s->total_rows = 0;
    op_begin(s);
    s->want_ddl_meta = 1;
    s->ddl_only = 1;
    rc = parse_expdp_dump(s, 0);
    s->ddl_only = 0;
    s->want_ddl_meta = 0;
    return op_finish(s, rc);
}

ODV_API void ODV_CALL odv_set_csv_delimiter(ODV_SESSION *s, char delimiter)
{
    if (s) s->csv_delimiter = delimiter;
}

ODV_API int ODV_CALL odv_export_csv(ODV_SESSION *s, const char *table_name, const char *output_path)
{
    if (!s || !output_path) return ODV_ERROR_INVALID_ARG;
    op_begin(s);
    return op_finish(s, write_csv_file(s, table_name, output_path));
}

ODV_API int ODV_CALL odv_export_sql(ODV_SESSION *s, const char *table_name, const char *output_path, int dbms_type)
{
    if (!s || !output_path) return ODV_ERROR_INVALID_ARG;
    op_begin(s);
    return op_finish(s, write_sql_file(s, table_name, output_path, dbms_type));
}

/*---------------------------------------------------------------------------
    LOB Extraction Helpers
 ---------------------------------------------------------------------------*/

/* Check if the target LOB column exists in the current table.
   Sets s->lob_column_index to the LOB-only index (0-based). */
int odv_lob_check_column(ODV_SESSION *s)
{
    int i, lob_idx = 0;
    s->lob_column_index = -1;

    if (!s->lob_extract_mode || s->lob_column[0] == '\0')
        return ODV_OK;

    for (i = 0; i < s->table.col_count; i++) {
        int t = s->table.columns[i].type;
        if (t == COL_BLOB || t == COL_CLOB || t == COL_NCLOB) {
            if (odv_stricmp(s->table.columns[i].name, s->lob_column) == 0) {
                s->lob_column_index = lob_idx;
                odv_lob_reset_buffer(s);
                return ODV_OK;
            }
            lob_idx++;
        }
    }

    /* The parsers skip the table on this error and carry on (and the exp
       parser does not pass it up), so it is also kept here for
       odv_extract_lob to report: rc 0 with 0 files hid it (M-27). */
    s->lob_column_missing = 1;
    set_error(s, "LOB column not found in table");
    return ODV_ERROR;
}

/* Accumulate LOB chunk data into the session buffer.
   Only accumulates if lob_col_idx matches the target column. */
int odv_lob_accumulate(ODV_SESSION *s, int lob_col_idx, const unsigned char *data, int len)
{
    if (!s->lob_extract_mode || s->lob_column_index < 0)
        return ODV_OK;
    if (lob_col_idx != s->lob_column_index)
        return ODV_OK;
    if (!data || len <= 0)
        return ODV_OK;

    /* Ensure buffer has space (guard against integer overflow) */
    if (s->state.lob_buf_len > INT_MAX - len - 0x1000)
        return ODV_ERROR_MALLOC;
    if (s->state.lob_buf_len + len > s->state.lob_buf_alloc) {
        int new_alloc = s->state.lob_buf_len + len + 0x1000; /* +4KB headroom */
        unsigned char *new_buf = (unsigned char *)realloc(s->state.lob_buf, new_alloc);
        if (!new_buf) return ODV_ERROR_MALLOC;
        s->state.lob_buf = new_buf;
        s->state.lob_buf_alloc = new_alloc;
    }

    memcpy(s->state.lob_buf + s->state.lob_buf_len, data, len);
    s->state.lob_buf_len += len;
    return ODV_OK;
}

/* True if the given base name (portion before the first '.') is a Windows
   reserved device name (CON, PRN, AUX, NUL, COM1-9, LPT1-9), case-insensitive. */
static int odv_is_reserved_devname(const char *name)
{
    static const char *dev3[] = { "CON", "PRN", "AUX", "NUL" };
    size_t i;
    for (i = 0; i < sizeof(dev3) / sizeof(dev3[0]); i++)
        if (odv_stricmp(name, dev3[i]) == 0) return 1;
    if ((odv_strnicmp(name, "COM", 3) == 0 || odv_strnicmp(name, "LPT", 3) == 0) &&
        name[3] >= '1' && name[3] <= '9' && name[4] == '\0')
        return 1;
    return 0;
}

/* Sanitize an untrusted value (e.g. a column value from the dump) into a safe
   single path component usable as a filename (H-07, path-traversal defense).

   Replaces path separators ('/' '\\'), the drive/stream separator (':') and
   Windows-reserved characters ('<' '>' '"' '|' '?' '*') with '_', drops NUL and
   other control characters, trims leading/trailing spaces and trailing dots,
   rejects names made only of dots ("." / ".."), and prefixes reserved device
   names with '_'. Bytes >= 0x80 (UTF-8 continuation/lead bytes) are kept so
   non-ASCII filenames survive; none of the dangerous path characters are
   representable as UTF-8 continuation bytes, so this cannot reintroduce a
   separator. Returns the length written (0 => no usable name; *out is "").  */
static int odv_sanitize_filename(const unsigned char *in, int in_len,
                                 char *out, int out_sz)
{
    int i, n = 0;
    if (out_sz <= 0) return 0;
    out[0] = '\0';
    if (!in || in_len <= 0) return 0;

    for (i = 0; i < in_len && n < out_sz - 1; i++) {
        unsigned char c = in[i];
        if (c < 0x20 || c == 0x7F) continue;    /* drop control characters */
        switch (c) {
        case '/': case '\\':                     /* path separators */
        case ':':                                /* drive / alternate-stream */
        case '<': case '>': case '"':            /* Windows reserved */
        case '|': case '?': case '*':
            out[n++] = '_';
            break;
        default:
            out[n++] = (char)c;
            break;
        }
    }
    out[n] = '\0';

    while (n > 0 && (out[n - 1] == '.' || out[n - 1] == ' '))
        out[--n] = '\0';                          /* trim trailing dots/spaces */
    {
        int lead = 0;
        while (out[lead] == ' ') lead++;
        if (lead > 0) { memmove(out, out + lead, (size_t)(n - lead + 1)); n -= lead; }
    }
    {
        int alldots = (n > 0);
        for (i = 0; i < n; i++) if (out[i] != '.') { alldots = 0; break; }
        if (alldots) { out[0] = '\0'; return 0; }  /* "." / ".." / ... */
    }
    if (n == 0) return 0;

    {
        char base[16];
        int b = 0;
        for (i = 0; i < n && out[i] != '.' && b < (int)sizeof(base) - 1; i++)
            base[b++] = out[i];
        base[b] = '\0';
        if (odv_is_reserved_devname(base) && n < out_sz - 1) {
            memmove(out + 1, out, (size_t)(n + 1));
            out[0] = '_';
            n++;
        }
    }
    return n;
}

/* Write accumulated LOB buffer to a file and reset the buffer.
   Filename is based on lob_filename_col or sequential number.
   The filename column value is untrusted dump data, so it is sanitized to a
   safe single path component before being joined to the output directory, and
   the result is confirmed to stay inside that directory (H-07). A write that
   fails (unsafe/too-long path, fopen, fwrite) is counted in lob_files_failed
   and reported via odv_get_lob_files_failed(); it does not abort extraction of
   the remaining rows (the parser's error path discards this call's rc). */
int odv_lob_write_file(ODV_SESSION *s)
{
    char path[ODV_PATH_LEN * 2 + 1];
    char fname[256];
    char base[300];
    const char *ext;
    FILE *fpw;

    if (s->lob_column_index < 0)
        return ODV_OK;
    if (!s->state.lob_buf || s->state.lob_buf_len <= 0) {
        odv_lob_reset_buffer(s);
        return ODV_OK; /* NULL LOB — skip */
    }

    /* Output directory (default: current directory). This is set by the
       application, not by dump data, so it is trusted as the containment root. */
    if (s->lob_output_dir[0])
        snprintf(path, sizeof(path), "%s", s->lob_output_dir);
    else
        snprintf(path, sizeof(path), ".");

    /* Determine the file name. The filename-column value is untrusted dump
       data, so it is sanitized to a safe single path component (H-07). */
    fname[0] = '\0';
    if (s->lob_filename_col[0] != '\0') {
        int i;
        for (i = 0; i < s->table.col_count; i++) {
            if (odv_stricmp(s->table.columns[i].name, s->lob_filename_col) == 0) {
                if (i < s->record.col_count && !s->record.values[i].is_null &&
                    s->record.values[i].data_len > 0) {
                    odv_sanitize_filename((const unsigned char *)s->record.values[i].data,
                                          s->record.values[i].data_len,
                                          fname, (int)sizeof(fname));
                }
                break;
            }
        }
    }
    if (fname[0] == '\0') {
        /* No usable name (sequential mode, NULL/empty value, or a value that
           sanitized away entirely): fall back to a sequential number. */
        snprintf(fname, sizeof(fname), "%lld", (long long)(s->lob_files_written + 1));
    }

    /* Extension is sanitized when stored (odv_extract_lob). */
    ext = s->lob_extension[0] ? s->lob_extension : "lob";

    /* Build the basename and confirm it is a single path component: after
       sanitization it carries no separator and is not "."/"..", so joining it
       to the output directory cannot escape that directory. */
    snprintf(base, sizeof(base), "%s.%s", fname, ext);
    if (strchr(base, '/') || strchr(base, '\\') ||
        strcmp(base, ".") == 0 || strcmp(base, "..") == 0) {
        s->lob_files_failed++;
        set_error(s, "Unsafe LOB file name rejected");
        odv_lob_reset_buffer(s);
        return ODV_OK; /* skip this row, keep extracting the rest */
    }

    /* Join: <output dir> + separator + <basename>. */
    {
        int plen = (int)strlen(path);
        int remain = (int)sizeof(path) - plen;
        const char *sep = "";

        if (plen > 0 && path[plen - 1] != '\\' && path[plen - 1] != '/')
#ifdef WINDOWS
            sep = "\\";
#else
            sep = "/";
#endif

        if (remain < (int)(strlen(sep) + strlen(base) + 1)) {
            s->lob_files_failed++;
            set_error(s, "LOB output path too long");
            odv_lob_reset_buffer(s);
            return ODV_OK;
        }
        snprintf(path + plen, remain, "%s%s", sep, base);
    }

    /* Write binary. A failure here (fopen/fwrite) is counted in
       lob_files_failed and surfaced via odv_get_lob_files_failed(); it is not
       made fatal, so the remaining rows of the table are still extracted.
       The path is UTF-8 (output directory from the application, file name
       from the dump), so it is opened through UTF-16 on Windows (M-26). */
    fpw = odv_fopen_path(path, "wb");
    if (!fpw) {
        s->lob_files_failed++;
        set_error(s, "Cannot create LOB output file");
        odv_lob_reset_buffer(s);
        return ODV_OK;
    }
    if ((int)fwrite(s->state.lob_buf, 1, s->state.lob_buf_len, fpw) != s->state.lob_buf_len) {
        fclose(fpw);
        s->lob_files_failed++;
        set_error(s, "LOB file write error");
        odv_lob_reset_buffer(s);
        return ODV_OK;
    }
    fclose(fpw);

    s->lob_files_written++;
    odv_lob_reset_buffer(s);
    return ODV_OK;
}

void odv_lob_reset_buffer(ODV_SESSION *s)
{
    if (s->state.lob_buf) {
        free(s->state.lob_buf);
        s->state.lob_buf = NULL;
    }
    s->state.lob_buf_len = 0;
    s->state.lob_buf_alloc = 0;
}

/*---------------------------------------------------------------------------
    LOB Extraction API
 ---------------------------------------------------------------------------*/

ODV_API int ODV_CALL odv_extract_lob(
    ODV_SESSION *s,
    const char *schema, const char *table,
    const char *lob_column,
    const char *output_dir,
    const char *filename_col,
    const char *extension,
    int64_t data_offset)
{
    int rc;

    if (!s || !table || !lob_column || !output_dir)
        return ODV_ERROR_INVALID_ARG;

    /* A cut directory name would write somewhere else (M-26). */
    if (strlen(output_dir) > ODV_PATH_LEN) {
        set_error(s, "LOB output directory path is too long");
        return ODV_ERROR_INVALID_ARG;
    }

    /* Configure LOB extraction */
    s->lob_extract_mode = 1;
    odv_strcpy(s->lob_column, lob_column, ODV_OBJNAME_LEN);
    odv_strcpy(s->lob_output_dir, output_dir, ODV_PATH_LEN);
    s->lob_column_index = -1;
    s->lob_files_written = 0;
    s->lob_files_failed = 0;
    s->lob_column_missing = 0;

    if (filename_col && filename_col[0])
        odv_strcpy(s->lob_filename_col, filename_col, ODV_OBJNAME_LEN);
    else
        s->lob_filename_col[0] = '\0';

    /* Sanitize the extension (H-07): it is joined into the output path just
       like the filename, so a value such as "..\\x" or "a/b" must not survive. */
    {
        const char *e = (extension && extension[0]) ? extension : "lob";
        char safe_ext[32];
        while (*e == '.') e++;   /* a user-typed ".pdf" means extension "pdf" */
        odv_sanitize_filename((const unsigned char *)e, (int)strlen(e),
                              safe_ext, (int)sizeof(safe_ext));
        odv_strcpy(s->lob_extension, safe_ext[0] ? safe_ext : "lob",
                   sizeof(s->lob_extension) - 1);
    }

    /* Auto-detect dump kind if not done */
    if (s->dump_type == DUMP_UNKNOWN) {
        rc = detect_dump_kind(s);
        if (rc != ODV_OK) goto cleanup;
    }

    /* Set table filter and offset */
    odv_set_table_filter(s, schema, table);
    odv_set_data_offset(s, data_offset);

    /* Reset state */
    s->cancelled = 0;
    s->total_rows = 0;
    odv_lob_reset_buffer(s);
    op_begin(s);

    /* Run the parse (LOB accumulation happens inside parse_*_dump) */
    switch (s->dump_type) {
    case DUMP_EXPDP:
        rc = parse_expdp_dump(s, 0);
        break;
    case DUMP_EXPDP_COMPRESS:
        set_error(s, "Compressed EXPDP dumps are not supported.");
        rc = ODV_ERROR_UNSUPPORTED;
        break;
    case DUMP_EXP:
    case DUMP_EXP_DIRECT:
        rc = parse_exp_dump(s, 0);
        break;
    default:
        set_error(s, "Unknown or unsupported dump format");
        rc = ODV_ERROR_FORMAT;
        break;
    }

    /* The table was found but has no LOB column of that name: nothing could
       be extracted, which is an error, not "0 files written" (M-27). */
    if (rc == ODV_OK && s->lob_column_missing) {
        set_error(s, "LOB column not found in table");
        rc = ODV_ERROR;
    }
    rc = op_finish(s, rc);

cleanup:
    /* Always clean up LOB state regardless of success/failure */
    s->lob_extract_mode = 0;
    s->lob_column_index = -1;
    odv_lob_reset_buffer(s);

    return rc;
}

ODV_API int64_t ODV_CALL odv_get_lob_files_written(ODV_SESSION *s)
{
    if (!s) return 0;
    return s->lob_files_written;
}

ODV_API int64_t ODV_CALL odv_get_lob_files_failed(ODV_SESSION *s)
{
    if (!s) return 0;
    return s->lob_files_failed;
}

ODV_API int ODV_CALL odv_cancel(ODV_SESSION *s)
{
    if (!s) return ODV_ERROR_INVALID_ARG;
    s->cancelled = 1;
    return ODV_OK;
}

/*---------------------------------------------------------------------------
    Utilities
 ---------------------------------------------------------------------------*/

ODV_API const char * ODV_CALL odv_get_version(void)
{
    return ODV_VERSION_STRING;
}

ODV_API const char * ODV_CALL odv_get_last_error(ODV_SESSION *s)
{
    if (!s) return "Invalid session";
    return s->last_error;
}

ODV_API int ODV_CALL odv_get_progress_pct(ODV_SESSION *s)
{
    if (!s) return 0;
    return s->last_progress_pct;
}
