#include <editor.h>
#include <eft.h>

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

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#if RIO_IS_WIN
    #include <file.hpp>
    #include <globals.hpp>
#endif // RIO_IS_WIN

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
    , mpViewportTexture(nullptr)
    , mpViewportDepthTexture(nullptr)
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

    mTimer = 0;
}

void Editor::calcEftSystem_() const
{
    g_EftSystem->BeginFrame();
    g_EftSystem->SwapDoubleBuffer();

    g_EftSystem->CalcEmitter(0);
    g_EftSystem->CalcParticle(true);
}

void Editor::drawEftSystem_(const nw::math::MTX44& proj, const nw::math::MTX34& view, const nw::math::VEC3& camPos, f32 zNear, f32 zFar)
{
    // I want to see if we can get the current method working
    // or we can just ignore that if you want
    // Which would it be?

    mpViewportTexture = new rio::Texture2D(rio::TEXTURE_FORMAT_R8_G8_B8_A8_UNORM, mViewportWidth, mViewportHeight, 1);
    mpViewportDepthTexture = new rio::Texture2D(rio::DEPTH_TEXTURE_FORMAT_R32_FLOAT, mViewportWidth, mViewportHeight, 1);

    glCopyImageSubData(
        rio::Window::instance()->getWindowColorBufferTexture(),
        GL_TEXTURE_2D,
        0,
        mViewportX, mViewportY,
        0,
        mpViewportTexture->getNativeTextureHandle(),
        GL_TEXTURE_2D,
        0, 0, 0, 0,
        mViewportWidth, mViewportHeight, 1
    );

    rio::Window::instance()->updateDepthBufferTexture();
    glCopyImageSubData(
        rio::Window::instance()->getWindowDepthBufferTexture(),
        GL_TEXTURE_2D,
        0,
        mViewportX, mViewportY,
        0,
        mpViewportDepthTexture->getNativeTextureHandle(),
        GL_TEXTURE_2D,
        0, 0, 0, 0,
        mViewportWidth, mViewportHeight, 1
    );

    g_EftSystem->GetRenderer()->SetFrameBufferTexture(mpViewportTexture->getNativeTextureHandle());
    g_EftSystem->GetRenderer()->SetDepthTexture(mpViewportDepthTexture->getNativeTextureHandle());

    rio::Shader::setShaderMode(rio::Shader::MODE_UNIFORM_BLOCK);

#if RIO_IS_CAFE
    GX2Invalidate(GX2_INVALIDATE_SHADER, 0, 0xFFFFFFFF);
#endif // RIO_IS_CAFE

    g_EftSystem->BeginRender(proj, view, camPos, zNear, zFar);

    for (nw::eft::EmitterInstance* emitter = g_EftSystem->GetEmitterHead(0); emitter != NULL; emitter = emitter->next)
        g_EftSystem->RenderEmitter(emitter, true, NULL);

    g_EftSystem->EndRender();

    rio::Shader::setShaderMode(rio::Shader::MODE_UNIFORM_REGISTER);

    delete mpViewportTexture;
    delete mpViewportDepthTexture;
}

void Editor::calcEftSystemNextEmitterSet_()
{
    mTimer += 1;

    if (!g_EftHandle.GetEmitterSet()->IsAlive())
    {
        s32 nextEmitterSetId = g_EftHandle.GetEmitterSet()->GetEmitterSetID() + 1;
        RIO_LOG("Next emitterSet id: %d\n", nextEmitterSetId);

        g_EftHandle.GetEmitterSet()->Kill();
        mTimer = 0;

        if (nextEmitterSetId == g_EftSystem->GetResource(0)->GetNumEmitterSet())
        {
            mIsRunning = false;
            return;
        }

        [[maybe_unused]] bool created = g_EftSystem->CreateEmitterSetID(&g_EftHandle, nw::math::MTX34::Identity(), nextEmitterSetId);
        RIO_ASSERT(created);

        rio::Matrix34f mtx;
        mtx.makeS({ cScale * 1.5f, cScale * 1.5f, cScale });
        g_EftHandle.GetEmitterSet()->SetMtx(reinterpret_cast<const nw::math::MTX34&>(mtx.a[0]));
    }

    if (mTimer == 5*60) // 5 seconds or effect is over
        g_EftHandle.GetEmitterSet()->Fade();

    // IIRC This is used to profile performance
    g_EftSystem->Calc(true);
}

void Editor::initUi_()
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(rio::Window::instance()->getNativeWindow().getGLFWwindow(), true);
    ImGui_ImplOpenGL3_Init("#version 400");
}

