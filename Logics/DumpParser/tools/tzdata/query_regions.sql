-- Oracle の地域名と地域 ID を取る (oracle_tz_regions.txt の元データ)
--   sqlplus で各版の DB に流し、R| の行の dump(...) の 17・18 バイト目 (リトルエンディアン) を地域 ID とする。
--   結果の和集合を oracle_tz_regions.txt に書く (版の間で同じ名前に別の ID が付かないことも確かめる)。
set pagesize 0 linesize 400 feedback off heading off trimspool on
select 'TZVER='||version from v$timezone_file;
select 'R|'||tzname||'|'||dump(to_timestamp_tz('2000-01-01 00:00:00 '||tzname,'YYYY-MM-DD HH24:MI:SS TZR'),16)
  from (select distinct tzname from v$timezone_names) order by 1;
