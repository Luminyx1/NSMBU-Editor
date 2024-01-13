#include <editor.h>
#include <eft.h>
#include <ui/ImGuiUtil.h>

#include <new>
#include <string>

#include <nw/eft/eft_Config.h>
#include <nw/eft/eft_Emitter.h>
#include <nw/eft/eft_EmitterSet.h>
#include <nw/eft/eft_Handle.h>
#include <nw/eft/eft_Renderer.h>
#include <nw/eft/eft_Resource.h>
#include <nw/eft/eft_System.h>

#include <filedevice/rio_FileDeviceMgr.h>
#include <gfx/lyr/rio_Renderer.h>
#include <gfx/rio_PrimitiveRenderer.h>
#include <gfx/rio_Projection.h>
#include <gfx/rio_Window.h>
#include <gpu/rio_RenderState.h>
#include <math/rio_Matrix.h>

#if RIO_IS_WIN
    #include <file.hpp>
    #include <globals.hpp>
#endif // RIO_IS_WIN

#include <rio.h>

#include <imgui_internal.h>

static constexpr f32 cScale = 4.0f;

bool ReadContentFile(const char* filename, u8** out_data, u32* out_size);
void FreeContentFile(const void* data);

static inline bool InitEftSystem()
{
    if (g_EftSystem)
        return false;

    nw::eft::Config config;
    config.SetEffectHeap(&g_EftRootHeap);
    config.SetResourceNum(1);
    config.SetEmitterSetNum(128);
    config.SetEmitterNum(256);
    config.SetParticleNum(2048);
    config.SetStripeNum(256);

    g_EftSystem = new (g_EftRootHeap.Alloc(sizeof(nw::eft::System))) nw::eft::System(config);
    if (!g_EftSystem)
        return false;

    return true;
}

static inline bool DeInitEftSystem()
{
    if (!g_EftSystem)
        return false;

    g_EftSystem->~System();
    g_EftRootHeap.Free(g_EftSystem);
    g_EftSystem = NULL;

    return true;
}

Editor::Editor()
    : rio::ITask("NSMBU Editor")
    , mCurrentEmitterSet(0)
    , mViewPos{ 0.0f, 0.0f }
    , mViewResized(false)
    , mViewHovered(false)
    , mViewFocused(false)
    , mpColorTexture(nullptr)
    , mpDepthTexture(nullptr)
{
}

void Editor::initEftSystem_()
{
    [[maybe_unused]] bool eft_system_initialized = InitEftSystem();
    RIO_ASSERT(eft_system_initialized);

    mPtclFile = NULL;
    u32 ptcl_file_len = 0;

    [[maybe_unused]] bool read = ReadContentFile("Eset_Cafe.ptcl", &mPtclFile, &ptcl_file_len);
    RIO_ASSERT(read);

    RIO_LOG("Ptcl file size: %u\n", ptcl_file_len);
    RIO_LOG("Ptcl file magic: %c%c%c%c\n", mPtclFile[0], mPtclFile[1], mPtclFile[2], mPtclFile[3]);

    g_EftSystem->EntryResource(&g_EftRootHeap, mPtclFile, 0);

    [[maybe_unused]] bool created = g_EftSystem->CreateEmitterSetID(&g_EftHandle, nw::math::MTX34::Identity(), 0);
    RIO_ASSERT(created);

    RIO_LOG("Current EmitterSet: %s\n", g_EftSystem->GetResource(0)->GetEmitterSetName(g_EftHandle.GetEmitterSet()->GetEmitterSetID()));
}

void Editor::calcEftSystem_()
{
    g_EftSystem->BeginFrame();
    g_EftSystem->SwapDoubleBuffer();

    g_EftSystem->CalcEmitter(0);
    g_EftSystem->CalcParticle(true);

    // --------- All modifications happen here ---------

    if (mLoopEmitterSet && !g_EftHandle.GetEmitterSet()->IsAlive())
    {
        changeEftEmitterSet_();
    }

    // -------------------------------------------------

    g_EftSystem->Calc(true);
}

