# Windows convenience wrapper only.
# PatchTrack's canonical build path is the normal U++ package build
# with umk or TheIDE. This script just repeats those U++ builds and
# runs the local smoke/protocol checks for Windows development.
param(
    [string]$UppRoot = "E:\upp-18468",
    [string]$RepoRoot = $PSScriptRoot,
    [switch]$SkipProtocolTests
)

$ErrorActionPreference = "Stop"

function Run-Step {
    param(
        [string]$Name,
        [scriptblock]$Body
    )

    Write-Host ""
    Write-Host "== $Name =="
    & $Body
}

$umk = Join-Path $UppRoot "umk.exe"
$assembly = "$RepoRoot,$UppRoot\uppsrc"
$outDir = Join-Path $RepoRoot "out"
$buildDir = Join-Path $RepoRoot "build"

if(!(Test-Path -LiteralPath $umk)) {
    throw "umk.exe not found at $umk"
}

New-Item -ItemType Directory -Force -Path $buildDir | Out-Null

Run-Step "Build patchtrack" {
    & $umk $assembly patchtrack CLANGx64 --out-dir $outDir -br +CONSOLE (Join-Path $buildDir "patchtrack")
}

Run-Step "Build patchtrack_mcp" {
    & $umk $assembly patchtrack_mcp CLANGx64 --out-dir $outDir -br +CONSOLE (Join-Path $buildDir "patchtrack_mcp")
}

Run-Step "Build patchtrack_tests" {
    & $umk $assembly patchtrack_tests CLANGx64 --out-dir $outDir -br +CONSOLE (Join-Path $buildDir "patchtrack_tests")
}

Run-Step "Engine selftest" {
    & (Join-Path $buildDir "patchtrack.exe") selftest
}

Run-Step "MCP selftest" {
    & (Join-Path $buildDir "patchtrack_mcp.exe") --selftest
}

if(!$SkipProtocolTests) {
    Run-Step "Protocol harness" {
        & (Join-Path $buildDir "patchtrack_tests.exe")
    }
}
else {
    Write-Host ""
    Write-Host "Protocol harness skipped."
}

Write-Host ""
Write-Host "verify.ps1: ok"
