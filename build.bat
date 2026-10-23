@echo off
rem Builds wheelie_assist64.dlo / .dli (and the offline dev tools, if present) into build\
setlocal
set VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat
if not defined VSCMD_VER call "%VCVARS%" >nul || exit /b 1

cd /d "%~dp0"
if not exist build mkdir build
rem /Brepro: deterministic output, so a build from this source matches the release byte for byte
set CFLAGS=/nologo /O2 /W4 /MT /Brepro /Iinclude /D_CRT_SECURE_NO_WARNINGS /wd4100

cl %CFLAGS% /LD src\telemetry_plugin.c src\shared.c src\config.c src\diag.c ^
	/Fobuild\ /Fe:build\wheelie_assist64.dlo /link /Brepro /NOEXP /NOIMPLIB || exit /b 1

cl %CFLAGS% /LD src\input_plugin.c src\bindings.c src\shared.c src\config.c src\assist.c src\diag.c ^
	/Fobuild\ /Fe:build\wheelie_assist64.dli /link /Brepro /NOEXP /NOIMPLIB dinput8.lib dxguid.lib shell32.lib user32.lib || exit /b 1

rem offline dev tools, only when tools\ is present
if exist tools\sim.c (
	cl %CFLAGS% /Isrc tools\sim.c src\assist.c /Fobuild\ /Fe:build\sim.exe || exit /b 1
	cl %CFLAGS% /Isrc tools\harness.c src\shared.c /Fobuild\ /Fe:build\harness.exe /link dinput8.lib dxguid.lib || exit /b 1
)

echo Build OK
