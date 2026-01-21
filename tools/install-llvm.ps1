<#!
Installs LLVM (clang-tidy, clang-format) on Windows.
- Installs Chocolatey if missing.
- Installs llvm package.
- Adds LLVM bin to PATH for the current session.
Requires Administrator for installation steps.
#>

[CmdletBinding()]
param()

function Assert-Admin {
    if (-not ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        Write-Host "ERROR: Please run this script as Administrator." -ForegroundColor Red
        exit 1
    }
}

function Ensure-Choco {
    if (Get-Command choco -ErrorAction SilentlyContinue) { return }
    Write-Host "Chocolatey not found. Installing Chocolatey..." -ForegroundColor Yellow
    Set-ExecutionPolicy Bypass -Scope Process -Force
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    Invoke-Expression ((New-Object Net.WebClient).DownloadString('https://community.chocolatey.org/install.ps1'))
    if (-not (Get-Command choco -ErrorAction SilentlyContinue)) {
        Write-Host "ERROR: Chocolatey installation failed." -ForegroundColor Red
        exit 1
    }
}

function Install-LLVM {
    Write-Host "Installing LLVM (clang-tidy, clang-format)..." -ForegroundColor Cyan
    choco install llvm -y --no-progress
    if ($LASTEXITCODE -ne 0) {
        Write-Host "ERROR: LLVM installation failed." -ForegroundColor Red
        exit 1
    }
}

function Update-PathPermanent {
    $llvmBin = "C:\Program Files\LLVM\bin"
    if (-not (Test-Path $llvmBin)) {
        Write-Host "LLVM bin directory not found: $llvmBin" -ForegroundColor Yellow
        return
    }
    
    # Get current machine PATH
    $machinePath = [Environment]::GetEnvironmentVariable("Path", "Machine")
    
    # Check if LLVM is already in PATH
    if ($machinePath -like "*$llvmBin*") {
        Write-Host "LLVM already in system PATH" -ForegroundColor Green
    } else {
        Write-Host "Adding LLVM to system PATH permanently..." -ForegroundColor Cyan
        $newPath = "$llvmBin;$machinePath"
        [Environment]::SetEnvironmentVariable("Path", $newPath, "Machine")
        Write-Host "System PATH updated permanently" -ForegroundColor Green
    }
    
    # Update current session PATH
    $env:PATH = "$llvmBin;$env:PATH"
    Write-Host "Session PATH updated" -ForegroundColor Green
}

Assert-Admin
Ensure-Choco
Install-LLVM
Update-PathPermanent

Write-Host "SUCCESS: LLVM installed and PATH configured. Restart your terminal/VS Code for PATH changes to take effect." -ForegroundColor Green
