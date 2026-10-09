// metal-cpp is header-only; exactly one source file compiles its implementation.
#define NS_PRIVATE_IMPLEMENTATION
#define MTL_PRIVATE_IMPLEMENTATION
#include "Renderer.h"
#include <algorithm>
#include <fstream>
#include <sstream>

bool Renderer::Init() {
    device_ = NS::TransferPtr(MTL::CreateSystemDefaultDevice());
    if (!device_) return false;
    queue_ = NS::TransferPtr(device_->newCommandQueue());

    // Fixed pipeline state.
    MTL::SamplerDescriptor* samp = MTL::SamplerDescriptor::alloc()->init();
    samp->setMinFilter(MTL::SamplerMinMagFilterLinear);
    samp->setMagFilter(MTL::SamplerMinMagFilterLinear);
    samp->setMipFilter(MTL::SamplerMipFilterLinear);
    samp->setMaxAnisotropy(8);
    samp->setSAddressMode(MTL::SamplerAddressModeRepeat);
    samp->setTAddressMode(MTL::SamplerAddressModeRepeat);
    samp->setRAddressMode(MTL::SamplerAddressModeRepeat);
    sampler_ = NS::TransferPtr(device_->newSamplerState(samp));
    samp->release();

    MTL::DepthStencilDescriptor* ds = MTL::DepthStencilDescriptor::alloc()->init();
    ds->setDepthCompareFunction(MTL::CompareFunctionLessEqual);
    ds->setDepthWriteEnabled(true);
    depthOn_ = NS::TransferPtr(device_->newDepthStencilState(ds));
    ds->setDepthCompareFunction(MTL::CompareFunctionAlways);
    ds->setDepthWriteEnabled(false);
    depthOff_ = NS::TransferPtr(device_->newDepthStencilState(ds));
    ds->release();

    // 1x1 mid-gray texture bound when a material lacks a map.
    const uint32_t gray = 0xff808080;
    MTL::TextureDescriptor* td = MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, 1, 1, false);
    td->setUsage(MTL::TextureUsageShaderRead);
    fallbackGray_ = NS::TransferPtr(device_->newTexture(td));
    fallbackGray_->replaceRegion(MTL::Region::Make2D(0, 0, 1, 1), 0, &gray, 4);
    return true;
}

void Renderer::Shutdown() {
    for (GpuMesh& m : meshes_) m = GpuMesh{};
}

MTL::CommandBuffer* Renderer::BeginFrame() {
    frame_ = queue_->commandBuffer();
    return frame_;
}

static NS::String* Str(const std::string& s) { return NS::String::string(s.c_str(), NS::UTF8StringEncoding); }

static NS::SharedPtr<MTL::Library> Compile(MTL::Device* device, const std::filesystem::path& file,
                                           std::string& log) {
    std::ifstream in(file);
    if (!in) {
        log += "Cannot open " + file.string() + "\n";
        return {};
    }
    std::stringstream source;
    source << in.rdbuf();
    NS::Error* error = nullptr;
    MTL::CompileOptions* options = MTL::CompileOptions::alloc()->init();
    MTL::Library* lib = device->newLibrary(Str(source.str()), options, &error);
    options->release();
    if (error) log += file.filename().string() + ": " + error->localizedDescription()->utf8String() + "\n";
    return NS::TransferPtr(lib);
}

