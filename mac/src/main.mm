// Texture Explorer (macOS) - an interactive Metal tool for learning about texturing:
// color, bump, displacement and roughness maps, UV space and tangent-space normals.
// This is the only Objective-C++ file: it creates the window and runs the frame loop.
// Everything else is C++, using Metal through metal-cpp.
#include "App.h"
#include <imgui.h>
#include <imgui_impl_metal.h>
#include <imgui_impl_osx.h>
#import <Cocoa/Cocoa.h>
#import <MetalKit/MetalKit.h>
#include <chrono>
#include <string>

// A metal-cpp pointer and an Objective-C reference name the same object: converting one
// into the other is a cast.
@interface AppDelegate : NSObject <NSApplicationDelegate, NSWindowDelegate, MTKViewDelegate>
@end

@implementation AppDelegate {
    App* _app;
    NSWindow* _window;
    MTKView* _view;
    std::chrono::steady_clock::time_point _last;
}

- (void)applicationDidFinishLaunching:(NSNotification*)notification {
    _app = new App();
    if (!_app->Init()) {
        NSAlert* alert = [[[NSAlert alloc] init] autorelease];
        alert.messageText = @"Could not create a Metal device.";
        [alert runModal];
        [NSApp terminate:nil];
        return;
    }
    id<MTLDevice> device = (__bridge id<MTLDevice>)_app->GetRenderer().Device();

    const NSRect screen = NSScreen.mainScreen.visibleFrame;
    const NSRect frame = NSMakeRect(0, 0, MIN(1600, screen.size.width * 0.95), MIN(950, screen.size.height * 0.95));
    _window = [[NSWindow alloc] initWithContentRect:frame
                                          styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                                                    NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable
                                            backing:NSBackingStoreBuffered
                                              defer:NO];
    _window.title = @"Texture Explorer - bump, displacement & roughness maps";
    _window.delegate = self;
    [_window center];

    // MTKView owns the drawable (the swap chain) and calls drawInMTKView: once per display refresh.
    _view = [[MTKView alloc] initWithFrame:frame device:device];
    _view.colorPixelFormat = MTLPixelFormatBGRA8Unorm;
    _view.clearColor = MTLClearColorMake(0.08, 0.08, 0.09, 1);
    _view.preferredFramesPerSecond = 60;
    _view.delegate = self;
    _window.contentView = _view;
    [_window makeKeyAndOrderFront:nil];

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_NavEnableKeyboard;
    // Keep the window layout in the user's Application Support folder.
    static std::string iniPath;
    {
        NSString* dir = [NSSearchPathForDirectoriesInDomains(NSApplicationSupportDirectory, NSUserDomainMask, YES)
                             .firstObject stringByAppendingPathComponent:@"TextureExplorer"];
        [NSFileManager.defaultManager createDirectoryAtPath:dir withIntermediateDirectories:YES attributes:nil error:nil];
        iniPath = std::string(dir.UTF8String) + "/imgui.ini";
        io.IniFilename = iniPath.c_str();
    }
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 4;
    style.FrameRounding = 3;
    // ImGui works in points; the font is rasterized at the pixel density of the display.
    const float scale = float(_window.backingScaleFactor);
    const char* uiFont = "/System/Library/Fonts/SFNS.ttf";
    if ([NSFileManager.defaultManager fileExistsAtPath:@(uiFont)] &&
        io.Fonts->AddFontFromFileTTF(uiFont, 16.0f * scale))
        io.FontGlobalScale = 1.0f / scale;  // otherwise the built-in font is used
    ImGui_ImplMetal_Init(device);
    ImGui_ImplOSX_Init(_view);

    _last = std::chrono::steady_clock::now();
    [NSApp activateIgnoringOtherApps:YES];
}

- (void)drawInMTKView:(MTKView*)view {
    @autoreleasepool {
        const auto now = std::chrono::steady_clock::now();
        const float dt = std::chrono::duration<float>(now - _last).count();
        _last = now;

        id<MTLCommandBuffer> cmd = (__bridge id<MTLCommandBuffer>)_app->GetRenderer().BeginFrame();
        MTLRenderPassDescriptor* pass = view.currentRenderPassDescriptor;  // waits for a free drawable
        if (!pass) {
            [cmd commit];
            return;
        }
        ImGui_ImplMetal_NewFrame(pass);
        ImGui_ImplOSX_NewFrame(view);
        ImGui::NewFrame();
        _app->Frame(dt);  // the scene's render pass is encoded into cmd here
        ImGui::Render();

        id<MTLRenderCommandEncoder> enc = [cmd renderCommandEncoderWithDescriptor:pass];
        ImGui_ImplMetal_RenderDrawData(ImGui::GetDrawData(), cmd, enc);
        [enc endEncoding];
        [cmd presentDrawable:view.currentDrawable];
        [cmd commit];
    }
}

- (void)mtkView:(MTKView*)view drawableSizeWillChange:(CGSize)size {
    // Nothing to do: MTKView resizes its drawables itself.
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)sender {
    return YES;
}

- (void)applicationWillTerminate:(NSNotification*)notification {
    if (!_app) return;
    _view.delegate = nil;
    if (ImGui::GetCurrentContext()) {
        ImGui_ImplMetal_Shutdown();
        ImGui_ImplOSX_Shutdown();
        ImGui::DestroyContext();
    }
    _app->Shutdown();
    delete _app;
    _app = nullptr;
}
@end

int main(int, const char**) {
    @autoreleasepool {
        NSApplication* app = NSApplication.sharedApplication;
        app.activationPolicy = NSApplicationActivationPolicyRegular;
        // A minimal menu bar, so that Cmd+Q quits.
        NSMenu* bar = [[[NSMenu alloc] init] autorelease];
        NSMenuItem* appItem = [[[NSMenuItem alloc] init] autorelease];
        NSMenu* appMenu = [[[NSMenu alloc] init] autorelease];
        [appMenu addItemWithTitle:@"Quit Texture Explorer" action:@selector(terminate:) keyEquivalent:@"q"];
        appItem.submenu = appMenu;
        [bar addItem:appItem];
        app.mainMenu = bar;

        AppDelegate* delegate = [[AppDelegate alloc] init];
        app.delegate = delegate;
        [app run];
    }
    return 0;
}
