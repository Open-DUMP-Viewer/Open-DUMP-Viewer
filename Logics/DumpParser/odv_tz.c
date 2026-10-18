/*****************************************************************************
    Open DUMP Viewer for Oracle database

    odv_tz.c
    TIMESTAMP WITH TIME ZONE / TIMESTAMP WITH LOCAL TIME ZONE

    Storage formats (same in exp conventional, exp DIRECT=Y and expdp):

    TIMESTAMP WITH TIME ZONE: 13 bytes
      [0-6]   date/time in UTC (same encoding as DATE)
      [7-10]  nanoseconds (big-endian)
      [11-12] time zone
              b11 bit 7 clear: offset   = (b11 - 20) hours + (b12 - 60) minutes
              b11 bit 7 set  : region id = ((b11 & 0x7F) << 6) | (b12 >> 2)
      e.g. 2024-03-10 12:34:56 Asia/Tokyo = 78 7c 03 0a 04 23 39 .. .. .. .. 84 2c
           (03:34:56 UTC, region 267)

    TIMESTAMP WITH LOCAL TIME ZONE: 7 or 11 bytes, the local time in the
    source database's DBTIMEZONE (no zone bytes).  The DBTIMEZONE is recorded
    in the dump: exp writes it in the second header block (a length-prefixed
    string just before NLS_LENGTH_SEMANTICS; imp converts TSLTZ values from
    it), expdp in every table definition (<DBTIMEZONE> in STRMTABLE_T).

    Display (preview / CSV): the stored UTC instant is converted to the local
    time of the value's own offset or region (region rules from the bundled
    IANA tzdata, odv_tzdata.c) and formatted like a TIMESTAMP with the session
    date format, followed by the offset ("+14:00") or by the region name and
    the offset in effect at that instant ("America/New_York -04:00"), so that
    a local time in a daylight-saving overlap still reads as one instant.
    TSLTZ shows the stored DBTIMEZONE local time followed by the DBTIMEZONE
    ("+00:00"; a region DBTIMEZONE also gets its offset).  Date-only formats
    (YYYYMMDD, a custom format without a time token) show the local date only.

    SQL (Oracle dialect): written so that neither the loading session's time
    zone nor a different time zone file version on the target changes the
    instant, and the offset / region is kept:
      offset : FROM_TZ(TO_TIMESTAMP('<local>', 'SYYYY-MM-DD HH24:MI:SS.FF'), '+14:00')
      region : FROM_TZ(TO_TIMESTAMP('<UTC>', ...), 'UTC') AT TIME ZONE 'Asia/Tokyo'
      TSLTZ  : FROM_TZ(TO_TIMESTAMP('<local>', ...), '<DBTIMEZONE>')
    write_sql_file() sets s->tz_raw_values while it parses, so these columns
    reach the SQL row callback as "#<hex of the stored bytes>"; the other
    dialects get exactly the text they got before (odv_tz_sql_value).

    Calendar: Oracle uses the Julian calendar before 1582-10-15.

    Copyright (C) 2026 YANAI Taketo
 *****************************************************************************/

#include "odv_types.h"
#include "odv_tzdata.h"
#include <ctype.h>

#define TSTZ_LEN            13
#define JDN_UNIX_EPOCH      2440588   /* 1970-01-01 */
#define JDN_GREGORIAN_START 2299161   /* 1582-10-15 (Gregorian) */
#define RAW_MARK            '#'

/*---------------------------------------------------------------------------
    Calendar helpers
 ---------------------------------------------------------------------------*/
static int64_t floor_div(int64_t a, int64_t b)
{
    int64_t q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) q--;
    return q;
}

static int64_t floor_mod(int64_t a, int64_t b)
{
    return a - floor_div(a, b) * b;
}

