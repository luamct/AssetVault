@echo off
echo ========================================
echo Checking Windows logs for AssetInventory crashes
echo ========================================
echo.

echo Checking Application Event Log for crashes...
echo ----------------------------------------
wevtutil qe Application /q:"*[System[Provider[@Name='Application Error']] and EventData[Data[@Name='AppName']='AssetInventory.exe']]" /f:text /c:5 /rd:true

echo.
echo Checking for Windows Error Reporting entries...
echo ----------------------------------------
wevtutil qe Application /q:"*[System[Provider[@Name='Windows Error Reporting']]]" /f:text /c:5 /rd:true | findstr /i "AssetInventory"

echo.
echo Checking for crash dump files...
echo ----------------------------------------
echo User-specific dumps:
dir "%LOCALAPPDATA%\Microsoft\Windows\WER\ReportArchive\AppCrash_AssetInventory*" 2>nul
echo.
echo System-wide dumps:
dir "C:\ProgramData\Microsoft\Windows\WER\ReportArchive\AppCrash_AssetInventory*" 2>nul
echo.
echo Custom dump location (if configured):
if exist "C:\CrashDumps\AssetInventory*.dmp" (
    dir "C:\CrashDumps\AssetInventory*.dmp"
) else (
    echo No dumps found in C:\CrashDumps
)

echo.
echo ========================================
echo To enable detailed crash dumps, run enable_crash_dumps.reg as Administrator
echo To analyze .dmp files, use Visual Studio or WinDbg
echo ========================================
pause