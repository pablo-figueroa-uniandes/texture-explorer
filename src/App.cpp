#include "App.h"
#include <imgui.h>
#include <imgui_internal.h>  // DockBuilder
#include <shellapi.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace DirectX;

namespace {
constexpr float kPi = 3.14159265f;

ImTextureID Tex(ID3D11ShaderResourceView* srv) { return (ImTextureID)(intptr_t)srv; }

std::filesystem::path FindRoot() {
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    for (auto p = std::filesystem::path(buf).parent_path(); p.has_relative_path(); p = p.parent_path())
        if (std::filesystem::exists(p / "shaders" / "pbr.hlsl")) return p;
    return std::filesystem::path(TEXEX_ROOT);
}

ImVec4 Encode(const XMFLOAT3& n) { return {n.x * 0.5f + 0.5f, n.y * 0.5f + 0.5f, n.z * 0.5f + 0.5f, 1}; }

void Vec3Row(const char* label, const XMFLOAT3& v, bool swatch, const char* tip) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(label);
    if (tip && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
    ImGui::TableNextColumn();
    ImGui::Text("(%+.3f, %+.3f, %+.3f)", v.x, v.y, v.z);
    ImGui::TableNextColumn();
    if (swatch) {
        ImGui::PushID(label);
        ImGui::ColorButton("##sw", Encode(v), ImGuiColorEditFlags_NoTooltip, ImVec2(36, 0));
        ImGui::PopID();
    }
}

void ScalarRow(const char* label, float v, const char* tip) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(label);
    if (tip && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
    ImGui::TableNextColumn();
    ImGui::Text("%.4f   (%d/255)", v, int(std::lround(v * 255)));
    ImGui::TableNextColumn();
    ImGui::ColorButton("##g", ImVec4(v, v, v, 1), ImGuiColorEditFlags_NoTooltip, ImVec2(36, 0));
}

void AddArrow(std::vector<LineVertex>& out, XMVECTOR from, XMVECTOR dir, float len, XMFLOAT4 color) {
    const XMVECTOR to = from + XMVector3Normalize(dir) * len;
    XMFLOAT3 a, b;
    XMStoreFloat3(&a, from);
    XMStoreFloat3(&b, to);
    out.push_back({a, color});
    out.push_back({b, color});
    // Small arrow head: two short lines in a plane containing dir.
    XMVECTOR side = XMVector3Cross(dir, XMVectorSet(0, 1, 0, 0));
    if (XMVectorGetX(XMVector3LengthSq(side)) < 1e-6f) side = XMVector3Cross(dir, XMVectorSet(1, 0, 0, 0));
    side = XMVector3Normalize(side) * (len * 0.12f);
    const XMVECTOR back = to - XMVector3Normalize(dir) * (len * 0.2f);
    XMFLOAT3 h1, h2;
    XMStoreFloat3(&h1, back + side);
    XMStoreFloat3(&h2, back - side);
    out.push_back({b, color}); out.push_back({h1, color});
    out.push_back({b, color}); out.push_back({h2, color});
}

void AddCircle(std::vector<LineVertex>& out, XMFLOAT3 c, XMVECTOR ax, XMVECTOR ay, float r, XMFLOAT4 color) {
    const XMVECTOR center = XMLoadFloat3(&c);
    const int n = 24;
    for (int i = 0; i < n; ++i) {
        const float a0 = 2 * kPi * i / n, a1 = 2 * kPi * (i + 1) / n;
        XMFLOAT3 p0, p1;
        XMStoreFloat3(&p0, center + r * (std::cos(a0) * ax + std::sin(a0) * ay));
        XMStoreFloat3(&p1, center + r * (std::cos(a1) * ax + std::sin(a1) * ay));
        out.push_back({p0, color});
        out.push_back({p1, color});
    }
}

const char* kViewModes[] = {"Lit (GGX)", "Albedo only", "World normal (bumped)", "Tangent-space normal",
                            "Roughness", "Bump height", "UV coordinates"};
} // namespace

XMFLOAT3 OrbitCamera::Eye() const {
    return {distance * std::cos(pitch) * std::sin(yaw), distance * std::sin(pitch),
            distance * std::cos(pitch) * std::cos(yaw)};
}

XMFLOAT3 OrbitLight::Position() const {
    return {radius * std::cos(elevation) * std::sin(azimuth), radius * std::sin(elevation),
            radius * std::cos(elevation) * std::cos(azimuth)};
}

