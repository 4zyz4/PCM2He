#requires -Version 5.1
<#
  用 MinGW g++ 直接编译 PCM2He（无需 CMake）。
  用法：
    .\build.ps1            # 输出 build\pcm2he.exe
    .\build.ps1 -Run ...   # 编译后立即运行，附带的参数透传给程序
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
if (-not $sources) { throw "未找到源文件: $src" }

$gargs = @(
    "-std=c++17", "-O2", "-Wall", "-Wextra",
    "-I", $src,
    "-o", $exe
) + $sources

Write-Host "g++ $($sources.Count) 个源文件 -> $exe"
& g++ @gargs
if ($LASTEXITCODE -ne 0) { throw "编译失败 (exit $LASTEXITCODE)" }
Write-Host "构建成功: $exe"

if ($Run) {
    & $exe @RunArgs
}
