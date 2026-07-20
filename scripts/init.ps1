$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoDir = Split-Path -Parent $ScriptDir
& "$RepoDir/init.ps1" @args
exit $LASTEXITCODE