bool App::Init(HWND hwnd) {
    if (!renderer_.Init(hwnd)) return false;
    root_ = FindRoot();
    if (library_.Load(root_ / "assets" / "materials", loadError_))
        for (int i = 0; i < int(library_.Items().size()); ++i)  // all, for the thumbnails
            library_.EnsureLoaded(i, renderer_.Device(), renderer_.Context());
    ReloadShaders();
    return true;
}

void App::Shutdown() { renderer_.Shutdown(); }

void App::ReloadShaders() { shadersOk_ = renderer_.ReloadShaders(root_ / "shaders", shaderLog_); }

XMMATRIX App::World() const { return XMMatrixRotationY(rotAngle_); }

void App::Frame(float dt) {
    if (rotate_) rotAngle_ = std::fmod(rotAngle_ + rotSpeed_ * dt, 2 * kPi);
    if (light_.autoOrbit) light_.azimuth = std::fmod(light_.azimuth + light_.speed * dt, 2 * kPi);
    if (ImGui::IsKeyPressed(ImGuiKey_F5)) ReloadShaders();
    if (ImGui::IsKeyPressed(ImGuiKey_Space) && !ImGui::GetIO().WantTextInput) rotate_ = !rotate_;

    const ImGuiID dockId = ImGui::DockSpaceOverViewport(0, nullptr, ImGuiDockNodeFlags_PassthruCentralNode);
    static bool layoutBuilt = false;
    if (!layoutBuilt) {
        layoutBuilt = true;
        ImGuiDockNode* node = ImGui::DockBuilderGetNode(dockId);
        if (!node || node->IsLeafNode()) BuildDockLayout(dockId);
    } else if (ImGui::GetFrameCount() == 2) {
        ImGui::SetWindowFocus("Scene");  // Scene and Concepts share a dock; start on Scene
    }

    if (library_.Items().empty()) {
        ImGui::Begin("Viewport");
        ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", loadError_.c_str());
        ImGui::End();
        return;
    }

    // The probe is driven by whatever map image is hovered during this frame.
    hoveredThisFrame_ = false;
    UiMaterials();
    UiScene();
    UiMaps();
    UiInspector();
    if (!hoveredThisFrame_ && !pinned_) probeValid_ = false;
    if (probeValid_) {
        info_ = Inspect(Current(), probeUV_.x, probeUV_.y, shape_, World(), bump_);
    } else {
        info_ = HoverInfo{};
    }
    UiViewport();
    UiConcepts();
}

void App::BuildDockLayout(ImGuiID dockId) {
    ImGui::DockBuilderRemoveNode(dockId);
    ImGui::DockBuilderAddNode(dockId, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockId, ImGui::GetMainViewport()->Size);
    ImGuiID center = dockId;
    ImGuiID left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.21f, nullptr, &center);
    ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.46f, nullptr, &center);
    ImGuiID leftBottom = ImGui::DockBuilderSplitNode(left, ImGuiDir_Down, 0.55f, nullptr, &left);
    ImGuiID rightBottom = ImGui::DockBuilderSplitNode(right, ImGuiDir_Down, 0.66f, nullptr, &right);
    ImGui::DockBuilderDockWindow("Materials", left);
    ImGui::DockBuilderDockWindow("Scene", leftBottom);
    ImGui::DockBuilderDockWindow("Concepts", leftBottom);
    ImGui::DockBuilderDockWindow("Maps", right);
    ImGui::DockBuilderDockWindow("Inspector", rightBottom);
    ImGui::DockBuilderDockWindow("Viewport", center);
    ImGui::DockBuilderFinish(dockId);
}

