param(
    [string]$QtRoot = "D:\Qt\6.8.3\mingw_64",
    [string]$MingwBin = "D:\Qt\Tools\mingw1310_64\bin",
    [string]$BuildDirectory,
    [string]$OutputDirectory,
    [string]$Version = "0.1.0",
    [switch]$CreateArchive
)

$ErrorActionPreference = "Stop"

$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
if (-not $BuildDirectory) {
    $BuildDirectory = Join-Path $projectRoot "qt\build-release"
}
if (-not $OutputDirectory) {
    $OutputDirectory = Join-Path $projectRoot "dist\RagDiagnosticWorkbench"
}

$buildDirectoryPath = [System.IO.Path]::GetFullPath($BuildDirectory)
$outputDirectoryPath = [System.IO.Path]::GetFullPath($OutputDirectory)
$allowedDistRoot = [System.IO.Path]::GetFullPath((Join-Path $projectRoot "dist"))
if (-not $outputDirectoryPath.StartsWith($allowedDistRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "OutputDirectory must stay inside $allowedDistRoot"
}

$application = Join-Path $buildDirectoryPath "RagDiagnosticWorkbench.exe"
$deployTool = Join-Path $QtRoot "bin\windeployqt.exe"
foreach ($requiredPath in @($application, $deployTool)) {
    if (-not (Test-Path -LiteralPath $requiredPath -PathType Leaf)) {
        throw "Required file not found: $requiredPath"
    }
}

if (Test-Path -LiteralPath $outputDirectoryPath) {
    Remove-Item -LiteralPath $outputDirectoryPath -Recurse -Force
}
New-Item -ItemType Directory -Path $outputDirectoryPath | Out-Null

Copy-Item -LiteralPath $application -Destination $outputDirectoryPath
$previousPath = $env:Path
try {
    $env:Path = "$MingwBin;$QtRoot\bin;$env:Path"
    & $deployTool --release --no-translations --compiler-runtime --no-network `
        --no-system-d3d-compiler --no-system-dxc-compiler --no-opengl-sw `
        --skip-plugin-types generic,networkinformation,tls `
        --exclude-plugins qsqlmimer,qsqlodbc,qsqlpsql `
        --dir $outputDirectoryPath $application
    $deployExitCode = $LASTEXITCODE
} finally {
    $env:Path = $previousPath
}
if ($deployExitCode -ne 0) {
    throw "windeployqt failed with exit code $deployExitCode"
}

foreach ($runtime in @("libgcc_s_seh-1.dll", "libstdc++-6.dll", "libwinpthread-1.dll")) {
    $runtimePath = Join-Path $MingwBin $runtime
    if ((Test-Path -LiteralPath $runtimePath -PathType Leaf) -and
        -not (Test-Path -LiteralPath (Join-Path $outputDirectoryPath $runtime))) {
        Copy-Item -LiteralPath $runtimePath -Destination $outputDirectoryPath
    }
}

$packagingRoot = Join-Path $projectRoot "packaging\windows"
foreach ($file in @("Launch-RagWorkbench.ps1", "Launch-RagWorkbench.cmd", "README-WINDOWS.md")) {
    Copy-Item -LiteralPath (Join-Path $packagingRoot $file) -Destination $outputDirectoryPath
}

$smokeExecutables = Get-ChildItem -LiteralPath $outputDirectoryPath -Filter "*Smoke.exe" -File
if ($smokeExecutables) {
    throw "Smoke executables must not be present in the release directory."
}

$hash = Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $outputDirectoryPath "RagDiagnosticWorkbench.exe")
$manifest = @(
    "RagDiagnosticWorkbench Windows package"
    "Built: $([DateTimeOffset]::Now.ToString('yyyy-MM-ddTHH:mm:sszzz'))"
    "Executable SHA-256: $($hash.Hash)"
    "Runtime model: source-assisted; Python, models, manuals and indexes are not bundled."
) -join [Environment]::NewLine
Set-Content -LiteralPath (Join-Path $outputDirectoryPath "BUILD-INFO.txt") -Value $manifest -Encoding UTF8

Write-Host "Windows package created: $outputDirectoryPath"
Write-Host "Executable SHA-256: $($hash.Hash)"

if ($CreateArchive) {
    $archivePath = Join-Path $allowedDistRoot "RagDiagnosticWorkbench-v$Version-windows-x64.zip"
    Compress-Archive -Path (Join-Path $outputDirectoryPath "*") -DestinationPath $archivePath -Force
    $archiveHash = Get-FileHash -Algorithm SHA256 -LiteralPath $archivePath
    Write-Host "Release archive created: $archivePath"
    Write-Host "Archive SHA-256: $($archiveHash.Hash)"
}
