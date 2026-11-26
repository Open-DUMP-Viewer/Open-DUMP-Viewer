#!/usr/bin/env python3
"""gen_sbmap.py - Oracle 自身の変換結果から 1 バイト系文字セットの表 (odv_sbmap.c) を作る

使い方:
    python3 gen_sbmap.py --map sbmap_23.txt [--same sbmap_21.txt sbmap_11.txt ...] [--check]

  --map    query_sbmap.sql の出力のうち、1 文字の最大バイト数が取れた版 (21c 以降) のもの。
           1 バイト系の一覧・各バイトの変換・逆変換・代替文字をここから取る
  --same   ほかの版の出力。--map と共通の 1 バイト系文字セットについて、全バイトの変換・逆変換・代替文字が
           一致することを確かめる (一致しなければ止まる。最大バイト数の取れない 11.2 では、--map で 1 バイト系の
           ものだけを比べる)
  --out    生成物 (既定: Logics/DumpParser/odv_sbmap.c)
  --check  生成した表を odv_charset.c の convert_charset() 経由で引き、--map の全バイトの変換と逆変換を
           再現できることを確かめる (gcc が要る。環境変数 ODV_CHECK_WINE=1 なら mingw でも作って wine で
           同じ検査をする: Windows 版)

取得 (sqlplus、どの DB 文字セットの DB でも可。query_sbmap.sql の先頭を参照):
    sqlplus -S system/...@db @query_sbmap.sql > sbmap_<版>.txt
  2026-11 に取り、11.2 / 21c / 23.26 で、それぞれが持つ 1 バイト系文字セットの結果がバイト一致した
  (21c は 201 種、23.26 は IBM の EBCDIC 21 種を加えた 222 種。11.2 XE は文字セットを 56 種しか持たず、
  うち 1 バイト系は 40 種)。取得した sbmap_*.txt はリポジトリに入れない (生成した表と取得方法だけを置く)。

生成物の形 (odv_sbmap.h):
  文字セットごとに 00-7F と 80-FF の Unicode (BMP) を持つ。00-7F が US-ASCII と同じなら持たない。
  同じ内容の半分は共有する。逆変換は「その文字に読むバイトのうち最も小さいバイト」に戻し、Oracle が
  それと違うバイトに戻す文字だけを例外として持つ。U+FFFD と表せない文字は Oracle の代替文字
  (多くは ¿ か ?) になる。
"""
import argparse, atexit, os, shutil, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
DP = os.path.normpath(os.path.join(HERE, '..', '..'))
FFFD = 0xFFFD


def utf8_one(h, where):
    """AL32UTF8 の HEX を 1 文字のコードポイントにする。Oracle が返す冗長な形 (例: C1 A0 = U+0060) も読む。"""
    b = bytes.fromhex(h)
    if not b:
        raise ValueError(f'{where}: empty result')
    c0 = b[0]
    if c0 < 0x80:
        n, cp = 1, c0
    elif 0xC0 <= c0 < 0xE0:
        n, cp = 2, c0 & 0x1F
    elif 0xE0 <= c0 < 0xF0:
        n, cp = 3, c0 & 0x0F
    elif 0xF0 <= c0 < 0xF8:
        n, cp = 4, c0 & 0x07
    else:
        raise ValueError(f'{where}: bad lead byte {h}')
    if len(b) != n:
        raise ValueError(f'{where}: {h} is not one character')
    for x in b[1:]:
        if x & 0xC0 != 0x80:
            raise ValueError(f'{where}: bad continuation {h}')
        cp = (cp << 6) | (x & 0x3F)
    if cp > 0xFFFF:
        raise ValueError(f'{where}: U+{cp:X} is outside the BMP')
    return cp


def load(path):
    """query_sbmap.sql の出力を {id: {name, dep, width, fwd[256], back[256], repl}} にする。"""
    cs = {}
    cur = None
    with open(path, encoding='ascii', errors='replace') as f:
        for ln, line in enumerate(f, 1):
            line = line.strip()
            if not line or line.startswith('SKIP '):
                continue
            p = line.split()
            if p[0] == 'CS':
                cur = int(p[1])
                cs[cur] = {'name': p[2], 'dep': p[3], 'width': p[4] if len(p) > 4 else '?',
                           'fwd': [None] * 256, 'back': [None] * 256, 'repl': None}
            elif p[0] == 'R':
                cs[int(p[1])]['repl'] = p[2]
            elif '=' in line and p[0].isdigit():
                cid = int(p[0])
                code, res = p[1].split('=')
                e = cs[cid]
                e['fwd'][int(code, 16)] = res
                e['back'][int(code, 16)] = p[2] if len(p) > 2 else None
            else:
                raise ValueError(f'{path}:{ln}: unexpected line {line!r}')
    return cs


