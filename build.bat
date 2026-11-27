@echo off
setlocal
rem Locate MSVC via vswhere and set up the x64 build environment.
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR ( echo MSVC not found & exit /b 1 )
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>nul || exit /b 1

cd /d "%~dp0"
if not exist build\obj mkdir build\obj

set CFLAGS=/nologo /utf-8 /std:c++17 /O1 /Os /GL /Gy /MT /W4 /EHsc /DUNICODE /D_UNICODE /DNOMINMAX /DNDEBUG /Fobuild\obj\ /Fdbuild\obj\
set LFLAGS=/LTCG /OPT:REF /OPT:ICF /MANIFEST:NO
set CORE=src\hidpp.cpp src\btbattery.cpp src\ble.cpp src\galaxy.cpp src\pads.cpp src\config.cpp src\devices.cpp src\icon.cpp

rem App (taskbar button + tray when closed)
rc /nologo /c65001 /fo build\obj\juice.res src\juice.rc || exit /b 1
cl %CFLAGS% src\juice.cpp src\illust.cpp %CORE% build\obj\juice.res ^
   /Fe:build\Juice.exe /link %LFLAGS% /SUBSYSTEM:WINDOWS || exit /b 1

rem Dev tools
cl %CFLAGS% tools\probe.cpp src\hidpp.cpp src\btbattery.cpp src\galaxy.cpp src\ble.cpp src\pads.cpp /Fe:build\probe.exe /link %LFLAGS% /SUBSYSTEM:CONSOLE || exit /b 1
cl %CFLAGS% tools\iconpreview.cpp src\icon.cpp /Fe:build\iconpreview.exe /link %LFLAGS% /SUBSYSTEM:CONSOLE || exit /b 1
cl %CFLAGS% tools\illustpreview.cpp src\illust.cpp /Fe:build\illustpreview.exe /link %LFLAGS% /SUBSYSTEM:CONSOLE || exit /b 1

echo Build OK
