# Texture Explorer

A native Direct3D 11 program for learning texturing concepts in computer graphics: UV space,
bump mapping, displacement mapping, specular roughness, and tangent-space vs. world-space normals.

## Build & run

Requirements: Windows 10+, Visual Studio 2022 (C++ workload), CMake ≥ 3.20, internet access for
the first configure (Dear ImGui, stb_image and nlohmann/json are fetched by CMake).

```powershell
# 1. Download the 10 example materials (once). Writes assets\materials\manifest.json
powershell -ExecutionPolicy Bypass -File tools\fetch_textures.ps1

# 2. Build
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release

# 3. Run
build\Release\TextureExplorer.exe
```

The executable locates `shaders\` and `assets\` by walking up from its own folder.

## What it shows

| Panel | Content |
|---|---|
| **Materials** | The 10 materials with thumbnails, and the origin of the selected one (site, author, license, link). |
| **Maps** | Color, bump, displacement, specular roughness and the pack's own normal map (for reference). |
| **Inspector** | The selected map at a large size. When you hover a map, it shows **u,v** (normalized 0–1), texel, color, **bump**, **displacement** and **roughness** values, and the **normal computed from the bump map** in **tangent space** and in **world space** (plus the geometric N, T and B). Left-click pins the probe, right-click releases it. |
| **Viewport** | The material on a sphere, pyramid, cylinder or capsule. Left-drag orbits the camera, right-drag orbits the light, the wheel zooms, and Space toggles the object's rotation. The probed texel is shown as a magenta ring, with arrows for N, N′ (bumped), T and B. |
| **Scene** | Shape, rotation animation, camera, light (with automatic orbit), checkboxes for each map, bump strength, displacement scale, and debug views (albedo, world normal, tangent normal, roughness, height, UV). |
| **Concepts** | Short explanations of each idea. |

### Bump → normal

The bump map is a height field *h(u,v)*. Using central differences one texel apart:

```
dh/dx = (h(x+1,y) - h(x-1,y)) / 2      dh/dy = (h(x,y+1) - h(x,y-1)) / 2
n_ts  = normalize(-k·dh/dx, -k·dh/dy, 1)          (k = bump strength)
n_world = R_object · (T·n_ts.x + B·n_ts.y + N·n_ts.z)
```

The same formula appears in `shaders/pbr.hlsl` (`BumpNormalTS`) and `src/Inspector.cpp`
(`BumpNormalTangent`), so the Inspector's numbers match the pixels on screen. The shaders compile
at runtime: edit them and press **F5** to reload.

Neither source site ships a separate *bump* map for these assets. The height (displacement) map
is therefore used as the bump map, and the UI labels it "from height". This illustrates the point
that one height field can either tilt normals (bump) or move geometry (displacement).

## Texture sources (all CC0 1.0)

| Material | Source |
|---|---|
| Bricks 076 C | ambientCG — https://ambientcg.com/view?id=Bricks076C |
| Wood 066 | ambientCG — https://ambientcg.com/view?id=Wood066 |
| Rock 030 | ambientCG — https://ambientcg.com/view?id=Rock030 |
| Metal 032 | ambientCG — https://ambientcg.com/view?id=Metal032 |
| Tiles 101 | ambientCG — https://ambientcg.com/view?id=Tiles101 |
| Fabric 048 | ambientCG — https://ambientcg.com/view?id=Fabric048 |
| Paving Stones 131 | ambientCG — https://ambientcg.com/view?id=PavingStones131 |
| Castle Brick 07 | Poly Haven (Rob Tuytel) — https://polyhaven.com/a/castle_brick_07 |
| Aerial Rocks 02 | Poly Haven (Rob Tuytel) — https://polyhaven.com/a/aerial_rocks_02 |
| Bark Willow 02 | Poly Haven (Charlotte Baglioni) — https://polyhaven.com/a/bark_willow_02 |

## Literate version

[`LiterateP/`](LiterateP/) contains the whole program written as a literate program in
Knuth's style, together with the tools that turn it into a PDF
(`LiterateP/out/TextureExplorer.pdf`). Those tools also check that the document reproduces
every source file exactly.

## Code map

- `src/Mesh.*`: parametric shapes. The same functions build the GPU mesh and map a uv back to a point on the surface for the Inspector.
- `src/Material.*`: the manifest, plus GPU textures (with mipmaps) and CPU copies of each map for probing.
- `src/Inspector.*`: computes the values and normals at the probe position.
- `src/Renderer.*`: the D3D11 device, the 4× MSAA offscreen viewport, and the mesh and line passes.
- `src/App.*`: the ImGui docking UI and interaction.
- `shaders/pbr.hlsl`: vertex displacement, bump-mapped normals and the GGX/Cook-Torrance BRDF.