void App::UiMaterials() {
    ImGui::Begin("Materials");
    auto& items = library_.Items();
    for (int i = 0; i < int(items.size()); ++i) {
        ImGui::PushID(i);
        const Material& m = items[i];
        if (ImGui::Selectable("##sel", current_ == i, 0, ImVec2(0, 36))) {
            current_ = i;
            library_.EnsureLoaded(i, renderer_.Device(), renderer_.Context());
        }
        ImGui::SameLine(0, 0);
        ImGui::SetCursorPosX(ImGui::GetStyle().WindowPadding.x);
        if (m.loaded && m.maps[MapColor])
            ImGui::Image(Tex(m.maps[MapColor]->srvLinear.Get()), ImVec2(36, 36));
        else
            ImGui::Dummy(ImVec2(36, 36));
        ImGui::SameLine();
        ImGui::BeginGroup();
        ImGui::TextUnformatted(m.name.c_str());
        ImGui::TextDisabled("%s", m.site.c_str());
        ImGui::EndGroup();
        ImGui::PopID();
    }

    ImGui::SeparatorText("Origin of this material");
    const Material& m = Current();
    ImGui::Text("Source:  %s", m.site.c_str());
    ImGui::Text("Author:  %s", m.author.c_str());
    ImGui::Text("License: %s", m.license.c_str());
    ImGui::TextWrapped("%s", m.url.c_str());
    if (ImGui::Button("Open source page")) {
        const std::wstring url(m.url.begin(), m.url.end());
        ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
    if (m.bumpFromHeight)
        ImGui::TextWrapped("Note: this pack ships no separate bump map, so the height (displacement) "
                           "map is used as the bump map.");
    ImGui::End();
}

void App::UiScene() {
    ImGui::Begin("Scene");
    ImGui::SeparatorText("Object");
    int s = int(shape_);
    const char* shapes[] = {ShapeName(Shape::Sphere), ShapeName(Shape::Pyramid), ShapeName(Shape::Cylinder),
                            ShapeName(Shape::Capsule)};
    if (ImGui::Combo("Shape", &s, shapes, int(Shape::Count))) shape_ = Shape(s);
    ImGui::Checkbox("Rotate object (Space)", &rotate_);
    ImGui::SliderFloat("Rotation speed", &rotSpeed_, -2.0f, 2.0f, "%.2f rad/s");
    float deg = XMConvertToDegrees(rotAngle_);
    if (ImGui::SliderFloat("Rotation angle", &deg, 0, 360, "%.0f deg")) rotAngle_ = XMConvertToRadians(deg);

    ImGui::SeparatorText("Camera  (left-drag / wheel in viewport)");
    ImGui::SliderAngle("Cam yaw", &camera_.yaw, -180, 180);
    ImGui::SliderAngle("Cam pitch", &camera_.pitch, -89, 89);
    ImGui::SliderFloat("Distance", &camera_.distance, 1.5f, 12.0f);

    ImGui::SeparatorText("Light  (right-drag in viewport)");
    ImGui::Checkbox("Orbit light automatically", &light_.autoOrbit);
    ImGui::SliderAngle("Light azimuth", &light_.azimuth, -180, 180);
    ImGui::SliderAngle("Light elevation", &light_.elevation, -89, 89);
    ImGui::SliderFloat("Light distance", &light_.radius, 1.5f, 8.0f);
    ImGui::SliderFloat("Intensity", &light_.intensity, 0.0f, 10.0f);
    ImGui::ColorEdit3("Light color", light_.color, ImGuiColorEditFlags_NoInputs);
    ImGui::SameLine();
    ImGui::SliderFloat("Orbit speed", &light_.speed, -3.0f, 3.0f);

    ImGui::SeparatorText("Maps used for shading");
    ImGui::Checkbox("Color", &useColor_);
    ImGui::SameLine();
    ImGui::Checkbox("Bump", &useBump_);
    ImGui::SameLine();
    ImGui::Checkbox("Displacement", &bump_.useDisplacement);
    ImGui::SameLine();
    ImGui::Checkbox("Roughness", &useRough_);
    ImGui::SliderFloat("Bump strength", &bump_.strength, 0.0f, 30.0f);
    ImGui::SliderFloat("Displacement scale", &bump_.displacementScale, 0.0f, 0.3f);
    ImGui::SliderFloat("Displacement mid-level", &bump_.displacementMid, 0.0f, 1.0f);
    ImGui::BeginDisabled(useRough_);
    ImGui::SliderFloat("Constant roughness", &constRough_, 0.04f, 1.0f);
    ImGui::EndDisabled();
    ImGui::SliderFloat("Metallic", &metallic_, 0.0f, 1.0f);
    ImGui::SliderFloat("Ambient", &ambient_, 0.0f, 0.3f);
    ImGui::Combo("View", &viewMode_, kViewModes, IM_ARRAYSIZE(kViewModes));
    ImGui::Checkbox("Show probe arrows (N, N', T, B)", &showArrows_);

    ImGui::SeparatorText("Shaders");
    if (ImGui::Button("Reload shaders (F5)")) ReloadShaders();
    ImGui::SameLine();
    ImGui::TextColored(shadersOk_ ? ImVec4(0.4f, 0.9f, 0.4f, 1) : ImVec4(1, 0.4f, 0.4f, 1),
                       shadersOk_ ? "OK" : "compile error");
    if (!shaderLog_.empty()) ImGui::TextWrapped("%s", shaderLog_.c_str());
    ImGui::End();
}

void App::MapImage(MapKind kind, const ImVec2& size) {
    const auto& map = Current().maps[kind];
    if (!map) {
        ImGui::Button("not available", size);
        return;
    }
    ImGui::Image(Tex(map->srvLinear.Get()), size);
    const ImVec2 mn = ImGui::GetItemRectMin();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (ImGui::IsItemHovered()) {
        const ImVec2 mp = ImGui::GetMousePos();
        const XMFLOAT2 uv{std::clamp((mp.x - mn.x) / size.x, 0.0f, 1.0f), std::clamp((mp.y - mn.y) / size.y, 0.0f, 1.0f)};
        hoveredThisFrame_ = true;
        if (!pinned_) {
            probeUV_ = uv;
            probeValid_ = true;
        }
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            probeUV_ = uv;
            probeValid_ = pinned_ = true;
        }
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) pinned_ = false;
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) inspected_ = kind;
    }
    // Crosshair at the probe position, mirrored on every map image.
    if (probeValid_) {
        const ImVec2 p(mn.x + probeUV_.x * size.x, mn.y + probeUV_.y * size.y);
        const ImU32 col = pinned_ ? IM_COL32(255, 60, 200, 255) : IM_COL32(255, 255, 255, 220);
        dl->AddLine(ImVec2(p.x, mn.y), ImVec2(p.x, mn.y + size.y), IM_COL32(0, 0, 0, 120), 3);
        dl->AddLine(ImVec2(mn.x, p.y), ImVec2(mn.x + size.x, p.y), IM_COL32(0, 0, 0, 120), 3);
        dl->AddLine(ImVec2(p.x, mn.y), ImVec2(p.x, mn.y + size.y), col);
        dl->AddLine(ImVec2(mn.x, p.y), ImVec2(mn.x + size.x, p.y), col);
        dl->AddCircle(p, 5, col, 0, 2);
    }
}

