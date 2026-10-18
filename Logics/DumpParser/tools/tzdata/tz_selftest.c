/*****************************************************************************
    Open DUMP Viewer for Oracle database

    tz_selftest.c
    Self-check helper for gen_tzdata.py --check (not part of the DLL).

    Reads "<oracle region id> <UTC seconds since 1970>" lines from stdin and
    prints the UTC offset in seconds that odv_tz.c computes from the generated
    tables (odv_tzdata.c), one per line ("ERR" for an unknown region).
    gen_tzdata.py compares the output with Python's zoneinfo.

    Copyright (C) 2026 YANAI Taketo
 *****************************************************************************/

#include "odv_types.h"

int main(void)
{
    int id;
    long long t;
    while (scanf("%d %lld", &id, &t) == 2) {
        int32_t off;
        if (odv_tz_region_offset(id, (int64_t)t, &off) != ODV_OK)
            printf("ERR\n");
        else
            printf("%d\n", (int)off);
    }
    return 0;
}
