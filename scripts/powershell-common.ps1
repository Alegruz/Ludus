function Initialize-LudusMsvcEnvironment {
    if ($env:OS -ne "Windows_NT") {
        return
    }

    if (Get-Command cl.exe -ErrorAction SilentlyContinue) {
        return
    }

    $vswherePath = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswherePath)) {
        throw "Visual Studio 2022 Build Tools were not found. Install the Desktop development with C++ workload."
    }

    $installationPath = & $vswherePath -latest -version "[17.0,18.0)" -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1
    if (-not $installationPath) {
        throw "Visual Studio 2022 Build Tools were not found. Install the Desktop development with C++ workload."
    }

    $vsDevCmdPath = Join-Path $installationPath "Common7\Tools\VsDevCmd.bat"
    if (-not (Test-Path $vsDevCmdPath)) {
        throw "VsDevCmd.bat was not found under the detected Visual Studio installation."
    }

    # Import the environment from a transient cmd session back into this PowerShell process.
    $envDump = & cmd.exe /s /c "\"$vsDevCmdPath\" -no_logo -arch=x64 -host_arch=x64 && set"
    if ($LASTEXITCODE -ne 0) {
        throw "Visual Studio 2022 could not initialize its x64 build environment."
    }

    foreach ($line in $envDump) {
        if ($line -notmatch "^([^=]+)=(.*)$") {
            continue
        }

        $name = $matches[1]
        $value = $matches[2]
        if ($name.StartsWith("=")) {
            continue
        }

        Set-Item -Path "Env:$name" -Value $value
    }

    if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
        throw "MSVC compiler (cl.exe) is still unavailable after running VsDevCmd.bat."
    }
}


function Get-LudusPythonLauncher {
    $python = Get-Command python -ErrorAction SilentlyContinue
    if ($python) {
        return @{ Path = $python.Source; PrefixArgs = @() }
    }

    $py = Get-Command py -ErrorAction SilentlyContinue
    if ($py) {
        return @{ Path = $py.Source; PrefixArgs = @("-3") }
    }

    return $null
}


function Invoke-LudusEngine {
    param(
        [Parameter(Mandatory = $true)]
        [string]$ScriptDir,
        [Parameter(Mandatory = $true)]
        [string]$Command,
        [string[]]$Arguments = @()
    )

    Initialize-LudusMsvcEnvironment

    $launcher = Get-LudusPythonLauncher
    if (-not $launcher) {
        Write-Error "Python is required to run Ludus scripts."
        return 1
    }

    & $launcher.Path @($launcher.PrefixArgs + @("$ScriptDir/python/engine.py", $Command) + $Arguments)
    return $LASTEXITCODE
}
