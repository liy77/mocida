#!/usr/bin/env pwsh
# Thin wrapper: all build logic lives in build.py. This just locates a
# Python 3 interpreter and forwards every argument to it.
#   .\build.ps1 [all|c|rust|setup|clean] [--config release] [--force] ...

$ErrorActionPreference = 'Stop'
$py = $null
foreach ($cand in @('python', 'python3', 'py')) {
    $cmd = Get-Command $cand -ErrorAction SilentlyContinue
    if ($cmd) { $py = $cmd.Source; break }
}
if (-not $py) {
    Write-Error "Python 3 not found on PATH (tried python, python3, py). Install it and retry."
    exit 1
}

& $py (Join-Path $PSScriptRoot 'build.py') @args
exit $LASTEXITCODE
