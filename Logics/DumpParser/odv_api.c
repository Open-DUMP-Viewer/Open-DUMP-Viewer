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
#define ODV_VERSION_STRING "4.6.2"

/*---------------------------------------------------------------------------
    Internal helpers
 ---------------------------------------------------------------------------*/

static void set_error(ODV_SESSION *s, const char *msg)
{
    if (s && msg) {
        odv_strcpy(s->last_error, msg, ODV_MSG_LEN);
    }
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

    /* Free LOB buffer if allocated */
    if (session->state.lob_buf) {
        free(session->state.lob_buf);
        session->state.lob_buf = NULL;
    }

    free(session);
    return ODV_OK;
}

/*---------------------------------------------------------------------------
    Configuration
 ---------------------------------------------------------------------------*/

ODV_API int ODV_CALL odv_set_dump_file(ODV_SESSION *s, const char *path)
{
    FILE *fp;

    if (!s || !path) return ODV_ERROR_INVALID_ARG;

    odv_strcpy(s->dump_path, path, ODV_PATH_LEN);

    /* Verify file is accessible and get size */
    fp = fopen(path, "rb");
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

    return ODV_OK;
}

ODV_API int ODV_CALL odv_set_row_callback(ODV_SESSION *s, ODV_ROW_CALLBACK cb, void *user_data)
{
    if (!s) return ODV_ERROR_INVALID_ARG;
    s->row_cb = cb;
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
    FILE *fp = fopen(path, "rb");
    int64_t sz;
    if (!fp) return -1;
    if (odv_fseek(fp, 0, SEEK_END) != 0) { fclose(fp); return -1; }
    sz = odv_ftell(fp);
    fclose(fp);
    return sz;
}

static int vf_read_head(const char *path, unsigned char *buf, int n)
{
    FILE *fp = fopen(path, "rb");
    int got;
    if (!fp) return -1;
    got = (int)fread(buf, 1, (size_t)n, fp);
    fclose(fp);
    return got;
}

/* Parse the exp header's block-size field (4th 0x0A/0x00-delimited field after
   the "EXPORT:Vnn.nn.nn" signature) and the start of the charset region
   (record 7), the byte just past the 7th delimiter.  The 2-byte big-endian
   volume number sits at cs_start+6 (right after the 6-byte charset region).
   *block_size / *cs_start are set to 0 / -1 when not found. */
static void exp_header_info(const unsigned char *hdr, int len,
                            int *block_size, int *cs_start)
{
    int i, field = 0, fs = 3, bs = 0, cs = -1;
    *block_size = 0;
    *cs_start = -1;
    if (len < 4 || hdr[0] != 0x03) return;
    for (i = 3; i < len; i++) {
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

/* 1 if `cand_head` looks like volume `seq` of the same split set as
   `f0_head`.  exp: EXP signature + volume number == seq.  expdp: same leading
   DataPump block type and job-id region [6,34) as the first file. */
static int vf_candidate_ok(const unsigned char *f0_head, int f0_len,
                           const unsigned char *cand_head, int cand_len,
                           int dump_type, int seq)
{
    if (dump_type == DUMP_EXP || dump_type == DUMP_EXP_DIRECT) {
        int i, found = 0, bs, cs;
        if (cand_len < 64) return 0;
        if (cand_head[0] < 0x01 || cand_head[0] > 0x05) return 0;
        for (i = 3; i + 7 <= 0x20 && i + 7 <= cand_len; i++) {
            if (memcmp(cand_head + i, "EXPORT:", 7) == 0) { found = 1; break; }
        }
        if (!found) return 0;
        exp_header_info(cand_head, cand_len, &bs, &cs);
        if (cs < 0 || cs + 8 > cand_len) return 0;
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
    return ODV_OK;
}

static FILE *vf_ensure_open(ODV_VFILE *vf, int idx)
{
    if (vf->open_idx == idx && vf->fp) return vf->fp;
    if (vf->fp) { fclose(vf->fp); vf->fp = NULL; }
    vf->fp = fopen(vf->seg[idx].path, "rb");
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

    switch (s->dump_type) {
    case DUMP_EXPDP:
        return parse_expdp_dump(s, 1 /* list_only */);
    case DUMP_EXPDP_COMPRESS:
        set_error(s, "Compressed EXPDP dumps (COMPRESSION=ALL) are not supported. "
                     "Please re-export with COMPRESSION=NONE.");
        return ODV_ERROR_UNSUPPORTED;
    case DUMP_EXP:
    case DUMP_EXP_DIRECT:
        return parse_exp_dump(s, 1 /* list_only */);
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
 * Returns pointer to static buffer (overwritten on each call). */
ODV_API const char * ODV_CALL odv_get_table_constraints_json(ODV_SESSION *s, int index)
{
    static char json_buf[8192];
    int pos = 0, i;

    if (!s || index < 0 || index >= s->table_count) {
        json_buf[0] = '['; json_buf[1] = ']'; json_buf[2] = '\0';
        return json_buf;
    }

    ODV_TABLE_ENTRY *e = &s->table_list[index];
    if (e->meta_constraint_count == 0) {
        json_buf[0] = '['; json_buf[1] = ']'; json_buf[2] = '\0';
        return json_buf;
    }

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

        remain = (int)sizeof(json_buf) - pos - 2;  /* keep room for "]\0" */
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

    switch (s->dump_type) {
    case DUMP_EXPDP:
        return parse_expdp_dump(s, 0 /* full parse */);
    case DUMP_EXPDP_COMPRESS:
        set_error(s, "Compressed EXPDP dumps (COMPRESSION=ALL) are not supported. "
                     "Please re-export with COMPRESSION=NONE.");
        return ODV_ERROR_UNSUPPORTED;
    case DUMP_EXP:
    case DUMP_EXP_DIRECT:
        return parse_exp_dump(s, 0 /* full parse */);
    default:
        set_error(s, "Unknown or unsupported dump format");
        return ODV_ERROR_FORMAT;
    }
}

ODV_API void ODV_CALL odv_set_csv_delimiter(ODV_SESSION *s, char delimiter)
{
    if (s) s->csv_delimiter = delimiter;
}

ODV_API int ODV_CALL odv_export_csv(ODV_SESSION *s, const char *table_name, const char *output_path)
{
    if (!s || !output_path) return ODV_ERROR_INVALID_ARG;
    return write_csv_file(s, table_name, output_path);
}

ODV_API int ODV_CALL odv_export_sql(ODV_SESSION *s, const char *table_name, const char *output_path, int dbms_type)
{
    if (!s || !output_path) return ODV_ERROR_INVALID_ARG;
    return write_sql_file(s, table_name, output_path, dbms_type);
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
       made fatal, so the remaining rows of the table are still extracted. */
    fpw = fopen(path, "wb");
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

    /* Configure LOB extraction */
    s->lob_extract_mode = 1;
    odv_strcpy(s->lob_column, lob_column, ODV_OBJNAME_LEN);
    odv_strcpy(s->lob_output_dir, output_dir, ODV_PATH_LEN);
    s->lob_column_index = -1;
    s->lob_files_written = 0;
    s->lob_files_failed = 0;

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
