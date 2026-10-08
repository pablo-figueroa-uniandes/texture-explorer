<#
.SYNOPSIS
  A small literate-programming toolchain in the spirit of Knuth's WEB/CWEB.

.DESCRIPTION
  The web (TextureExplorer.nw and the parts it includes) is a sequence of numbered
  sections. Each section has a TeX part (commentary) and an optional code part.

    @* Title.  text...    starts a "starred" section: a major group, listed in the contents
    @ text...             starts an ordinary section
    <<Name>>=             starts the code part; it runs until the next line beginning with @
    <<Name>>              (alone on a code line) refers to another module; tangle expands it
    <<file:path>>=        a root module, written to the file "path" by tangle
    |code|                inside commentary, typesets "code" in typewriter type
    @i file               includes another file of the web at this point

  A module may be defined in several sections; tangle joins the pieces in order,
  separated by one blank line. Trailing blank lines of every code part are ignored.

  Commands:
    tangle  writes every root module into out\tangled (or -Target <dir>)
    check   tangles and compares the result with the real program files in ..\
    weave   writes out\TextureExplorer.tex (+ lpweb.sty)
    pdf     weaves and runs a TeX engine (latexmk, pdflatex or tectonic)
    all     check + pdf (default)

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File lp.ps1 pdf
#>
param(
    [Parameter(Position = 0)]
    [ValidateSet('tangle', 'check', 'weave', 'pdf', 'all')]
    [string]$Command = 'all',
    [string]$Web,
    [string]$Target,
    [string]$OutDir,
    [string]$TeX
)

$ErrorActionPreference = 'Stop'
# Windows PowerShell 5.1 does not set $PSScriptRoot while evaluating parameter defaults.
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
if (-not $Web) { $Web = Join-Path $here 'TextureExplorer.nw' }
if (-not $OutDir) { $OutDir = Join-Path $here 'out' }
$utf8 = New-Object System.Text.UTF8Encoding $false

# ------------------------------------------------------------------ reading the web
function Read-Web([string]$path) {
    $dir = Split-Path -Parent $path
    $result = New-Object System.Collections.Generic.List[string]
    foreach ($line in [IO.File]::ReadAllLines($path, $utf8)) {
        if ($line -match '^@i\s+(\S+)\s*$') {
            foreach ($l in (Read-Web (Join-Path $dir $Matches[1]))) { $result.Add($l) }
        } else {
            $result.Add($line)
        }
    }
    return , $result
}

function Parse-Web([string]$path) {
    $web = @{
        Limbo    = New-Object System.Collections.Generic.List[string]
        Sections = New-Object System.Collections.Generic.List[object]
        Chunks   = [ordered]@{}   # name -> list of definitions (each: Section, Lines)
        Uses     = @{}            # name -> sorted list of section numbers
    }
    $cur = $null; $code = $null; $mode = 'limbo'
    foreach ($line in (Read-Web $path)) {
        if ($line -match '^@\*\s*(.*)$') {
            $rest = $Matches[1]; $title = $rest; $text = ''
            if ($rest -match '^([^.]*)\.\s*(.*)$') { $title = $Matches[1]; $text = $Matches[2] }
            $cur = @{ Number = $web.Sections.Count + 1; Starred = $true; Title = $title
                      Doc = New-Object System.Collections.Generic.List[string]
                      Code = New-Object System.Collections.Generic.List[object] }
            if ($text) { $cur.Doc.Add($text) }
            $web.Sections.Add($cur); $mode = 'doc'
        } elseif ($line -match '^@(\s(.*))?$') {
            $cur = @{ Number = $web.Sections.Count + 1; Starred = $false; Title = ''
                      Doc = New-Object System.Collections.Generic.List[string]
                      Code = New-Object System.Collections.Generic.List[object] }
            if ($Matches[2]) { $cur.Doc.Add($Matches[2]) }
            $web.Sections.Add($cur); $mode = 'doc'
        } elseif ($line -match '^<<(.+)>>=\s*$') {
            if (-not $cur) { throw "Code part outside of a section: $line" }
            $code = @{ Name = $Matches[1]; Lines = New-Object System.Collections.Generic.List[string]; Section = $cur.Number }
            $cur.Code.Add($code); $mode = 'code'
        } elseif ($mode -eq 'code') {
            $code.Lines.Add($line)
        } elseif ($mode -eq 'doc') {
            $cur.Doc.Add($line)
        } else {
            $web.Limbo.Add($line)
        }
    }
    foreach ($s in $web.Sections) {
        foreach ($c in $s.Code) {
            while ($c.Lines.Count -gt 0 -and $c.Lines[$c.Lines.Count - 1].Trim() -eq '') { $c.Lines.RemoveAt($c.Lines.Count - 1) }
            if (-not $web.Chunks.Contains($c.Name)) { $web.Chunks[$c.Name] = New-Object System.Collections.Generic.List[object] }
            $web.Chunks[$c.Name].Add($c)
            foreach ($l in $c.Lines) {
                foreach ($m in [regex]::Matches($l, '<<(.+?)>>')) {
                    $n = $m.Groups[1].Value
                    if (-not $web.Uses.ContainsKey($n)) { $web.Uses[$n] = New-Object System.Collections.Generic.List[int] }
                    if (-not $web.Uses[$n].Contains($s.Number)) { $web.Uses[$n].Add($s.Number) }
                }
            }
        }
    }
    foreach ($n in $web.Uses.Keys) { if (-not $web.Chunks.Contains($n)) { throw "Module <<$n>> is used but never defined" } }
    foreach ($n in $web.Chunks.Keys) {
        if (-not $n.StartsWith('file:') -and -not $web.Uses.ContainsKey($n)) { Write-Warning "Module <<$n>> is never used" }
    }
    return $web
}

