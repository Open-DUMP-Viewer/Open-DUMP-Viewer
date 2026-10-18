#!/usr/bin/env python3
"""gen_tzdata.py - IANA tzdata から TIMESTAMP WITH TIME ZONE の地域用の表 (odv_tzdata.c) を作る

使い方 (Logics/DumpParser/tools/tzdata で):
    python3 gen_tzdata.py [--zoneinfo /usr/share/zoneinfo] [--regions oracle_tz_regions.txt]
                          [--out ../../odv_tzdata.c] [--check]

入力:
  - <zoneinfo>/tzdata.zi : 版 ("# version 2026c") と、旧名のリンク (L 行)
  - <zoneinfo>/<地域>     : zic が作った TZif (v2 以上。64 ビットの遷移と POSIX TZ 形式の規則)
  - oracle_tz_regions.txt : Oracle の地域 ID と地域名 (query_regions.sql の実測。3 列目は IANA に
                            同じ名前が無いものの対応先)
出力:
  - odv_tzdata.c : 地域 ID -> 地域名・時差の表。tzdata の版を odv_tzdata_version に記録する

容量を抑える工夫:
  - UTC からの時差が変わらない遷移 (略称・夏時間フラグだけの変化) は捨てる
  - 遷移の時刻は地域ごとに先頭だけを 64 ビットで持ち、以降は前との差を 30 分単位の 16 ビットで持つ
    (30 分の倍数でない差・長すぎる差だけを 32 ビットの別表に置く)
  - 時差は全地域で共通の表 (int32) に集め、遷移ごとには 1 バイトの番号だけを持つ
  - TZif 末尾の POSIX TZ 規則 (2037 年以降に使う規則) で再現できる遷移は持たない。
    規則で再現できるようになった時点 (rule_from) より後は、規則から計算する
  - 内容が同じ地域 (リンク・同じ規則の地域) は 1 つの表を共有する

--check を付けると、生成した表を C (odv_tz.c) で引いた時差を Python の zoneinfo (同じ
zoneinfo を読む) と突き合わせる (gcc が要る)。全地域で 1850〜2200 年の毎月・各遷移の前後・
9999 年までの抜き取りが一致することを確かめる。
"""
import argparse, atexit, shutil, bisect, os, re, struct, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
DP = os.path.normpath(os.path.join(HERE, '..', '..'))  # Logics/DumpParser

INT64_MIN = -(1 << 63)
DELTA_UNIT = 1800          # odv_tzdata.h の ODV_TZ_DELTA_UNIT と同じ


# --------------------------------------------------------------------------
# 入力の読み込み
# --------------------------------------------------------------------------
def read_tzdata_zi(zi):
    version = None
    links, zones = {}, set()
    with open(os.path.join(zi, 'tzdata.zi'), encoding='utf-8') as f:
        for ln in f:
            if version is None and ln.startswith('# version '):
                version = ln.split()[2]
            if ln.startswith('L '):
                _, target, name = ln.split()
                links[name] = target
            elif ln.startswith('Z '):
                zones.add(ln.split()[1])
    if not version:
        sys.exit('tzdata.zi: version line not found')
    return version, links, zones


def read_regions(path):
    regs = []
    with open(path, encoding='utf-8') as f:
        for ln in f:
            ln = ln.strip()
            if not ln or ln.startswith('#'):
                continue
            a = ln.split()
            rid, name = int(a[0]), a[1]
            target = a[2] if len(a) > 2 else None
            regs.append((rid, name, target))
    ids = [r[0] for r in regs]
    if len(set(ids)) != len(ids):
        sys.exit('regions: duplicate id')
    if any(not (0 < i < (1 << 13)) for i in ids):
        sys.exit('regions: id out of 13-bit range')
    return sorted(regs)


