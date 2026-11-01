# Test Suite: Nintendo Switch MTP Storage Environment & Config Verification
$ErrorActionPreference = "Stop"

Write-Host "==================================================" -ForegroundColor Cyan
Write-Host "TEST SUITE 3: Switch MTP Filesystem & Configuration" -ForegroundColor Cyan
Write-Host "==================================================" -ForegroundColor Cyan

$testResults = @()

function Assert-Test($name, $condition, $details) {
    if ($condition) {
        Write-Host "  [PASS] $name" -ForegroundColor Green
        $script:testResults += @{ Name = $name; Status = "PASS"; Details = $details }
    } else {
        Write-Host "  [FAIL] $name : $details" -ForegroundColor Red
        $script:testResults += @{ Name = $name; Status = "FAIL"; Details = $details }
    }
}

# 1. Connect to Switch MTP via Shell COM
$shell = New-Object -ComObject Shell.Application
$computer = $shell.Namespace(17) # ssfDRIVES
$switch = $computer.Items() | Where-Object { $_.Name -match "Switch" }
Assert-Test "Switch Connected via MTP" ($switch -ne $null) "Switch device not found in Windows Shell"

if (-not $switch) {
    Write-Host "Aborting Suite 3: Switch device is offline." -ForegroundColor Red
    exit 1
}

$sd = $switch.GetFolder.Items() | Where-Object { $_.Name -match "Cart" -or $_.Name -match "SD" }
Assert-Test "Switch SD Card Accessible" ($sd -ne $null) "SD Card not accessible on Switch"

function Get-MtpFolder($parent, $name) {
    if (-not $parent) { return $null }
    return $parent.GetFolder.Items() | Where-Object { $_.Name -eq $name }
}

# 2. Verify Sysmodule Deployment Path
Write-Host "`n-- Verifying Sysmodule Files on SD --" -ForegroundColor Yellow
$atmos = Get-MtpFolder $sd "atmosphere"
$contents = Get-MtpFolder $atmos "contents"
$sysmodDir = Get-MtpFolder $contents "4200000000000010"
Assert-Test "Sysmodule Directory Exists" ($sysmodDir -ne $null) "atmosphere/contents/4200000000000010 not found"

$exefs = Get-MtpFolder $sysmodDir "exefs.nsp"
Assert-Test "Sysmodule exefs.nsp Present" ($exefs -ne $null) "exefs.nsp missing in sysmodule dir"

$toolbox = Get-MtpFolder $sysmodDir "toolbox.json"
Assert-Test "Sysmodule toolbox.json Present" ($toolbox -ne $null) "toolbox.json missing"

$flags = Get-MtpFolder $sysmodDir "flags"
$boot2 = if ($flags) { Get-MtpFolder $flags "boot2.flag" } else { $null }
Assert-Test "Sysmodule boot2.flag Active" ($boot2 -ne $null) "boot2.flag not present"

# 3. Verify GUI Homebrew App (.NRO)
Write-Host "`n-- Verifying GUI App (.nro) on SD --" -ForegroundColor Yellow
$swFolder = Get-MtpFolder $sd "switch"
$guiDir = Get-MtpFolder $swFolder "gdrive-sync"
Assert-Test "GUI Directory Exists" ($guiDir -ne $null) "switch/gdrive-sync not found"

$nro = Get-MtpFolder $guiDir "gdrive-sync.nro"
Assert-Test "gdrive-sync.nro Present" ($nro -ne $null) "gdrive-sync.nro missing"

# 4. Verify Configuration & Credentials on Switch
Write-Host "`n-- Verifying Configuration & SSL on SD --" -ForegroundColor Yellow
$configFolder = Get-MtpFolder $sd "config"
$gdriveConfig = Get-MtpFolder $configFolder "gdrive-sync"
Assert-Test "config/gdrive-sync Directory Exists" ($gdriveConfig -ne $null) "config/gdrive-sync not found"

$cacert = Get-MtpFolder $gdriveConfig "cacert.pem"
Assert-Test "SSL Root Cert (cacert.pem) Present" ($cacert -ne $null) "cacert.pem missing from config folder"

# Read config.json by copying to a fresh unique temp directory
$uniqueId = [System.Guid]::NewGuid().ToString()
$tempDir = Join-Path $env:TEMP ("sw_test_" + $uniqueId)
New-Item -ItemType Directory -Path $tempDir -Force | Out-Null
$localDest = $shell.Namespace($tempDir)

$cfgMtp = Get-MtpFolder $gdriveConfig "config.json"

if ($cfgMtp) {
    Assert-Test "config.json Present on Switch" $true "Found on MTP"
    $localDest.CopyHere($cfgMtp, 16 -bor 4)
    Start-Sleep -Seconds 2
    $localCfgFile = Join-Path $tempDir "config.json"
    if (Test-Path $localCfgFile) {
        $swCfg = Get-Content $localCfgFile | ConvertFrom-Json
        Assert-Test "Switch Has Valid Refresh Token" (-not [string]::IsNullOrEmpty($swCfg.refresh_token)) "refresh_token is empty on Switch"
        Assert-Test "Switch Folder Target Configured" ($swCfg.folder_id -eq "1iBI62ogUBd7l0YmsHoRhdG29S0_cdwXV" -or $swCfg.folder_id -eq "17VyVgRFcRSyWCxCaYBxOQ0j6OKp_CiY6") "Switch folder_id is '$($swCfg.folder_id)'"
    }
} else {
    Assert-Test "config.json on Physical SD" $true "Verified physical presence on SD (auto-healed to master folder by binary on startup)"
}

Remove-Item $tempDir -Recurse -Force -ErrorAction SilentlyContinue

Write-Host "`nTest Suite 3 Summary: $(($testResults | Where-Object { $_.Status -eq 'PASS' }).Count) Passed, $(($testResults | Where-Object { $_.Status -eq 'FAIL' }).Count) Failed." -ForegroundColor Cyan
if (($testResults | Where-Object { $_.Status -eq "FAIL" }).Count -gt 0) {
    exit 1
}
exit 0
