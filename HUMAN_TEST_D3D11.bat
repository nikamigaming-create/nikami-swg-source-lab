@echo off
setlocal

set "TOOLS=%~dp0"
if "%TOOLS:~-1%"=="\" set "TOOLS=%TOOLS:~0,-1%"
for %%I in ("%TOOLS%\..") do set "ROOT=%%~fI"

set "CLIENT=%ROOT%\client\SWGSource Client v3.0"
set "EXE_SRC=%TOOLS%\src\compile\win32\SwgClient\Release\SwgClient_r.exe"
set "DPVS_SRC=%TOOLS%\src\compile\win32\dpvs\Release\dpvs.dll"
set "DLLEXPORT_SRC=%TOOLS%\src\compile\win32\DllExport\Release\DllExport.dll"
set "D3D11_SRC=%TOOLS%\src\compile\win32\Direct3d11\Release\gl05_r.dll"
set "EXE_DST=%CLIENT%\SwgClient_r.exe"

echo ============================================================
echo HUMAN TEST HERE - SWG OG Client D3D11
echo ============================================================
echo.

if not exist "%CLIENT%\" (
    echo ERROR: Client runtime folder not found:
    echo   "%CLIENT%"
    goto :fail
)

if not exist "%EXE_SRC%" (
    echo ERROR: Built client exe not found:
    echo   "%EXE_SRC%"
    echo Build first with:
    echo   powershell -ExecutionPolicy Bypass -File "%TOOLS%\scripts\dev\build-og-client.ps1" -Target SwgClient
    goto :fail
)

if not exist "%DPVS_SRC%" (
    echo ERROR: dpvs.dll not found:
    echo   "%DPVS_SRC%"
    goto :fail
)

if not exist "%DLLEXPORT_SRC%" (
    echo ERROR: DllExport.dll not found:
    echo   "%DLLEXPORT_SRC%"
    goto :fail
)

if not exist "%D3D11_SRC%" (
    echo ERROR: D3D11 gl05_r.dll not found:
    echo   "%D3D11_SRC%"
    echo Build first with:
    echo   powershell -ExecutionPolicy Bypass -File "%TOOLS%\scripts\dev\build-og-client.ps1" -Target Direct3d11
    goto :fail
)

echo Refreshing runtime files...
copy /Y "%EXE_SRC%" "%EXE_DST%" >nul || goto :copyfail
copy /Y "%DPVS_SRC%" "%CLIENT%\dpvs.dll" >nul || goto :copyfail
copy /Y "%DLLEXPORT_SRC%" "%CLIENT%\DllExport.dll" >nul || goto :copyfail
copy /Y "%D3D11_SRC%" "%CLIENT%\gl05_r.dll" >nul || goto :copyfail

echo.
echo Launching D3D11 client...
pushd "%CLIENT%" || goto :fail
start "SWG Human Test D3D11" "%EXE_DST%"
popd

echo.
echo HUMAN TEST HERE
echo Walk around and test the current D3D11 renderer.
echo Close Star Wars Galaxies when done.
echo.
pause
exit /b 0

:copyfail
echo.
echo ERROR: Failed while copying runtime files. Close any running client and try again.
goto :fail

:fail
echo.
pause
exit /b 1