def parse_tzif(path):
    d = open(path, 'rb').read()
    if d[:4] != b'TZif' or d[4] < 0x32:      # '2' 以上
        sys.exit(f'{path}: not a TZif v2+ file')

    def counts(o):
        return struct.unpack('>6l', d[o + 20:o + 44])
    isut, isstd, leap, timecnt, typecnt, charcnt = counts(0)
    o = 44 + timecnt * 5 + typecnt * 6 + charcnt + leap * 8 + isstd + isut
    if d[o:o + 4] != b'TZif':
        sys.exit(f'{path}: second header missing')
    isut, isstd, leap, timecnt, typecnt, charcnt = counts(o)
    if leap:
        sys.exit(f'{path}: leap-second ("right/") data is not supported')
    p = o + 44
    times = struct.unpack('>%dq' % timecnt, d[p:p + 8 * timecnt]); p += 8 * timecnt
    idx = d[p:p + timecnt]; p += timecnt
    types = [struct.unpack('>lBB', d[p + 6 * i:p + 6 * i + 6]) for i in range(typecnt)]
    p += 6 * typecnt + charcnt + isstd + isut
    footer = d[p:].strip(b'\n').decode('ascii')
    # 最初の遷移より前は、夏時間でない最初の型 (RFC 8536 の型 0 と同じ。CPython zoneinfo と同じ選び方)
    before = next((t for t in types if not t[1]), types[0])
    trans = [(times[i], types[idx[i]][0]) for i in range(timecnt)]
    return before[0], trans, footer


# --------------------------------------------------------------------------
# POSIX TZ 規則 (TZif の末尾)
# --------------------------------------------------------------------------
_NAME = r'(?:<[^>]*>|[A-Za-z]+)'
_OFF = r'[+-]?\d{1,3}(?::\d{1,2}){0,2}'


def _hms(s):
    sign = -1 if s.startswith('-') else 1
    s = s.lstrip('+-')
    parts = [int(x) for x in s.split(':')] + [0, 0]
    return sign * (parts[0] * 3600 + parts[1] * 60 + parts[2])


def _ruledate(s):
    date, _, tm = s.partition('/')
    secs = _hms(tm) if tm else 7200
    if date.startswith('M'):
        m, w, dd = (int(x) for x in date[1:].split('.'))
        assert 1 <= m <= 12 and 1 <= w <= 5 and 0 <= dd <= 6, s
        return ('M', m, w, dd, 0, secs)
    if date.startswith('J'):
        n = int(date[1:]); assert 1 <= n <= 365, s
        return ('J', 0, 0, 0, n, secs)
    n = int(date); assert 0 <= n <= 365, s
    return ('N', 0, 0, 0, n, secs)


def parse_posix(tz):
    m = re.fullmatch(rf'({_NAME})({_OFF})(?:({_NAME})({_OFF})?(?:,([^,]+),([^,]+))?)?', tz)
    if not m:
        sys.exit(f'unsupported POSIX TZ string: {tz!r}')
    std_off = -_hms(m.group(2))
    if not m.group(3):
        return (std_off, std_off, 0, None, None)
    dst_off = -_hms(m.group(4)) if m.group(4) else std_off + 3600
    if not m.group(5):
        sys.exit(f'POSIX TZ without DST rule: {tz!r}')
    return (std_off, dst_off, 1, _ruledate(m.group(5)), _ruledate(m.group(6)))


# 暦の計算 (C 側 odv_tz.c と同じ。先発グレゴリオ暦、1970-01-01 からの日数)
def days_from_civil(y, m, d):
    y -= m <= 2
    era = (y if y >= 0 else y - 399) // 400
    yoe = y - era * 400
    doy = (153 * (m + (-3 if m > 2 else 9)) + 2) // 5 + d - 1
    doe = yoe * 365 + yoe // 4 - yoe // 100 + doy
    return era * 146097 + doe - 719468


def is_leap(y):
    return y % 4 == 0 and (y % 100 != 0 or y % 400 == 0)


