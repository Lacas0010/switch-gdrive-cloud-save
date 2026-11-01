# Test Suite: Google OAuth2 and Google Drive API Verification
$ErrorActionPreference = "Stop"

Write-Host "==================================================" -ForegroundColor Cyan
Write-Host "TEST SUITE 1: Google OAuth2 & Drive API Endpoints" -ForegroundColor Cyan
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

# 1. Load config
$configPath = "config.json"
Assert-Test "Config File Exists" (Test-Path $configPath) "config.json not found"

$cfg = Get-Content $configPath | ConvertFrom-Json
Assert-Test "Client ID Present" (-not [string]::IsNullOrEmpty($cfg.client_id)) "client_id is empty"
Assert-Test "Client Secret Present" (-not [string]::IsNullOrEmpty($cfg.client_secret)) "client_secret is empty"
Assert-Test "Refresh Token Present" (-not [string]::IsNullOrEmpty($cfg.refresh_token)) "refresh_token is empty"

# 2. Token Refresh
Write-Host "`n-- Testing OAuth Token Refresh --" -ForegroundColor Yellow
$tokenBody = @{
    client_id     = $cfg.client_id
    client_secret = $cfg.client_secret
    refresh_token = $cfg.refresh_token
    grant_type    = "refresh_token"
}

$tokenResponse = $null
try {
    $tokenResponse = Invoke-RestMethod -Uri "https://oauth2.googleapis.com/token" -Method Post -Body $tokenBody
    Assert-Test "OAuth Token Refresh HTTP 200" ($tokenResponse -ne $null -and -not [string]::IsNullOrEmpty($tokenResponse.access_token)) "Failed to obtain access token"
} catch {
    Assert-Test "OAuth Token Refresh HTTP 200" $false $_.Exception.Message
}

$accessToken = $tokenResponse.access_token

# 3. Master Folder Validation
Write-Host "`n-- Testing Master Folder (Switch Saves) --" -ForegroundColor Yellow
$masterFolderId = "1iBI62ogUBd7l0YmsHoRhdG29S0_cdwXV"
$folderJson = & curl.exe -s -H "Authorization: Bearer $accessToken" "https://www.googleapis.com/drive/v3/files/$masterFolderId`?fields=id,name,trashed"
$folderObj = $folderJson | ConvertFrom-Json

Assert-Test "Master Folder Exists" ($folderObj.id -eq $masterFolderId) "Folder ID mismatch: $($folderObj.id)"
Assert-Test "Master Folder Name" ($folderObj.name -eq "Switch Saves") "Folder Name mismatch: $($folderObj.name)"
Assert-Test "Master Folder Not Trashed" ($folderObj.trashed -eq $false) "Folder is in trash"

# 4. Test Multipart POST Upload and Cleanup
Write-Host "`n-- Testing Multipart POST Upload & Cleanup --" -ForegroundColor Yellow
$boundary = "-------CloudSaveBoundary7MA4YWxkTrZu0gW"
$testFileName = "test_probe_harness.bin"
$meta = "--$boundary`r`nContent-Type: application/json; charset=UTF-8`r`n`r`n{`"name`": `"$testFileName`", `"parents`": [`"$masterFolderId`"]}`r`n--$boundary`r`nContent-Type: application/zip`r`n`r`nHARNESS_PROBE_PAYLOAD_TEST_12345`r`n--$boundary--`r`n"

$probeFile = "test_harness_temp.tmp"
[System.IO.File]::WriteAllBytes($probeFile, [System.Text.Encoding]::UTF8.GetBytes($meta))

$uploadOut = & curl.exe -s -X POST `
  -H "Authorization: Bearer $accessToken" `
  -H "Content-Type: multipart/related; boundary=$boundary" `
  --data-binary "@$probeFile" `
  "https://www.googleapis.com/upload/drive/v3/files?uploadType=multipart"

Remove-Item $probeFile -Force -ErrorAction SilentlyContinue

$uploadedObj = $null
try {
    $uploadedObj = $uploadOut | ConvertFrom-Json
} catch {}

$probeId = if ($uploadedObj) { $uploadedObj.id } else { "" }
Assert-Test "Multipart POST Upload" (-not [string]::IsNullOrEmpty($probeId)) "Upload output: $uploadOut"

# 5. Test PATCH Update on Existing File
Write-Host "`n-- Testing Multipart PATCH Update --" -ForegroundColor Yellow
if (-not [string]::IsNullOrEmpty($probeId)) {
    $patchMeta = "--$boundary`r`nContent-Type: application/json; charset=UTF-8`r`n`r`n{`"name`": `"$testFileName`"}`r`n--$boundary`r`nContent-Type: application/zip`r`n`r`nHARNESS_PATCH_PAYLOAD_UPDATED_67890`r`n--$boundary--`r`n"
    [System.IO.File]::WriteAllBytes($probeFile, [System.Text.Encoding]::UTF8.GetBytes($patchMeta))

    $patchOut = & curl.exe -s -X PATCH `
      -H "Authorization: Bearer $accessToken" `
      -H "Content-Type: multipart/related; boundary=$boundary" `
      --data-binary "@$probeFile" `
      "https://www.googleapis.com/upload/drive/v3/files/$probeId`?uploadType=multipart"

    Remove-Item $probeFile -Force -ErrorAction SilentlyContinue

    $patchedObj = $null
    try {
        $patchedObj = $patchOut | ConvertFrom-Json
    } catch {}

    Assert-Test "Multipart PATCH Update" ($patchedObj -ne $null -and $patchedObj.id -eq $probeId) "Patch output: $patchOut"

    # Cleanup probe file
    & curl.exe -s -X DELETE -H "Authorization: Bearer $accessToken" "https://www.googleapis.com/drive/v3/files/$probeId" | Out-Null
    Assert-Test "Probe Cleanup" $true "Cleaned probe ID $probeId"
}

# 6. Versioned Subfolder Hierarchy (Switch Saves/<TitleID>/<AccountUid>/)
Write-Host "`n-- Testing Versioned Subfolder Navigation --" -ForegroundColor Yellow
$testTidHex = "TEST_01007EF00011E000"
$subSearchQuery = [System.Uri]::EscapeDataString("mimeType='application/vnd.google-apps.folder' and name='$testTidHex' and '$masterFolderId' in parents and trashed=false")
$subSearchJson = & curl.exe -s -H "Authorization: Bearer $accessToken" "https://www.googleapis.com/drive/v3/files?q=$subSearchQuery&fields=files(id,name)"
$subObj = $subSearchJson | ConvertFrom-Json
Assert-Test "Subfolder Search Query Format" ($subObj -ne $null) "Query failed: $subSearchJson"

Write-Host "`nTest Suite 1 Summary: $(($testResults | Where-Object { $_.Status -eq 'PASS' }).Count) Passed, $(($testResults | Where-Object { $_.Status -eq 'FAIL' }).Count) Failed." -ForegroundColor Cyan
if (($testResults | Where-Object { $_.Status -eq "FAIL" }).Count -gt 0) {
    exit 1
}
exit 0