# ------------------------------------------------------------------ tangle
function Expand-Chunk($web, [string]$name, [string]$prefix, $out, [string[]]$stack) {
    if ($stack -contains $name) { throw "Module <<$name>> refers to itself" }
    $first = $true
    foreach ($def in $web.Chunks[$name]) {
        if (-not $first) { $out.Add('') }
        $first = $false
        foreach ($l in $def.Lines) {
            if ($l -match '^(\s*)<<(.+)>>\s*$') {
                Expand-Chunk $web $Matches[2] ($prefix + $Matches[1]) $out ($stack + $name)
            } elseif ($l -match '<<.+?>>') {
                throw "A module reference must be alone on its line: $l"
            } elseif ($l.Length -eq 0) {
                $out.Add('')
            } else {
                $out.Add($prefix + $l)
            }
        }
    }
}

function Invoke-Tangle($web, [string]$dir) {
    $files = @()
    foreach ($name in $web.Chunks.Keys) {
        if (-not $name.StartsWith('file:')) { continue }
        $rel = $name.Substring(5)
        $out = New-Object System.Collections.Generic.List[string]
        Expand-Chunk $web $name '' $out @()
        $path = Join-Path $dir $rel
        New-Item -ItemType Directory -Force (Split-Path -Parent $path) | Out-Null
        [IO.File]::WriteAllText($path, (($out -join "`n") + "`n"), $utf8)
        $files += $rel
    }
    return $files
}

# ------------------------------------------------------------------ weave
$Keywords = @{
    cpp  = 'alignas auto bool break case catch char char8_t class const constexpr continue default delete do double else enum explicit extern false float for if inline int long namespace new nullptr operator private protected public return short signed size_t sizeof static static_assert static_cast reinterpret_cast struct switch template this throw true try typedef typename uint8_t uint32_t union unsigned using virtual void volatile while' -split ' '
    hlsl = 'cbuffer register row_major float float2 float3 float4 float4x4 int bool struct return if else static const for while void Texture2D SamplerState in out inout' -split ' '
    cmake = 'if endif else elseif foreach endforeach function endfunction' -split ' '
    ps   = 'param function foreach if else elseif return switch continue break try catch throw' -split ' '
}

function Escape-TeX([string]$s) {
    $sb = New-Object System.Text.StringBuilder
    foreach ($ch in $s.ToCharArray()) {
        switch -CaseSensitive ($ch) {
            '\' { [void]$sb.Append('\textbackslash{}') }
            '{' { [void]$sb.Append('\{') }
            '}' { [void]$sb.Append('\}') }
            '$' { [void]$sb.Append('\$') }
            '&' { [void]$sb.Append('\&') }
            '#' { [void]$sb.Append('\#') }
            '^' { [void]$sb.Append('\textasciicircum{}') }
            '_' { [void]$sb.Append('\_') }
            '%' { [void]$sb.Append('\%') }
            '~' { [void]$sb.Append('\textasciitilde{}') }
            "'" { [void]$sb.Append('\textquotesingle{}') }
            '`' { [void]$sb.Append('\textasciigrave{}') }
            '"' { [void]$sb.Append('\textquotedbl{}') }
            '<' { [void]$sb.Append('\textless{}') }
            '>' { [void]$sb.Append('\textgreater{}') }
            '|' { [void]$sb.Append('\textbar{}') }
            '-' { [void]$sb.Append('-{}') }
            ' ' { [void]$sb.Append('~') }
            default { [void]$sb.Append($ch) }
        }
    }
    return $sb.ToString()
}