void App::UiMaps() {
    ImGui::Begin("Maps");
    const Material& m = Current();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float sp = ImGui::GetStyle().ItemSpacing.y, line = ImGui::GetTextLineHeightWithSpacing();
    const float tile = std::max(64.0f, std::min((avail.x - 2 * ImGui::GetStyle().ItemSpacing.x) / 3,
                                                (avail.y - 4 * line - 2 * sp) / 2));
    const MapKind kinds[] = {MapColor, MapBump, MapDisplacement, MapRoughness, MapNormal};
    for (int i = 0; i < 5; ++i) {
        if (i % 3) ImGui::SameLine();
        ImGui::BeginGroup();
        ImGui::PushID(i);
        const bool sel = inspected_ == kinds[i];
        std::string label = MapName(kinds[i]);
        if (kinds[i] == MapBump && m.bumpFromHeight) label += " (from height)";
        if (sel) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 0.8f, 0.3f, 1));
        ImGui::TextUnformatted(label.c_str());
        if (sel) ImGui::PopStyleColor();
        MapImage(kinds[i], ImVec2(tile, tile));
        if (ImGui::IsItemClicked() || (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Middle)))
            inspected_ = kinds[i];
        ImGui::PopID();
        ImGui::EndGroup();
    }
    ImGui::TextDisabled("Hover a map to probe it. Left-click pins the probe, right-click unpins.\n"
                        "The selected map is shown large in the Inspector.");
    ImGui::End();
}

