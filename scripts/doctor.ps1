$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$Python = Get-Command python -ErrorAction SilentlyContinue
if (-not $Python) {
    $Python = Get-Command py -ErrorAction SilentlyContinue
}
if (-not $Python) {
    Write-Error "Python is required to run Ludus scripts."
    exit 1
}
& $Python.Source "$ScriptDir/python/engine.py" doctor @args
exit $LASTEXITCODE
