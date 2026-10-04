#requires -Version 5.1
<#
  Build PCM2He directly with MinGW g++ (no CMake required).
  Usage:
    .\build.ps1            # outputs build\pcm2he.exe
    .\build.ps1 -Run ...   # build then run, passing extra arguments through
#>
param(
    [switch]$Clean,
    [switch]$Run,
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$RunArgs
)

$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
$src = Join-Path $root "src"
$out = Join-Path $root "build"
$exe = Join-Path $out "pcm2he.exe"

if ($Clean -and (Test-Path -LiteralPath $out)) {
    Remove-Item -LiteralPath $out -Recurse -Force
}
if (-not (Test-Path -LiteralPath $out)) {
    New-Item -ItemType Directory -Path $out | Out-Null
}

$sources = Get-ChildItem -LiteralPath $src -Filter *.cpp | ForEach-Object { $_.FullName }
if (-not $sources) { throw "no source files found in: $src" }

$gargs = @(
    "-std=c++17", "-O2", "-Wall", "-Wextra",
    "-I", $src,
    "-o", $exe
) + $sources

Write-Host "g++ $($sources.Count) source file(s) -> $exe"
& g++ @gargs
if ($LASTEXITCODE -ne 0) { throw "build failed (exit $LASTEXITCODE)" }
Write-Host "build succeeded: $exe"

if ($Run) {
    & $exe @RunArgs
}