bool Renderer::ReloadShaders(const std::filesystem::path& dir, std::string& log) {
    log.clear();
    auto pbr = Compile(device_.get(), dir / "pbr.metal", log);
    auto gizmo = Compile(device_.get(), dir / "gizmo.metal", log);
    if (!pbr || !gizmo) return false;  // keep the previous working shaders
    auto vs = NS::TransferPtr(pbr->newFunction(Str("VSMain")));
    auto ps = NS::TransferPtr(pbr->newFunction(Str("PSMain")));
    auto lvs = NS::TransferPtr(gizmo->newFunction(Str("VSLine")));
    auto lps = NS::TransferPtr(gizmo->newFunction(Str("PSLine")));
    if (!vs || !ps || !lvs || !lps) {
        log += "Missing shader function\n";
        return false;
    }

    // The vertex descriptor is Metal's input layout.
    MTL::VertexDescriptor* meshElems = MTL::VertexDescriptor::alloc()->init();
    const MTL::VertexFormat formats[] = {MTL::VertexFormatFloat3, MTL::VertexFormatFloat3, MTL::VertexFormatFloat4,
                                         MTL::VertexFormatFloat2};
    const size_t offsets[] = {offsetof(Vertex, pos), offsetof(Vertex, normal), offsetof(Vertex, tangent),
                              offsetof(Vertex, uv)};
    for (int a = 0; a < 4; ++a) {
        meshElems->attributes()->object(a)->setFormat(formats[a]);
        meshElems->attributes()->object(a)->setOffset(offsets[a]);
        meshElems->attributes()->object(a)->setBufferIndex(0);
    }
    meshElems->layouts()->object(0)->setStride(sizeof(Vertex));
    MTL::VertexDescriptor* lineElems = MTL::VertexDescriptor::alloc()->init();
    lineElems->attributes()->object(0)->setFormat(MTL::VertexFormatFloat3);
    lineElems->attributes()->object(0)->setOffset(offsetof(LineVertex, pos));
    lineElems->attributes()->object(1)->setFormat(MTL::VertexFormatFloat4);
    lineElems->attributes()->object(1)->setOffset(offsetof(LineVertex, color));
    lineElems->layouts()->object(0)->setStride(sizeof(LineVertex));

    // A pipeline state object bundles the shaders, the input layout, the formats of the
    // render target and the blend state (separate objects in Direct3D 11).
    MTL::RenderPipelineDescriptor* pd = MTL::RenderPipelineDescriptor::alloc()->init();
    pd->setRasterSampleCount(4);
    pd->colorAttachments()->object(0)->setPixelFormat(MTL::PixelFormatRGBA8Unorm_sRGB);
    pd->setDepthAttachmentPixelFormat(MTL::PixelFormatDepth32Float);
    pd->setVertexFunction(vs.get());
    pd->setFragmentFunction(ps.get());
    pd->setVertexDescriptor(meshElems);
    NS::Error* error = nullptr;
    auto newMesh = NS::TransferPtr(device_->newRenderPipelineState(pd, &error));
    if (error) log += std::string(error->localizedDescription()->utf8String()) + "\n";

    auto* rt = pd->colorAttachments()->object(0);
    rt->setBlendingEnabled(true);
    rt->setSourceRGBBlendFactor(MTL::BlendFactorSourceAlpha);
    rt->setDestinationRGBBlendFactor(MTL::BlendFactorOneMinusSourceAlpha);
    rt->setRgbBlendOperation(MTL::BlendOperationAdd);
    rt->setSourceAlphaBlendFactor(MTL::BlendFactorOne);
    rt->setDestinationAlphaBlendFactor(MTL::BlendFactorZero);
    rt->setAlphaBlendOperation(MTL::BlendOperationAdd);
    pd->setVertexFunction(lvs.get());
    pd->setFragmentFunction(lps.get());
    pd->setVertexDescriptor(lineElems);
    error = nullptr;
    auto newLine = NS::TransferPtr(device_->newRenderPipelineState(pd, &error));
    if (error) log += std::string(error->localizedDescription()->utf8String()) + "\n";
    pd->release();
    meshElems->release();
    lineElems->release();

    if (!newMesh || !newLine) {
        log += "Failed to create shader objects\n";
        return false;
    }
    meshPipeline_ = newMesh;
    linePipeline_ = newLine;
    return true;
}

void Renderer::EnsureSceneTarget(uint32_t width, uint32_t height) {
    if (width == sceneW_ && height == sceneH_ && msaaColor_) return;
    sceneW_ = width;
    sceneH_ = height;
    // Rendered as sRGB (gamma-correct lighting) but shown by ImGui through a UNORM view,
    // i.e. the already-encoded values reach the screen untouched.
    MTL::TextureDescriptor* td = MTL::TextureDescriptor::texture2DDescriptor(
        MTL::PixelFormatRGBA8Unorm_sRGB, width, height, false);
    td->setTextureType(MTL::TextureType2DMultisample);
    td->setSampleCount(4);
    td->setUsage(MTL::TextureUsageRenderTarget);
    td->setStorageMode(MTL::StorageModePrivate);
    msaaColor_ = NS::TransferPtr(device_->newTexture(td));

    td->setTextureType(MTL::TextureType2D);
    td->setSampleCount(1);
    td->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead | MTL::TextureUsagePixelFormatView);
    resolved_ = NS::TransferPtr(device_->newTexture(td));
    resolvedUnorm_ = NS::TransferPtr(resolved_->newTextureView(MTL::PixelFormatRGBA8Unorm));

    td->setPixelFormat(MTL::PixelFormatDepth32Float);
    td->setTextureType(MTL::TextureType2DMultisample);
    td->setSampleCount(4);
    td->setUsage(MTL::TextureUsageRenderTarget);
    depth_ = NS::TransferPtr(device_->newTexture(td));
}

