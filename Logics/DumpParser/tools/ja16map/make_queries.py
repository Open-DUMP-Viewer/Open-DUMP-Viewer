#!/usr/bin/env python3
"""make_queries.py - JA16 系の Oracle 自身の変換を取る SQL を作る (gen_ja16map.py の入力の取得用)

使い方:
    python3 make_queries.py forward <CS>            > q_<CS>.sql
    python3 make_queries.py reverse <CS> <map_<CS>.txt>  > qrev_<CS>.sql
  CS = JA16SJIS | JA16SJISTILDE | JA16SJISYEN | JA16EUC | JA16EUCTILDE | JA16EUCYEN

  forward: 全コード (1 バイト 00-FF と、SJIS は先行 81-9F,E0-FC x 後続 40-7E,80-FC、
           EUC は A1-FE x A1-FE、8E A1-DF、8F A1-FE A1-FE) を
           UTL_RAW.CONVERT(code, 'AMERICAN_AMERICA.AL32UTF8', 'AMERICAN_AMERICA.<CS>') で変換し、
           "<入力HEX>=<AL32UTF8 HEX>" を 1 行ずつ出す SQL。結果を map_<CS>.txt とする
  reverse: forward の結果に 1 文字として現れる文字 (U+FFFD を除く) を逆向きに変換し、
           "<UTF-16 の 4 桁 HEX>=<CS の HEX>" を出す SQL。結果を rev_<CS>.txt とする

取得 (sqlplus、AL32UTF8 の DB で可):
    sqlplus -S system/...@db @q_<CS>.sql   > map_<CS>.txt
    sqlplus -S system/...@db @qrev_<CS>.sql > rev_<CS>.txt
  2026-10 に取り、JA16SJIS / JA16SJISTILDE / JA16EUC / JA16EUCTILDE は 11.2 / 21c / 23.26、
  JA16SJISYEN / JA16EUCYEN は 21c / 23.26 で、それぞれ版の間でバイト一致した
  (11.2 XE はこの 2 つを持たない: NLS_CHARSET_ID が NULL、UTL_RAW.CONVERT は ORA-06502)。
  変換の端の挙動 (不正な並び・末尾の欠け) は query_edge.sql で取り、ja16_selftest.c に埋め込んである。
"""
import sys


def codes(cs):
    out = [bytes([b]) for b in range(0x100)]
    if cs.startswith('JA16SJIS'):
        leads = list(range(0x81, 0xA0)) + list(range(0xE0, 0xFD))
        trails = list(range(0x40, 0x7F)) + list(range(0x80, 0xFD))
        out += [bytes([l, t]) for l in leads for t in trails]
    else:
        out += [bytes([l, t]) for l in range(0xA1, 0xFF) for t in range(0xA1, 0xFF)]
        out += [bytes([0x8E, t]) for t in range(0xA1, 0xE0)]
        out += [bytes([0x8F, l, t]) for l in range(0xA1, 0xFF) for t in range(0xA1, 0xFF)]
    return out


def emit(selects):
    print("set pagesize 0 linesize 100 feedback off heading off trimspool on termout on")
    for i in range(0, len(selects), 500):
        print("\nunion all\n".join(selects[i:i + 500]) + ";")


def main():
    mode, cs = sys.argv[1], sys.argv[2]
    if cs not in ('JA16SJIS', 'JA16SJISTILDE', 'JA16SJISYEN', 'JA16EUC', 'JA16EUCTILDE', 'JA16EUCYEN'):
        sys.exit('unknown charset ' + cs)
    if mode == 'forward':
        emit([f"select '{c.hex().upper()}='||rawtohex(utl_raw.convert(hextoraw('{c.hex().upper()}'),"
              f"'AMERICAN_AMERICA.AL32UTF8','AMERICAN_AMERICA.{cs}')) from dual" for c in codes(cs)])
    elif mode == 'reverse':
        chars = set()
        for ln in open(sys.argv[3], encoding='ascii'):
            k, _, v = ln.strip().partition('=')
            if not v:
                continue
            s = bytes.fromhex(v).decode('utf-8')
            if len(s) == 1 and s != '�':
                chars.add(s)
        emit([f"select '{ord(c):04X}='||rawtohex(utl_raw.convert(hextoraw('{c.encode('utf-8').hex().upper()}'),"
              f"'AMERICAN_AMERICA.{cs}','AMERICAN_AMERICA.AL32UTF8')) from dual" for c in sorted(chars)])
    else:
        sys.exit('mode must be forward or reverse')


if __name__ == '__main__':
    main()
