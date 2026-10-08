#pragma once
#include "Inspector.h"
#include "Material.h"
#include "Mesh.h"
#include "Renderer.h"
#include <filesystem>
#include <string>

struct ImVec2;

struct OrbitCamera {
    float yaw = -1.3f, pitch = 0.3f, distance = 3.6f;  // faces the texture's center (u = 0.5)
    DirectX::XMFLOAT3 Eye() const;
};

struct OrbitLight {
    float azimuth = -2.0f, elevation = 0.5f, radius = 2.4f;
    float intensity = 5.0f;
    float color[3] = {1.0f, 0.97f, 0.92f};
    bool autoOrbit = false;
    float speed = 0.6f;  // rad/s
    DirectX::XMFLOAT3 Position() const;
};

class App {
public:
    bool Init(HWND hwnd);
    void Shutdown();
    void Frame(float dt);           // builds the UI and renders the scene
    Renderer& GetRenderer() { return renderer_; }
    void ReloadShaders();

private:
    void BuildDockLayout(unsigned int dockspaceId);
    void UiMaterials();
    void UiScene();
    void UiMaps();
    void UiInspector();
    void UiViewport();
    void UiConcepts();
    // Draws a map image; if hovered, records the hovered uv. Click pins the probe.
    void MapImage(MapKind kind, const ImVec2& size);
    Material& Current() { return library_.Items()[current_]; }
    DirectX::XMMATRIX World() const;

    Renderer renderer_;
    MaterialLibrary library_;
    std::filesystem::path root_;
    std::string loadError_, shaderLog_;
    bool shadersOk_ = false;

    int current_ = 0;
    Shape shape_ = Shape::Sphere;
    OrbitCamera camera_;
    OrbitLight light_;
    bool rotate_ = false;
    float rotSpeed_ = 0.5f, rotAngle_ = 0;

    // Shading
    BumpSettings bump_;
    bool useColor_ = true, useBump_ = true, useRough_ = true;
    float constRough_ = 0.5f, metallic_ = 0, ambient_ = 0.03f;
    int viewMode_ = 0;
    bool showArrows_ = true;

    // Inspector
    MapKind inspected_ = MapColor;
    bool hoveredThisFrame_ = false, pinned_ = false;
    DirectX::XMFLOAT2 probeUV_{0.5f, 0.5f};
    bool probeValid_ = false;
    HoverInfo info_;
};
