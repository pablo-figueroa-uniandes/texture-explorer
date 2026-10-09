# Texture Explorer as a literate program

This folder holds the complete Texture Explorer program written as a *literate program*,
in the style of Donald Knuth's WEB/CWEB. The program is presented as an essay of 348
numbered sections. Each section has some commentary and usually a piece of code, and the
sections come in the order that is easiest to understand rather than the order the compiler
needs.

| File | Purpose |
|---|---|
| `TextureExplorer.nw` | The master file of the web: title material, plus the parts it includes |
| `web/*.nw` | The parts of the Windows version: intro, shapes, materials, inspector, shaders, renderer, app, main, build |
| `web/mac-*.nw` | The macOS port (`../mac`): its math, shapes, materials, inspector, Metal shaders, renderer, app, main and build, and the comparison of the two versions |
| `lp.ps1` | The toolchain: **tangle** (web → source files), **check**, **weave** (web → LaTeX) and **pdf** |
| `lpweb.sty` | The LaTeX style: Knuth-like section numbers, ⟨module⟩ names, cross-references and the index |
| `out/TextureExplorer.pdf` | The typeset document (generated) |

## Making the PDF

You need a TeX engine. Any one of these works: MiKTeX or TeX Live (`latexmk` or `pdflatex`),
or the single-file [tectonic](https://tectonic-typesetting.github.io/).

On macOS (or Linux) run the same script with PowerShell 7, `pwsh`
(`brew install --cask powershell`; `brew install tectonic` for the TeX engine):

```sh
cd LiterateP
pwsh lp.ps1 all
```

```powershell
cd LiterateP
powershell -ExecutionPolicy Bypass -File lp.ps1 all          # check + weave + pdf
powershell -ExecutionPolicy Bypass -File lp.ps1 pdf -TeX C:\path\to\tectonic.exe
```

The other commands are:

```powershell
lp.ps1 check     # tangle into out\tangled and compare with ..\src, ..\shaders, ...
lp.ps1 tangle    # write the program files into out\tangled (or -Target <dir>)
lp.ps1 weave     # write out\TextureExplorer.tex only
```

`check` proves that the document is the program. The 36 files it tangles must be
byte-for-byte identical to the files in the repository: the Windows version (C++, HLSL,
CMake and the PowerShell download script) and the macOS port in `../mac` (C++,
Objective-C++, Metal Shading Language, CMake, and the Python and C++ of the comparison
tool). If you change the code, change the web as well, and run `check`.

The two versions share every module whose code is identical in both. Such a module is
defined once, in the Windows part, and the woven document notes that it is used in two
sections, one in each version. If someone changes one copy and not the other, `check`
fails, so the document always shows exactly what the versions have in common.

## Syntax of the web

The syntax is noweb's, plus two of Knuth's conventions:

```
@* Title.  Text...      starred section: a major part, listed in the contents
@ Text...               ordinary section
<<Module name>>=        the code part of the section (runs until the next @ line)
    <<Other module>>    a reference, alone on its line; tangle replaces it, keeping the indentation
<<file:src/App.cpp>>=   a root module, written to that file
|code|                  typewriter text inside the commentary (as in CWEB)
<<Module name>>         inside the commentary, a typeset reference to a module
@i file                 include another file of the web
```

A module defined in several sections is the concatenation of its parts, separated by one
blank line. In the commentary, `\label{x}` and `\S\ref{x}` refer to section numbers.