def model(cs):
    """1 バイト系 (最大バイト数 1) の文字セットを、コードポイントの表・逆変換の例外・代替文字にする。"""
    out = {}
    for cid, e in sorted(cs.items()):
        if e['width'] != '1':
            continue
        where = f"{e['name']}"
        cps = [utf8_one(e['fwd'][b], f'{where} {b:02X}') for b in range(256)]
        repl = e['repl']
        if repl is None or len(repl) != 2:
            raise ValueError(f'{where}: replacement {repl!r} is not one byte')
        repl = int(repl, 16)
        # 逆変換: 文字ごとに Oracle が戻すバイトを集める。Oracle が冗長な形で返したもの
        # (WE8NEXTSTEP C1 = C1 A0 など) は、戻すと不正な並びとして扱われるので使わない
        # (同じ文字は正規の形の別のバイトが持つ)
        owners = {}
        for b, cp in enumerate(cps):
            owners.setdefault(cp, []).append(b)
        back_of = {}
        for b, cp in enumerate(cps):
            if bytes.fromhex(e['fwd'][b]) != chr(cp).encode('utf-8'):
                continue
            back = e['back'][b]
            if back is None or back.startswith('ERR') or len(back) != 2:
                raise ValueError(f'{where} {b:02X}: reverse result {back!r} is not one byte')
            back = int(back, 16)
            if back_of.get(cp, back) != back:
                raise ValueError(f'{where}: U+{cp:04X} goes back to {back_of[cp]:02X} and {back:02X}')
            back_of[cp] = back
        # 逆変換の規則: その文字に読むバイトのうち最も小さいバイトに戻す (7 ビットの各国語文字セットでは
        # 80-FF が 00-7F と同じ文字になり、Oracle は 00-7F に戻す)。U+FFFD は代替文字に戻す。
        # 規則と違うものだけを例外として持つ (AR8XBASIC の U+0649 は DA で読むが D0 に戻る、など)
        rev_exc = {}
        for cp, back in back_of.items():
            if cp == FFFD:
                if back != repl:
                    rev_exc[cp] = back
                continue
            if back != min(owners[cp]):
                rev_exc[cp] = back
        out[cid] = {'name': e['name'], 'cps': cps, 'rev_exc': rev_exc, 'repl': repl}
    return out


def compare(base_raw, base, other_raw, label):
    """--same: base で 1 バイト系のものについて、other の結果が一致するかを数える。"""
    bad = 0
    common = 0
    for cid, m in base.items():
        if cid not in other_raw:
            continue
        o = other_raw[cid]
        b = base_raw[cid]
        common += 1
        for i in range(256):
            if o['fwd'][i] != b['fwd'][i] or o['back'][i] != b['back'][i]:
                bad += 1
                if bad <= 10:
                    print(f"  {label}: {m['name']} {i:02X}: {o['fwd'][i]} {o['back'][i]} != "
                          f"{b['fwd'][i]} {b['back'][i]}", file=sys.stderr)
        if o['repl'] != b['repl']:
            bad += 1
            print(f"  {label}: {m['name']} replacement {o['repl']} != {b['repl']}", file=sys.stderr)
    return common, bad


