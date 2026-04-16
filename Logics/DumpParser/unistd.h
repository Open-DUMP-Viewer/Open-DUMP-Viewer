/*****************************************************************************
    Open DUMP Viewer for Oracle database

    unistd.h
    POSIX compatibility stub for Windows (MSVC)

    Copyright (C) 2026 YANAI Taketo
 *****************************************************************************/

#ifndef ODV_UNISTD_H
#define ODV_UNISTD_H

#ifdef WINDOWS
#include <windows.h>
#include <io.h>

#define sleep(s)   Sleep((s) * 1000)
#define usleep(us) Sleep((us) / 1000)

#ifndef F_OK
#define F_OK 0
#endif

#ifndef R_OK
#define R_OK 4
#endif

typedef int ssize_t;

#endif /* WINDOWS */
#endif /* ODV_UNISTD_H */
