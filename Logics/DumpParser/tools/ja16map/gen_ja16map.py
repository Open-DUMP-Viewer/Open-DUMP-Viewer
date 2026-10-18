#!/usr/bin/env python3
"""gen_ja16map.py - Oracle 自身の変換結果から JA16 系の変換表 (odv_ja16map.c) を作る

使い方 (Logics/DumpParser/tools/ja16map で):
    python3 gen_ja16map.py --fwd '<dir>/map_{cs}.txt' --rev '<dir>/rev_{cs}.txt'
                           [--out ../../odv_ja16map.c] [--check]

入力 (リポジトリには入れない。取得方法は make_queries.py):
  map_{cs}.txt : "<入力HEX>=<AL32UTF8 HEX>"  (JA16 -> AL32UTF8、全コード)
  rev_{cs}.txt : "<UCS 4 桁 HEX>=<CS の HEX>" (AL32UTF8 -> JA16、順方向に現れる全文字)
  cs = JA16SJIS / JA16SJISTILDE / JA16EUC / JA16EUCTILDE / JA16SJISYEN / JA16EUCYEN

表の作り方:
  - 1 バイトの表 (256) と、2 バイト (SJIS: 先行 x 後続 188、EUC: JIS X 0208 94x94、
    JIS X 0212 94x94) の行の表。中身が同じ行 (未定義だけの行など) は 1 つにまとめる
  - TILDE 版・YEN 版は基の表 (JA16SJIS / JA16EUC) との違いだけを持つ (順方向・逆方向とも)。
    YEN 版の順方向の違いは 1 バイトの 5C (U+00A5) だけ。逆方向は U+00A5 -> 5C と、JA16SJISYEN では
    NEC 特殊文字の 13 字 (Ⅰ…Ⅹ № ℡ ㈱) を IBM 拡張のコード (FAxx) にする
  - 逆方向は、基の表のコードを UCS の順に並べた表 (二分探索)。1 つの文字に複数のコードが
    対応するもの (SJIS の NEC 選定 IBM 拡張と IBM 拡張など) は、Oracle が選ぶコードを採る
  - 復号の規則 (不正なバイト・末尾の欠けの扱い) は fix 時の実測 (query_edge.sql) による:
      SJIS: 先行バイトは後続によらず 2 バイト消費 (後続が 40-7E,80-FC 以外なら U+FFFD)、
            不正な単独バイトは U+FFFD で 1 バイト消費、末尾で後続が無ければ捨てる
      EUC : A1-FE / 8E は 2 バイト、8F は 3 バイト消費 (範囲外は U+FFFD)、
            不正な単独バイトは U+FFFD、末尾で足りなければ捨てる
    この規則と生成した表で入力の全行 (順方向・逆方向) が再現できることを確かめてから書き出す

--check: 生成した表を C (odv_charset.c 経由) で引き、入力の全行と一致することを確かめる (gcc)。
         環境変数 ODV_CHECK_WINE=1 なら mingw でも作って wine で同じ検査をする (Windows 版)。
"""
import argparse, atexit, shutil, os, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
DP = os.path.normpath(os.path.join(HERE, '..', '..'))
CSS = ['JA16SJIS', 'JA16SJISTILDE', 'JA16EUC', 'JA16EUCTILDE', 'JA16SJISYEN', 'JA16EUCYEN']
VARIANT = {cs: i for i, cs in enumerate(CSS)}
VARIANT_NAMES = ['ODV_JA16_SJIS', 'ODV_JA16_SJIS_TILDE', 'ODV_JA16_EUC', 'ODV_JA16_EUC_TILDE',
                 'ODV_JA16_SJIS_YEN', 'ODV_JA16_EUC_YEN']
BASE = {'JA16SJIS': 'JA16SJIS', 'JA16SJISTILDE': 'JA16SJIS', 'JA16SJISYEN': 'JA16SJIS',
        'JA16EUC': 'JA16EUC', 'JA16EUCTILDE': 'JA16EUC', 'JA16EUCYEN': 'JA16EUC'}
