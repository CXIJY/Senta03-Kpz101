# Build a portable folder for copying this app to another Windows PC.

$ErrorActionPreference = "Stop"

$projectRoot = $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($projectRoot))
{
    $projectRoot = (Get-Location).Path
}

$packageRoot = Join-Path $projectRoot "portable_package"
$appFolder = Join-Path $packageRoot "1_portable"
$captureFolder = Join-Path $appFolder "captures"

Remove-Item -LiteralPath $packageRoot -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Path $appFolder | Out-Null
New-Item -ItemType Directory -Path $captureFolder | Out-Null

$copyPairs = @(
    @((Join-Path $projectRoot "1.exe"), "1.exe"),
    @("C:\mingw64\bin\libgcc_s_seh-1.dll", "libgcc_s_seh-1.dll"),
    @("C:\mingw64\bin\libstdc++-6.dll", "libstdc++-6.dll"),
    @("C:\mingw64\bin\libwinpthread-1.dll", "libwinpthread-1.dll"),
    @("C:\Program Files\Thorlabs\Kinesis\Thorlabs.MotionControl.KCube.Piezo.dll", "Thorlabs.MotionControl.KCube.Piezo.dll"),
    @("C:\Program Files\Thorlabs\Kinesis\Thorlabs.MotionControl.DeviceManager.dll", "Thorlabs.MotionControl.DeviceManager.dll"),
    @("C:\Program Files\Thorlabs\Kinesis\ftd2xx.dll", "ftd2xx.dll"),
    @("C:\Program Files\Thorlabs\Kinesis\ThorlabsDefaultSettings.xml", "ThorlabsDefaultSettings.xml"),
    @((Join-Path $projectRoot "PORTABLE_README.txt"), "PORTABLE_README.txt")
)

foreach ($pair in $copyPairs)
{
    $sourcePath = $pair[0]
    $targetName = $pair[1]

    if (-not (Test-Path $sourcePath))
    {
        throw "Missing required file: $sourcePath"
    }

    Copy-Item -LiteralPath $sourcePath `
              -Destination (Join-Path $appFolder $targetName) `
              -Force
}

Write-Host "Portable package created at: $appFolder"
