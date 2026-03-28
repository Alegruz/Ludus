# Clean build directories with corrupted Ninja files
# Usage: .\scripts\clean-build.ps1 [preset-pattern]
# Example: .\scripts\clean-build.ps1 d3d12

param(
    [string]$PresetPattern = "*"
)

$BuildDir = "out\build"
$Removed = 0

if (-not (Test-Path $BuildDir)) {
    Write-Host "Build directory not found: $BuildDir"
    exit 0
}

Get-ChildItem -Path $BuildDir -Directory -Filter "*$PresetPattern*" | ForEach-Object {
    $BuildPath = $_.FullName
    $NinjaFile = Join-Path $BuildPath "build.ninja"
    
    if (Test-Path $NinjaFile) {
        try {
            # Try to read the file to check if it's corrupted
            $Content = Get-Content $NinjaFile -TotalCount 50 -ErrorAction Stop
            if ($Content -match "CMakeFiles\\rules\.ninja") {
                Write-Host "Removing corrupted build: $($_.Name)"
                Remove-Item -Recurse -Force $BuildPath
                $Removed++
            }
        } catch {
            Write-Host "Removing corrupted build: $($_.Name) (unreadable build.ninja)"
            Remove-Item -Recurse -Force $BuildPath
            $Removed++
        }
    }
}

if ($Removed -gt 0) {
    Write-Host "`nCleaned $Removed corrupted build directory/ies"
    Write-Host "Next steps:"
    Write-Host "  1. Run: .\init.bat --install"
    Write-Host "  2. Run: cmake --preset <preset>"
} else {
    Write-Host "No corrupted builds found"
}