void App::UiInspector() {
    ImGui::Begin("Inspector");
    const Material& m = Current();
    if (ImGui::BeginCombo("Map", MapName(inspected_))) {
        for (int k = 0; k < MapCount; ++k)
            if (ImGui::Selectable(MapName(MapKind(k)), inspected_ == k)) inspected_ = MapKind(k);
        ImGui::EndCombo();
    }
    // Large map on the left with the readouts beside it; stacked if the panel is narrow.
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float readoutWidth = 24 * ImGui::GetFontSize();
    const bool sideBySide = avail.x - readoutWidth >= 200;
    const float side = sideBySide ? std::max(128.0f, std::min(avail.x - readoutWidth, avail.y - 8))
                                  : std::max(128.0f, std::min(avail.x, avail.y * 0.38f));
    MapImage(inspected_, ImVec2(side, side));
    if (sideBySide) ImGui::SameLine();
    ImGui::BeginGroup();

    const HoverInfo& h = info_;
    if (!h.valid) {
        ImGui::TextDisabled("Move the mouse over a map\nto inspect it.");
    } else {
        ImGui::Text("Probe %s", pinned_ ? "(pinned - right-click a map to release)" : "(hover)");
        if (ImGui::BeginTable("vals", 3, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg)) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::TextUnformatted("u, v");
            ImGui::TableNextColumn(); ImGui::Text("(%.4f, %.4f)", h.uv.x, h.uv.y);
            ImGui::TableNextColumn();
            const auto& c = m.maps[MapColor];
            if (c) ImGui::Text("texel (%d, %d)", int(h.uv.x * c->width), int(h.uv.y * c->height));
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::TextUnformatted("Color (sRGB)");
            ImGui::TableNextColumn(); ImGui::Text("(%.3f, %.3f, %.3f)", h.color.x, h.color.y, h.color.z);
            ImGui::TableNextColumn();
            ImGui::ColorButton("##c", ImVec4(h.color.x, h.color.y, h.color.z, 1), ImGuiColorEditFlags_NoTooltip, ImVec2(36, 0));
            ScalarRow("Bump value", h.bump, "Height used only to tilt the shading normal.");
            ScalarRow("Displacement", h.displacement, "Height used to move vertices along the normal.");
            ScalarRow("Spec. roughness", h.roughness, "0 = mirror-like, 1 = fully rough (GGX alpha = r^2).");
            ImGui::EndTable();
        }

        ImGui::SeparatorText("Normal computed from the bump map");
        ImGui::Text("slope dh/dx = %+.4f, dh/dy = %+.4f  (per texel)", h.slope.x, h.slope.y);
        ImGui::TextDisabled("n_ts = normalize(-k*dh/dx, -k*dh/dy, 1),  k = %.1f", bump_.strength);
        if (ImGui::BeginTable("normals", 3, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg)) {
            Vec3Row("Tangent space n_ts", h.normalTangent, true,
                    "Relative to the surface: x along T (+u), y along B (+v), z along N.\nSwatch = n*0.5+0.5.");
            if (m.maps[MapNormal]) {
                const XMFLOAT4 t = m.maps[MapNormal]->Sample(h.uv.x, h.uv.y);
                XMFLOAT3 pn;
                XMStoreFloat3(&pn, XMVector3Normalize(XMVectorSet(t.x * 2 - 1, t.y * 2 - 1, t.z * 2 - 1, 0)));
                Vec3Row("Pack normal map", pn, true,
                        "Decoded from the material's own (DirectX convention) normal map, for comparison.\n"
                        "It was baked with a different strength, so only the direction trend matches.");
            }
            if (h.onSurface) {
                Vec3Row("World n' (bumped)", h.normalBumped, true, "n' = T*n_ts.x + B*n_ts.y + N*n_ts.z, rotated to world.");
                Vec3Row("World N (geometric)", h.normalGeom, true, "Normal of the shape itself, without bump.");
                Vec3Row("World T (dP/du)", h.tangent, false, "Tangent: direction of increasing u.");
                Vec3Row("World B (dP/dv)", h.bitangent, false, "Bitangent: direction of increasing v.");
                Vec3Row("World position", h.position, false, "Displaced surface point (magenta ring in 3D).");
            }
            ImGui::EndTable();
        }
        if (!h.onSurface)
            ImGui::TextDisabled("This uv is not on the %s's main UV chart.", ShapeName(shape_));
    }
    ImGui::EndGroup();
    ImGui::End();
}

