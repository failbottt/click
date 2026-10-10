@echo off
setlocal enabledelayedexpansion

if exist build rmdir /s /q "./build"

:: --- unpack arguments -------------------------------------------------------
REM for %%a in (%*) do set "%%a=2"
REM if not "%msvc%"=="1" if not "%clang%"=="1" set msvc=1
REM if not "%release%"=="1" set debug=1
REM if "%debug%"=="1"   set release=0 && echo [debug mode]
REM if "%release%"=="1" set debug=0 && echo [release mode]
REM if "%msvc%"=="1"    set clang=0 && echo [msvc compile]
REM if "%clang%"=="1"   set msvc=0 && echo [clang compile]

set cl_common=     /I..\src\ /nologo /FC /Z7
set cl_debug=      call cl /Od /Ob1 /DBUILD_DEBUG=1 /W2 /DBUILD_INTERNAL=1 %cl_common%

:: ------ prep build directory ------
if not exist build mkdir build

:: ------ build ------
pushd build
echo [building...] && %cl_debug% ../main.c /link user32.lib gdi32.lib winmm.lib
popd
