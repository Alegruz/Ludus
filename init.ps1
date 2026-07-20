$RepoDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$Python = Get-Command python -ErrorAction SilentlyContinue
if (-not $Python) {
    $Python = Get-Command py -ErrorAction SilentlyContinue
}
if (-not $Python) {
    Write-Error "Python is required before Ludus can initialize this host."
    exit 1
}
& $Python.Source "$RepoDir/scripts/python/engine.py" init @args
exit $LASTEXITCODE
