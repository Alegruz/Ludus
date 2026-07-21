$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
. "$ScriptDir/powershell-common.ps1"
$ExitCode = Invoke-LudusEngine -ScriptDir $ScriptDir -Command "check" -Arguments $args
exit $ExitCode
