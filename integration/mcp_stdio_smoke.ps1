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
            throw "MCP server closed stdout while reading a header."
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

function Read-Exact {
    param([System.IO.Stream]$Stream, [int]$Length)
    $buffer = New-Object byte[] $Length
    $offset = 0
    while($offset -lt $Length) {
        $read = $Stream.Read($buffer, $offset, $Length - $offset)
        if($read -le 0) {
            throw "MCP server closed stdout before the full response arrived."
        }
        $offset += $read
    }
    return $buffer
}

function Send-McpRequest {
    param([System.Diagnostics.Process]$Process, [string]$Body)
    $payload = [Text.Encoding]::UTF8.GetBytes($Body)
    $header = [Text.Encoding]::ASCII.GetBytes("Content-Length: $($payload.Length)`r`n`r`n")
    $frame = New-Object byte[] ($header.Length + $payload.Length)
    [Array]::Copy($header, 0, $frame, 0, $header.Length)
    [Array]::Copy($payload, 0, $frame, $header.Length, $payload.Length)
    $Process.StandardInput.BaseStream.Write($frame, 0, $frame.Length)
    $Process.StandardInput.BaseStream.Flush()

    $length = -1
    do {
        $line = Read-LineBytes $Process.StandardOutput.BaseStream
        if($line -match '^Content-Length:\s*(\d+)$') {
            $length = [int]$Matches[1]
        }
    } while($length -lt 0)
    [void](Read-LineBytes $Process.StandardOutput.BaseStream)
    return [Text.Encoding]::UTF8.GetString((Read-Exact $Process.StandardOutput.BaseStream $length))
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
    if($tools -notmatch 'patchtrack_apply') {
        throw "tools/list response did not expose patchtrack_apply."
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
