# F4Menu Build Script - MSBuild
# Usage:
#   .\build.ps1              # Build Release x64 (default)
#   .\build.ps1 -Platform Win32   # Build Release Win32
#   .\build.ps1 -Clean            # Clean build artifacts
#   .\build.ps1 -Verbose          # Show MSBuild output

param(
    [ValidateSet("x64", "Win32")]
    [string]$Platform = "x64",
    [switch]$Clean,
    [switch]$Verbose
)

$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$MSBuildPath = $null

$vsWhere = "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"
if (Test-Path $vsWhere) {
    $installPath = & $vsWhere -all -products * -property installationPath 2>$null
    if ($installPath) {
        foreach ($ip in $installPath) {
            $candidates = @(
                (Join-Path $ip "MSBuild\Current\Bin\amd64\MSBuild.exe"),
                (Join-Path $ip "MSBuild\Current\Bin\MSBuild.exe")
            )
            foreach ($c in $candidates) {
                if (Test-Path $c) { $MSBuildPath = $c; break }
            }
            if ($MSBuildPath) { break }
        }
    }
}

if (-not $MSBuildPath) {
    Write-Host "ERROR: MSBuild not found. Please install VS Build Tools." -ForegroundColor Red
    exit 1
}

Write-Host "MSBuild: $MSBuildPath" -ForegroundColor Cyan
Write-Host "Platform: $Platform" -ForegroundColor Cyan

$ProjectFile = Join-Path $ScriptDir "F4Menu.vcxproj"

if ($Clean) {
    Write-Host "`nCleaning..." -ForegroundColor Yellow
    & $MSBuildPath $ProjectFile /p:Platform=$Platform /t:Clean /v:minimal
    $objDir = Join-Path $ScriptDir "obj\$Platform"
    if (Test-Path $objDir) { Remove-Item $objDir -Recurse -Force }
    Write-Host "Done." -ForegroundColor Green
    exit 0
}

$verbosity = if ($Verbose) { "normal" } else { "minimal" }

Write-Host "`nBuilding F4Menu ($Platform Release)..." -ForegroundColor Yellow

& $MSBuildPath $ProjectFile `
    /p:Configuration=Release `
    /p:Platform=$Platform `
    /p:SolutionDir="$ScriptDir\\" `
    /v:$verbosity `
    /nologo

if ($LASTEXITCODE -eq 0) {
    $exe = Join-Path $ScriptDir "F4Menu.exe"
    if (Test-Path $exe) {
        $info = Get-Item $exe
        $sizeKB = [math]::Round($info.Length / 1024, 1)
        Write-Host "`nBuild successful! F4Menu.exe ($sizeKB KB)" -ForegroundColor Green
    }
} else {
    Write-Host "`nBuild failed!" -ForegroundColor Red
    exit 1
}
