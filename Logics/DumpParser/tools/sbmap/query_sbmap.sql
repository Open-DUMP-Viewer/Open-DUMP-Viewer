-- query_sbmap.sql - 1 バイト系の文字セットの一覧と、各バイトの Oracle 自身の変換を取る (gen_sbmap.py の入力)
--
-- 取得 (sqlplus、どの DB 文字セットの DB でも可):
--     sqlplus -S system/...@db @query_sbmap.sql > sbmap_<版>.txt
--
-- V$NLS_VALID_VALUES の各文字セットについて、1 文字の最大バイト数を UTL_I18N.GET_MAX_CHARACTER_SIZE で取り、
-- 1 のもの (1 バイト系) について次を出す。
--     CS <id> <name> <isdeprecated> <最大バイト数>
--     <id> <入力 HEX>=<AL32UTF8 HEX> <戻した HEX>
--     R <id> <代替 HEX>
--   2 行目: 00-FF の 256 バイトを 1 バイトずつ UTL_RAW.CONVERT(code, 'AMERICAN_AMERICA.AL32UTF8',
--           'AMERICAN_AMERICA.<CS>') で変換し、その結果を逆向きに <CS> へ戻したもの。
--   3 行目: <CS> で表せない文字 (U+4E00) を <CS> へ変換した結果 (Oracle の代替文字)。
-- GET_MAX_CHARACTER_SIZE の無い版 (11.2) では最大バイト数を "?" とし、すべての文字セットについて出す
-- (1 バイト系かどうかは、関数のある版の結果で gen_sbmap.py が決める)。
-- 最大バイト数を取れない文字セットは "SKIP <name> <SQLCODE>"、変換できないものは "ERR<SQLCODE>"。
SET SERVEROUTPUT ON SIZE UNLIMITED FORMAT TRUNCATED
SET FEEDBACK OFF HEADING OFF PAGESIZE 0 LINESIZE 200 TRIMSPOOL ON TRIMOUT ON VERIFY OFF ECHO OFF
DECLARE
    l_max    PLS_INTEGER;
    l_width  VARCHAR2(8);
    l_hex    VARCHAR2(2);
    l_out    RAW(16);
    l_back   VARCHAR2(40);
    l_has_fn BOOLEAN := TRUE;
BEGIN
    BEGIN
        EXECUTE IMMEDIATE 'BEGIN :m := UTL_I18N.GET_MAX_CHARACTER_SIZE(''US7ASCII''); END;' USING OUT l_max;
    EXCEPTION WHEN OTHERS THEN
        l_has_fn := FALSE;
    END;
    FOR c IN (SELECT value AS name, NLS_CHARSET_ID(value) AS id, isdeprecated
                FROM v$nls_valid_values
               WHERE parameter = 'CHARACTERSET'
               ORDER BY NLS_CHARSET_ID(value), value) LOOP
        l_width := '?';
        IF l_has_fn THEN
            BEGIN
                EXECUTE IMMEDIATE 'BEGIN :m := UTL_I18N.GET_MAX_CHARACTER_SIZE(:n); END;'
                    USING OUT l_max, IN c.name;
                l_width := TO_CHAR(l_max);
            EXCEPTION WHEN OTHERS THEN
                DBMS_OUTPUT.PUT_LINE('SKIP ' || c.name || ' ' || SQLCODE);
                l_width := NULL;
            END;
        END IF;
        IF (l_width = '1' OR l_width = '?') AND c.id IS NOT NULL THEN
            DBMS_OUTPUT.PUT_LINE('CS ' || c.id || ' ' || c.name || ' ' || c.isdeprecated || ' ' || l_width);
            FOR b IN 0 .. 255 LOOP
                l_hex := LPAD(TRIM(TO_CHAR(b, 'XX')), 2, '0');
                BEGIN
                    l_out := UTL_RAW.CONVERT(HEXTORAW(l_hex), 'AMERICAN_AMERICA.AL32UTF8',
                                             'AMERICAN_AMERICA.' || c.name);
                    BEGIN
                        l_back := RAWTOHEX(UTL_RAW.CONVERT(l_out, 'AMERICAN_AMERICA.' || c.name,
                                                           'AMERICAN_AMERICA.AL32UTF8'));
                    EXCEPTION WHEN OTHERS THEN
                        l_back := 'ERR' || SQLCODE;
                    END;
                    DBMS_OUTPUT.PUT_LINE(c.id || ' ' || l_hex || '=' || RAWTOHEX(l_out) || ' ' || l_back);
                EXCEPTION WHEN OTHERS THEN
                    DBMS_OUTPUT.PUT_LINE(c.id || ' ' || l_hex || '=ERR' || SQLCODE);
                END;
            END LOOP;
            BEGIN
                l_back := RAWTOHEX(UTL_RAW.CONVERT(HEXTORAW('E4B880'), 'AMERICAN_AMERICA.' || c.name,
                                                   'AMERICAN_AMERICA.AL32UTF8'));
            EXCEPTION WHEN OTHERS THEN
                l_back := 'ERR' || SQLCODE;
            END;
            DBMS_OUTPUT.PUT_LINE('R ' || c.id || ' ' || l_back);
        END IF;
    END LOOP;
END;
/
EXIT
