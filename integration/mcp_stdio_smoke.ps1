# Runs a real Windows stdio MCP smoke test against one long-lived server process.
# This complements the in-process and --oneshot protocol harness tests.

param(
    [string]$Server = (Join-Path $PSScriptRoot "..\build\patchtrack_mcp.exe")
)

$ErrorActionPreference = "Stop"

function Read-LineBytes {
    param([System.IO.Stream]$Stream)
    $bytes = New-Object System.Collections.Generic.List[byte]
    while($true) {
        $value = $Stream.ReadByte()
        if($value -lt 0) {
            throw "MCP server closed stdout while reading an MCP message."
        }
        if($value -eq 10) {
            break
        }
        if($value -ne 13) {
            [void]$bytes.Add([byte]$value)
        }
    }
    return [Text.Encoding]::ASCII.GetString($bytes.ToArray())
}

function Send-McpRequest {
    param([System.Diagnostics.Process]$Process, [string]$Body)
    $payload = [Text.Encoding]::UTF8.GetBytes($Body + "`n")
    $Process.StandardInput.BaseStream.Write($payload, 0, $payload.Length)
    $Process.StandardInput.BaseStream.Flush()
    return Read-LineBytes $Process.StandardOutput.BaseStream
}

if(!(Test-Path -LiteralPath $Server)) {
    throw "MCP server not found: $Server"
}

$start = New-Object System.Diagnostics.ProcessStartInfo
$start.FileName = (Resolve-Path -LiteralPath $Server).Path
$start.UseShellExecute = $false
$start.CreateNoWindow = $true
$start.RedirectStandardInput = $true
$start.RedirectStandardOutput = $true
$start.RedirectStandardError = $true
$process = New-Object System.Diagnostics.Process
$process.StartInfo = $start

try {
    if(!$process.Start()) {
        throw "MCP server failed to start."
    }

    $initialize = Send-McpRequest $process '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{}}'
    $tools = Send-McpRequest $process '{"jsonrpc":"2.0","id":2,"method":"tools/list"}'
    if($initialize -notmatch 'patchtrack_mcp') {
        throw "initialize response did not identify patchtrack_mcp."
    }
    if($tools -notmatch '"name"\s*:\s*"apply"') {
        throw "tools/list response did not expose apply."
    }
    if($process.HasExited) {
        throw "MCP server exited between requests."
    }
    Write-Host "mcp-stdio-smoke: ok"
}
finally {
    if(!$process.HasExited) {
        $process.StandardInput.Close()
        if(!$process.WaitForExit(3000)) {
            $process.Kill()
        }
    }
    $process.Dispose()
}
