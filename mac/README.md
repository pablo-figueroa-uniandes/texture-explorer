# Texture Explorer for macOS

A port of Texture Explorer to macOS and Metal. It is the same program as the Direct3D 11
version in the repository root: the same panels and controls, the same defaults, the same
materials (`../assets` is shared) and the same formulas.

## Build & run

Requirements: macOS 13+, Xcode or the Xcode command-line tools, CMake ≥ 3.20, and internet
access for the first configure. CMake fetches Dear ImGui, stb_image and nlohmann/json at the
versions used by the Windows build, plus Apple's [metal-cpp](https://developer.apple.com/metal/cpp/).

```sh
cmake -S mac -B mac/build -DCMAKE_BUILD_TYPE=Release
cmake --build mac/build
open mac/build/TextureExplorer.app
```

The application finds `assets/` and `mac/shaders/` by walking up from its bundle. The shaders
are compiled at run time, so you can edit `mac/shaders/pbr.metal` while the program runs and
reload it with **F5** (fn-F5 on most Mac keyboards) or **⌘R**. The mouse controls are those of
the Windows version. On a trackpad, scroll with two fingers to zoom.

## How it maps to the Windows version

| Windows | macOS | What changes |
|---|---|---|
| DirectXMath | `src/VecMath.h` + Apple `simd` | column vectors (`M * v`); `Float3`/`Float4` for packed GPU data |
| `src/Mesh.*`, `src/Inspector.*` | `mac/src/Mesh.*`, `mac/src/Inspector.*` | vector types only |
| `src/Material.*` | `mac/src/Material.*` | Metal textures; an sRGB *texture view* replaces the sRGB shader resource view |
| `shaders/*.hlsl` | `mac/shaders/*.metal` | HLSL → Metal Shading Language; resources become function parameters; `packed_float3` keeps the cbuffer layout |
| `src/Renderer.*` | `mac/src/Renderer.*` | Direct3D 11 → Metal via metal-cpp: command buffers, pipeline states, render-pass load/store actions (clear and MSAA resolve) |
| `src/App.*` | `mac/src/App.*` | texture handles, paths, opening URLs, Retina scale, ⌘R |
| `src/main.cpp` (Win32 loop) | `mac/src/main.mm` (AppKit delegate + `MTKView`) | the only Objective-C++ file |

## Comparing the two versions

`compare/` builds the CPU half of **both** versions on a Mac: the unmodified Windows files
`src/Mesh.cpp`, `src/Material.cpp` and `src/Inspector.cpp` (with DirectXMath and the small
Direct3D stand-ins in `compare/shims/`), and their counterparts in `mac/src/`. A probe program
prints the meshes and the inspector's results for every material and shape, and
`compare.py` checks that the two reports agree:

```sh
cmake -S mac/compare -B mac/compare/build
cmake --build mac/compare/build --target compare
```

Map values agree exactly; vectors differ by at most about 1e-6 (float rounding). Chapter 15
of `Docs/TextureExplorer-Guide.pdf` explains this and other ways to compare the
implementations, and the macOS sections of `LiterateP/out/TextureExplorer.pdf` present the port as a
literate program, with the code that both versions share written only once.