# Splits one code line into tokens: @{T = text; S = style}, style in kw, cm, str, sp, tx.
function Get-Tokens([string]$line, [string]$lang) {
    $tokens = New-Object System.Collections.Generic.List[object]
    $kw = $Keywords[$lang]
    $i = 0; $n = $line.Length
    $addWords = {
        param($text, $style)
        foreach ($m in [regex]::Matches($text, '\s+|\S+')) {
            $tokens.Add(@{ T = $m.Value; S = $(if ($m.Value -match '^\s') { 'sp' } else { $style }) })
        }
    }
    while ($i -lt $n) {
        $c = $line[$i]
        $rest = $line.Substring($i)
        $hashComments = $lang -eq 'cmake' -or $lang -eq 'ps'
        if ((-not $hashComments -and $rest.StartsWith('//')) -or ($hashComments -and $c -eq '#')) {
            & $addWords $rest 'cm'; break
        }
        if ($c -eq ' ' -or $c -eq "`t") {
            $m = [regex]::Match($rest, '^\s+'); $tokens.Add(@{ T = $m.Value; S = 'sp' }); $i += $m.Length; continue
        }
        if ($c -eq '"') {
            $m = [regex]::Match($rest, '^"(\\.|[^"\\])*"?'); & $addWords $m.Value 'str'; $i += $m.Length; continue
        }
        if (($lang -eq 'cpp' -or $lang -eq 'ps') -and $c -eq "'") {
            $m = [regex]::Match($rest, "^'(\\.|[^'\\])*'?"); $tokens.Add(@{ T = $m.Value; S = 'str' }); $i += $m.Length; continue
        }
        if ($lang -ne 'ps' -and $c -eq '#' -and $line.Substring(0, $i).Trim() -eq '') {
            $m = [regex]::Match($rest, '^#\s*\w+'); $tokens.Add(@{ T = $m.Value; S = 'kw' }); $i += $m.Length; continue
        }
        if ($c -match '[A-Za-z_]') {
            $m = [regex]::Match($rest, '^[A-Za-z_]\w*')
            $tokens.Add(@{ T = $m.Value; S = $(if ($kw -ccontains $m.Value) { 'kw' } else { 'tx' }) }); $i += $m.Length; continue
        }
        $m = [regex]::Match($rest, '^[^\sA-Za-z_"'']+')
        if ($m.Length -eq 0) { $m = [regex]::Match($rest, '^.') }
        $tokens.Add(@{ T = $m.Value; S = 'tx' }); $i += $m.Length
    }
    return , $tokens
}

function Format-Token($t) {
    $e = Escape-TeX $t.T
    switch ($t.S) {
        'kw' { return "\LPkw{$e}" }
        'cm' { return "\LPcm{$e}" }
        'str' { return "\LPstr{$e}" }
        default { return $e }
    }
}

$WrapAt = 92   # longer lines are folded when typeset (never when tangled)

function Format-CodeLine([string]$line, [string]$lang, $web) {
    if ($line -match '^(\s*)<<(.+)>>\s*$') {
        return '\LPl{' + ('~' * $Matches[1].Length) + (Format-ModuleName $Matches[2] $web) + '}'
    }
    $tokens = Get-Tokens $line $lang
    $indent = ([regex]::Match($line, '^\s*')).Length
    $parts = New-Object System.Collections.Generic.List[string]
    $sb = New-Object System.Text.StringBuilder
    $col = 0
    for ($k = 0; $k -lt $tokens.Count; $k++) {
        $t = $tokens[$k]
        if ($t.S -eq 'sp' -and $col -gt $indent) {
            # Length of the next run of non-space tokens.
            $len = 0
            for ($j = $k + 1; $j -lt $tokens.Count -and $tokens[$j].S -ne 'sp'; $j++) { $len += $tokens[$j].T.Length }
            if ($col + $t.T.Length + $len -gt $WrapAt -and $col -gt $indent + 12) {
                $parts.Add($sb.ToString()); [void]$sb.Clear()
                [void]$sb.Append('~' * ($indent + 4)); [void]$sb.Append('\LPcont ')
                $col = $indent + 6
                continue
            }
        }
        [void]$sb.Append((Format-Token $t))
        $col += $t.T.Length
    }
    $parts.Add($sb.ToString())
    return ($parts | ForEach-Object { "\LPl{$_}" }) -join "`n"
}