void Editor::calcUi_()
{
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    ImGuiID id = ImGui::DockSpaceOverViewport(nullptr, ImGuiDockNodeFlags_NoDockingInCentralNode | ImGuiDockNodeFlags_PassthruCentralNode, nullptr);
    ImGuiDockNode* node = ImGui::DockBuilderGetCentralNode(id);
    mViewportWidth = node->Size.x;
    mViewportHeight = node->Size.y;
    mViewportX = node->Pos.x;
    mViewportY = node->Pos.y;

    mViewportProjection.setTBLR(mViewportHeight * 0.5f, -mViewportHeight * 0.5f, -mViewportWidth * 0.5f, mViewportWidth * 0.5f);

    if (ImGui::Begin("Info"))
    {
        nw::eft::Resource* const resource = g_EftSystem->GetResource(0);
        nw::eft::EmitterSet* const emitter_set = g_EftHandle.GetEmitterSet();

        ImGui::Text("Emitter Set #%i: %s", emitter_set->GetEmitterSetID(), resource->GetEmitterSetName(emitter_set->GetEmitterSetID()));
        ImGui::Text("mViewportX %f", mViewportX);
        ImGui::Text("mViewportY %f", mViewportY);
        ImGui::Text("mViewportWidth %f", mViewportWidth);
        ImGui::Text("mViewportHeight %f", mViewportHeight);
    }
    ImGui::End();
}

void Editor::drawUi_()
{
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void Editor::terminateUi_()
{
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

void Editor::prepare_()
{
#if RIO_IS_WIN
    RIO_LOG("My directory is %s\n", g_CWD.c_str());
#endif // RIO_IS_WIN

    initEftSystem_();
    initUi_();
    calcUi_();
    drawUi_();

    mIsRunning = true;

    // Foreground layer
    {
        rio::lyr::Layer* const layer = const_cast<rio::lyr::Layer*>(rio::lyr::Layer::peelIterator(rio::lyr::Renderer::instance()->addLayer("Foreground", 0)));
        layer->setProjection(&mViewportProjection);

        layer->addRenderStep("Foreground");
        layer->addDrawMethod(0, { this, &Editor::renderForeground });
    }
    // Background Layer
    {
        rio::lyr::Layer* const layer = const_cast<rio::lyr::Layer*>(rio::lyr::Layer::peelIterator(rio::lyr::Renderer::instance()->addLayer("Background", 1)));
        layer->setProjection(&mViewportProjection);

        layer->setClearColor({ 0.25f, 0.25f, 0.25f, 1.0f });
        layer->setClearDepth();

        layer->addRenderStep("Background");
        layer->addDrawMethod(0, { this, &Editor::renderBackground });
    }

    rio::Matrix34f mtx;
    mtx.makeS({ cScale * 1.5f, cScale * 1.5f, cScale });
    g_EftHandle.GetEmitterSet()->SetMtx(reinterpret_cast<const nw::math::MTX34&>(mtx.a[0]));
}

void Editor::calc_()
{
    if (!mIsRunning)
        return;

    calcEftSystem_();
    calcEftSystemNextEmitterSet_();

    calcUi_();
}

void Editor::exit_()
{
    mIsRunning = false;

    if (g_EftHandle.IsValid())
        g_EftHandle.GetEmitterSet()->Kill();

    g_EftSystem->ClearResource(&g_EftRootHeap, 0);
    FreeContentFile(mPtclFile);
    DeInitEftSystem();

    terminateUi_();
}

void Editor::renderForeground(const rio::lyr::DrawInfo& drawInfo)
{
    if (!mIsRunning)
        return;

    rio::RenderState render_state;
    render_state.setDepthTestEnable(false);
    render_state.setDepthWriteEnable(false);
    render_state.setBlendEnable(false);
    render_state.apply();

    const rio::lyr::Layer& layer = drawInfo.parent_layer;
    const rio::OrthoProjection* const proj = static_cast<const rio::OrthoProjection*>(layer.projection());

    rio::Graphics::setViewport(mViewportX, mViewportY, mViewportWidth, mViewportHeight);
    rio::Graphics::setScissor(mViewportX, mViewportY, mViewportWidth, mViewportHeight);
    drawEftSystem_(
        reinterpret_cast<const nw::math::MTX44&>(proj->getMatrix()),
        nw::math::MTX34::Identity(),
        { 0.0f, 0.0f, 0.0f },
        proj->getNear(),
        proj->getFar()
    );

    rio::Graphics::setViewport(0, 0, rio::Window::instance()->getWidth(), rio::Window::instance()->getHeight());
    rio::Graphics::setScissor(0, 0, rio::Window::instance()->getWidth(), rio::Window::instance()->getHeight());
    drawUi_();
}

void Editor::renderBackground(const rio::lyr::DrawInfo& drawInfo)
{
    if (!mIsRunning)
        return;

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

    rio::Graphics::setViewport(mViewportX, mViewportY, mViewportWidth, mViewportHeight);
    rio::Graphics::setScissor(mViewportX, mViewportY, mViewportWidth, mViewportHeight);
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
    rio::Graphics::setViewport(0, 0, rio::Window::instance()->getWidth(), rio::Window::instance()->getHeight());
    rio::Graphics::setScissor(0, 0, rio::Window::instance()->getWidth(), rio::Window::instance()->getHeight());
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
