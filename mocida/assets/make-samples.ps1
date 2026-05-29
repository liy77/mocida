<#
    .SYNOPSIS
        Generate test media (sample.mp4 + click.wav) for Mocida's
        test_video / test_sound executables.

    .DESCRIPTION
        Uses ffmpeg to synthesize:
          - sample.mp4 — 1280×720, 8 s, H.264 + AAC, SMPTE bars +
                         sine sweep. ~200 KB.
          - click.wav  — 120 ms, 440 Hz mono sine. Matches what
                         test_sound.c::EnsureBeepFile builds at runtime;
                         created here so the test finds it on first run.

        Pass -Force to overwrite existing files. Pass -Verbose to see
        ffmpeg's stderr. By default existing outputs are left alone, so
        the script is safe to re-run.

    .PARAMETER Force
        Overwrite existing outputs.

    .PARAMETER Verbose
        Stream ffmpeg's stderr to the console instead of swallowing it.

    .EXAMPLE
        .\assets\make-samples.ps1
        .\assets\make-samples.ps1 -Force
#>

[CmdletBinding()]
param(
    [switch] $Force
)

$ErrorActionPreference = "Stop"
$here = Split-Path -Parent $MyInvocation.MyCommand.Path

function Need-Ffmpeg {
    $cmd = Get-Command ffmpeg -ErrorAction SilentlyContinue
    if (-not $cmd) {
        Write-Host "[make-samples] ffmpeg not found on PATH." -ForegroundColor Red
        Write-Host "               install it with:"
        Write-Host "                 winget install Gyan.FFmpeg"
        Write-Host "               then open a NEW terminal and re-run (PATH refresh)."
        exit 1
    }
    return $cmd.Source
}

# ---------------------------------------------------------------------
# PowerShell 5.1 wraps each line ffmpeg writes to stderr in an
# ErrorRecord (NativeCommandError), which combined with our top-level
# `$ErrorActionPreference = "Stop"` terminates the script on what is
# really just ffmpeg's normal progress output. Route ffmpeg through
# Start-Process with stderr → temp file: no stream crosses the
# PowerShell error pipeline, exit code is what we judge by.
# ---------------------------------------------------------------------
function Invoke-Ffmpeg {
    param(
        [string[]] $FfArgs,
        [string]   $Label
    )
    $ffmpeg = Need-Ffmpeg
    $stderrLog = [IO.Path]::GetTempFileName()
    $stdoutLog = [IO.Path]::GetTempFileName()
    try {
        $proc = Start-Process -FilePath $ffmpeg `
                              -ArgumentList $FfArgs `
                              -NoNewWindow -Wait -PassThru `
                              -RedirectStandardError  $stderrLog `
                              -RedirectStandardOutput $stdoutLog
        if ($proc.ExitCode -ne 0) {
            Write-Host "[make-samples] ffmpeg failed for $Label (exit $($proc.ExitCode))" -ForegroundColor Red
            Get-Content $stderrLog | Select-Object -Last 12 | ForEach-Object {
                Write-Host "  $_" -ForegroundColor DarkGray
            }
            return $false
        }
        if ($VerbosePreference -eq 'Continue') {
            Get-Content $stderrLog | ForEach-Object { Write-Verbose $_ }
        }
        return $true
    } finally {
        Remove-Item $stderrLog, $stdoutLog -ErrorAction SilentlyContinue
    }
}

function Make-Video($outFile) {
    if ((Test-Path $outFile) -and -not $Force) {
        Write-Host "[make-samples] keep $(Split-Path $outFile -Leaf)" -ForegroundColor DarkGray
        return
    }
    Write-Host "[make-samples] writing sample.mp4 (1280x720, 8 s)..." -ForegroundColor Green
    # SMPTE color bars + sine. Low CRF since test patterns compress
    # very efficiently. -movflags +faststart puts the moov atom at the
    # start so the file is playable while still streaming.
    $ffArgs = @(
        '-y',
        '-f', 'lavfi', '-i', 'smptebars=size=1280x720:rate=30:duration=8',
        '-f', 'lavfi', '-i', 'sine=frequency=220:beep_factor=2:sample_rate=48000:duration=8',
        '-c:v', 'libx264', '-preset', 'veryfast', '-crf', '28', '-pix_fmt', 'yuv420p',
        '-c:a', 'aac', '-b:a', '128k',
        '-movflags', '+faststart',
        $outFile
    )
    if (-not (Invoke-Ffmpeg -FfArgs $ffArgs -Label 'sample.mp4')) { exit 2 }
    if (-not (Test-Path $outFile)) {
        Write-Host "[make-samples] ffmpeg returned 0 but $outFile is missing" -ForegroundColor Red
        exit 2
    }
    $kb = [math]::Round((Get-Item $outFile).Length / 1KB, 1)
    Write-Host "[make-samples] ok    sample.mp4  ($kb KB)" -ForegroundColor Green
}

function Make-Wav($outFile) {
    if ((Test-Path $outFile) -and -not $Force) {
        Write-Host "[make-samples] keep $(Split-Path $outFile -Leaf)" -ForegroundColor DarkGray
        return
    }
    Write-Host "[make-samples] writing click.wav (440 Hz, 120 ms)..." -ForegroundColor Green
    $ffArgs = @(
        '-y',
        '-f', 'lavfi', '-i', 'sine=frequency=440:duration=0.12:sample_rate=44100',
        '-ac', '1', '-c:a', 'pcm_s16le',
        $outFile
    )
    if (-not (Invoke-Ffmpeg -FfArgs $ffArgs -Label 'click.wav')) { exit 2 }
    if (-not (Test-Path $outFile)) {
        Write-Host "[make-samples] ffmpeg returned 0 but $outFile is missing" -ForegroundColor Red
        exit 2
    }
    $b = (Get-Item $outFile).Length
    Write-Host "[make-samples] ok    click.wav  ($b bytes)" -ForegroundColor Green
}

Make-Video (Join-Path $here "sample.mp4")
Make-Wav   (Join-Path $here "click.wav")

Write-Host ""
Write-Host "[make-samples] done. Run:"
Write-Host "    .\build\win32\test_video.exe"
Write-Host "    .\build\win32\test_sound.exe"