function Format-ModuleName([string]$name, $web) {
    $num = $web.Chunks[$name][0].Section
    if ($name.StartsWith('file:')) {
        return "\LPfile{$(Escape-TeX $name.Substring(5))}{$num}"
    }
    return "\LPname{$(Format-Text $name $web)}{$num}"
}

function Format-Text([string]$text, $web) {
    # |code| -> typewriter; <<Name>> -> module reference.
    $text = [regex]::Replace($text, '<<(.+?)>>', { param($m) Format-ModuleName $m.Groups[1].Value $web })
    return [regex]::Replace($text, '\|([^|]+)\|', { param($m) '\LPinline{' + (Escape-TeX $m.Groups[1].Value) + '}' })
}

function Format-SectionList([System.Collections.Generic.List[int]]$nums) {
    $links = @($nums | Sort-Object | ForEach-Object { "\hyperlink{lp.$_}{$_}" })
    if ($links.Count -eq 1) { return "section $($links[0])" }
    return 'sections ' + (($links[0..($links.Count - 2)]) -join ', ') + ' and ' + $links[-1]
}

function Get-Language($web) {
    # A module's language is that of the file it ends up in.
    $lang = @{}
    $visit = $null
    $visit = {
        param($name, $l)
        if ($lang.ContainsKey($name)) { return }
        $lang[$name] = $l
        foreach ($def in $web.Chunks[$name]) {
            foreach ($line in $def.Lines) { if ($line -match '^\s*<<(.+)>>\s*$') { & $visit $Matches[1] $l } }
        }
    }
    foreach ($name in $web.Chunks.Keys) {
        if (-not $name.StartsWith('file:')) { continue }
        $ext = [IO.Path]::GetExtension($name).ToLower()
        $l = switch ($ext) { '.hlsl' { 'hlsl' } '.txt' { 'cmake' } '.ps1' { 'ps' } default { 'cpp' } }
        & $visit $name $l
    }
    return $lang
}

function Invoke-Weave($web, [string]$texPath) {
    $lang = Get-Language $web
    $o = New-Object System.Text.StringBuilder
    [void]$o.AppendLine('% Generated by lp.ps1 from the literate source. Do not edit.')
    [void]$o.AppendLine('\documentclass[10pt,a4paper]{article}')
    [void]$o.AppendLine('\usepackage{lpweb}')
    foreach ($l in $web.Limbo) { [void]$o.AppendLine($l) }
    [void]$o.AppendLine('\begin{document}')
    [void]$o.AppendLine('\LPfrontmatter')
    foreach ($s in $web.Sections) {
        if ($s.Starred) { [void]$o.AppendLine("\LPstar{$($s.Number)}{$(Format-Text $s.Title $web)}") }
        else { [void]$o.AppendLine("\LPsec{$($s.Number)}") }
        foreach ($l in $s.Doc) { [void]$o.AppendLine((Format-Text $l $web)) }
        foreach ($c in $s.Code) {
            $defs = $web.Chunks[$c.Name]
            $isFirst = [object]::ReferenceEquals($defs[0], $c)
            [void]$o.AppendLine('\begin{LPcode}')
            [void]$o.AppendLine("\LPdef{$(Format-ModuleName $c.Name $web)}{$(if ($isFirst) { '' } else { '+' })}")
            foreach ($l in $c.Lines) { [void]$o.AppendLine((Format-CodeLine $l $lang[$c.Name] $web)) }
            [void]$o.AppendLine('\end{LPcode}')
            if ($isFirst) {
                $notes = @()
                if ($defs.Count -gt 1) {
                    $others = New-Object System.Collections.Generic.List[int]
                    foreach ($d in $defs) { if ($d.Section -ne $s.Number -and -not $others.Contains($d.Section)) { $others.Add($d.Section) } }
                    if ($others.Count -gt 0) { $notes += "See also $(Format-SectionList $others)." }
                }
                if ($c.Name.StartsWith('file:')) {
                    $notes += "This code is written to the file \LPinline{$(Escape-TeX $c.Name.Substring(5))}."
                } elseif ($web.Uses.ContainsKey($c.Name)) {
                    $notes += "This code is used in $(Format-SectionList $web.Uses[$c.Name])."
                }
                if ($notes) { [void]$o.AppendLine("\LPnote{$($notes -join ' ')}") }
            }
        }
    }
    # Index of module names, as in Knuth's "Names of the sections".
    [void]$o.AppendLine('\LPindexstart')
    $names = $web.Chunks.Keys | Sort-Object { if ($_.StartsWith('file:')) { '~' + $_ } else { $_.ToLower() } }
    foreach ($name in $names) {
        $used = if ($name.StartsWith('file:')) { 'Written to a file.' }
                elseif ($web.Uses.ContainsKey($name)) { "Used in $(Format-SectionList $web.Uses[$name])." } else { '' }
        [void]$o.AppendLine("\LPindexentry{$(Format-ModuleName $name $web)}{$used}")
    }
    [void]$o.AppendLine('\LPindexend')
    [void]$o.AppendLine('\end{document}')
    [IO.File]::WriteAllText($texPath, $o.ToString(), $utf8)
}

