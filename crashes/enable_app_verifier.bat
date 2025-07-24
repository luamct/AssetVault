@echo off
echo ========================================
echo Setting up Application Verifier for AssetInventory.exe
echo ========================================
echo.
echo This will help detect heap corruption, handle leaks, and other issues.
echo.

REM Check if running as admin
net session >nul 2>&1
if %errorLevel% neq 0 (
    echo ERROR: Please run this script as Administrator!
    pause
    exit /b 1
)

REM Enable Application Verifier
echo Enabling Application Verifier for AssetInventory.exe...
appverif -enable Heaps Handles Locks Memory -for AssetInventory.exe

echo.
echo Application Verifier enabled!
echo.
echo Next steps:
echo 1. Run AssetInventory.exe normally
echo 2. If it crashes, check Event Viewer for detailed verifier stops
echo 3. To disable: appverif -disable * -for AssetInventory.exe
echo.
pause