void App::UiViewport() {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("Viewport");
    ImGui::PopStyleVar();
    const ImVec2 size = ImGui::GetContentRegionAvail();
    const UINT w = UINT(std::max(1.0f, size.x)), h = UINT(std::max(1.0f, size.y));

    const XMFLOAT3 eye = camera_.Eye();
    const XMFLOAT3 lp = light_.Position();
    const XMMATRIX view = XMMatrixLookAtRH(XMLoadFloat3(&eye), XMVectorZero(), XMVectorSet(0, 1, 0, 0));
    const XMMATRIX proj = XMMatrixPerspectiveFovRH(XMConvertToRadians(45), float(w) / float(h), 0.05f, 100.0f);

    const Material& m = Current();
    FrameConstants fc{};
    XMStoreFloat4x4(&fc.world, World());
    XMStoreFloat4x4(&fc.viewProj, view * proj);
    fc.cameraPos = eye;
    fc.lightPos = lp;
    fc.lightIntensity = light_.intensity;
    fc.lightColor = {light_.color[0], light_.color[1], light_.color[2]};
    fc.bumpStrength = bump_.strength;
    fc.dispScale = bump_.displacementScale;
    fc.dispMid = bump_.displacementMid;
    // Sample the height at a mip whose resolution matches the mesh grid (avoids aliasing).
    const auto& disp = m.maps[MapDisplacement];
    fc.dispLod = disp ? std::max(0.0f, std::log2(float(disp->width) / Renderer::kMeshSegments)) : 0;
    fc.metallic = metallic_;
    fc.ambient = ambient_;
    fc.viewMode = viewMode_;
    fc.useColor = useColor_ && m.maps[MapColor];
    fc.useBump = useBump_ && m.maps[MapBump];
    fc.useDisp = bump_.useDisplacement && disp;
    fc.useRough = useRough_ && m.maps[MapRoughness];
    fc.constRoughness = constRough_;
    fc.hoverUV = info_.uv;
    fc.hoverOn = info_.valid ? 1.0f : 0.0f;

    std::vector<LineVertex> lines;
    // Light marker: three small circles.
    const XMFLOAT4 lc{1, 0.9f, 0.3f, 1};
    AddCircle(lines, lp, XMVectorSet(1, 0, 0, 0), XMVectorSet(0, 1, 0, 0), 0.08f, lc);
    AddCircle(lines, lp, XMVectorSet(0, 1, 0, 0), XMVectorSet(0, 0, 1, 0), 0.08f, lc);
    AddCircle(lines, lp, XMVectorSet(1, 0, 0, 0), XMVectorSet(0, 0, 1, 0), 0.08f, lc);
    if (showArrows_ && info_.onSurface) {
        const XMVECTOR p = XMLoadFloat3(&info_.position);
        AddArrow(lines, p, XMLoadFloat3(&info_.normalGeom), 0.45f, {1, 0.85f, 0.1f, 1});    // N
        AddArrow(lines, p, XMLoadFloat3(&info_.normalBumped), 0.55f, {0.2f, 0.95f, 1, 1});  // N'
        AddArrow(lines, p, XMLoadFloat3(&info_.tangent), 0.25f, {1, 0.25f, 0.25f, 1});      // T
        AddArrow(lines, p, XMLoadFloat3(&info_.bitangent), 0.25f, {0.3f, 1, 0.3f, 1});      // B
    }

    ID3D11ShaderResourceView* img = renderer_.RenderScene(w, h, fc, m, shape_, lines);
    if (img) {
        ImGui::Image(Tex(img), ImVec2(float(w), float(h)));
    } else {
        ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "Shaders not available:\n%s", shaderLog_.c_str());
        ImGui::InvisibleButton("vp", ImVec2(float(w), float(h)));
    }

    // Mouse interaction
    if (ImGui::IsItemHovered()) {
        ImGuiIO& io = ImGui::GetIO();
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0)) {
            camera_.yaw -= io.MouseDelta.x * 0.008f;
            camera_.pitch = std::clamp(camera_.pitch + io.MouseDelta.y * 0.008f, -1.55f, 1.55f);
        }
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Right, 0)) {
            light_.azimuth -= io.MouseDelta.x * 0.01f;
            light_.elevation = std::clamp(light_.elevation + io.MouseDelta.y * 0.01f, -1.55f, 1.55f);
        }
        if (io.MouseWheel != 0)
            camera_.distance = std::clamp(camera_.distance * std::pow(0.9f, io.MouseWheel), 1.5f, 12.0f);
    }
    camera_.yaw = std::remainder(camera_.yaw, 2 * kPi);
    light_.azimuth = std::remainder(light_.azimuth, 2 * kPi);

    // Legend overlay
    const ImVec2 o = ImGui::GetItemRectMin();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float y = o.y + 8;
    auto legend = [&](ImU32 col, const char* text) {
        dl->AddRectFilled(ImVec2(o.x + 10, y + 4), ImVec2(o.x + 22, y + 10), col);
        dl->AddText(ImVec2(o.x + 28, y), IM_COL32(230, 230, 230, 255), text);
        y += 17;
    };
    char title[160];
    snprintf(title, sizeof(title), "%s on %s  |  %s", m.name.c_str(), ShapeName(shape_), kViewModes[viewMode_]);
    dl->AddText(ImVec2(o.x + 10, y), IM_COL32(255, 255, 255, 255), title);
    y += 20;
    if (showArrows_ && info_.onSurface) {
        legend(IM_COL32(255, 217, 25, 255), "N  geometric normal");
        legend(IM_COL32(51, 242, 255, 255), "N' normal from bump map");
        legend(IM_COL32(255, 64, 64, 255), "T  tangent (+u)");
        legend(IM_COL32(77, 255, 77, 255), "B  bitangent (+v)");
    }
    legend(IM_COL32(255, 230, 77, 255), "light");
    dl->AddText(ImVec2(o.x + 10, o.y + size.y - 22), IM_COL32(180, 180, 180, 255),
                "Left-drag: orbit camera   Right-drag: orbit light   Wheel: zoom   Space: rotate object");
    ImGui::End();
}

