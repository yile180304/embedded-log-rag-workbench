param(
    [string]$ProjectRoot,
    [Alias("WorkbenchArguments")]
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$Arguments = @()
)

$ErrorActionPreference = "Stop"
$packageDirectory = $PSScriptRoot

if (-not $ProjectRoot) {
    $candidate = [System.IO.DirectoryInfo]$packageDirectory
    for ($level = 0; $level -lt 6 -and $candidate; $level++) {
        if (Test-Path -LiteralPath (Join-Path $candidate.FullName "pyproject.toml") -PathType Leaf) {
            $ProjectRoot = $candidate.FullName
            break
        }
        $candidate = $candidate.Parent
    }
}

if (-not $ProjectRoot) {
    throw "Cannot find the RAG project root. Run with -ProjectRoot <path-to-repository>."
}

$resolvedProjectRoot = (Resolve-Path -LiteralPath $ProjectRoot).Path
$pythonExecutable = Join-Path $resolvedProjectRoot ".venv312\Scripts\python.exe"
$application = Join-Path $packageDirectory "RagDiagnosticWorkbench.exe"

if (-not (Test-Path -LiteralPath (Join-Path $resolvedProjectRoot "pyproject.toml") -PathType Leaf)) {
    throw "ProjectRoot does not contain pyproject.toml: $resolvedProjectRoot"
}
if (-not (Test-Path -LiteralPath $pythonExecutable -PathType Leaf)) {
    throw "Python environment not found: $pythonExecutable"
}
if (-not (Test-Path -LiteralPath $application -PathType Leaf)) {
    throw "Workbench executable not found: $application"
}

$env:RAG_DIAGNOSTIC_PROJECT_ROOT = $resolvedProjectRoot
$env:RAG_DIAGNOSTIC_PYTHON_EXECUTABLE = $pythonExecutable
$env:PYTHONUTF8 = "1"
$env:PYTHONIOENCODING = "utf-8"

$escapedArguments = foreach ($argument in $Arguments) {
    '"' + $argument.Replace('"', '\"') + '"'
}
if ($Arguments.Count -gt 0) {
    $process = Start-Process -FilePath $application -ArgumentList $escapedArguments -Wait -PassThru
} else {
    $process = Start-Process -FilePath $application -Wait -PassThru
}
exit $process.ExitCode