void Editor::drawEftSystem_(const nw::math::MTX44& proj, const nw::math::MTX34& view, const nw::math::VEC3& camPos, f32 zNear, f32 zFar)
{
    g_EftSystem->GetRenderer()->SetFrameBufferTexture(mpColorTexture->getNativeTextureHandle());
    g_EftSystem->GetRenderer()->SetDepthTexture(mpDepthTexture->getNativeTextureHandle());

    rio::Shader::setShaderMode(rio::Shader::MODE_UNIFORM_BLOCK);

#if RIO_IS_CAFE
    GX2Invalidate(GX2_INVALIDATE_SHADER, 0, 0xFFFFFFFF);
#endif // RIO_IS_CAFE

    g_EftSystem->BeginRender(proj, view, camPos, zNear, zFar);

    for (nw::eft::EmitterInstance* emitter = g_EftSystem->GetEmitterHead(0); emitter != NULL; emitter = emitter->next)
        g_EftSystem->RenderEmitter(emitter, true, NULL);

    g_EftSystem->EndRender();

    rio::Shader::setShaderMode(rio::Shader::MODE_UNIFORM_REGISTER);
}

void Editor::changeEftEmitterSet_()
{
    g_EftHandle.GetEmitterSet()->Kill();

    [[maybe_unused]] bool created = g_EftSystem->CreateEmitterSetID(&g_EftHandle, nw::math::MTX34::Identity(), mCurrentEmitterSet);
    RIO_ASSERT(created);

    rio::Matrix34f mtx;
    mtx.makeS({ cScale * 1.5f, cScale * 1.5f, cScale });
    g_EftHandle.GetEmitterSet()->SetMtx(reinterpret_cast<const nw::math::MTX34&>(mtx.a[0]));
}

void Editor::calcViewUi_()
{
    ImGuiID id = ImGui::DockSpaceOverViewport(nullptr, ImGuiDockNodeFlags_NoDockingInCentralNode | ImGuiDockNodeFlags_PassthruCentralNode, nullptr);
    ImGuiDockNode* node = ImGui::DockBuilderGetCentralNode(id);

    ImGuiWindowClass centralAlways = {};
    centralAlways.DockNodeFlagsOverrideSet |= ImGuiDockNodeFlags_NoTabBar | ImGuiDockNodeFlags_NoDockingOverMe;
    ImGui::SetNextWindowClass(&centralAlways);
    ImGui::SetNextWindowDockID(node->ID, ImGuiCond_Always);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 0.0f, 0.0f });
    bool ret = ImGui::Begin("EditorView", nullptr, ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse);
    ImGui::PopStyleVar();
    if (ret)
    {
        ImVec2 pos = ImGui::GetCursorScreenPos();
        const ImVec2& size = ImGui::GetContentRegionAvail();

        s32 width = std::max<s32>(1, size.x);
        s32 height = std::max<s32>(1, size.y);
        if (mViewSize.x != width || mViewSize.y != height)
        {
            mViewSize.x = width;
            mViewSize.y = height;
            mViewResized = true;
        }

        ImTextureID texture_id = nullptr;

#if RIO_IS_CAFE
        mImGuiGX2Texture.Texture = const_cast<GX2Texture*>(mpColorTexture->getNativeTextureHandle());
        texture_id = &mImGuiGX2Texture;
#elif RIO_IS_WIN
        texture_id = (void*)(mpColorTexture->getNativeTextureHandle());
#endif

        ImGui::Image(texture_id, size);

        bool moved = false;
        if (mViewPos.x != pos.x || mViewPos.y != pos.y)
        {
            mViewPos.x = pos.x;
            mViewPos.y = pos.y;
            moved = true;
        }

        mViewHovered = ImGui::IsWindowHovered();
        mViewFocused = ImGui::IsWindowFocused() && !(moved || mViewResized);
    }
    ImGui::End();

  //processMouseInput_();
  //processKeyboardInput_();
}

void Editor::drawUiEmitterSelection_()
{
    if (ImGui::Begin("EmitterSet Selection"))
    {
        nw::eft::Resource* resource = g_EftSystem->GetResource(0);
        
        ImGui::Checkbox("Loop", &mLoopEmitterSet);
        ImGui::SameLine();
        if (ImGui::Button("Play"))
            changeEftEmitterSet_();

        if (ImGui::TreeNode("Emitter Sets"))
        {
            for (u32 i = 0; i < resource->GetNumEmitterSet(); i++)
            {            
                if (ImGui::TreeNode(resource->GetEmitterSetName(i)))
                {
                    const nw::eft::EmitterSetData* set_data = resource->GetEmitterSetData(i);
                    u32 emitter_num = set_data->numEmitter;

                    for (u32 j = 0; j < emitter_num; j++)
                    {
                        if (ImGui::TreeNode(resource->GetEmitterName(i, j)))
                        {
                            ImGui::TreePop();
                        }
                    }
                    ImGui::TreePop();
                }
            }

            ImGui::TreePop();
        }
    }
    ImGui::End();
}

