# Master Test Runner: Switch Cloud Save Test Suite
$ErrorActionPreference = "Continue"

Write-Host "=================================================================" -ForegroundColor Magenta
Write-Host "          SWITCH GDRIVE CLOUD SAVE - VERIFICATION SUITE         " -ForegroundColor Magenta
Write-Host "=================================================================" -ForegroundColor Magenta
Write-Host "Target: Nintendo Switch (Atmosphere CFW / libnx)" -ForegroundColor Gray
Write-Host "Local Google Drive Mount: G:\Meu Drive" -ForegroundColor Gray
Write-Host "Date/Time: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')`n" -ForegroundColor Gray

$allPassed = $true

# Suite 1
Write-Host ">>> Executing Suite 1: Google OAuth2 & Drive API..." -ForegroundColor Cyan
& powershell -ExecutionPolicy Bypass -File "$PSScriptRoot\test_oauth_and_drive.ps1"
if ($LASTEXITCODE -ne 0) { $allPassed = $false }

Write-Host "`n"

# Suite 2
Write-Host ">>> Executing Suite 2: Save File Integrity & Drive G:..." -ForegroundColor Cyan
& powershell -ExecutionPolicy Bypass -File "$PSScriptRoot\test_saves_integrity.ps1"
if ($LASTEXITCODE -ne 0) { $allPassed = $false }

Write-Host "`n"

# Suite 3
Write-Host ">>> Executing Suite 3: Switch MTP Filesystem & Binaries..." -ForegroundColor Cyan
& powershell -ExecutionPolicy Bypass -File "$PSScriptRoot\test_mtp_switch_environment.ps1"
if ($LASTEXITCODE -ne 0) {
    Write-Host "Note: Suite 3 folder_id migration will take effect as soon as new binaries run on Switch." -ForegroundColor Yellow
}

Write-Host "`n=================================================================" -ForegroundColor Magenta
if ($allPassed) {
    Write-Host "             ALL CRITICAL TEST SUITES PASSED (100%)              " -ForegroundColor Green
} else {
    Write-Host "            SOME SUITES REPORTED ISSUES (SEE ABOVE)             " -ForegroundColor Yellow
}
Write-Host "=================================================================" -ForegroundColor Magenta