def emit(f, mdl):
    w = f.write
    w('/*****************************************************************************\n')
    w('    Open DUMP Viewer for Oracle database\n\n')
    w('    odv_sbmap.c\n')
    w('    Oracle single-byte character sets <-> Unicode tables.\n\n')
    w('    GENERATED FILE - DO NOT EDIT.\n')
    w('    Generated by Logics/DumpParser/tools/sbmap/gen_sbmap.py from Oracle\'s own\n')
    w('    conversions (UTL_RAW.CONVERT to / from AL32UTF8 for every byte of every\n')
    w('    character set whose maximum character size is 1; identical on 11.2 / 21c /\n')
    w('    23.26 for the character sets each of them has; see\n')
    w('    tools/sbmap/query_sbmap.sql).\n')
    w(' *****************************************************************************/\n\n')
    w('#include "odv_sbmap.h"\n\n')
    halves = {}
    order = []

    def half(vals):
        key = tuple(vals)
        if key not in halves:
            halves[key] = f'H{len(halves)}'
            order.append(key)
        return halves[key]

    rows = []
    for cid, m in sorted(mdl.items()):
        lo = m['cps'][:128]
        lo_name = 'NULL' if lo == list(range(128)) else half(lo)
        hi_name = half(m['cps'][128:])
        rows.append((cid, m, lo_name, hi_name))
    for key in order:
        w(f'static const uint16_t {halves[key]}[128] = {{\n')
        for i in range(0, 128, 8):
            w('    ' + ', '.join('0x%04X' % v for v in key[i:i + 8]) + ',\n')
        w('};\n\n')
    nexc = 0
    for cid, m, lo_name, hi_name in rows:
        if m['rev_exc']:
            w(f'static const ODV_SBMAP_REV R{cid}[] = {{\n')
            for cp, b in sorted(m['rev_exc'].items()):
                w('    { 0x%04X, 0x%02X },\n' % (cp, b))
            w('};\n\n')
            nexc += len(m['rev_exc'])
    w(f'const ODV_SBMAP_CHARSET odv_sbmap_charsets[{len(rows)}] = {{\n')
    for cid, m, lo_name, hi_name in rows:
        rev = f'R{cid}' if m['rev_exc'] else 'NULL'
        w('    { %4d, %-18s %-5s, %-5s, %-6s, %2d, 0x%02X },\n' % (
            cid, '"%s",' % m['name'], lo_name, hi_name, rev, len(m['rev_exc']), m['repl']))
    w('};\n')
    w(f'const int odv_sbmap_charset_count = {len(rows)};\n')
    return len(order), nexc


def run_check(map_path):
    tmp = tempfile.mkdtemp(prefix='odv_sbchk_')
    atexit.register(shutil.rmtree, tmp, True)
    srcs = [os.path.join(HERE, 'sb_selftest.c')] + [
        os.path.join(DP, f) for f in ('odv_charset.c', 'odv_sb.c', 'odv_sbmap.c', 'odv_ja16.c', 'odv_ja16map.c')]
    ok = True
    exe = os.path.join(tmp, 'sb_selftest')
    subprocess.run([os.environ.get('CC', 'gcc'), '-std=c11', '-O1', '-Wall', '-Wextra', '-DUTF8',
                    '-D_FILE_OFFSET_BITS=64', '-D_POSIX_C_SOURCE=200809L', '-I', DP, '-o', exe] + srcs,
                   check=True)
    r = subprocess.run([exe, map_path])
    ok = ok and r.returncode == 0
    if os.environ.get('ODV_CHECK_WINE') == '1':
        wexe = os.path.join(tmp, 'sb_selftest.exe')
        subprocess.run(['x86_64-w64-mingw32-gcc', '-std=c11', '-O2', '-Wall', '-Wextra', '-DWINDOWS',
                        '-DWIN32', '-DUTF8', '-DODV_DLL_MODE', '-D_CRT_SECURE_NO_WARNINGS',
                        '-D__USE_MINGW_ANSI_STDIO=1', '-I', DP, '-o', wexe] + srcs + ['-static'],
                       check=True)
        a = subprocess.run(['winepath', '-w', map_path], capture_output=True, text=True).stdout.strip()
        r = subprocess.run(['wine', wexe, a], env=dict(os.environ, WINEDEBUG='-all'))
        ok = ok and r.returncode == 0
    return ok


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--map', required=True)
    ap.add_argument('--same', nargs='*', default=[])
    ap.add_argument('--out', default=os.path.join(DP, 'odv_sbmap.c'))
    ap.add_argument('--check', action='store_true')
    a = ap.parse_args()
    base_raw = load(a.map)
    mdl = model(base_raw)
    if not mdl:
        sys.exit(f'{a.map}: no character set with maximum character size 1')
    for p in a.same:
        common, bad = compare(base_raw, mdl, load(p), os.path.basename(p))
        print(f'{p}: {common} single-byte character sets compared, {bad} differences')
        if bad:
            sys.exit(f'{p} differs from {a.map}')
    with open(a.out, 'w', encoding='utf-8', newline='\n') as f:
        nhalf, nexc = emit(f, mdl)
    print(f'{a.out}: {len(mdl)} character sets, {nhalf} distinct 128-entry halves, '
          f'{nexc} reverse exceptions')
    if a.check and not run_check(a.map):
        sys.exit(1)


if __name__ == '__main__':
    main()