def year_of(t):
    z = t // 86400 + 719468
    era = (z if z >= 0 else z - 146096) // 146097
    doe = z - era * 146097
    yoe = (doe - doe // 1460 + doe // 36524 - doe // 146096) // 365
    y = yoe + era * 400
    doy = doe - (365 * yoe + yoe // 4 - yoe // 100)
    mp = (5 * doy + 2) // 153
    m = mp + 3 if mp < 10 else mp - 9
    return y + (m <= 2)


_MDAYS = (31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31)


def rule_local_epoch(rd, year):
    """規則の日付 (現地の壁時計) を、UTC とみなした 1970 年からの秒で返す"""
    kind, mon, week, wday, yday, secs = rd
    if kind == 'M':
        first = days_from_civil(year, mon, 1)
        dow1 = (first + 4) % 7                 # 1970-01-01 は木曜 (4)
        mday = (wday - dow1) % 7 + 1 + (week - 1) * 7
        dim = _MDAYS[mon - 1] + (1 if mon == 2 and is_leap(year) else 0)
        if mday > dim:
            mday -= 7
        day = first + mday - 1
    elif kind == 'J':
        n = yday + (1 if yday >= 60 and is_leap(year) else 0)
        day = days_from_civil(year, 1, 1) + n - 1
    else:
        day = days_from_civil(year, 1, 1) + yday
    return day * 86400 + secs


def rule_offset(rule, t):
    """CPython zoneinfo (_TZStr._get_trans_info_fromutc) と同じ判定。年は UTC の年"""
    std_off, dst_off, has_dst, start, end = rule
    if not has_dst:
        return std_off
    y = year_of(t)
    s = rule_local_epoch(start, y) - std_off
    e = rule_local_epoch(end, y) - dst_off
    if s < e:
        dst = s <= t < e
    else:
        dst = not (e <= t < s)
    return dst_off if dst else std_off


def rule_breaks(rule, y0, y1):
    std_off, dst_off, has_dst, start, end = rule
    out = []
    if has_dst:
        for y in range(y0, y1 + 1):
            out.append(rule_local_epoch(start, y) - std_off)
            out.append(rule_local_epoch(end, y) - dst_off)
    return out


# --------------------------------------------------------------------------
# 1 地域の表を作る
# --------------------------------------------------------------------------
def build_zone(init_off, trans, footer):
    # 時差が変わらない遷移を捨てる
    tr = []
    prev = init_off
    for t, off in trans:
        if off != prev:
            tr.append((t, off))
            prev = off
    rule = parse_posix(footer)

    tts = [t for t, _ in tr]

    def explicit(t):
        i = bisect.bisect_right(tts, t)
        return tr[i - 1][1] if i else init_off

    # 持つ遷移の数 k を決める。t < rule_from は持った遷移から、t >= rule_from は規則から引く。
    #   k < len(tr): rule_from = tr[k] の時刻。[tr[k], 最後の遷移] で規則と遷移の時差が
    #                一致しなければならない (区間の端 = 遷移と規則の切り替わりの全点で確かめる)
    #   k = len(tr): rule_from = 最後の遷移 + 1 秒 (最後の遷移より後は規則。CPython と同じ)
    #   遷移なし   : 常に規則
    def valid(k):
        if k == len(tr):
            return True
        c, last = tr[k][0], tr[-1][0]
        pts = {c}
        pts.update(t for t, _ in tr if c <= t <= last)
        pts.update(b for b in rule_breaks(rule, year_of(c) - 1, year_of(last) + 1) if c <= b <= last)
        return all(explicit(b) == rule_offset(rule, b) for b in pts)

    k = len(tr)
    while k > 0 and valid(k - 1):
        k -= 1
    if not tr:
        rule_from = INT64_MIN
    elif k < len(tr):
        rule_from = tr[k][0]
    else:
        rule_from = tr[-1][0] + 1
    kept = tr[:k]
    return (init_off, tuple(kept), rule, rule_from)


# --------------------------------------------------------------------------
# C の出力
# --------------------------------------------------------------------------
def c_str(s):
    return '"' + s.replace('\\', '\\\\').replace('"', '\\"') + '"'


def emit(out, version, regions, zone_of_region, zones):
    offsets = sorted({off for z in zones for _, off in z[1]})
    if len(offsets) > 255:
        sys.exit('too many distinct offsets for uint8_t index')
    oidx = {o: i for i, o in enumerate(offsets)}
    rules = sorted({z[2] for z in zones}, key=repr)
    ridx = {r: i for i, r in enumerate(rules)}
    # 遷移の間隔: 30 分 (DELTA_UNIT) の倍数で 0xFFFF 未満なら uint16 に、それ以外は 0xFFFF を
    # 置いて uint32 (秒) を別表 odv_tz_trans_long に持つ
    trans_d16, trans_long, trans_off, zrec = [], [], [], []
    for init_off, kept, rule, rule_from in zones:
        start, lstart = len(trans_d16), len(trans_long)
        first = kept[0][0] if kept else 0
        prev = first
        for t, off in kept:
            dlt = t - prev
            if dlt % DELTA_UNIT == 0 and dlt // DELTA_UNIT < 0xFFFF:
                trans_d16.append(dlt // DELTA_UNIT)
            elif 0 <= dlt <= 0xFFFFFFFF:
                trans_d16.append(0xFFFF)
                trans_long.append(dlt)
            else:
                sys.exit('transition delta does not fit uint32_t')
            trans_off.append(oidx[off])
            prev = t
        zrec.append((first, rule_from, start, len(kept), lstart, ridx[rule], init_off))
    if max(len(trans_d16), len(trans_long), len(zones)) > 0xFFFF:
        sys.exit('table too large for uint16_t index')
    names, name_off = [], {}
    pos = 0
    for rid, name, _ in regions:
        name_off[name] = pos
        names.append(name)
        pos += len(name.encode('ascii')) + 1
    if pos > 0xFFFF:
        sys.exit('name blob too large')

    w = out.write
    w('/*****************************************************************************\n')
    w('    Open DUMP Viewer for Oracle database\n\n')
    w('    odv_tzdata.c\n')
    w('    Time zone tables for TIMESTAMP WITH TIME ZONE region values.\n\n')
    w('    GENERATED FILE - DO NOT EDIT.\n')
    w('    Generated by Logics/DumpParser/tools/tzdata/gen_tzdata.py from\n')
    w(f'    IANA tzdata {version} (public domain, https://www.iana.org/time-zones)\n')
    w('    and the Oracle region ids in tools/tzdata/oracle_tz_regions.txt.\n')
    w('    Regenerate after a tzdata update (see AGENTS.md).\n')
    w(' *****************************************************************************/\n\n')
    w('#include "odv_tzdata.h"\n\n')
    w(f'const char odv_tzdata_version[] = {c_str(version)};\n\n')
    w(f'/* Distinct UTC offsets after a transition (seconds east of UTC): {len(offsets)} */\n')
    w(f'const int32_t odv_tz_offsets[{max(1, len(offsets))}] = {{\n')
    for i in range(0, len(offsets), 8):
        w('    ' + ', '.join(str(o) for o in offsets[i:i + 8]) + ',\n')
    w('};\n\n')

    def rd(r):
        if r is None:
            return "{ 0, 0, 0, 0, 0, 0 }"
        kind, m, wk, dd, yd, secs = r
        return "{ '%s', %d, %d, %d, %d, %d }" % (kind, m, wk, dd, yd, secs)
    w(f'/* POSIX TZ rules (TZif footers): {len(rules)} */\n')
    w(f'const ODV_TZ_RULE odv_tz_rules[{len(rules)}] = {{\n')
    for r in rules:
        w('    { %d, %d, %d, %s, %s },\n' % (r[0], r[1], r[2], rd(r[3]), rd(r[4])))
    w('};\n\n')
    n = max(1, len(trans_d16))
    w(f'/* Transitions: time since the previous one in units of ODV_TZ_DELTA_UNIT seconds\n'
      f'   (0 for a zone\'s first); 0xFFFF = take the next odv_tz_trans_long entry: {len(trans_d16)} */\n')
    w(f'const uint16_t odv_tz_trans_d16[{n}] = {{\n')
    for i in range(0, len(trans_d16), 12):
        w('    ' + ', '.join(str(x) for x in trans_d16[i:i + 12]) + ',\n')
    if not trans_d16:
        w('    0\n')
    w('};\n\n')
    w(f'/* Transition gaps that are not a multiple of ODV_TZ_DELTA_UNIT (seconds): {len(trans_long)} */\n')
    w(f'const uint32_t odv_tz_trans_long[{max(1, len(trans_long))}] = {{\n')
    for i in range(0, len(trans_long), 8):
        w('    ' + ', '.join('%uu' % x for x in trans_long[i:i + 8]) + ',\n')
    if not trans_long:
        w('    0\n')
    w('};\n\n')
    w('/* Offset (index into odv_tz_offsets) in effect from each transition */\n')
    w(f'const uint8_t odv_tz_trans_off[{n}] = {{\n')
    for i in range(0, len(trans_off), 20):
        w('    ' + ', '.join(str(x) for x in trans_off[i:i + 20]) + ',\n')
    if not trans_off:
        w('    0\n')
    w('};\n\n')
    w(f'/* Zones (distinct after de-duplication): {len(zrec)} */\n')
    w(f'const ODV_TZ_ZONE odv_tz_zones[{len(zrec)}] = {{\n')
    for first, rule_from, start, cnt, lstart, ri, io in zrec:
        rf = 'ODV_TZ_NO_LIMIT' if rule_from == INT64_MIN else ('INT64_C(%d)' % rule_from)
        w('    { INT64_C(%d), %s, %d, %d, %d, %d, %d },\n' % (first, rf, start, cnt, lstart, ri, io))
    w('};\n\n')
    w(f'/* Region names (NUL separated): {len(names)} */\n')
    w('const char odv_tz_names[] =\n')
    for nm in names:
        w('    %s "\\0"\n' % c_str(nm))
    w('    ;\n\n')
    w(f'/* Oracle region id -> name / zone, sorted by id: {len(regions)} */\n')
    w(f'const ODV_TZ_REGION odv_tz_regions[{len(regions)}] = {{\n')
    for rid, name, _ in regions:
        w('    { %d, %d, %d },  /* %s */\n' % (rid, name_off[name], zone_of_region[rid], name))
    w('};\n\n')
    w(f'const int odv_tz_region_count = {len(regions)};\n')
    size = (len(offsets) * 4 + len(rules) * 32 + len(trans_d16) * 3 + len(trans_long) * 4 +
            len(zrec) * 32 + pos + len(regions) * 6)
    return size, len(trans_d16), len(zrec), len(offsets), len(rules)


# --------------------------------------------------------------------------
# --check: C の実装を zoneinfo と突き合わせる
# --------------------------------------------------------------------------
def check(zi, regions, target_of, raw):
    import datetime, zoneinfo
    os.environ['PYTHONTZPATH'] = zi
    zoneinfo.reset_tzpath([zi])
    tmp = tempfile.mkdtemp(prefix='odv_tzchk_')
    atexit.register(shutil.rmtree, tmp, True)
    exe = os.path.join(tmp, 'tz_selftest')
    srcs = [os.path.join(HERE, 'tz_selftest.c')] + [
        os.path.join(DP, f) for f in ('odv_tz.c', 'odv_tzdata.c', 'odv_datetime.c')]
    cc = os.environ.get('CC', 'gcc')
    subprocess.run([cc, '-std=c11', '-O1', '-Wall', '-Wextra', '-DUTF8', '-D_FILE_OFFSET_BITS=64',
                    '-D_POSIX_C_SOURCE=200809L', '-I', DP, '-o', exe] + srcs,
                   check=True)
    epoch = datetime.datetime(1970, 1, 1, tzinfo=datetime.timezone.utc)
    lo = int((datetime.datetime(1, 1, 2, tzinfo=datetime.timezone.utc) - epoch).total_seconds())
    hi = int((datetime.datetime(9999, 12, 30, tzinfo=datetime.timezone.utc) - epoch).total_seconds())
    samples = []
    seen = set()
    for rid, name, _ in regions:
        tgt = target_of[name]
        if tgt in seen:
            # 同じ IANA 地域を指す別名は少しだけ (名前の引き当ての確認)
            ts = [0, 1700000000]
        else:
            seen.add(tgt)
            ts = set()
            for t, _ in raw[tgt][1]:
                ts.update((t - 1, t, t + 1))
            for y in range(1850, 2201):
                for m in range(1, 13):
                    ts.add(days_from_civil(y, m, 1) * 86400 + 43200)
            for y in range(2201, 10000, 37):
                ts.add(days_from_civil(y, 1, 15) * 86400)
                ts.add(days_from_civil(y, 7, 15) * 86400)
            for y in range(1, 1850, 61):
                ts.add(days_from_civil(y, 6, 1) * 86400)
            ts = sorted(t for t in ts if lo <= t <= hi)
        for t in ts:
            samples.append((rid, tgt, t))
    inp = ''.join('%d %d\n' % (rid, t) for rid, _, t in samples)
    res = subprocess.run([exe], input=inp, capture_output=True, text=True, check=True).stdout.split()
    if len(res) != len(samples):
        sys.exit('check: selftest output count mismatch')
    bad = 0
    zcache = {}
    for (rid, tgt, t), got in zip(samples, res):
        z = zcache.get(tgt) or zcache.setdefault(tgt, zoneinfo.ZoneInfo(tgt))
        dt = epoch + datetime.timedelta(seconds=t)
        exp = int(dt.astimezone(z).utcoffset().total_seconds())
        if int(got) != exp:
            bad += 1
            if bad <= 20:
                print(f'  MISMATCH id={rid} {tgt} t={t} C={got} zoneinfo={exp}')
    print(f'check: {len(samples)} samples, {bad} mismatches')
    return bad == 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--zoneinfo', default='/usr/share/zoneinfo')
    ap.add_argument('--regions', default=os.path.join(HERE, 'oracle_tz_regions.txt'))
    ap.add_argument('--out', default=os.path.join(DP, 'odv_tzdata.c'))
    ap.add_argument('--check', action='store_true')
    a = ap.parse_args()

    version, links, zones = read_tzdata_zi(a.zoneinfo)
    regions = read_regions(a.regions)
    target_of = {}
    for rid, name, target in regions:
        t = target or name
        hops = 0
        while t not in zones and t in links and hops < 8:
            t = links[t]
            hops += 1
        if t not in zones:
            sys.exit(f'region {name}: no IANA zone (add the target as the 3rd column)')
        target_of[name] = t
    raw, built = {}, {}
    for tgt in sorted(set(target_of.values())):
        raw[tgt] = parse_tzif(os.path.join(a.zoneinfo, tgt))
        built[tgt] = build_zone(*raw[tgt])
    zlist, zindex = [], {}
    for tgt in sorted(built):
        z = built[tgt]
        if z not in zindex:
            zindex[z] = len(zlist)
            zlist.append(z)
    zone_of_region = {rid: zindex[built[target_of[name]]] for rid, name, _ in regions}
    with open(a.out, 'w', encoding='utf-8', newline='\n') as f:
        size, nt, nz, no, nr = emit(f, version, regions, zone_of_region, zlist)
    print(f'{a.out}: tzdata {version}, {len(regions)} regions -> {nz} zones, {nt} transitions, '
          f'{no} offsets, {nr} rules, ~{size} bytes of data')
    if a.check and not check(a.zoneinfo, regions, target_of, raw):
        sys.exit(1)


if __name__ == '__main__':
    main()