Renderer::GpuMesh& Renderer::MeshFor(Shape shape) {
    GpuMesh& m = meshes_[int(shape)];
    if (m.vb) return m;
    const MeshData data = BuildMesh(shape, kMeshSegments);
    m.vb = NS::TransferPtr(device_->newBuffer(data.vertices.data(), data.vertices.size() * sizeof(Vertex),
                                              MTL::ResourceStorageModeShared));
    m.ib = NS::TransferPtr(device_->newBuffer(data.indices.data(), data.indices.size() * sizeof(uint32_t),
                                              MTL::ResourceStorageModeShared));
    m.indexCount = data.indices.size();
    return m;
}

MTL::Texture* Renderer::RenderScene(uint32_t width, uint32_t height, const FrameConstants& fc,
                                    const Material& material, Shape shape,
                                    const std::vector<LineVertex>& lines) {
    if (width == 0 || height == 0 || !meshPipeline_ || !frame_) return nullptr;
    EnsureSceneTarget(width, height);

    // Clearing and resolving the multisampled target are actions of the render pass.
    MTL::RenderPassDescriptor* pass = MTL::RenderPassDescriptor::renderPassDescriptor();
    auto* color = pass->colorAttachments()->object(0);
    color->setTexture(msaaColor_.get());
    color->setResolveTexture(resolved_.get());
    color->setLoadAction(MTL::LoadActionClear);
    color->setClearColor(MTL::ClearColor::Make(0.035, 0.04, 0.05, 1));  // linear (sRGB target)
    color->setStoreAction(MTL::StoreActionMultisampleResolve);
    auto* depth = pass->depthAttachment();
    depth->setTexture(depth_.get());
    depth->setLoadAction(MTL::LoadActionClear);
    depth->setClearDepth(1);
    depth->setStoreAction(MTL::StoreActionDontCare);

    MTL::RenderCommandEncoder* enc = frame_->renderCommandEncoder(pass);
    enc->setViewport(MTL::Viewport{0, 0, double(width), double(height), 0, 1});
    enc->setCullMode(MTL::CullModeNone);
    enc->setDepthStencilState(depthOn_.get());

    // Small constants are copied into the command buffer, so the next frame can change
    // them while the GPU still reads this frame's values.
    enc->setVertexBytes(&fc, sizeof(fc), 1);
    enc->setFragmentBytes(&fc, sizeof(fc), 1);

    // Surface
    GpuMesh& mesh = MeshFor(shape);
    enc->setRenderPipelineState(meshPipeline_.get());
    enc->setVertexBuffer(mesh.vb.get(), 0, 0);
    auto tex = [&](MapKind k, bool srgb) -> MTL::Texture* {
        const auto& map = material.maps[k];
        if (!map) return fallbackGray_.get();
        return srgb ? map->texSrgb.get() : map->texLinear.get();
    };
    MTL::Texture* texs[4] = {tex(MapColor, true), tex(MapBump, false), tex(MapDisplacement, false),
                             tex(MapRoughness, false)};
    enc->setVertexTextures(texs, NS::Range::Make(0, 4));
    enc->setFragmentTextures(texs, NS::Range::Make(0, 4));
    enc->setVertexSamplerState(sampler_.get(), 0);
    enc->setFragmentSamplerState(sampler_.get(), 0);
    enc->drawIndexedPrimitives(MTL::PrimitiveTypeTriangle, mesh.indexCount, MTL::IndexTypeUInt32, mesh.ib.get(), 0);

    // Lines: a faint "x-ray" pass ignoring depth, then a depth-tested pass on top.
    if (!lines.empty()) {
        const size_t n = std::min(lines.size(), kMaxLineVertices / 2);
        // A new buffer each frame: the command buffer keeps it alive until the GPU is done.
        auto vb = NS::TransferPtr(device_->newBuffer(2 * n * sizeof(LineVertex), MTL::ResourceStorageModeShared));
        auto* dst = static_cast<LineVertex*>(vb->contents());
        for (size_t i = 0; i < n; ++i) {
            dst[i] = lines[i];
            dst[i].color.w *= 0.3f;
            dst[n + i] = lines[i];
        }
        enc->setRenderPipelineState(linePipeline_.get());
        enc->setVertexBuffer(vb.get(), 0, 0);
        enc->setDepthStencilState(depthOff_.get());
        enc->drawPrimitives(MTL::PrimitiveTypeLine, NS::UInteger(0), NS::UInteger(n));
        enc->setDepthStencilState(depthOn_.get());
        enc->drawPrimitives(MTL::PrimitiveTypeLine, NS::UInteger(n), NS::UInteger(n));
    }

    enc->endEncoding();
    return resolvedUnorm_.get();
}