/* Proleptic Gregorian: days since 1970-01-01 (astronomical year) */
static int64_t days_from_civil(int64_t y, int m, int d)
{
    int64_t era, yoe, doy, doe;
    y -= (m <= 2);
    era = floor_div(y, 400);
    yoe = y - era * 400;
    doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

static void civil_from_days(int64_t z, int64_t *y, int *m, int *d)
{
    int64_t era, doe, yoe, doy, mp;
    z += 719468;
    era = floor_div(z, 146097);
    doe = z - era * 146097;
    yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    mp = (5 * doy + 2) / 153;
    *d = (int)(doy - (153 * mp + 2) / 5 + 1);
    *m = (int)(mp < 10 ? mp + 3 : mp - 9);
    *y = yoe + era * 400 + (*m <= 2);
}

/* Julian calendar: days since 1970-01-01 (astronomical year) */
static int64_t days_from_julian(int64_t y, int m, int d)
{
    int64_t a = (14 - m) / 12;
    int64_t yy = y + 4800 - a;
    int64_t mm = m + 12 * a - 3;
    return d + (153 * mm + 2) / 5 + 365 * yy + floor_div(yy, 4) - 32083 - JDN_UNIX_EPOCH;
}

static void julian_from_days(int64_t days, int64_t *y, int *m, int *d)
{
    int64_t j = days + JDN_UNIX_EPOCH;
    int64_t c = j + 32082;
    int64_t dd = floor_div(4 * c + 3, 1461);
    int64_t e = c - floor_div(1461 * dd, 4);
    int64_t mm = (5 * e + 2) / 153;
    *d = (int)(e - (153 * mm + 2) / 5 + 1);
    *m = (int)(mm + 3 - 12 * (mm / 10));
    *y = dd - 4800 + mm / 10;
}

/* Oracle year (no year 0; -1 = 1 BC) <-> astronomical year */
static int64_t astro_year(int y) { return y < 0 ? (int64_t)y + 1 : (int64_t)y; }
static int64_t oracle_year(int64_t a) { return a <= 0 ? a - 1 : a; }

/* Oracle calendar date -> days since 1970-01-01 */
static int64_t days_from_oracle(int y, int m, int d)
{
    int64_t a = astro_year(y);
    if (y > 1582 || (y == 1582 && (m > 10 || (m == 10 && d >= 15))))
        return days_from_civil(a, m, d);
    return days_from_julian(a, m, d);
}

static void oracle_from_days(int64_t days, int64_t *y, int *m, int *d)
{
    int64_t a;
    if (days + JDN_UNIX_EPOCH >= JDN_GREGORIAN_START)
        civil_from_days(days, &a, m, d);
    else
        julian_from_days(days, &a, m, d);
    *y = oracle_year(a);
}

/* Broken-down Oracle date/time */
typedef struct {
    int64_t  year;
    int      mon, day, hour, min, sec;
    uint32_t nano;
} TZ_TM;

/* Decode the 7 DATE bytes (+ optional 4 nanosecond bytes) */
static int tm_from_bytes(const unsigned char *b, int len, TZ_TM *t)
{
    if (len < 7) return ODV_ERROR_FORMAT;
    if (b[0] >= 100) t->year = ((int)b[0] - 100) * 100 + ((int)b[1] - 100);
    else             t->year = -((100 - (int)b[0]) * 100 + (100 - (int)b[1]));
    t->mon  = b[2];
    t->day  = b[3];
    t->hour = b[4] - 1;
    t->min  = b[5] - 1;
    t->sec  = b[6] - 1;
    t->nano = (len >= 11) ? (((uint32_t)b[7] << 24) | ((uint32_t)b[8] << 16) |
                             ((uint32_t)b[9] << 8) | (uint32_t)b[10]) : 0;
    if (t->year == 0 || t->mon < 1 || t->mon > 12 || t->day < 1 || t->day > 31 ||
        t->hour < 0 || t->hour > 23 || t->min < 0 || t->min > 59 ||
        t->sec < 0 || t->sec > 59 || t->nano > 999999999u)
        return ODV_ERROR_FORMAT;
    return ODV_OK;
}

/* Encode as an 11-byte Oracle TIMESTAMP (for decode_oracle_timestamp) */
static int tm_to_bytes(const TZ_TM *t, unsigned char *b)
{
    int64_t ay = t->year < 0 ? -t->year : t->year;
    if (t->year == 0 || ay > 9999) return ODV_ERROR_FORMAT;
    if (t->year > 0) {
        b[0] = (unsigned char)(ay / 100 + 100);
        b[1] = (unsigned char)(ay % 100 + 100);
    } else {
        b[0] = (unsigned char)(100 - ay / 100);
        b[1] = (unsigned char)(100 - ay % 100);
    }
    b[2] = (unsigned char)t->mon;
    b[3] = (unsigned char)t->day;
    b[4] = (unsigned char)(t->hour + 1);
    b[5] = (unsigned char)(t->min + 1);
    b[6] = (unsigned char)(t->sec + 1);
    b[7] = (unsigned char)(t->nano >> 24);
    b[8] = (unsigned char)(t->nano >> 16);
    b[9] = (unsigned char)(t->nano >> 8);
    b[10] = (unsigned char)t->nano;
    return ODV_OK;
}

static int64_t tm_to_secs(const TZ_TM *t)
{
    return days_from_oracle((int)t->year, t->mon, t->day) * 86400 +
           t->hour * 3600 + t->min * 60 + t->sec;
}

static void tm_from_secs(int64_t secs, uint32_t nano, TZ_TM *t)
{
    int64_t days = floor_div(secs, 86400);
    int64_t sod = floor_mod(secs, 86400);
    oracle_from_days(days, &t->year, &t->mon, &t->day);
    t->hour = (int)(sod / 3600);
    t->min  = (int)(sod / 60 % 60);
    t->sec  = (int)(sod % 60);
    t->nano = nano;
}

/*---------------------------------------------------------------------------
    Time zone rules (generated tables in odv_tzdata.c)
 ---------------------------------------------------------------------------*/
static int is_leap(int64_t y)
{
    return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
}

static int64_t gregorian_year_of(int64_t t)
{
    int64_t y; int m, d;
    civil_from_days(floor_div(t, 86400), &y, &m, &d);
    return y;
}

/* Local wall-clock instant of a rule date in year y, as seconds since the
   epoch if it were UTC (same arithmetic as CPython zoneinfo and the generator) */
static int64_t rule_local_epoch(const ODV_TZ_RULEDATE *r, int64_t y)
{
    static const int mdays[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    int64_t day;
    if (r->kind == 'M') {
        int mon = (r->month >= 1 && r->month <= 12) ? r->month : 1;
        int64_t first = days_from_civil(y, mon, 1);
        int dow1 = (int)floor_mod(first + 4, 7);      /* 1970-01-01 = Thursday */
        int mday = (int)floor_mod(r->wday - dow1, 7) + 1 + (r->week - 1) * 7;
        int dim = mdays[mon - 1] + ((mon == 2 && is_leap(y)) ? 1 : 0);
        if (mday > dim) mday -= 7;
        day = first + mday - 1;
    } else if (r->kind == 'J') {
        int n = r->yday + ((r->yday >= 60 && is_leap(y)) ? 1 : 0);
        day = days_from_civil(y, 1, 1) + n - 1;
    } else {
        day = days_from_civil(y, 1, 1) + r->yday;
    }
    return day * 86400 + r->secs;
}

static int32_t rule_offset(const ODV_TZ_RULE *r, int64_t t)
{
    int64_t y, st, en;
    int dst;
    if (!r->has_dst) return r->std_off;
    y = gregorian_year_of(t);
    st = rule_local_epoch(&r->start, y) - r->std_off;
    en = rule_local_epoch(&r->end, y) - r->dst_off;
    if (st < en) dst = (st <= t && t < en);
    else         dst = !(en <= t && t < st);
    return dst ? r->dst_off : r->std_off;
}

static int32_t zone_offset(const ODV_TZ_ZONE *z, int64_t t)
{
    int32_t off;
    int64_t tt;
    int i, li;
    if (t >= z->rule_from) return rule_offset(&odv_tz_rules[z->rule], t);
    off = z->init_off;
    tt = z->first;
    li = z->long_start;
    for (i = 0; i < z->trans_count; i++) {
        uint16_t d = odv_tz_trans_d16[z->trans_start + i];
        tt += (d == ODV_TZ_DELTA_LONG) ? (int64_t)odv_tz_trans_long[li++]
                                       : (int64_t)d * ODV_TZ_DELTA_UNIT;
        if (tt > t) break;
        off = odv_tz_offsets[odv_tz_trans_off[z->trans_start + i]];
    }
    return off;
}

static const ODV_TZ_REGION *find_region(int id)
{
    int lo = 0, hi = odv_tz_region_count - 1;
    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        int v = odv_tz_regions[mid].id;
        if (v == id) return &odv_tz_regions[mid];
        if (v < id) lo = mid + 1; else hi = mid - 1;
    }
    return NULL;
}

/* Oracle region name for a region id (NULL if not in the bundled table) */
const char *odv_tz_region_name(int region_id)
{
    const ODV_TZ_REGION *r = find_region(region_id);
    return r ? odv_tz_names + r->name : NULL;
}

/* UTC offset (seconds east) of a region at a UTC instant (seconds since 1970) */
int odv_tz_region_offset(int region_id, int64_t utc_secs, int32_t *off_secs)
{
    const ODV_TZ_REGION *r = find_region(region_id);
    if (!r || !off_secs) return ODV_ERROR;
    *off_secs = zone_offset(&odv_tz_zones[r->zone], utc_secs);
    return ODV_OK;
}

/* Version of the bundled IANA tzdata (e.g. "2026c") */
const char *odv_tz_data_version(void)
{
    return odv_tzdata_version;
}

/*---------------------------------------------------------------------------
    TSTZ value
 ---------------------------------------------------------------------------*/
#define TZK_OFFSET   1
#define TZK_REGION   2
#define TZK_UNKNOWN  3   /* region id not in the table / invalid zone bytes */

typedef struct {
    TZ_TM   utc;
    int64_t utc_secs;
    int     kind;
    int     offset_min;   /* TZK_OFFSET */
    int     region_id;    /* TZK_REGION / TZK_UNKNOWN */
    const char *region;   /* TZK_REGION */
    int     zone_word;    /* raw b11 << 8 | b12 (TZK_UNKNOWN) */
} TSTZ_VAL;

static int tstz_parse(const unsigned char *b, int len, TSTZ_VAL *v)
{
    int rc;
    if (len < TSTZ_LEN) return ODV_ERROR_FORMAT;
    rc = tm_from_bytes(b, 11, &v->utc);
    if (rc != ODV_OK) return rc;
    v->utc_secs = tm_to_secs(&v->utc);
    v->zone_word = (b[11] << 8) | b[12];
    v->region = NULL;
    v->region_id = 0;
    v->offset_min = 0;
    if (b[11] & 0x80) {
        v->region_id = ((b[11] & 0x7F) << 6) | (b[12] >> 2);
        v->region = odv_tz_region_name(v->region_id);
        v->kind = v->region ? TZK_REGION : TZK_UNKNOWN;
    } else {
        int h = (int)b[11] - 20, m = (int)b[12] - 60;
        if (h < -15 || h > 15 || m < -59 || m > 59 || (h > 0 && m < 0) || (h < 0 && m > 0)) {
            v->kind = TZK_UNKNOWN;
        } else {
            v->kind = TZK_OFFSET;
            v->offset_min = h * 60 + m;
        }
    }
    return ODV_OK;
}

/* UTC offset (seconds) of the value at its instant (its own offset / region) */
static int32_t tstz_offset_secs(const TSTZ_VAL *v)
{
    int32_t off = 0;
    if (v->kind == TZK_OFFSET) off = v->offset_min * 60;
    else if (v->kind == TZK_REGION) odv_tz_region_offset(v->region_id, v->utc_secs, &off);
    return off;
}

/* Local time of the value (its own offset / region) */
static void tstz_local(const TSTZ_VAL *v, TZ_TM *local)
{
    tm_from_secs(v->utc_secs + tstz_offset_secs(v), v->utc.nano, local);
}

static void format_offset(int minutes, char *out, int out_size)
{
    int a = minutes < 0 ? -minutes : minutes;
    snprintf(out, (size_t)out_size, "%c%02d:%02d", minutes < 0 ? '-' : '+', a / 60, a % 60);
}

/* Offset in seconds as +HH:MM, or +HH:MM:SS for a local mean time offset
   with seconds (tzdata before standard time), so local - offset = UTC exactly */
static void format_offset_secs(int32_t secs, char *out, int out_size)
{
    int32_t a = secs < 0 ? -secs : secs;
    if (a % 60 == 0)
        format_offset(secs / 60, out, out_size);
    else
        snprintf(out, (size_t)out_size, "%c%02d:%02d:%02d", secs < 0 ? '-' : '+',
                 (int)(a / 3600), (int)(a / 60 % 60), (int)(a % 60));
}

/* "+09:00" / "-5:30" -> minutes; 0 if the text is not an offset */
static int parse_offset(const char *s, int *minutes)
{
    int sign, h = 0, m = 0, nh = 0;
    if (!s || (s[0] != '+' && s[0] != '-')) return 0;
    sign = (s[0] == '-') ? -1 : 1;
    s++;
    while (isdigit((unsigned char)*s) && nh < 2) { h = h * 10 + (*s - '0'); s++; nh++; }
    if (nh == 0 || *s != ':') return 0;
    s++;
    if (!isdigit((unsigned char)s[0]) || !isdigit((unsigned char)s[1]) || s[2]) return 0;
    m = (s[0] - '0') * 10 + (s[1] - '0');
    if (h > 15 || m > 59) return 0;
    *minutes = sign * (h * 60 + m);
    return 1;
}

/* Region id for an Oracle region name (case-insensitive); 0 if unknown */
static int region_id_by_name(const char *name)
{
    int i;
    for (i = 0; i < odv_tz_region_count; i++) {
        const char *a = odv_tz_names + odv_tz_regions[i].name, *b = name;
        while (*a && *b && toupper((unsigned char)*a) == toupper((unsigned char)*b)) { a++; b++; }
        if (!*a && !*b) return odv_tz_regions[i].id;
    }
    return 0;
}

/* Offset of a region at a local wall-clock time (seconds since 1970 as if UTC).
   In a daylight-saving overlap the standard (smaller) offset is taken, as
   Oracle does for a local time without TZD; in a gap, the offset before it. */
static int32_t region_offset_at_local(int region_id, int64_t local_secs)
{
    int32_t before = 0, after = 0, chk;
    int ok_b, ok_a;
    odv_tz_region_offset(region_id, local_secs - 2 * 86400, &before);
    odv_tz_region_offset(region_id, local_secs + 2 * 86400, &after);
    odv_tz_region_offset(region_id, local_secs - before, &chk);
    ok_b = (chk == before);
    odv_tz_region_offset(region_id, local_secs - after, &chk);
    ok_a = (chk == after);
    if (ok_b && ok_a) return before < after ? before : after;
    if (ok_a) return after;
    return before;
}

/* Does the date format show the time of day? (YYYYMMDD and a custom format
   without HH24 / MI / SS do not.) */
static int format_has_time(int fmt, const char *custom_fmt)
{
    if (fmt == DATE_FMT_COMPACT) return 0;
    if (fmt == DATE_FMT_CUSTOM && custom_fmt && custom_fmt[0])
        return strstr(custom_fmt, "HH24") || strstr(custom_fmt, "MI") || strstr(custom_fmt, "SS");
    return 1;
}

static int append_str(char *out, int out_size, const char *s)
{
    size_t n = strlen(out), m = strlen(s);
    if (n + m + 1 > (size_t)out_size) return ODV_ERROR_BUFFER_OVER;
    memcpy(out + n, s, m + 1);
    return ODV_OK;
}

static int hex_encode(const unsigned char *b, int len, char *out, int out_size)
{
    static const char hx[] = "0123456789abcdef";
    int i;
    if (out_size < 2 + len * 2) return ODV_ERROR_BUFFER_OVER;
    out[0] = RAW_MARK;
    for (i = 0; i < len; i++) {
        out[1 + i * 2] = hx[b[i] >> 4];
        out[2 + i * 2] = hx[b[i] & 15];
    }
    out[1 + len * 2] = '\0';
    return ODV_OK;
}

static int hex_decode(const char *s, unsigned char *b, int max)
{
    int n = 0;
    if (!s || s[0] != RAW_MARK) return -1;
    s++;
    while (s[0] && s[1] && n < max) {
        int hi = isdigit((unsigned char)s[0]) ? s[0] - '0' : (tolower((unsigned char)s[0]) - 'a' + 10);
        int lo = isdigit((unsigned char)s[1]) ? s[1] - '0' : (tolower((unsigned char)s[1]) - 'a' + 10);
        if (hi < 0 || hi > 15 || lo < 0 || lo > 15) return -1;
        b[n++] = (unsigned char)((hi << 4) | lo);
        s += 2;
    }
    return s[0] ? -1 : n;
}

static int decode_tstz_display(ODV_SESSION *s, const unsigned char *buf, int len,
                               char *out, int out_size, int prec)
{
    TSTZ_VAL v;
    TZ_TM local;
    unsigned char lb[11];
    char suffix[96];
    int rc;

    if (tstz_parse(buf, len, &v) != ODV_OK) {
        /* No zone bytes or an impossible date: show what is stored, as before */
        return decode_oracle_timestamp(buf, len, out, out_size, s->date_format,
                                       s->custom_date_format, prec);
    }
    if (v.kind == TZK_UNKNOWN) {
        /* Region not in the bundled table: the UTC instant, marked as such */
        local = v.utc;
        if (v.region_id)
            snprintf(suffix, sizeof(suffix), " UTC (unknown time zone region %d)", v.region_id);
        else
            snprintf(suffix, sizeof(suffix), " UTC (unknown time zone 0x%04X)", (unsigned)v.zone_word);
    } else {
        tstz_local(&v, &local);
        if (v.kind == TZK_OFFSET) {
            suffix[0] = ' ';
            format_offset(v.offset_min, suffix + 1, (int)sizeof(suffix) - 1);
        } else {
            /* region and the offset in effect: unambiguous in a DST overlap */
            char off[16];
            format_offset_secs(tstz_offset_secs(&v), off, (int)sizeof(off));
            snprintf(suffix, sizeof(suffix), " %s %s", v.region, off);
        }
    }
    if (tm_to_bytes(&local, lb) != ODV_OK) {
        /* Local year outside the Oracle range: fall back to the stored bytes */
        return decode_oracle_timestamp(buf, len, out, out_size, s->date_format,
                                       s->custom_date_format, prec);
    }
    rc = decode_oracle_timestamp(lb, 11, out, out_size, s->date_format,
                                 s->custom_date_format, prec);
    if (rc != ODV_OK) return rc;
    if (format_has_time(s->date_format, s->custom_date_format))
        rc = append_str(out, out_size, suffix);
    return rc;
}

/* TSLTZ: the stored DBTIMEZONE local time, followed by the DBTIMEZONE */
static int decode_tsltz_display(ODV_SESSION *s, const unsigned char *buf, int len,
                                char *out, int out_size, int prec)
{
    char suffix[96], off[16];
    int rc, minutes, region;
    TZ_TM t;

    rc = decode_oracle_timestamp(buf, len, out, out_size, s->date_format,
                                 s->custom_date_format, prec);
    if (rc != ODV_OK || !s->db_timezone[0] ||
        !format_has_time(s->date_format, s->custom_date_format))
        return rc;
    if (parse_offset(s->db_timezone, &minutes)) {
        suffix[0] = ' ';
        format_offset(minutes, suffix + 1, (int)sizeof(suffix) - 1);
    } else if ((region = region_id_by_name(s->db_timezone)) != 0 &&
               tm_from_bytes(buf, len, &t) == ODV_OK) {
        format_offset_secs(region_offset_at_local(region, tm_to_secs(&t)), off, (int)sizeof(off));
        snprintf(suffix, sizeof(suffix), " %s %s", s->db_timezone, off);
    } else {
        snprintf(suffix, sizeof(suffix), " %s", s->db_timezone);
    }
    return append_str(out, out_size, suffix);
}

/*---------------------------------------------------------------------------
    odv_decode_timestamp_value

    Decodes a TIMESTAMP / TIMESTAMP WITH TIME ZONE / TIMESTAMP WITH LOCAL
    TIME ZONE column value for delivery (exp and expdp call this).
 ---------------------------------------------------------------------------*/
int odv_decode_timestamp_value(ODV_SESSION *s, int col_type,
                               const unsigned char *buf, int len,
                               char *out, int out_size, int ts_precision)
{
    if (!s || !buf || !out || out_size < 2) return ODV_ERROR_INVALID_ARG;
    if (col_type == COL_TIMESTAMP_TZ || col_type == COL_TIMESTAMP_LTZ) {
        if (s->tz_raw_values && len >= 7 && len <= TSTZ_LEN)
            return hex_encode(buf, len, out, out_size);
        if (col_type == COL_TIMESTAMP_TZ && len >= TSTZ_LEN)
            return decode_tstz_display(s, buf, len, out, out_size, ts_precision);
        if (col_type == COL_TIMESTAMP_LTZ)
            return decode_tsltz_display(s, buf, len, out, out_size, ts_precision);
    }
    return decode_oracle_timestamp(buf, len, out, out_size, s->date_format,
                                   s->custom_date_format, ts_precision);
}

/*---------------------------------------------------------------------------
    SQL
 ---------------------------------------------------------------------------*/

/* 'SYYYY-MM-DD HH24:MI:SS[.FF]' literal and mask for a broken-down time.
   Fraction digits: the column precision, widened so no non-zero digit is lost. */
static void sql_ts_text(const TZ_TM *t, int prec, char *lit, int lit_size,
                        const char **mask)
{
    int digits = (prec < 0) ? 6 : (prec > 9 ? 9 : prec);
    uint32_t n = t->nano;
    int need = 9;
    char frac[16];
    int64_t ay = t->year < 0 ? -t->year : t->year;

    if (n == 0) need = 0;
    else while (need > 0 && n % 10 == 0) { n /= 10; need--; }
    if (need > digits) digits = need;
    snprintf(lit, (size_t)lit_size, "%s%04lld-%02d-%02d %02d:%02d:%02d",
             t->year < 0 ? "-" : "", (long long)ay, t->mon, t->day,
             t->hour, t->min, t->sec);
    if (digits > 0) {
        snprintf(frac, sizeof(frac), ".%09u", (unsigned)t->nano);
        frac[1 + digits] = '\0';
        if (strlen(lit) + strlen(frac) < (size_t)lit_size) strcat(lit, frac);
        *mask = "SYYYY-MM-DD HH24:MI:SS.FF";
    } else {
        *mask = "SYYYY-MM-DD HH24:MI:SS";
    }
}

/* DBTIMEZONE text that is safe inside a SQL string literal */
static int dbtz_usable(const char *tz)
{
    const char *p;
    if (!tz || !tz[0]) return 0;
    for (p = tz; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (!(isalnum(c) || c == '/' || c == '_' || c == '+' || c == '-' || c == ':'))
            return 0;
    }
    return 1;
}

/*---------------------------------------------------------------------------
    odv_tz_sql_value

    Turns a TSTZ / TSLTZ value delivered in raw form ("#<hex>", see
    s->tz_raw_values) into SQL.  Oracle dialect: a complete expression
    (*is_expr = 1).  Other dialects: the same text the value used to be shown
    as (UTC wall clock for TSTZ, DBTIMEZONE wall clock for TSLTZ, in the
    session date format), to be written as a string literal (*is_expr = 0).
    Returns ODV_ERROR when the value is not in raw form.
 ---------------------------------------------------------------------------*/
int odv_tz_sql_value(ODV_SESSION *s, int col_type, int ts_precision,
                     const char *value, int dbms_type,
                     char *out, int out_size, int *is_expr)
{
    unsigned char b[TSTZ_LEN];
    char lit[64], zone[96];
    const char *mask;
    int len;

    if (!s || !out || out_size < 2 || !is_expr) return ODV_ERROR_INVALID_ARG;
    len = hex_decode(value, b, (int)sizeof(b));
    if (len < 7) return ODV_ERROR;
    *is_expr = 0;

    if (dbms_type != DBMS_ORACLE) {
        return decode_oracle_timestamp(b, len, out, out_size, s->date_format,
                                       s->custom_date_format, ts_precision);
    }

    if (col_type == COL_TIMESTAMP_TZ && len >= TSTZ_LEN) {
        TSTZ_VAL v;
        if (tstz_parse(b, len, &v) == ODV_OK) {
            *is_expr = 1;
            if (v.kind == TZK_OFFSET) {
                TZ_TM local;
                tstz_local(&v, &local);
                sql_ts_text(&local, ts_precision, lit, (int)sizeof(lit), &mask);
                format_offset(v.offset_min, zone, (int)sizeof(zone));
                snprintf(out, (size_t)out_size, "FROM_TZ(TO_TIMESTAMP('%s', '%s'), '%s')",
                         lit, mask, zone);
            } else if (v.kind == TZK_REGION) {
                sql_ts_text(&v.utc, ts_precision, lit, (int)sizeof(lit), &mask);
                snprintf(out, (size_t)out_size,
                         "FROM_TZ(TO_TIMESTAMP('%s', '%s'), 'UTC') AT TIME ZONE '%s'",
                         lit, mask, v.region);
            } else {
                sql_ts_text(&v.utc, ts_precision, lit, (int)sizeof(lit), &mask);
                if (v.region_id)
                    snprintf(zone, sizeof(zone), "unknown time zone region %d", v.region_id);
                else
                    snprintf(zone, sizeof(zone), "unknown time zone 0x%04X", (unsigned)v.zone_word);
                snprintf(out, (size_t)out_size,
                         "FROM_TZ(TO_TIMESTAMP('%s', '%s'), 'UTC') /* %s */", lit, mask, zone);
            }
            return ODV_OK;
        }
    } else if (col_type == COL_TIMESTAMP_LTZ) {
        TZ_TM t;
        if (tm_from_bytes(b, len, &t) == ODV_OK) {
            *is_expr = 1;
            sql_ts_text(&t, ts_precision, lit, (int)sizeof(lit), &mask);
            if (dbtz_usable(s->db_timezone))
                snprintf(out, (size_t)out_size, "FROM_TZ(TO_TIMESTAMP('%s', '%s'), '%s')",
                         lit, mask, s->db_timezone);
            else
                /* DBTIMEZONE not found in the dump: the loading session's time zone applies */
                snprintf(out, (size_t)out_size, "TO_TIMESTAMP('%s', '%s')", lit, mask);
            return ODV_OK;
        }
    }
    /* Undecodable: the old literal */
    return decode_oracle_timestamp(b, len, out, out_size, s->date_format,
                                   s->custom_date_format, ts_precision);
}

/*---------------------------------------------------------------------------
    DBTIMEZONE of the source database
 ---------------------------------------------------------------------------*/
static int tz_text_char(unsigned char c)
{
    return isalnum(c) || c == '/' || c == '_' || c == '+' || c == '-' || c == ':';
}

static void set_dbtz(ODV_SESSION *s, const char *p, int n)
{
    int i;
    if (n <= 0 || n >= (int)sizeof(s->db_timezone)) return;
    for (i = 0; i < n; i++)
        if (!tz_text_char((unsigned char)p[i])) return;
    memcpy(s->db_timezone, p, (size_t)n);
    s->db_timezone[n] = '\0';
}

/* exp: <len 2B LE><DBTIMEZONE> 00 00 04 00 "BYTE"|"CHAR" in the block after
   the 1st header block (offset = RECORDLENGTH + ~200; RECORDLENGTH <= 65535). */
void odv_tz_scan_exp_dbtz(ODV_SESSION *s, FILE *fp)
{
    enum { SCAN_LEN = 131072 };
    unsigned char *buf;
    int n, i;

    if (!s || !fp) return;
    s->db_timezone[0] = '\0';
    buf = (unsigned char *)malloc(SCAN_LEN);
    if (!buf) return;
    if (odv_fseek(fp, 0, SEEK_SET) == 0) {
        n = (int)fread(buf, 1, SCAN_LEN, fp);
        for (i = 8; i + 8 <= n; i++) {
            if (buf[i] == 0 && buf[i + 1] == 0 && buf[i + 2] == 4 && buf[i + 3] == 0 &&
                (memcmp(buf + i + 4, "BYTE", 4) == 0 || memcmp(buf + i + 4, "CHAR", 4) == 0)) {
                /* the TZ string ends at i; its 2-byte length precedes it */
                int len;
                for (len = 1; len <= 63 && i - len - 2 >= 0; len++) {
                    int at = i - len;
                    if (!tz_text_char(buf[at])) break;
                    if (buf[at - 2] == (unsigned char)len && buf[at - 1] == 0) {
                        set_dbtz(s, (const char *)buf + at, len);
                        break;
                    }
                }
                if (s->db_timezone[0]) break;
            }
        }
    }
    free(buf);
}

/* expdp: <DBTIMEZONE>...</DBTIMEZONE> in a table definition (STRMTABLE_T) */
void odv_tz_capture_dbtz_xml(ODV_SESSION *s, const char *xml, int len)
{
    static const char open_tag[] = "<DBTIMEZONE>";
    static const char close_tag[] = "</DBTIMEZONE>";
    const int ol = (int)sizeof(open_tag) - 1;
    int i, j;

    if (!s || !xml || s->db_timezone[0]) return;
    for (i = 0; i + ol <= len; i++) {
        if (xml[i] == '<' && memcmp(xml + i, open_tag, (size_t)ol) == 0) {
            int st = i + ol;
            for (j = st; j < len && j - st < 64; j++) {
                if (xml[j] == '<') {
                    if (j + (int)sizeof(close_tag) - 1 <= len &&
                        memcmp(xml + j, close_tag, sizeof(close_tag) - 1) == 0)
                        set_dbtz(s, xml + st, j - st);
                    return;
                }
            }
            return;
        }
    }
}