# ------------------------------------------------------------------ commands
function Invoke-Check($web) {
    $tmp = Join-Path $OutDir 'tangled'
    $files = Invoke-Tangle $web $tmp
    $root = Split-Path -Parent $here
    $bad = 0
    foreach ($rel in $files) {
        $a = ([IO.File]::ReadAllText((Join-Path $tmp $rel), $utf8)) -replace "`r`n", "`n"
        $orig = Join-Path $root $rel
        if (-not (Test-Path $orig)) { Write-Host "  MISSING  $rel"; $bad++; continue }
        $b = ([IO.File]::ReadAllText($orig, $utf8)) -replace "`r`n", "`n"
        if ($a.TrimEnd("`n") -ceq $b.TrimEnd("`n")) { Write-Host "  same     $rel" }
        else {
            $bad++
            $la = $a -split "`n"; $lb = $b -split "`n"
            for ($i = 0; $i -lt [Math]::Max($la.Count, $lb.Count); $i++) {
                if ($la[$i] -cne $lb[$i]) {
                    Write-Host "  DIFFERS  $rel, line $($i + 1):`n    web:    $($la[$i])`n    source: $($lb[$i])"
                    break
                }
            }
        }
    }
    if ($bad) { throw "$bad file(s) differ from the web" }
    Write-Host "The web reproduces all $($files.Count) program files exactly."
}

function Invoke-Pdf([string]$texPath) {
    $dir = Split-Path -Parent $texPath
    $file = Split-Path -Leaf $texPath
    Push-Location $dir
    try {
        if ($TeX) {
            & $TeX $file
        } elseif (Get-Command latexmk -ErrorAction SilentlyContinue) {
            & latexmk -pdf -interaction=nonstopmode $file
        } elseif (Get-Command pdflatex -ErrorAction SilentlyContinue) {
            # Three runs settle the table of contents and the hyperlinks.
            1..3 | ForEach-Object { & pdflatex -interaction=nonstopmode $file | Out-Null }
        } elseif (Get-Command tectonic -ErrorAction SilentlyContinue) {
            & tectonic $file
        } else {
            throw 'No TeX engine found. Install MiKTeX or TeX Live (pdflatex), or tectonic, or pass -TeX <engine>.'
        }
        if ($LASTEXITCODE -ne 0) { throw "TeX failed with exit code $LASTEXITCODE (see the .log file in $dir)" }
    } finally {
        Pop-Location
    }
    Write-Host "Wrote $([IO.Path]::ChangeExtension($texPath, '.pdf'))"
}

New-Item -ItemType Directory -Force $OutDir | Out-Null
# (PowerShell names are case-insensitive: the parsed web must not be called $web here,
# or it would overwrite the [string] parameter $Web.)
$parsed = Parse-Web $Web
Write-Host "$($parsed.Sections.Count) sections, $($parsed.Chunks.Count) modules"
$texPath = Join-Path $OutDir ([IO.Path]::GetFileNameWithoutExtension($Web) + '.tex')
switch ($Command) {
    'tangle' {
        $dir = if ($Target) { $Target } else { Join-Path $OutDir 'tangled' }
        Invoke-Tangle $parsed $dir | ForEach-Object { Write-Host "  wrote $_" }
    }
    'check' { Invoke-Check $parsed }
    'weave' {
        Copy-Item (Join-Path $here 'lpweb.sty') $OutDir -Force
        Invoke-Weave $parsed $texPath; Write-Host "Wrote $texPath"
    }
    'pdf' {
        Copy-Item (Join-Path $here 'lpweb.sty') $OutDir -Force
        Invoke-Weave $parsed $texPath; Invoke-Pdf $texPath
    }
    'all' {
        Invoke-Check $parsed
        Copy-Item (Join-Path $here 'lpweb.sty') $OutDir -Force
        Invoke-Weave $parsed $texPath; Invoke-Pdf $texPath
    }
}
