# Texture Explorer: theory and code

**[TextureExplorer-Guide.pdf](TextureExplorer-Guide.pdf)** is a 55-page guide to the
project, written for a computer graphics course.

- **Part I, Theory:**
  - texture space and parametric surfaces (texture mapping of the four solids, orientation, distortion, seams);
  - sampling (bilinear filtering, mipmaps, sRGB);
  - height fields (displacement, bump mapping with Blinn's derivation, normal maps);
  - tangent space (TBN, Gram–Schmidt, transforming normals);
  - lighting (Cook–Torrance, GGX roughness, Schlick).
- **Part II, Implementation:** the architecture of the program and a walk-through of its
  code. Every listing is extracted from the real source files when the guide is built.
- **Exercises:** experiments to do in the program, and programming exercises.

## Files

| Path | Content |
|---|---|
| `guide.tex` | Main LaTeX file: preamble, styles, list of chapters |
| `chapters/*.tex` | One file per chapter (the diagrams are drawn with TikZ/pgfplots inside them) |
| `figures/*.png` | Screenshots of the program |
| `excerpts.txt` | The code excerpts shown in Part II: a source file and a regular expression for the first line of each one |
| `build.ps1` | Extracts the excerpts into `build/excerpts`, runs TeX and writes `TextureExplorer-Guide.pdf` |

## Building

You need a TeX engine. Any one of these works: MiKTeX or TeX Live (`latexmk` or
`pdflatex`), or [tectonic](https://tectonic-typesetting.github.io/), which downloads the
packages it needs.

```powershell
powershell -ExecutionPolicy Bypass -File Docs\build.ps1
powershell -ExecutionPolicy Bypass -File Docs\build.ps1 -TeX C:\path\to\tectonic.exe
```

An excerpt runs from its matching first line until the braces opened there close again. An
excerpt can also give an explicit end pattern. If a function is renamed, the build stops
with an error that names the excerpt to fix in `excerpts.txt`.

See also [`../LiterateP`](../LiterateP), the same program written as a Knuth-style literate
program. That document follows the code; this one follows the theory.