void Editor::resizeView_(s32 width, s32 height)
{
#if RIO_IS_CAFE
    GX2DrawDone();
#elif RIO_IS_WIN
    RIO_GL_CALL(glFinish());
#endif

    const f32 w_half = width  * 0.5f;
    const f32 h_half = height * 0.5f;

    mProjection.setTBLR(
         h_half,    // Top
        -h_half,    // Bottom
        -w_half,    // Left
         w_half     // Right
    );

    createRenderBuffer_(width, height);
}

void Editor::createRenderBuffer_(s32 width, s32 height)
{
    if (mpColorTexture)
    {
        delete mpColorTexture;
        mpColorTexture = nullptr;
    }

    if (mpDepthTexture)
    {
        delete mpDepthTexture;
        mpDepthTexture = nullptr;
    }

    mpColorTexture = new rio::Texture2D(rio::TEXTURE_FORMAT_R8_G8_B8_A8_UNORM, width, height, 1);
    mpDepthTexture = new rio::Texture2D(rio::DEPTH_TEXTURE_FORMAT_R32_FLOAT, width, height, 1);

    mRenderBuffer.setSize(width, height);
    mColorTarget.linkTexture2D(*mpColorTexture);
    mDepthTarget.linkTexture2D(*mpDepthTexture);
    mRenderBuffer.clear(rio::RenderBuffer::CLEAR_FLAG_DEPTH);
}

#if RIO_IS_WIN

void Editor::resize_(s32 width, s32 height)
{
    ImGuiUtil::setDisplaySize(width, height);
}

void Editor::onResizeCallback_(s32 width, s32 height)
{
    static_cast<Editor*>(rio::sRootTask)->resize_(width, height);
}

#endif // RIO_IS_WIN

void Editor::prepare_()
{
#if RIO_IS_WIN
    RIO_LOG("My directory is %s\n", g_CWD.c_str());
#endif // RIO_IS_WIN

#if RIO_IS_WIN
    rio::Window::instance()->setOnResizeCallback(&Editor::onResizeCallback_);
#endif // RIO_IS_WIN

    s32 width = rio::Window::instance()->getWidth();
    s32 height = rio::Window::instance()->getHeight();

    ImGuiUtil::initialize(width, height);
    extern void setupImGuiStyle();
    setupImGuiStyle();

    mViewSize.x = width;
    mViewSize.y = height;

    const f32 w_half = width  * 0.5f;
    const f32 h_half = height * 0.5f;

    mProjection.set(
        -1000.0f,   // Near
         1000.0f,   // Far
         h_half,    // Top
        -h_half,    // Bottom
        -w_half,    // Left
         w_half     // Right
    );

    mRenderBuffer.setRenderTargetColor(&mColorTarget);
    mRenderBuffer.setRenderTargetDepth(&mDepthTarget);

    createRenderBuffer_(width, height);

    initEftSystem_();

    // Foreground layer
    {
        rio::lyr::Layer* const layer = const_cast<rio::lyr::Layer*>(rio::lyr::Layer::peelIterator(rio::lyr::Renderer::instance()->addLayer("Foreground", 0)));

        layer->addRenderStep("Foreground");
        layer->addDrawMethod(0, { this, &Editor::renderForeground });
        layer->setProjection(&mProjection);
    }
    // Background Layer
    {
        rio::lyr::Layer* const layer = const_cast<rio::lyr::Layer*>(rio::lyr::Layer::peelIterator(rio::lyr::Renderer::instance()->addLayer("Background", 1)));

        layer->setClearColor({ 0.25f, 0.25f, 0.25f, 1.0f });
        layer->setClearDepth();

        layer->addRenderStep("Background");
        layer->addDrawMethod(0, { this, &Editor::renderBackground });
        layer->setProjection(&mProjection);
    }

    rio::Matrix34f mtx;
    mtx.makeS({ cScale * 1.5f, cScale * 1.5f, cScale });
    g_EftHandle.GetEmitterSet()->SetMtx(reinterpret_cast<const nw::math::MTX34&>(mtx.a[0]));
}

