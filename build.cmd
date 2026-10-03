@echo off
setlocal
if /i "%VSCMD_ARG_TGT_ARCH%"=="x86" goto compiler_ready
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
 echo Install Visual Studio C++ x86 build tools and Windows SDK, or use an x86 Native Tools prompt.
 exit /b 1
)
for /f "usebackq tokens=*" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "BROWSER_VS=%%I"
if not defined BROWSER_VS exit /b 1
call "%BROWSER_VS%\VC\Auxiliary\Build\vcvars32.bat"
if errorlevel 1 exit /b 1
:compiler_ready
cd /d "%~dp0"
if not exist build mkdir build
if not exist package\FOSE\Plugins mkdir package\FOSE\Plugins
if not exist package\FOSE\Plugins\LukesItemBrowserFO3 mkdir package\FOSE\Plugins\LukesItemBrowserFO3
cl /nologo /LD /MT src\zlibwrap.cpp /Fobuild\zlibwrap.obj /Fepackage\FOSE\Plugins\LukesItemBrowserFO3\zlib1.dll /link third_party\libz.a
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /EHsc /O2 /MT /W4 /GS /DUNICODE /D_UNICODE /LD src\browser.cpp /Fobuild\browser.obj /Fepackage\FOSE\Plugins\LukesItemBrowserFO3.dll /link /DYNAMICBASE /NXCOMPAT dinput8.lib dxguid.lib user32.lib gdi32.lib shell32.lib /MACHINE:X86
if errorlevel 1 exit /b 1

cl /nologo /std:c++17 /EHsc /O2 /MT /W4 /GS /DUNICODE /D_UNICODE src\fo3check.cpp /Fobuild\fo3check.obj /Febuild\fo3check.exe /link /DYNAMICBASE /NXCOMPAT dinput8.lib dxguid.lib user32.lib gdi32.lib shell32.lib /MACHINE:X86
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /EHsc /O2 /MT /W4 /GS /DUNICODE /D_UNICODE src\check.cpp /Fobuild\check.obj /Febuild\CatalogCheck.exe /link /DYNAMICBASE /NXCOMPAT /MACHINE:X86
if errorlevel 1 exit /b 1
dumpbin /nologo /imports package\FOSE\Plugins\LukesItemBrowserFO3.dll > build\imports.txt
dumpbin /nologo /exports package\FOSE\Plugins\LukesItemBrowserFO3.dll > build\exports.txt
build\fo3check.exe
exit /b %errorlevel%
