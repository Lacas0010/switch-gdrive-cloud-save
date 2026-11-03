Write-Host "===============================================================================" -ForegroundColor Cyan
Write-Host "   Google Drive Cloud Save Suite (gdrive-sync) - PowerShell Test Runner" -ForegroundColor Cyan
Write-Host "===============================================================================" -ForegroundColor Cyan

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RootDir = Split-Path -Parent $ScriptDir

# Executa suite unificada via Python
python "$ScriptDir\run_tests.py"
$exitCode = $LASTEXITCODE

if ($exitCode -eq 0) {
    Write-Host "`n[SUCESSO] Todos os testes passaram sem erros!" -ForegroundColor Green
} else {
    Write-Host "`n[ERRO] Foram detectadas falhas durante a execucao dos testes." -ForegroundColor Red
}

exit $exitCode