void App::UiConcepts() {
    ImGui::Begin("Concepts");
    auto section = [](const char* title, const char* body) {
        if (ImGui::CollapsingHeader(title)) {
            ImGui::PushTextWrapPos(0);
            ImGui::TextUnformatted(body);
            ImGui::PopTextWrapPos();
        }
    };
    section("UV space",
            "Every vertex stores a texture coordinate (u,v). Normalized texture space runs from (0,0) at the "
            "top-left texel corner to (1,1) at the bottom-right (Direct3D convention: v grows downwards). "
            "The rasterizer interpolates uv across each triangle and the pixel shader samples the textures "
            "there. Choose the 'UV coordinates' view to see the mapping; notice how the sphere stretches "
            "u around its equator and pinches it at the poles, and how the pyramid gives each face a quarter "
            "of the texture.");
    section("Bump mapping",
            "A bump map is a height field h(u,v). It does NOT move geometry: the shader only tilts the "
            "normal according to the slope of h. With central differences, dh/dx = (h(x+1)-h(x-1))/2, and the "
            "surface z = k*h has tangent-space normal n_ts = normalize(-k*dh/dx, -k*dh/dy, 1). Flat areas give "
            "(0,0,1). Turn displacement off and look at the silhouette: it stays perfectly smooth.");
    section("Tangent space and TBN",
            "Tangent space is a frame attached to the surface: T points to +u, B to +v and N is the geometric "
            "normal. The matrix [T B N] converts a tangent-space normal to object/world space: "
            "n' = T*n.x + B*n.y + N*n.z. That is why the same bump map works on any shape. The Inspector "
            "shows n_ts and the resulting world-space n' for the probed texel; the cyan arrow is n'.");
    section("Normal maps",
            "A normal map stores n_ts directly as a color (rgb = n*0.5+0.5), precomputed from a height map or "
            "a high-poly model. Mostly bluish because flat areas are (0,0,1). Conventions differ in the sign of "
            "green (OpenGL vs DirectX). The 'Pack normal map' row in the Inspector decodes the material's own "
            "map so you can compare it with the one computed from the bump map.");
    section("Displacement mapping",
            "Displacement really moves the surface: each vertex is pushed along its normal by "
            "scale*(h - mid). It changes silhouettes and self-occlusion, but needs dense geometry (here a "
            "256x256 grid per patch). The shading normal does not change unless a bump/normal map is also used, "
            "which is why both are usually combined. Note the cracks on the pyramid edges and cylinder rims: "
            "vertices shared by faces with different normals are pushed in different directions.");
    section("Specular roughness",
            "The roughness map controls the microfacet distribution of the GGX/Cook-Torrance BRDF "
            "(alpha = r^2). Low values give small, sharp highlights; high values spread the highlight "
            "into a dull sheen. Orbit the light and compare shiny and rough regions of the same material.");
    ImGui::End();
}
