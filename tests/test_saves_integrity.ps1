# Test Suite: Save Archive Integrity & G: Drive Verification
$ErrorActionPreference = "Stop"

Write-Host "==================================================" -ForegroundColor Cyan
Write-Host "TEST SUITE 2: Save File Integrity & Drive G: Sync" -ForegroundColor Cyan
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

# 1. Check Google Drive G: Mount
$gDrivePath = "G:\Meu Drive"
Assert-Test "Google Drive G: Mount Exists" (Test-Path $gDrivePath) "Google Drive is not mounted at G:\Meu Drive"

# 2. Check Switch Saves Folder on G:
$switchSavesPath = "G:\Meu Drive\Switch Saves"
Assert-Test "Master Folder on G: Exists" (Test-Path $switchSavesPath) "Folder $switchSavesPath not found"

# 3. Inspect and Validate Zip Archives
$saveFiles = Get-ChildItem -Path $switchSavesPath -Filter "save_*.zip" -File
Assert-Test "Saves Found on G:" ($saveFiles.Count -gt 0) "Found $($saveFiles.Count) saves"
Write-Host "  Found $($saveFiles.Count) save archives in G:\Meu Drive\Switch Saves" -ForegroundColor Gray

Add-Type -AssemblyName System.IO.Compression.FileSystem

foreach ($file in $saveFiles) {
    $tid = $file.BaseName.Replace("save_", "")
    $isValidZip = $false
    $entryCount = 0
    $uncompressedTotal = 0

    try {
        $zip = [System.IO.Compression.ZipFile]::OpenRead($file.FullName)
        $entryCount = $zip.Entries.Count
        foreach ($entry in $zip.Entries) {
            $uncompressedTotal += $entry.Length
        }
        $zip.Dispose()
        $isValidZip = ($entryCount -gt 0)
    } catch {
        $isValidZip = $false
    }

    Assert-Test "Zip Integrity: $($file.Name)" $isValidZip "Entries: $entryCount, Size: $($file.Length) bytes, Uncompressed: $uncompressedTotal bytes"
}

# 4. Check Known Core Games
$expectedGames = @{
    "01007ef00011e000" = "The Legend of Zelda: Breath of the Wild"
    "0100000000010000" = "Super Mario Odyssey"
    "0100152000022000" = "Mario Kart 8 Deluxe"
    "010003f003a34000" = "Animal Crossing: New Horizons"
    "01002da013484000" = "The Legend of Zelda: Skyward Sword HD"
    "0100abf008968000" = "Pokemon Sword"
    "0100dcd01f306000" = "Balatro"
}

Write-Host "`n-- Verifying Core Game Saves on G: --" -ForegroundColor Yellow
foreach ($entry in $expectedGames.GetEnumerator()) {
    $expectedPath = Join-Path $switchSavesPath "save_$($entry.Key).zip"
    $exists = Test-Path $expectedPath
    $sizeStr = if ($exists) { "$((Get-Item $expectedPath).Length) bytes" } else { "Not Found" }
    Assert-Test "Save Available: $($entry.Value)" $exists "Path: $expectedPath ($sizeStr)"
}

Write-Host "`nTest Suite 2 Summary: $(($testResults | Where-Object { $_.Status -eq 'PASS' }).Count) Passed, $(($testResults | Where-Object { $_.Status -eq 'FAIL' }).Count) Failed." -ForegroundColor Cyan
if (($testResults | Where-Object { $_.Status -eq "FAIL" }).Count -gt 0) {
    exit 1
}
exit 0

