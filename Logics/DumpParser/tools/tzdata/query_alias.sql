-- IANA tzdata に同じ名前が無い Oracle の地域名 (oracle_tz_regions.txt の 3 列目) の対応を確かめる
--   各時点で、Oracle の地域名と対応先の IANA 名の時差・略称 (TZH:TZM TZD) が一致することを見る。
--   11.2 / 21c / 23.26 の 14 時点すべてで一致した (2026-10 実測)。
set pagesize 0 linesize 300 feedback off heading off
select a.n||'|'||a.t||'|'||to_char(d.dt,'YYYY-MM-DD')||'|'||
       to_char(from_tz(d.dt,'UTC') at time zone a.n,'TZH:TZM TZD')||'|'||
       to_char(from_tz(d.dt,'UTC') at time zone a.t,'TZH:TZM TZD') line
  from (select 'PST' n, 'America/Los_Angeles' t from dual union all
        select 'CST', 'America/Chicago' from dual union all
        select 'Canada/East-Saskatchewan', 'America/Regina' from dual union all
        select 'US/Pacific-New', 'America/Los_Angeles' from dual) a,
       (select timestamp '1920-07-01 12:00:00' dt from dual union all
        select timestamp '1945-08-20 12:00:00' from dual union all
        select timestamp '1950-07-01 12:00:00' from dual union all
        select timestamp '1960-01-15 12:00:00' from dual union all
        select timestamp '1966-07-01 12:00:00' from dual union all
        select timestamp '1974-01-15 12:00:00' from dual union all
        select timestamp '1987-04-10 12:00:00' from dual union all
        select timestamp '2000-07-01 12:00:00' from dual union all
        select timestamp '2006-04-10 12:00:00' from dual union all
        select timestamp '2007-03-20 12:00:00' from dual union all
        select timestamp '2010-11-05 12:00:00' from dual union all
        select timestamp '2024-07-01 12:00:00' from dual union all
        select timestamp '2024-12-01 12:00:00' from dual union all
        select timestamp '2037-07-01 12:00:00' from dual) d
 order by a.n, d.dt;
