@echo off
rem Builds bin\tessel.exe and runs the unit tests. Usage: build.cmd [debug]
setlocal
set ROOT=%~dp0
for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set VS=%%i
if not defined VS (echo Visual Studio C++ build tools not found & exit /b 1)
call "%VS%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>nul || exit /b 1

rem Intermediate files live outside OneDrive, which locks files while syncing.
set OBJ=%TEMP%\tessel-build
if not exist "%ROOT%bin" mkdir "%ROOT%bin"
if not exist "%OBJ%" mkdir "%OBJ%"
set OPT=/O1 /GL /MT
if /i "%1"=="debug" set OPT=/Od /Zi /MTd
set CFLAGS=/nologo /utf-8 /std:c++20 /EHsc /W4 /wd4100 /DUNICODE /D_UNICODE %OPT% /Fo"%OBJ%\\"
set LIBS=user32.lib gdi32.lib shell32.lib advapi32.lib ole32.lib dwmapi.lib comdlg32.lib shcore.lib

rem App icon: drawn by the same code as the tray icon, then compiled into a resource.
cl /nologo /utf-8 /std:c++20 /EHsc /O1 /DUNICODE /D_UNICODE /Fo"%OBJ%\\" "%ROOT%tools\make_icon.cpp" "%ROOT%src\icon.cpp" /Fe"%OBJ%\make_icon.exe" /link user32.lib gdi32.lib advapi32.lib || exit /b 1
"%OBJ%\make_icon.exe" "%OBJ%\tessel.ico" || exit /b 1
> "%OBJ%\tessel.rc" echo 1 ICON "tessel.ico"
rc /nologo /fo "%OBJ%\tessel.res" "%OBJ%\tessel.rc" || exit /b 1

set SRC=main combos layout snap store editor icon preview settings dragsnap
set FILES=
for %%f in (%SRC%) do call set FILES=%%FILES%% "%ROOT%src\%%f.cpp"
cl %CFLAGS% %FILES% "%OBJ%\tessel.res" /Fe"%ROOT%bin\tessel.exe" /link /SUBSYSTEM:WINDOWS /OPT:REF /OPT:ICF %LIBS% || exit /b 1
cl %CFLAGS% "%ROOT%tests\combos_test.cpp" "%ROOT%src\combos.cpp" "%ROOT%src\layout.cpp" "%ROOT%src\store.cpp" /Fe"%ROOT%bin\combos_test.exe" /link /SUBSYSTEM:CONSOLE %LIBS% || exit /b 1
"%ROOT%bin\combos_test.exe"