FFFD = 0xFFFD
LEAD = 0xFFFF
SJIS_TRAILS = list(range(0x40, 0x7F)) + list(range(0x80, 0xFD))
TRAIL_IDX = {t: i for i, t in enumerate(SJIS_TRAILS)}
# 逆方向で、順方向に現れない文字を Oracle が近い字に寄せるもの (query_reverse_edge.sql の実測)。
# 表せない文字の代替 (SJIS 8148 ／ EUC A1A9) と、不正な UTF-8 の '?' も同じ実測による
REV_EXTRA = {
    'JA16SJIS': {0xFF5E: 0x8160},
    'JA16SJISTILDE': {0x301C: 0x8160},
    'JA16EUCTILDE': {0x301C: 0xA1C1},
    'JA16SJISYEN': {0xFF5E: 0x8160, 0x005C: 0x5C},
    'JA16EUCYEN': {0x005C: 0x5C},
}
REPLACEMENT = {'JA16SJIS': 0x8148, 'JA16SJISTILDE': 0x8148, 'JA16EUC': 0xA1A9, 'JA16EUCTILDE': 0xA1A9,
               'JA16SJISYEN': 0x8148, 'JA16EUCYEN': 0xA1A9}


def load_fwd(path):
    m = {}
    for ln in open(path, encoding='ascii'):
        ln = ln.strip()
        if not ln:
            continue
        k, _, v = ln.partition('=')
        m[bytes.fromhex(k)] = bytes.fromhex(v).decode('utf-8')
    return m


def load_rev(path):
    m = {}
    for ln in open(path, encoding='ascii'):
        ln = ln.strip()
        if not ln:
            continue
        k, _, v = ln.partition('=')
        m[int(k, 16)] = int(v, 16)
    return m


def code_int(b):
    return int.from_bytes(b, 'big')


