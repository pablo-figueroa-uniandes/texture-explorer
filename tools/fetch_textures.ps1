<#
  Downloads a small, curated set (10) of CC0 PBR materials from ambientCG and
  Poly Haven, normalizes their file names and writes assets/materials/manifest.json.

  Every material keeps: color, displacement, roughness and (reference) normal map.
  Neither site ships a separate "bump" map for these assets; the viewer therefore
  derives the bump map from the height/displacement map and labels it as such.

  Usage:  powershell -ExecutionPolicy Bypass -File tools\fetch_textures.ps1 [-Force]
#>
param([switch]$Force)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

$root = Join-Path (Split-Path -Parent $PSScriptRoot) 'assets\materials'
New-Item -ItemType Directory -Force $root | Out-Null

$ambientCG = @('Bricks076C', 'Wood066', 'Rock030', 'Metal032', 'Tiles101', 'Fabric048', 'PavingStones131')
$polyHaven = @('castle_brick_07', 'aerial_rocks_02', 'bark_willow_02')

$manifest = @()

function Save-Url($url, $path) {
    if ($Force -or -not (Test-Path $path)) {
        Write-Host "  GET $url"
        Invoke-WebRequest -UseBasicParsing -Uri $url -OutFile $path
    }
}

# ---------------------------------------------------------------- ambientCG
$ids = $ambientCG -join ','
$api = Invoke-WebRequest -UseBasicParsing "https://ambientcg.com/api/v2/full_json?id=$ids&include=downloadData" |
       Select-Object -ExpandProperty Content | ConvertFrom-Json
foreach ($id in $ambientCG) {
    $asset = $api.foundAssets | Where-Object assetId -eq $id
    if (-not $asset) { Write-Warning "ambientCG asset $id not found"; continue }
    Write-Host "ambientCG: $id"
    $dir = Join-Path $root $id
    New-Item -ItemType Directory -Force $dir | Out-Null
    $dl = $asset.downloadFolders.default.downloadFiletypeCategories.zip.downloads | Where-Object attribute -eq '1K-JPG'
    $zip = Join-Path $env:TEMP "$id`_1K-JPG.zip"
    $tmp = Join-Path $env:TEMP "texex_$id"
    if ($Force -or -not (Test-Path (Join-Path $dir 'color.jpg'))) {
        Save-Url $dl.fullDownloadPath $zip
        if (Test-Path $tmp) { Remove-Item -Recurse -Force $tmp }
        Expand-Archive -Path $zip -DestinationPath $tmp
        $map = @{ 'Color' = 'color.jpg'; 'Displacement' = 'displacement.jpg'; 'Roughness' = 'roughness.jpg'; 'NormalDX' = 'normal.jpg' }
        foreach ($k in $map.Keys) {
            $src = Get-ChildItem $tmp -Filter "*_$k.jpg" | Select-Object -First 1
            if ($src) { Copy-Item $src.FullName (Join-Path $dir $map[$k]) -Force }
        }
        Remove-Item -Recurse -Force $tmp
        Remove-Item -Force $zip
    }
    $manifest += [ordered]@{
        id      = $id
        name    = $asset.displayName
        site    = 'ambientCG'
        url     = "https://ambientcg.com/view?id=$id"
        author  = 'Lennart Demes (ambientCG)'
        license = 'CC0 1.0'
    }
}

# ---------------------------------------------------------------- Poly Haven
foreach ($id in $polyHaven) {
    Write-Host "Poly Haven: $id"
    $files = Invoke-WebRequest -UseBasicParsing "https://api.polyhaven.com/files/$id" | Select-Object -ExpandProperty Content | ConvertFrom-Json
    $info  = Invoke-WebRequest -UseBasicParsing "https://api.polyhaven.com/info/$id"  | Select-Object -ExpandProperty Content | ConvertFrom-Json
    $dir = Join-Path $root $id
    New-Item -ItemType Directory -Force $dir | Out-Null
    $map = @{ 'Diffuse' = 'color.jpg'; 'Displacement' = 'displacement.jpg'; 'Rough' = 'roughness.jpg'; 'nor_dx' = 'normal.jpg' }
    foreach ($k in $map.Keys) {
        $url = $files.$k.'1k'.jpg.url
        if ($url) { Save-Url $url (Join-Path $dir $map[$k]) }
    }
    $manifest += [ordered]@{
        id      = $id
        name    = $info.name
        site    = 'Poly Haven'
        url     = "https://polyhaven.com/a/$id"
        author  = ($info.authors.PSObject.Properties.Name -join ', ')
        license = 'CC0 1.0'
    }
}

# ---------------------------------------------------------------- manifest
foreach ($m in $manifest) {
    $dir = Join-Path $root $m.id
    $m.files = [ordered]@{}
    foreach ($f in 'color', 'bump', 'displacement', 'roughness', 'normal') {
        $p = Join-Path $dir "$f.jpg"
        if (Test-Path $p) { $m.files[$f] = "$($m.id)/$f.jpg" }
    }
    # No pack provides a dedicated bump map: bump is derived from the height map.
    $m.bumpFromHeight = -not $m.files.Contains('bump')
}
$json = ConvertTo-Json -InputObject @($manifest) -Depth 5
[IO.File]::WriteAllText((Join-Path $root 'manifest.json'), $json, (New-Object Text.UTF8Encoding $false))
Write-Host "Wrote $($manifest.Count) materials to $root\manifest.json"
