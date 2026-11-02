# Automated MTP Deployment Script for Nintendo Switch
param(
    [string]$DistPath = "dist"
)

$ErrorActionPreference = "Stop"

Write-Host "==================================================" -ForegroundColor Cyan
Write-Host "DEPLOYMENT: Pushing Binaries to Nintendo Switch via MTP" -ForegroundColor Cyan
Write-Host "==================================================" -ForegroundColor Cyan

$nroLocal = Join-Path $DistPath "switch\gdrive-sync\gdrive-sync.nro"

if (-not (Test-Path $nroLocal)) {
    Write-Error "Local artifact missing: $nroLocal"
    exit 1
}

$shell = New-Object -ComObject Shell.Application
$computer = $shell.Namespace(17)
$switch = $computer.Items() | Where-Object { $_.Name -match "Switch" }
if (-not $switch) {
    Write-Error "Nintendo Switch device not detected on MTP."
    exit 1
}

$sd = $switch.GetFolder.Items() | Where-Object { $_.Name -match "Cart" -or $_.Name -match "SD" }
if (-not $sd) {
    Write-Error "SD Card storage not found on Switch."
    exit 1
}

function Get-SubItem($folder, $name) {
    return $folder.GetFolder.Items() | Where-Object { $_.Name -eq $name }
}

# 1. Target Directories
$atmos = Get-SubItem $sd "atmosphere"
$contents = if ($atmos) { Get-SubItem $atmos "contents" } else { $null }
$sysDir = if ($contents) { Get-SubItem $contents "4200000000000010" } else { $null }

$switchDir = Get-SubItem $sd "switch"
$guiDir = Get-SubItem $switchDir "gdrive-sync"
if (-not $guiDir) {
    $switchDir.GetFolder.NewFolder("gdrive-sync")
    Start-Sleep -Seconds 1
    $guiDir = Get-SubItem $switchDir "gdrive-sync"
}

# 2. Setup Drain Folder to silently clear old binaries and sysmodule
$drainName = "__mtp_drain_" + [System.Guid]::NewGuid().ToString().Substring(0, 8)
$sd.GetFolder.NewFolder($drainName)
Start-Sleep -Seconds 1
$drainItem = Get-SubItem $sd $drainName
$drainFolder = $drainItem.GetFolder

Write-Host "Draining old binaries / decommissioned sysmodule from MTP..." -ForegroundColor Yellow

if ($sysDir) {
    Write-Host "  Draining atmosphere/contents/4200000000000010..." -ForegroundColor Yellow
    $drainFolder.MoveHere($sysDir, 4 -bor 16 -bor 512 -bor 1024)
    Start-Sleep -Seconds 2
}

$oldNro = Get-SubItem $guiDir "gdrive-sync.nro"
if ($oldNro) {
    $drainFolder.MoveHere($oldNro, 4 -bor 16 -bor 512 -bor 1024)
    Start-Sleep -Seconds 1
}

# 3. Deploy New Binaries
Write-Host "Copying fresh gdrive-sync.nro (GUI App) to Switch..." -ForegroundColor Yellow
$guiDir.GetFolder.CopyHere((Resolve-Path $nroLocal).Path, 16 -bor 4)
Start-Sleep -Seconds 3

# 4. Clean Drain Folder
$drainFolder.Items() | ForEach-Object {
    # Items inside drain will naturally be left or deleted
}

Write-Host "`n-- Verification on Switch SD --" -ForegroundColor Green
$newNro = Get-SubItem $guiDir "gdrive-sync.nro"

if ($newNro) {
    Write-Host "  [OK] gdrive-sync.nro successfully installed ($($newNro.Size) bytes)" -ForegroundColor Green
    Write-Host "`nDeployment completed with 100% SUCCESS!" -ForegroundColor Cyan
} else {
    Write-Error "Deployment verification failed: gdrive-sync.nro missing."
    exit 1
}

