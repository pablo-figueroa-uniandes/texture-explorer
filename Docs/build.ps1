<#
.SYNOPSIS
  Builds Docs\TextureExplorer-Guide.pdf, the guide to the theory and the code of Texture Explorer.

.DESCRIPTION
  1. Extracts the code excerpts listed in excerpts.txt from the real source files into
     build\excerpts, so the listings in the guide always match the current code. Each
     excerpt gets a small .tex wrapper with the file name and line numbers.
  2. Runs a TeX engine on guide.tex: latexmk, pdflatex or tectonic, whichever is found
     first, or the one passed with -TeX.
  3. Copies the result to Docs\TextureExplorer-Guide.pdf.

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File Docs\build.ps1
  powershell -ExecutionPolicy Bypass -File Docs\build.ps1 -TeX C:\tools\tectonic.exe
#>
param([string]$TeX, [switch]$ExcerptsOnly)

$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$root = Split-Path -Parent $here
$build = Join-Path $here 'build'
$exDir = Join-Path $build 'excerpts'
$utf8 = New-Object System.Text.UTF8Encoding $false
New-Item -ItemType Directory -Force $exDir | Out-Null

# ---------------------------------------------------------------- excerpts
function Escape-TeX([string]$s) {
    return ($s -replace '\\', '/' -replace '_', '\_' -replace '#', '\#' -replace '&', '\&' -replace '%', '\%')
}

$count = 0
foreach ($spec in [IO.File]::ReadAllLines((Join-Path $here 'excerpts.txt'), $utf8)) {
    if ($spec.Trim() -eq '' -or $spec.TrimStart().StartsWith('#')) { continue }
    $f = $spec -split '\s*\|\s*'
    $name, $file, $lang, $startRx = $f[0].Trim(), $f[1].Trim(), $f[2].Trim(), $f[3]
    $endRx = if ($f.Count -gt 4) { $f[4] } else { $null }
    $lines = [IO.File]::ReadAllLines((Join-Path $root $file), $utf8)

    $start = -1
    for ($i = 0; $i -lt $lines.Count; $i++) { if ($lines[$i] -match $startRx) { $start = $i; break } }
    if ($start -lt 0) { throw "excerpt '$name': no line of $file matches /$startRx/" }

    # The excerpt ends at the line matching the end pattern or, without one, where the
    # braces opened from the start line are balanced again.
    $end = -1
    if ($endRx) {
        for ($i = $start; $i -lt $lines.Count; $i++) { if ($lines[$i] -match $endRx) { $end = $i; break } }
    } else {
        $depth = 0; $opened = $false
        for ($i = $start; $i -lt $lines.Count; $i++) {
            $code = ($lines[$i] -replace '"(\\.|[^"\\])*"', '""') -replace '//.*$', ''
            foreach ($ch in $code.ToCharArray()) {
                if ($ch -eq '{') { $depth++; $opened = $true } elseif ($ch -eq '}') { $depth-- }
            }
            if ($opened -and $depth -le 0) { $end = $i; break }
        }
    }
    if ($end -lt 0) { throw "excerpt '$name': cannot find its end in $file" }

    $body = $lines[$start..$end]
    # Remove the indentation common to all lines, for excerpts taken from inside a function.
    $indent = ($body | Where-Object { $_.Trim() } | ForEach-Object { ([regex]::Match($_, '^ *')).Length } |
               Measure-Object -Minimum).Minimum
    if ($indent -gt 0) { $body = $body | ForEach-Object { if ($_.Length -ge $indent) { $_.Substring($indent) } else { $_.TrimStart() } } }
    [IO.File]::WriteAllText((Join-Path $exDir "$name.txt"), (($body -join "`n") + "`n"), $utf8)
    $first = $start + 1; $last = $end + 1
    $wrapper = "\lstinputlisting[style=excerpt,language=$lang,firstnumber=$first," +
               "title={\excerpttitle{$(Escape-TeX $file)}{$first}{$last}}]{build/excerpts/$name.txt}"
    [IO.File]::WriteAllText((Join-Path $exDir "$name.tex"), $wrapper + "`n", $utf8)
    $count++
}
Write-Host "Extracted $count excerpts into $exDir"
if ($ExcerptsOnly) { return }

# ---------------------------------------------------------------- typesetting
Push-Location $here
try {
    if ($TeX) {
        & $TeX guide.tex
    } elseif (Get-Command latexmk -ErrorAction SilentlyContinue) {
        & latexmk -pdf -interaction=nonstopmode -outdir=build guide.tex
    } elseif (Get-Command pdflatex -ErrorAction SilentlyContinue) {
        # Three runs settle the contents, the cross-references and the hyperlinks.
        1..3 | ForEach-Object { & pdflatex -interaction=nonstopmode -output-directory=build guide.tex | Out-Null }
    } elseif (Get-Command tectonic -ErrorAction SilentlyContinue) {
        & tectonic --outdir build guide.tex
    } else {
        throw 'No TeX engine found. Install MiKTeX or TeX Live (pdflatex), or tectonic, or pass -TeX <engine>.'
    }
    if ($LASTEXITCODE -ne 0) { throw "TeX failed with exit code $LASTEXITCODE" }
} finally {
    Pop-Location
}
$pdf = @((Join-Path $build 'guide.pdf'), (Join-Path $here 'guide.pdf')) | Where-Object { Test-Path $_ } |
       Sort-Object { (Get-Item $_).LastWriteTime } | Select-Object -Last 1
if (-not $pdf) { throw 'TeX did not produce guide.pdf' }
Copy-Item $pdf (Join-Path $here 'TextureExplorer-Guide.pdf') -Force
if ($pdf -eq (Join-Path $here 'guide.pdf')) { Remove-Item $pdf }
Write-Host "Wrote $(Join-Path $here 'TextureExplorer-Guide.pdf')"
