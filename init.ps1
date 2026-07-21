$RepoDir = Split-Path -Parent $MyInvocation.MyCommand.Path
. "$RepoDir/scripts/powershell-common.ps1"
$ExitCode = Invoke-LudusEngine -ScriptDir "$RepoDir/scripts" -Command "init" -Arguments $args
exit $ExitCode
