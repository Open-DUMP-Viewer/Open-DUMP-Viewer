@echo off
REM ========================================================
REM  Open DUMP Viewer for Oracle database - CHM Help Builder
REM
REM  Prerequisites:
REM    HTML Help Workshop (hhc.exe) must be installed.
REM    Download: https://www.microsoft.com/en-us/download/details.aspx?id=21138
REM
REM  Usage:
REM    cd Help
REM    _build_chm.cmd
REM ========================================================

set HHC_PATH=
if exist "C:\Program Files (x86)\HTML Help Workshop\hhc.exe" (
    set "HHC_PATH=C:\Program Files (x86)\HTML Help Workshop\hhc.exe"
) else if exist "C:\Program Files\HTML Help Workshop\hhc.exe" (
    set "HHC_PATH=C:\Program Files\HTML Help Workshop\hhc.exe"
)

if "%HHC_PATH%"=="" (
    echo ERROR: HTML Help Workshop (hhc.exe) not found.
    echo Please install from: https://www.microsoft.com/en-us/download/details.aspx?id=21138
    exit /b 1
)

echo Building OpenDumpViewer.chm ...
"%HHC_PATH%" OpenDumpViewer.hhp

REM hhc.exe returns 1 on success (non-zero exit code)
if exist OpenDumpViewer.chm (
    echo BUILD SUCCEEDED: OpenDumpViewer.chm
    exit /b 0
) else (
    echo BUILD FAILED
    exit /b 1
)
