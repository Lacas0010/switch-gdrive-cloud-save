# Automated MTP Deployment Script for Nintendo Switch
param(
    [string]$DistPath = "dist"
)

$ErrorActionPreference = "Stop"

Write-Host "==================================================" -ForegroundColor Cyan
Write-Host "DEPLOYMENT: Pushing Binaries to Nintendo Switch via MTP" -ForegroundColor Cyan
Write-Host "==================================================" -ForegroundColor Cyan

$exefsLocal = Join-Path $DistPath "atmosphere\contents\4200000000000010\exefs.nsp"
$nroLocal   = Join-Path $DistPath "switch\gdrive-sync\gdrive-sync.nro"

if (-not (Test-Path $exefsLocal)) {
    Write-Error "Local artifact missing: $exefsLocal"
    exit 1
}
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
$contents = Get-SubItem $atmos "contents"
$sysDir = Get-SubItem $contents "4200000000000010"

$switchDir = Get-SubItem $sd "switch"
$guiDir = Get-SubItem $switchDir "gdrive-sync"

if (-not $sysDir -or -not $guiDir) {
    Write-Error "Target folders on Switch SD not found."
    exit 1
}

# 2. Setup Drain Folder to silently clear old binaries
$drainName = "__mtp_drain_" + [System.Guid]::NewGuid().ToString().Substring(0, 8)
$sd.GetFolder.NewFolder($drainName)
Start-Sleep -Seconds 1
$drainItem = Get-SubItem $sd $drainName
$drainFolder = $drainItem.GetFolder

Write-Host "Draining existing binaries from MTP..." -ForegroundColor Yellow

$oldExefs = Get-SubItem $sysDir "exefs.nsp"
if ($oldExefs) {
    $drainFolder.MoveHere($oldExefs, 4 -bor 16 -bor 512 -bor 1024)
    Start-Sleep -Seconds 1
}

$oldNro = Get-SubItem $guiDir "gdrive-sync.nro"
if ($oldNro) {
    $drainFolder.MoveHere($oldNro, 4 -bor 16 -bor 512 -bor 1024)
    Start-Sleep -Seconds 1
}

# 3. Deploy New Binaries
Write-Host "Copying fresh exefs.nsp (Sysmodule) to Switch..." -ForegroundColor Yellow
$sysDir.GetFolder.CopyHere((Resolve-Path $exefsLocal).Path, 16 -bor 4)
Start-Sleep -Seconds 2

Write-Host "Copying fresh gdrive-sync.nro (GUI App) to Switch..." -ForegroundColor Yellow
$guiDir.GetFolder.CopyHere((Resolve-Path $nroLocal).Path, 16 -bor 4)
Start-Sleep -Seconds 3

# 4. Clean Drain Folder
$drainFolder.Items() | ForEach-Object {
    # Items inside drain will naturally be left or deleted
}

Write-Host "`n-- Verification on Switch SD --" -ForegroundColor Green
$newExefs = Get-SubItem $sysDir "exefs.nsp"
$newNro = Get-SubItem $guiDir "gdrive-sync.nro"

if ($newExefs -and $newNro) {
    Write-Host "  [OK] exefs.nsp successfully installed ($($newExefs.Size) bytes)" -ForegroundColor Green
    Write-Host "  [OK] gdrive-sync.nro successfully installed ($($newNro.Size) bytes)" -ForegroundColor Green
    Write-Host "`nDeployment completed with 100% SUCCESS!" -ForegroundColor Cyan
} else {
    Write-Error "Deployment verification failed: One or more binaries missing."
    exit 1
}