void Editor::calc_()
{
    ImGuiUtil::newFrame();

    calcViewUi_();
    drawUiEmitterSelection_();
    
    ImGui::ShowDemoWindow();

    if (mViewResized)
    {
        resizeView_(mViewSize.x, mViewSize.y);
        mViewResized = false;
    }

    calcEftSystem_();
}

void Editor::exit_()
{
    if (g_EftHandle.IsValid())
        g_EftHandle.GetEmitterSet()->Kill();

    g_EftSystem->ClearResource(&g_EftRootHeap, 0);
    FreeContentFile(mPtclFile);
    DeInitEftSystem();

    if (mpColorTexture)
    {
        delete mpColorTexture;
        mpColorTexture = nullptr;
    }

    if (mpDepthTexture)
    {
        delete mpDepthTexture;
        mpDepthTexture = nullptr;
    }

    ImGuiUtil::shutdown();

#if RIO_IS_WIN
    rio::Window::instance()->setOnResizeCallback(nullptr);
#endif // RIO_IS_WIN
}

void Editor::bindViewRenderBuffer_()
{
    mpColorTexture->setCompMap(0x00010203);
    mRenderBuffer.bind();
}

void Editor::unbindViewRenderBuffer_()
{
    mRenderBuffer.getRenderTargetColor()->invalidateGPUCache();
    mpColorTexture->setCompMap(0x00010205);

    rio::Window::instance()->makeContextCurrent();

    u32 width = rio::Window::instance()->getWidth();
    u32 height = rio::Window::instance()->getHeight();

    rio::Graphics::setViewport(0, 0, width, height);
    rio::Graphics::setScissor(0, 0, width, height);
}

void Editor::renderForeground(const rio::lyr::DrawInfo& drawInfo)
{
    bindViewRenderBuffer_();

    rio::RenderState render_state;
    render_state.setDepthTestEnable(false);
    render_state.setDepthWriteEnable(false);
    render_state.setBlendEnable(false);
    render_state.apply();

    const rio::lyr::Layer& layer = drawInfo.parent_layer;
    const rio::OrthoProjection* const proj = static_cast<const rio::OrthoProjection*>(layer.projection());

    drawEftSystem_(
        reinterpret_cast<const nw::math::MTX44&>(proj->getMatrix()),
        nw::math::MTX34::Identity(),
        { 0.0f, 0.0f, 0.0f },
        proj->getNear(),
        proj->getFar()
    );

    unbindViewRenderBuffer_();

    ImGuiUtil::render();
}

void Editor::renderBackground(const rio::lyr::DrawInfo& drawInfo)
{
    mpColorTexture->setCompMap(0x00010203);
    mRenderBuffer.clear(
        rio::RenderBuffer::CLEAR_FLAG_COLOR_DEPTH,
        {
            119 / 255.f,
            136 / 255.f,
            153 / 255.f,
            1.0f
        }
    );
    bindViewRenderBuffer_();

    rio::RenderState render_state;
    render_state.setCullingMode(rio::Graphics::CULLING_MODE_NONE);
    render_state.apply();

    const rio::lyr::Layer& layer = drawInfo.parent_layer;

    rio::PrimitiveRenderer* const primitive_renderer = rio::PrimitiveRenderer::instance();
    primitive_renderer->setCamera(*(layer.camera()));
    primitive_renderer->setProjection(*(layer.projection()));

    rio::Matrix34f mtx;
    mtx.makeT({ -128.0f, 16.0f, -600.0f });

    primitive_renderer->setModelMatrix(mtx);

    primitive_renderer->begin();
    {
        primitive_renderer->drawSphere8x16(
            {  },
            128.0f,
            rio::Color4f::cRed,
            rio::Color4f::cBlue
        );
    }
    primitive_renderer->end();

    unbindViewRenderBuffer_();
}

bool ReadContentFile(const char* filename, u8** out_data, u32* out_size)
{
    if (!out_data && !out_size)
        return false;

    rio::FileDevice::LoadArg arg;
    arg.path        = filename;
    arg.alignment   = 0x2000;

    u8* const data = rio::FileDeviceMgr::instance()->tryLoad(arg);

    if (data)
    {
        if (out_data)
            *out_data = data;

        if (out_size)
            *out_size = arg.read_size;

        return true;
    }
    else
    {
        if (out_data)
            *out_data = nullptr;

        if (out_size)
            *out_size = 0;

        return false;
    }
}

void FreeContentFile(const void* data)
{
    rio::MemUtil::free(const_cast<void*>(data));
}
