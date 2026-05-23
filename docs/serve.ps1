<#
    .SYNOPSIS
        Lightweight local HTTP server for the Mocida documentation site.

    .DESCRIPTION
        Uses System.Net.HttpListener (ships with .NET on every Windows
        machine), so there is no dependency on Python, Node, or any
        third-party tool. Serves the contents of $Root as plain static
        files with MIME types inferred from the extension.

        Press Ctrl+C in the console to stop.

    .PARAMETER Root
        Directory to serve. Defaults to docs\generated\html relative to
        this script.

    .PARAMETER Port
        TCP port to bind on localhost. Default 8080.

    .PARAMETER Open
        If present, opens the default browser at the root URL once the
        listener is up.

    .EXAMPLE
        powershell -File docs\serve.ps1 -Port 4242 -Open
#>

param(
    [string] $Root = "",
    [int]    $Port = 8080,
    [switch] $Open
)

$ErrorActionPreference = "Stop"

# Resolve default root relative to the script location.
if ([string]::IsNullOrWhiteSpace($Root)) {
    $Root = Join-Path $PSScriptRoot "generated\html"
}

if (-not (Test-Path -Path $Root -PathType Container)) {
    Write-Host "[docs/serve] root not found: $Root" -ForegroundColor Red
    Write-Host "             run docs.bat first to generate the site."
    exit 1
}

# Canonicalize so we can detect path-traversal attempts.
$RootFull = (Resolve-Path -LiteralPath $Root).Path
Write-Host "[docs/serve] root  = $RootFull"
Write-Host "[docs/serve] url   = http://localhost:$Port/"

# MIME table — enough for what Doxygen emits + the custom theme.
$Mime = @{
    ".html" = "text/html; charset=utf-8"
    ".htm"  = "text/html; charset=utf-8"
    ".css"  = "text/css; charset=utf-8"
    ".js"   = "application/javascript; charset=utf-8"
    ".json" = "application/json; charset=utf-8"
    ".svg"  = "image/svg+xml"
    ".png"  = "image/png"
    ".jpg"  = "image/jpeg"
    ".jpeg" = "image/jpeg"
    ".gif"  = "image/gif"
    ".ico"  = "image/x-icon"
    ".webp" = "image/webp"
    ".woff" = "font/woff"
    ".woff2"= "font/woff2"
    ".ttf"  = "font/ttf"
    ".otf"  = "font/otf"
    ".map"  = "application/json"
    ".md"   = "text/markdown; charset=utf-8"
    ".txt"  = "text/plain; charset=utf-8"
    ".xml"  = "application/xml; charset=utf-8"
}

function Get-MimeType([string] $path) {
    $ext = [System.IO.Path]::GetExtension($path).ToLowerInvariant()
    if ($Mime.ContainsKey($ext)) { return $Mime[$ext] }
    return "application/octet-stream"
}

# Build the listener. Binding to "localhost" (not "+") means we don't
# need admin / URL ACL configuration on Windows.
$listener = New-Object System.Net.HttpListener
$prefix   = "http://localhost:$Port/"
$listener.Prefixes.Add($prefix)

try {
    $listener.Start()
} catch {
    Write-Host "[docs/serve] failed to bind $prefix" -ForegroundColor Red
    Write-Host "             $($_.Exception.Message)"
    Write-Host "             try a different port: docs.bat --serve --port 4242"
    exit 2
}

Write-Host "[docs/serve] listening. Ctrl+C to stop." -ForegroundColor Green

# Async accept loop so Ctrl+C in PowerShell delivers promptly. The
# blocking GetContext() in a tight while can swallow interrupts on
# older PS hosts. BeginGetContext + WaitOne(timeout) lets us poll.
$cancel = $false
[Console]::TreatControlCAsInput = $false
$null = Register-EngineEvent PowerShell.Exiting -Action { $cancel = $true } -SupportEvent

if ($Open) {
    try { Start-Process $prefix | Out-Null } catch { }
}

try {
    while ($listener.IsListening -and -not $cancel) {
        $async = $listener.BeginGetContext($null, $null)
        # 500 ms tick so Ctrl+C terminates within half a second.
        while (-not $async.AsyncWaitHandle.WaitOne(500)) {
            if (-not $listener.IsListening) { break }
        }
        if (-not $listener.IsListening) { break }

        try {
            $ctx  = $listener.EndGetContext($async)
        } catch {
            break
        }
        $req  = $ctx.Request
        $resp = $ctx.Response

        try {
            $urlPath = [System.Uri]::UnescapeDataString($req.Url.AbsolutePath)
            if ([string]::IsNullOrEmpty($urlPath) -or $urlPath -eq "/") {
                $urlPath = "/index.html"
            }

            # Strip leading slash, normalize separators.
            $rel = $urlPath.TrimStart('/').Replace('/', [IO.Path]::DirectorySeparatorChar)
            $full = [IO.Path]::GetFullPath((Join-Path $RootFull $rel))

            # Path-traversal guard: refuse to serve anything outside Root.
            if (-not $full.StartsWith($RootFull, [StringComparison]::OrdinalIgnoreCase)) {
                $resp.StatusCode = 403
                $bytes = [Text.Encoding]::UTF8.GetBytes("403 Forbidden`n")
                $resp.OutputStream.Write($bytes, 0, $bytes.Length)
                $resp.Close()
                Write-Host "  403  $urlPath" -ForegroundColor Yellow
                continue
            }

            # Directory request → look for index.html inside it.
            if ((Test-Path -LiteralPath $full -PathType Container)) {
                $full = Join-Path $full "index.html"
            }

            if (-not (Test-Path -LiteralPath $full -PathType Leaf)) {
                $resp.StatusCode = 404
                $body = "<!doctype html><meta charset=utf-8><title>404</title>" +
                        "<h1 style='font-family:sans-serif;color:#0f172a'>404 Not Found</h1>" +
                        "<p style='font-family:sans-serif;color:#64748b'>" +
                        [System.Net.WebUtility]::HtmlEncode($urlPath) +
                        "</p>"
                $bytes = [Text.Encoding]::UTF8.GetBytes($body)
                $resp.ContentType = "text/html; charset=utf-8"
                $resp.OutputStream.Write($bytes, 0, $bytes.Length)
                $resp.Close()
                Write-Host "  404  $urlPath" -ForegroundColor DarkGray
                continue
            }

            $resp.ContentType = Get-MimeType $full
            $resp.Headers.Add("Cache-Control", "no-cache")
            $stream = [IO.File]::OpenRead($full)
            try {
                $resp.ContentLength64 = $stream.Length
                $buf = New-Object byte[] 8192
                while (($n = $stream.Read($buf, 0, $buf.Length)) -gt 0) {
                    $resp.OutputStream.Write($buf, 0, $n)
                }
            } finally {
                $stream.Dispose()
            }
            $resp.OutputStream.Flush()
            $resp.Close()
            Write-Host ("  200  {0}  ({1} bytes)" -f $urlPath, $resp.ContentLength64) `
                -ForegroundColor DarkGreen
        } catch {
            try { $resp.StatusCode = 500; $resp.Close() } catch { }
            Write-Host "  500  $urlPath  $($_.Exception.Message)" -ForegroundColor Red
        }
    }
} finally {
    if ($listener.IsListening) { $listener.Stop() }
    $listener.Close()
    Write-Host "[docs/serve] stopped." -ForegroundColor Green
}
