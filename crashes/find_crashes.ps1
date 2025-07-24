# PowerShell script to find AssetInventory crashes in Windows logs

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "Searching for AssetInventory crash logs" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# Search Application event log for crashes
Write-Host "Recent Application Crashes:" -ForegroundColor Yellow
Get-EventLog -LogName Application -EntryType Error -After (Get-Date).AddDays(-7) | 
    Where-Object {$_.Message -like "*AssetInventory*"} | 
    Select-Object TimeGenerated, Source, Message | 
    Format-List

# Search for Windows Error Reporting
Write-Host "`nWindows Error Reporting entries:" -ForegroundColor Yellow
Get-WinEvent -FilterHashtable @{LogName='Application'; ProviderName='Windows Error Reporting'} -MaxEvents 20 -ErrorAction SilentlyContinue |
    Where-Object {$_.Message -like "*AssetInventory*"} |
    Select-Object TimeCreated, Message |
    Format-List

# Look for dump files
Write-Host "`nSearching for crash dump files..." -ForegroundColor Yellow
$dumpPaths = @(
    "$env:LOCALAPPDATA\Microsoft\Windows\WER\ReportArchive",
    "C:\ProgramData\Microsoft\Windows\WER\ReportArchive",
    "C:\CrashDumps"
)

foreach ($path in $dumpPaths) {
    if (Test-Path $path) {
        $dumps = Get-ChildItem -Path $path -Recurse -Filter "*AssetInventory*" -ErrorAction SilentlyContinue
        if ($dumps) {
            Write-Host "Found dumps in: $path" -ForegroundColor Green
            $dumps | Select-Object FullName, CreationTime, Length | Format-Table
        }
    }
}

Write-Host "`nTo analyze .dmp files:" -ForegroundColor Cyan
Write-Host "1. Open them in Visual Studio (File > Open > File)" -ForegroundColor White
Write-Host "2. Or use WinDbg Preview from Microsoft Store" -ForegroundColor White
Write-Host "3. Look for the call stack when the crash occurred" -ForegroundColor White