@echo off
rem ============================================================================
rem  Kingdom Two Crowns trainer - build script (MSVC x64, no external deps)
rem  usage:  build.bat [recon|inject|mod|launcher|all]
rem ============================================================================
setlocal
set "ROOT=%~dp0"
set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" (
  echo [-] vcvars64.bat not found at "%VCVARS%"
  exit /b 1
)
call "%VCVARS%" >nul 2>&1
if errorlevel 1 ( echo [-] vcvars64 environment failed & exit /b 1 )

cd /d "%ROOT%"
if not exist build mkdir build
if not exist build\obj mkdir build\obj

set "TARGET=%~1"
if "%TARGET%"=="" set "TARGET=all"

set "CFLAGS=/nologo /std:c++17 /EHsc /O2 /MT /DNDEBUG /W3 /utf-8 /D_WIN32_WINNT=0x0601 /DWIN32_LEAN_AND_MEAN /Fo:build\obj\ /Fd:build\obj\vc.pdb"

if /I "%TARGET%"=="recon"    goto recon
if /I "%TARGET%"=="inject"   goto inject
if /I "%TARGET%"=="mod"      goto mod
if /I "%TARGET%"=="launcher" goto launcher
if /I "%TARGET%"=="all"      goto all
echo [-] unknown target "%TARGET%"
exit /b 1

:all
call "%~f0" recon   || exit /b 1
call "%~f0" inject  || exit /b 1
call "%~f0" mod     || exit /b 1
call "%~f0" launcher|| exit /b 1
echo [+] all targets built
exit /b 0

:recon
echo [*] building recon.dll ...
cl %CFLAGS% /LD src\recon\recon.cpp /link /OUT:build\recon.dll /IMPLIB:build\obj\recon.lib /SUBSYSTEM:WINDOWS
if errorlevel 1 ( echo [-] recon failed & exit /b 1 )
echo [+] build\recon.dll
exit /b 0

:inject
echo [*] building inject.exe ...
cl %CFLAGS% /Fe:build\inject.exe src\tools\inject.cpp /link /SUBSYSTEM:CONSOLE advapi32.lib
if errorlevel 1 ( echo [-] inject failed & exit /b 1 )
echo [+] build\inject.exe
exit /b 0

:mod
echo [*] building ktc_mod_build.dll ...
set "MH=third_party\minhook-master"
cl %CFLAGS% /LD /I "%MH%\include" /I "%MH%\src" /I "%MH%\src\hde" ^
   src\mod\mod.cpp ^
   "%MH%\src\hook.c" "%MH%\src\buffer.c" "%MH%\src\trampoline.c" "%MH%\src\hde\hde64.c" ^
   /link /OUT:build\ktc_mod_build.dll /IMPLIB:build\obj\ktc_mod.lib /SUBSYSTEM:WINDOWS user32.lib
if errorlevel 1 ( echo [-] mod failed & exit /b 1 )
echo [+] build\ktc_mod_build.dll
exit /b 0

:launcher
echo [*] building KTCTrainer.exe ...
cl %CFLAGS% /Fe:build\KTCTrainer.exe src\launcher\launcher.cpp /link /SUBSYSTEM:WINDOWS user32.lib gdi32.lib comctl32.lib advapi32.lib
if errorlevel 1 ( echo [-] launcher failed & exit /b 1 )
echo [+] build\KTCTrainer.exe
exit /b 0