def code_bytes(c):
    return c.to_bytes((c.bit_length() + 7) // 8 or 1, 'big')


# --------------------------------------------------------------------------
# 表の組み立て
# --------------------------------------------------------------------------
def one_char(s, where):
    if len(s) != 1:
        sys.exit(f'{where}: expected one character, got {s!r}')
    return ord(s)


def build_base_sjis(fm):
    single = [0] * 256
    for b in range(256):
        s = fm[bytes([b])]
        single[b] = LEAD if s == '' else one_char(s, 'sjis %02X' % b)
    rows, lead_row = [], [0] * 256
    for b in range(256):
        if single[b] != LEAD:
            continue
        row = tuple(one_char(fm[bytes([b, t])], 'sjis %02X%02X' % (b, t)) for t in SJIS_TRAILS)
        if row not in rows:
            rows.append(row)
        lead_row[b] = rows.index(row)
    return single, lead_row, rows


def build_base_euc(fm):
    single = [0] * 256
    for b in range(256):
        s = fm[bytes([b])]
        single[b] = LEAD if s == '' else one_char(s, 'euc %02X' % b)
    for b in [0x8E, 0x8F] + list(range(0xA1, 0xFF)):
        if single[b] != LEAD:
            sys.exit('euc: %02X is expected to be a lead byte' % b)
    for t in range(0xA1, 0xE0):
        if one_char(fm[bytes([0x8E, t])], 'euc 8E%02X' % t) != 0xFF61 + t - 0xA1:
            sys.exit('euc: 8E%02X is not the half-width katakana rule' % t)

    def plane(prefix):
        rows, idx = [], []
        for r in range(94):
            row = tuple(one_char(fm[prefix + bytes([0xA1 + r, 0xA1 + c])],
                                 'euc %s%02X%02X' % (prefix.hex(), 0xA1 + r, 0xA1 + c))
                        for c in range(94))
            if row not in rows:
                rows.append(row)
            idx.append(rows.index(row))
        return idx, rows
    i0208, r0208 = plane(b'')
    i0212, r0212 = plane(b'\x8f')
    return single, i0208, r0208, i0212, r0212


class Model:
    """C 側 (odv_ja16.c) と同じ手順の復号・符号化。表を書き出す前に全入力で確かめる"""

    def __init__(self, sj, eu, fwd_ovr, rev_ovr, sjis_rev, euc_rev):
        self.sj_single, self.sj_lead_row, self.sj_rows = sj
        self.eu_single, self.i0208, self.r0208, self.i0212, self.r0212 = eu
        self.fwd_ovr, self.rev_ovr = fwd_ovr, rev_ovr
        self.sjis_rev, self.euc_rev = sjis_rev, euc_rev

    def base_sjis2(self, l, t):
        ti = TRAIL_IDX.get(t)
        return FFFD if ti is None else self.sj_rows[self.sj_lead_row[l]][ti]

    def base_euc(self, code):
        if code < 0x2400:
            r, c = divmod(code, 94)
            return self.r0212[self.i0212[r]][c]
        l, t = code >> 8, code & 0xFF
        return self.r0208[self.i0208[l - 0xA1]][t - 0xA1]

    def decode(self, cs, b):
        v = VARIANT[cs]
        out, i, n = [], 0, len(b)
        sjis = cs.startswith('JA16SJIS')
        single = self.sj_single if sjis else self.eu_single
        while i < n:
            u = single[b[i]]
            if u != LEAD:
                out.append(self.fwd_ovr.get((v, b[i]), u)); i += 1; continue
            if sjis or b[i] != 0x8F:
                if i + 1 >= n:
                    break
                code = (b[i] << 8) | b[i + 1]
                if sjis:
                    u = self.base_sjis2(b[i], b[i + 1])
                elif b[i] == 0x8E:
                    u = (0xFF61 + b[i + 1] - 0xA1) if 0xA1 <= b[i + 1] <= 0xDF else FFFD
                else:
                    u = self.base_euc(code) if 0xA1 <= b[i + 1] <= 0xFE else FFFD
                i += 2
            else:
                if i + 2 >= n:
                    break
                code = (0x8F << 16) | (b[i + 1] << 8) | b[i + 2]
                if 0xA1 <= b[i + 1] <= 0xFE and 0xA1 <= b[i + 2] <= 0xFE:
                    u = self.base_euc((b[i + 1] - 0xA1) * 94 + (b[i + 2] - 0xA1))
                else:
                    u = FFFD
                i += 3
            u = self.fwd_ovr.get((v, code), u)
            out.append(u)
        return ''.join(chr(x) for x in out)

    def encode_char(self, cs, u):
        v = VARIANT[cs]
        sjis = cs.startswith('JA16SJIS')
        if (v, u) in self.rev_ovr:
            return self.rev_ovr[(v, u)]
        if u < 0x80:
            return u
        if 0xFF61 <= u <= 0xFF9F:
            return (u - 0xFF61 + 0xA1) if sjis else (0x8E00 | (u - 0xFF61 + 0xA1))
        tab = self.sjis_rev if sjis else self.euc_rev
        f = (lambda c: self.base_sjis2(c >> 8, c & 0xFF)) if sjis else self.base_euc
        lo, hi = 0, len(tab) - 1
        while lo <= hi:
            mid = (lo + hi) // 2
            x = f(tab[mid])
            if x == u:
                c = tab[mid]
                if not sjis and c < 0x2400:
                    r, cc = divmod(c, 94)
                    return (0x8F << 16) | ((0xA1 + r) << 8) | (0xA1 + cc)
                return c
            if x < u:
                lo = mid + 1
            else:
                hi = mid - 1
        return REPLACEMENT[cs]


def build(fwd, rev):
    sj = build_base_sjis(fwd['JA16SJIS'])
    eu = build_base_euc(fwd['JA16EUC'])
    # 順方向の違い (TILDE 版・YEN 版)。1 バイトの違いを先に採り、2 バイト以上の入力の違いは
    # それで説明できないものだけを足す (YEN 版の 855C などは「不正な 85 + 5C」で 5C の違いそのもの)
    fwd_ovr = {}
    m0 = Model(sj, eu, fwd_ovr, {}, [], [])
    for cs in CSS:
        if BASE[cs] == cs:
            continue
        for k, s in sorted(fwd[cs].items(), key=lambda kv: (len(kv[0]), kv[0])):
            if m0.decode(cs, k) == s:
                continue
            if len(s) != 1:
                sys.exit(f'{cs} {k.hex()}: variant difference is not one character')
            fwd_ovr[(VARIANT[cs], code_int(k))] = ord(s)
    if len(fwd_ovr) > 8:
        sys.exit('unexpectedly many variant differences')
    # 逆方向の基の表: 基の文字セットの実測で選ばれたコード
    def rev_table(cs, sjis):
        lst = []
        for u, c in rev[cs].items():
            if u < 0x80 or 0xFF61 <= u <= 0xFF9F:
                continue
            if sjis:
                ent = c
            elif c > 0xFFFF:
                if c >> 16 != 0x8F:
                    sys.exit('euc rev: bad 3-byte code %X' % c)
                ent = ((c >> 8 & 0xFF) - 0xA1) * 94 + ((c & 0xFF) - 0xA1)
            else:
                ent = c
            lst.append((u, ent))
        lst.sort()
        return [e for _, e in lst]
    sjis_rev = rev_table('JA16SJIS', True)
    euc_rev = rev_table('JA16EUC', False)
    model = Model(sj, eu, fwd_ovr, {}, sjis_rev, euc_rev)
    # 逆方向の違い (TILDE 版の実測と、順方向に無い文字の寄せ先)
    rev_ovr = {}
    for cs in CSS:
        exp = dict(rev[cs])
        exp.update(REV_EXTRA.get(cs, {}))
        for u, c in exp.items():
            if model.encode_char(cs, u) != c:
                rev_ovr[(VARIANT[cs], u)] = c
    model.rev_ovr = rev_ovr
    if len(rev_ovr) > 32:
        sys.exit('unexpectedly many reverse overrides: %r' % rev_ovr)
    return model, sj, eu, fwd_ovr, rev_ovr, sjis_rev, euc_rev


def verify(model, fwd, rev):
    bad = 0
    for cs in CSS:
        for k, s in fwd[cs].items():
            if model.decode(cs, k) != s:
                bad += 1
                if bad < 10:
                    print(f'  decode {cs} {k.hex()}: {model.decode(cs, k)!r} != {s!r}')
        exp = dict(rev[cs])
        exp.update(REV_EXTRA.get(cs, {}))
        for u, c in exp.items():
            if model.encode_char(cs, u) != c:
                bad += 1
                if bad < 10:
                    print(f'  encode {cs} U+{u:04X}: {model.encode_char(cs, u):X} != {c:X}')
    return bad


# --------------------------------------------------------------------------
# C の出力
# --------------------------------------------------------------------------
def emit(out, sj, eu, fwd_ovr, rev_ovr, sjis_rev, euc_rev):
    sj_single, sj_lead_row, sj_rows = sj
    eu_single, i0208, r0208, i0212, r0212 = eu
    w = out.write

    def arr(vals, per, fmt):
        for i in range(0, len(vals), per):
            w('    ' + ', '.join(fmt % v for v in vals[i:i + per]) + ',\n')

    w('/*****************************************************************************\n')
    w('    Open DUMP Viewer for Oracle database\n\n')
    w('    odv_ja16map.c\n')
    w('    Oracle JA16SJIS / JA16SJISTILDE / JA16SJISYEN / JA16EUC / JA16EUCTILDE /\n')
    w('    JA16EUCYEN <-> Unicode tables.\n\n')
    w('    GENERATED FILE - DO NOT EDIT.\n')
    w('    Generated by Logics/DumpParser/tools/ja16map/gen_ja16map.py from Oracle\'s own\n')
    w('    conversions (UTL_RAW.CONVERT to / from AL32UTF8 for every code, identical\n')
    w('    on 11.2 / 21c / 23.26, the YEN variants on 21c / 23.26; see\n')
    w('    tools/ja16map/make_queries.py).\n')
    w(' *****************************************************************************/\n\n')
    w('#include "odv_ja16map.h"\n\n')
    w('const uint16_t odv_ja16_sjis_single[256] = {\n'); arr(sj_single, 12, '0x%04X'); w('};\n\n')
    w('const uint8_t odv_ja16_sjis_lead_row[256] = {\n'); arr(sj_lead_row, 16, '%d'); w('};\n\n')
    w(f'/* {len(sj_rows)} distinct rows; columns = trail bytes 40-7E, 80-FC */\n')
    w(f'const uint16_t odv_ja16_sjis_rows[{len(sj_rows)}][188] = {{\n')
    for row in sj_rows:
        w('  {\n'); arr(row, 12, '0x%04X'); w('  },\n')
    w('};\n\n')
    w('const uint16_t odv_ja16_euc_single[256] = {\n'); arr(eu_single, 12, '0x%04X'); w('};\n\n')
    for name, idx, rows in (('jis0208', i0208, r0208), ('jis0212', i0212, r0212)):
        w(f'const uint8_t odv_ja16_{name}_row[94] = {{\n'); arr(idx, 16, '%d'); w('};\n\n')
        w(f'/* {len(rows)} distinct rows; columns = cell 0xA1-0xFE */\n')
        w(f'const uint16_t odv_ja16_{name}_rows[{len(rows)}][94] = {{\n')
        for row in rows:
            w('  {\n'); arr(row, 12, '0x%04X'); w('  },\n')
        w('};\n\n')
    w(f'const uint16_t odv_ja16_sjis_rev[{len(sjis_rev)}] = {{\n'); arr(sjis_rev, 12, '0x%04X'); w('};\n')
    w(f'const int odv_ja16_sjis_rev_count = {len(sjis_rev)};\n\n')
    w(f'const uint16_t odv_ja16_euc_rev[{len(euc_rev)}] = {{\n'); arr(euc_rev, 12, '0x%04X'); w('};\n')
    w(f'const int odv_ja16_euc_rev_count = {len(euc_rev)};\n\n')
    names = VARIANT_NAMES
    w(f'const ODV_JA16_FWD_OVR odv_ja16_fwd_ovr[{max(1, len(fwd_ovr))}] = {{\n')
    for (v, code), u in sorted(fwd_ovr.items()):
        w('    { %s, 0x%X, 0x%04X },\n' % (names[v], code, u))
    if not fwd_ovr:
        w('    { 0, 0, 0 }\n')
    w('};\n')
    w(f'const int odv_ja16_fwd_ovr_count = {len(fwd_ovr)};\n\n')
    w(f'const ODV_JA16_REV_OVR odv_ja16_rev_ovr[{max(1, len(rev_ovr))}] = {{\n')
    for (v, u), code in sorted(rev_ovr.items()):
        w('    { %s, 0x%04X, 0x%X },\n' % (names[v], u, code))
    if not rev_ovr:
        w('    { 0, 0, 0 }\n')
    w('};\n')
    w(f'const int odv_ja16_rev_ovr_count = {len(rev_ovr)};\n\n')
    w('const uint32_t odv_ja16_replacement[%d] = { %s };\n' %
      (len(CSS), ', '.join('0x%X' % REPLACEMENT[cs] for cs in CSS)))
    return (len(sj_rows) * 188 + len(r0208) * 94 + len(r0212) * 94 + 512 + len(sjis_rev) +
            len(euc_rev)) * 2 + 256 + 188


# --------------------------------------------------------------------------
# --check
# --------------------------------------------------------------------------
def run_check(fwd_pat, rev_pat):
    tmp = tempfile.mkdtemp(prefix='odv_ja16chk_')
    atexit.register(shutil.rmtree, tmp, True)
    srcs = [os.path.join(HERE, 'ja16_selftest.c')] + [
        os.path.join(DP, f) for f in ('odv_charset.c', 'odv_ja16.c', 'odv_ja16map.c')]
    args = []
    for cs in CSS:
        args += [cs, fwd_pat.format(cs=cs), rev_pat.format(cs=cs)]
    ok = True
    exe = os.path.join(tmp, 'ja16_selftest')
    subprocess.run([os.environ.get('CC', 'gcc'), '-std=c11', '-O1', '-Wall', '-Wextra', '-DUTF8',
                    '-D_FILE_OFFSET_BITS=64', '-D_POSIX_C_SOURCE=200809L', '-I', DP, '-o', exe] + srcs,
                   check=True)
    r = subprocess.run([exe] + args)
    ok = ok and r.returncode == 0
    if os.environ.get('ODV_CHECK_WINE') == '1':
        wexe = os.path.join(tmp, 'ja16_selftest.exe')
        subprocess.run(['x86_64-w64-mingw32-gcc', '-std=c11', '-O2', '-Wall', '-Wextra', '-DWINDOWS',
                        '-DWIN32', '-DUTF8', '-DODV_DLL_MODE', '-D_CRT_SECURE_NO_WARNINGS',
                        '-D__USE_MINGW_ANSI_STDIO=1', '-I', DP, '-o', wexe] + srcs + ['-static'],
                       check=True)
        wargs = []
        for a in args:
            if os.path.exists(a):
                a = subprocess.run(['winepath', '-w', a], capture_output=True, text=True).stdout.strip()
            wargs.append(a)
        r = subprocess.run(['wine', wexe] + wargs, env=dict(os.environ, WINEDEBUG='-all'))
        ok = ok and r.returncode == 0
    return ok


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--fwd', required=True, help="path pattern with {cs}, e.g. dir/map_21_{cs}.txt")
    ap.add_argument('--rev', required=True, help="path pattern with {cs}, e.g. dir/rev_21_{cs}.txt")
    ap.add_argument('--out', default=os.path.join(DP, 'odv_ja16map.c'))
    ap.add_argument('--check', action='store_true')
    a = ap.parse_args()
    fwd = {cs: load_fwd(a.fwd.format(cs=cs)) for cs in CSS}
    rev = {cs: load_rev(a.rev.format(cs=cs)) for cs in CSS}
    model, sj, eu, fwd_ovr, rev_ovr, sjis_rev, euc_rev = build(fwd, rev)
    bad = verify(model, fwd, rev)
    if bad:
        sys.exit(f'model does not reproduce the measured conversions ({bad} mismatches)')
    with open(a.out, 'w', encoding='utf-8', newline='\n') as f:
        size = emit(f, sj, eu, fwd_ovr, rev_ovr, sjis_rev, euc_rev)
    nf = sum(len(v) for v in fwd.values())
    nr = sum(len(v) for v in rev.values())
    print(f'{a.out}: {len(sj[2])} SJIS rows, {len(eu[2])}+{len(eu[4])} EUC rows, '
          f'{len(sjis_rev)}+{len(euc_rev)} reverse entries, {len(fwd_ovr)} forward / '
          f'{len(rev_ovr)} reverse variant overrides, ~{size} bytes; model reproduces '
          f'{nf} forward + {nr} reverse measurements')
    if a.check and not run_check(a.fwd, a.rev):
        sys.exit(1)


if __name__ == '__main__':
    main()
