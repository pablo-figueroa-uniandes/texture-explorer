// Texture Explorer - an interactive Direct3D 11 tool for learning about texturing:
// color, bump, displacement and roughness maps, UV space and tangent-space normals.
#include "App.h"
#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>
#include <windows.h>
#include <chrono>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

static App* g_app = nullptr;
static UINT g_resizeW = 0, g_resizeH = 0;

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam)) return true;
    switch (msg) {
    case WM_SIZE:
        if (wParam != SIZE_MINIMIZED) {
            g_resizeW = LOWORD(lParam);
            g_resizeH = HIWORD(lParam);
        }
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) return 0;  // no ALT application menu
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int) {
    ImGui_ImplWin32_EnableDpiAwareness();
    const float dpi = ImGui_ImplWin32_GetDpiScaleForMonitor(MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY));

    WNDCLASSEXW wc{sizeof(wc), CS_CLASSDC, WndProc, 0, 0, hInstance, LoadIcon(nullptr, IDI_APPLICATION),
                   LoadCursor(nullptr, IDC_ARROW), nullptr, nullptr, L"TextureExplorer", nullptr};
    RegisterClassExW(&wc);
    HWND hwnd = CreateWindowW(wc.lpszClassName, L"Texture Explorer - bump, displacement & roughness maps",
                              WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, int(1600 * dpi), int(950 * dpi),
                              nullptr, nullptr, hInstance, nullptr);

    App app;
    g_app = &app;
    if (!app.Init(hwnd)) {
        MessageBoxW(hwnd, L"Could not create a Direct3D 11 device.", L"Texture Explorer", MB_ICONERROR);
        return 1;
    }
    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_NavEnableKeyboard;
    // Keep the window layout next to the executable, not in the current directory.
    static std::string iniPath;
    {
        char exe[MAX_PATH];
        GetModuleFileNameA(nullptr, exe, MAX_PATH);
        iniPath = exe;
        iniPath = iniPath.substr(0, iniPath.find_last_of("\\/") + 1) + "imgui.ini";
        io.IniFilename = iniPath.c_str();
    }
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 4;
    style.FrameRounding = 3;
    style.ScaleAllSizes(dpi);
    const char* uiFont = "C:\\Windows\\Fonts\\segoeui.ttf";
    if (GetFileAttributesA(uiFont) == INVALID_FILE_ATTRIBUTES ||
        !io.Fonts->AddFontFromFileTTF(uiFont, 17.0f * dpi))
        io.FontGlobalScale = dpi;  // fall back to the built-in font
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(app.GetRenderer().Device(), app.GetRenderer().Context());

    auto last = std::chrono::steady_clock::now();
    bool running = true;
    while (running) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) running = false;
        }
        if (!running) break;
        if (IsIconic(hwnd)) {
            Sleep(10);
            continue;
        }
        if (g_resizeW && g_resizeH) {
            app.GetRenderer().Resize(g_resizeW, g_resizeH);
            g_resizeW = g_resizeH = 0;
        }

        const auto now = std::chrono::steady_clock::now();
        const float dt = std::chrono::duration<float>(now - last).count();
        last = now;

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        app.Frame(dt);
        ImGui::Render();

        const float clear[4] = {0.08f, 0.08f, 0.09f, 1};
        app.GetRenderer().BeginFrame(clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        app.GetRenderer().Present(true);
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    app.Shutdown();
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    return 0;
}
