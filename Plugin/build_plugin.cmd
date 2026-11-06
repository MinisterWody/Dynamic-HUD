@echo off
setlocal
REM Dynamic HUD 1.0 production build
REM Target: Fallout 4 OG/NG/AE desktop runtime families with matching F4SE and Address Library.
REM Build from an x64 environment with clang-cl and lld-link available.
REM No CRT, STL, CommonLib, F4SE SDK, Windows SDK import libraries, or Address Library files are linked.
REM /W4 /WX makes compiler warnings build failures.

pushd "%~dp0"
if errorlevel 1 exit /b 1

where clang-cl >nul 2>nul || (echo ERROR: clang-cl not found on PATH & popd & exit /b 1)
where lld-link >nul 2>nul || (echo ERROR: lld-link not found on PATH & popd & exit /b 1)

if not exist build mkdir build
if not exist Built mkdir Built

lld-link /lib /machine:x64 /def:BuildSupport\kernel32.def /out:build\kernel32.lib
if errorlevel 1 goto :fail
lld-link /lib /machine:x64 /def:BuildSupport\user32.def /out:build\user32.lib
if errorlevel 1 goto :fail
lld-link /lib /machine:x64 /def:BuildSupport\shell32.def /out:build\shell32.lib
if errorlevel 1 goto :fail

clang-cl /nologo /c /O2 /GS- /Zl /EHsc- /GR- /W4 /WX /Brepro /Fo:build\DynamicHUD.obj src\DynamicHUD.cpp
if errorlevel 1 goto :fail

lld-link /dll /machine:x64 /nodefaultlib /entry:DllMain /subsystem:windows /timestamp:0 /def:BuildSupport\DynamicHUD.def /implib:build\DynamicHUD.lib /out:Built\DynamicHUD.dll build\DynamicHUD.obj build\kernel32.lib build\user32.lib build\shell32.lib
if errorlevel 1 goto :fail

popd
exit /b 0

:fail
set "DYNAMICHUD_BUILD_RC=%ERRORLEVEL%"
popd
exit /b %DYNAMICHUD_BUILD_RC%
