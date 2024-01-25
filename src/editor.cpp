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
    , mPrevEmitterSet(0)
    , mCurrentEmitterSet(0)
    , mLoopEmitterSet(false)
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

    [[maybe_unused]] bool created = g_EftSystem->CreateEmitterSetID(&g_EftHandle, nw::math::MTX34::Identity(), mCurrentEmitterSet);
    RIO_ASSERT(created);

    rio::Matrix34f mtx;
    mtx.makeS({ cScale * 1.5f, cScale * 1.5f, cScale });
    g_EftHandle.GetEmitterSet()->SetMtx(reinterpret_cast<const nw::math::MTX34&>(mtx.a[0]));

    RIO_LOG("Current EmitterSet: %s\n", g_EftSystem->GetResource(0)->GetEmitterSetName(mCurrentEmitterSet));
}

void Editor::calcEftSystem_()
{
    g_EftSystem->BeginFrame();
    g_EftSystem->SwapDoubleBuffer();

    g_EftSystem->CalcEmitter(0);
    g_EftSystem->CalcParticle(true);

    // --------- All modifications happen here ---------

    if (mLoopEmitterSet && !g_EftHandle.GetEmitterSet()->IsAlive())
        changeEftEmitterSet_();

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

        u32 emitter_set_num = resource->GetNumEmitterSet();
        for (u32 i = 0; i < emitter_set_num; i++)
        {
            // Use ImGui::Selectable instead of ImGui::TreeNode to make the node selectable
            if (ImGui::Selectable(resource->GetEmitterSetName(i), mCurrentEmitterSet == i, ImGuiSelectableFlags_AllowDoubleClick))
            {
                // Set the selected index when the node is selected
                mCurrentEmitterSet = i;
            }

            // If the node is selected or opened, display its content
            if (ImGui::IsItemHovered() || ImGui::IsItemFocused())
            {
                // Display additional information or take action if the node is hovered or focused (optional)
            }

            // Use the selected index to determine whether to open the tree node
            if (mCurrentEmitterSet == i && ImGui::TreeNode(resource->GetEmitterSetName(i)))
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
    }
    ImGui::End();

    if (mCurrentEmitterSet != mPrevEmitterSet)
    {
        mPrevEmitterSet = mCurrentEmitterSet;
        changeEftEmitterSet_();
    }
}

void Editor::drawUiEmitterEdit_()
{
    if (ImGui::Begin("EmitterSet Edit"))
    {
        const nw::eft::Resource* resource = g_EftSystem->GetResource(0);
        u32 emitter_num = resource->GetNumEmitter(mCurrentEmitterSet);

        ImGui::Text("EmitterSet: %s", resource->GetEmitterSetName(mCurrentEmitterSet));
        for (u32 i = 0; i < emitter_num; i++)
        {
            const nw::eft::CommonEmitterData* emitter = resource->GetEmitterData(mCurrentEmitterSet, i);

            // TODO: Rest of CommonEmitterData info
            ImGui::Text("Flg: %u", emitter->flg);
            ImGui::Text("RandomSeed: %u", emitter->randomSeed);
            ImGui::Text("UserData1: %u", emitter->userData);
            ImGui::Text("UserData2: %u", emitter->userData2);

            for (u32 i = 0; i < nw::eft::EFT_USER_DATA_PARAM_MAX; i++)
            {
                ImGui::Text(("UserDataF" + std::to_string(i) + ": %f").c_str(), i);
            }

            ImGui::Text("UserCallbackID: %d", emitter->userCallbackID);
            ImGui::Text("NamePos: %d", emitter->namePos);
            ImGui::Text("Name: %s", emitter->name);

            for (u32 i = 0; i < nw::eft::EFT_TEXTURE_SLOT_BIN_MAX; i++)
            {
            }

            switch (emitter->type)
            {
                case nw::eft::EFT_EMITTER_TYPE_SIMPLE:
                {
                    const nw::eft::SimpleEmitterData* simple_emitter = static_cast<const nw::eft::SimpleEmitterData*>(emitter);

                    ImGui::Text("isPolygon: %1u", simple_emitter->isPolygon);
                    ImGui::Text("isFollowAll: %1u", simple_emitter->isFollowAll);
                    ImGui::Text("isEmitterBillboardMtx: %1u", simple_emitter->isEmitterBillboardMtx);
                    ImGui::Text("isWorldGravity: %1u", simple_emitter->isWorldGravity);
                    ImGui::Text("isDirectional: %1u", simple_emitter->isDirectional);
                    ImGui::Text("isStopEmitInFade: %1u", simple_emitter->isStopEmitInFade);
                    ImGui::Text("volumeTblIndex: %u", simple_emitter->volumeTblIndex);
                    ImGui::Text("volumeSweepStartRandom: %u", simple_emitter->volumeSweepStartRandom);
                    ImGui::Text("isDisplayParent: %1u", simple_emitter->isDisplayParent);
                    ImGui::Text("emitDistEnabled: %1u", simple_emitter->emitDistEnabled);
                    ImGui::Text("isVolumeLatitudeEnabled: %1u", simple_emitter->isVolumeLatitudeEnabled);
                    ImGui::Text("ptclRotType: %u", simple_emitter->ptclRotType);
                    ImGui::Text("ptclFollowType: %u", simple_emitter->ptclFollowType);
                    ImGui::Text("colorCombinerType: %u", simple_emitter->colorCombinerType);
                    ImGui::Text("alphaCombinerType: %u", simple_emitter->alphaCombinerType);
                    ImGui::Text("drawPath: %d", simple_emitter->drawPath);
                    ImGui::Text("displaySide: %u", simple_emitter->displaySide);
                    ImGui::Text("dynamicsRandom: %f", simple_emitter->dynamicsRandom);
                    ImGui::Text("transformSRT: %f, %f, %f", simple_emitter->transformSRT.m[0][0], simple_emitter->transformSRT.m[0][1], simple_emitter->transformSRT.m[0][2]);
                    ImGui::Text("transformSRT: %f, %f, %f", simple_emitter->transformSRT.m[1][0], simple_emitter->transformSRT.m[1][1], simple_emitter->transformSRT.m[1][2]);
                    ImGui::Text("transformSRT: %f, %f, %f", simple_emitter->transformSRT.m[2][0], simple_emitter->transformSRT.m[2][1], simple_emitter->transformSRT.m[2][2]);
                    ImGui::Text("transformSRT: %f, %f, %f", simple_emitter->transformSRT.m[3][0], simple_emitter->transformSRT.m[3][1], simple_emitter->transformSRT.m[3][2]);
                    ImGui::Text("transformRT: %f, %f, %f", simple_emitter->transformRT.m[0][0], simple_emitter->transformRT.m[0][1], simple_emitter->transformRT.m[0][2]);
                    ImGui::Text("transformRT: %f, %f, %f", simple_emitter->transformRT.m[1][0], simple_emitter->transformRT.m[1][1], simple_emitter->transformRT.m[1][2]);
                    ImGui::Text("transformRT: %f, %f, %f", simple_emitter->transformRT.m[2][0], simple_emitter->transformRT.m[2][1], simple_emitter->transformRT.m[2][2]);
                    ImGui::Text("transformRT: %f, %f, %f", simple_emitter->transformRT.m[3][0], simple_emitter->transformRT.m[3][1], simple_emitter->transformRT.m[3][2]);
                    ImGui::Text("scale: %f, %f, %f", simple_emitter->scale.x, simple_emitter->scale.y, simple_emitter->scale.z);
                    ImGui::Text("rot: %f, %f, %f", simple_emitter->rot.x, simple_emitter->rot.y, simple_emitter->rot.z);
                    ImGui::Text("trans: %f, %f, %f", simple_emitter->trans.x, simple_emitter->trans.y, simple_emitter->trans.z);
                    ImGui::Text("rotRnd: %f, %f, %f", simple_emitter->rotRnd.x, simple_emitter->rotRnd.y, simple_emitter->rotRnd.z);
                    ImGui::Text("transRnd: %f, %f, %f", simple_emitter->transRnd.x, simple_emitter->transRnd.y, simple_emitter->transRnd.z);
                    ImGui::Text("blendType: %u", simple_emitter->blendType);
                    ImGui::Text("zBufATestType: %u", simple_emitter->zBufATestType);
                    ImGui::Text("volumeType: %u", simple_emitter->volumeType);
                    ImGui::Text("volumeRadius: %f, %f, %f", simple_emitter->volumeRadius.x, simple_emitter->volumeRadius.y, simple_emitter->volumeRadius.z);
                    ImGui::Text("volumeSweepStart: %d", simple_emitter->volumeSweepStart);
                    ImGui::Text("volumeSweepParam: %u", simple_emitter->volumeSweepParam);
                    ImGui::Text("volumeCaliber: %f", simple_emitter->volumeCaliber);
                    ImGui::Text("volumeLatitude: %f", simple_emitter->volumeLatitude);
                    ImGui::Text("volumeLatitudeDir: %f, %f, %f", simple_emitter->volumeLatitudeDir.x, simple_emitter->volumeLatitudeDir.y, simple_emitter->volumeLatitudeDir.z);
                    ImGui::Text("lineCenter: %f", simple_emitter->lineCenter);
                    ImGui::Text("formScale: %f, %f, %f", simple_emitter->formScale.x, simple_emitter->formScale.y, simple_emitter->formScale.z);
                    ImGui::Text("color0: %f, %f, %f, %f", simple_emitter->color0.r, simple_emitter->color0.g, simple_emitter->color0.b, simple_emitter->color0.a);
                    ImGui::Text("color1: %f, %f, %f, %f", simple_emitter->color1.r, simple_emitter->color1.g, simple_emitter->color1.b, simple_emitter->color1.a);
                    ImGui::Text("alpha: %f", simple_emitter->alpha);
                    ImGui::Text("emitDistUnit: %f", simple_emitter->emitDistUnit);
                    ImGui::Text("emitDistMax: %f", simple_emitter->emitDistMax);
                    ImGui::Text("emitDistMin: %f", simple_emitter->emitDistMin);
                    ImGui::Text("emitDistMargin: %f", simple_emitter->emitDistMargin);
                    ImGui::Text("emitRate: %f", simple_emitter->emitRate);
                    ImGui::Text("startFrame: %d", simple_emitter->startFrame);
                    ImGui::Text("endFrame: %d", simple_emitter->endFrame);
                    ImGui::Text("lifeStep: %d", simple_emitter->lifeStep);
                    ImGui::Text("lifeStepRnd: %d", simple_emitter->lifeStepRnd);
                    ImGui::Text("figureVel: %f", simple_emitter->figureVel);
                    ImGui::Text("emitterVel: %f", simple_emitter->emitterVel);
                    ImGui::Text("initVelRnd: %f", simple_emitter->initVelRnd);
                    ImGui::Text("emitterVelDir: %f, %f, %f", simple_emitter->emitterVelDir.x, simple_emitter->emitterVelDir.y, simple_emitter->emitterVelDir.z);
                    ImGui::Text("emitterVelDirAngle: %f", simple_emitter->emitterVelDirAngle);
                    ImGui::Text("spreadVec: %f, %f, %f", simple_emitter->spreadVec.x, simple_emitter->spreadVec.y, simple_emitter->spreadVec.z);
                    ImGui::Text("airRegist: %f", simple_emitter->airRegist);
                    ImGui::Text("gravity: %f, %f, %f", simple_emitter->gravity.x, simple_emitter->gravity.y, simple_emitter->gravity.z);
                    ImGui::Text("xzDiffusionVel: %f", simple_emitter->xzDiffusionVel);
                    ImGui::Text("initPosRand: %f", simple_emitter->initPosRand);
                    ImGui::Text("ptclLife: %d", simple_emitter->ptclLife);
                    ImGui::Text("ptclLifeRnd: %d", simple_emitter->ptclLifeRnd);
                    ImGui::Text("meshType: %u", simple_emitter->meshType);
                    ImGui::Text("billboardType: %u", simple_emitter->billboardType);
                    ImGui::Text("rotBasis: %f, %f", simple_emitter->rotBasis.x, simple_emitter->rotBasis.y);
                    ImGui::Text("toCameraOffset: %f", simple_emitter->toCameraOffset);
                    for (u32 i = 0; i < nw::eft::EFT_TEXTURE_SLOT_BIN_MAX; i++)
                    {
                        const nw::eft::TextureEmitterData& texture_data = simple_emitter->textureData[i];

                        ImGui::Text("isTexPatAnim: %1u", texture_data.isTexPatAnim);
                        ImGui::Text("isTexPatAnimRand: %1u", texture_data.isTexPatAnimRand);
                        ImGui::Text("isTexPatAnimClump: %1u", texture_data.isTexPatAnimClump);
                        ImGui::Text("numTexDivX: %u", texture_data.numTexDivX);
                        ImGui::Text("numTexDivY: %u", texture_data.numTexDivY);
                        ImGui::Text("numTexPat: %u", texture_data.numTexPat);
                        ImGui::Text("texPatFreq: %d", texture_data.texPatFreq);
                        ImGui::Text("texPatTblUse: %d", texture_data.texPatTblUse);
                        for (u32 i = 0; i < nw::eft::EFT_TEXTURE_PATTERN_NUM; i++)
                        {
                            ImGui::Text("texPatTbl[%u]: %u", i, texture_data.texPatTbl[i]);
                        }
                        ImGui::Text("texAddressingMode: %u", texture_data.texAddressingMode);
                        ImGui::Text("texUScale: %f", texture_data.texUScale);
                        ImGui::Text("texVScale: %f", texture_data.texVScale);
                        ImGui::Text("uvShiftAnimMode: %u", texture_data.uvShiftAnimMode);
                        ImGui::Text("uvScroll: %f, %f", texture_data.uvScroll.x, texture_data.uvScroll.y);
                        ImGui::Text("uvScrollInit: %f, %f", texture_data.uvScrollInit.x, texture_data.uvScrollInit.y);
                        ImGui::Text("uvScrollInitRand: %f, %f", texture_data.uvScrollInitRand.x, texture_data.uvScrollInitRand.y);
                        ImGui::Text("uvScale: %f, %f", texture_data.uvScale.x, texture_data.uvScale.y);
                        ImGui::Text("uvScaleInit: %f, %f", texture_data.uvScaleInit.x, texture_data.uvScaleInit.y);
                        ImGui::Text("uvScaleInitRand: %f, %f", texture_data.uvScaleInitRand.x, texture_data.uvScaleInitRand.y);
                        ImGui::Text("uvRot: %f", texture_data.uvRot);
                        ImGui::Text("uvRotInit: %f", texture_data.uvRotInit);
                        ImGui::Text("uvRotInitRand: %f", texture_data.uvRotInitRand);
                    }
                    for (u32 i = 0; i < nw::eft::EFT_COLOR_KIND_MAX; i++)
                    {
                        ImGui::Text("colorCalcType[%u]: %u", i, simple_emitter->colorCalcType[i]);
                        ImGui::Text("color[%u][0]: %f, %f, %f, %f", i, simple_emitter->color[i][0].r, simple_emitter->color[i][0].g, simple_emitter->color[i][0].b, simple_emitter->color[i][0].a);
                        ImGui::Text("color[%u][1]: %f, %f, %f, %f", i, simple_emitter->color[i][1].r, simple_emitter->color[i][1].g, simple_emitter->color[i][1].b, simple_emitter->color[i][1].a);
                        ImGui::Text("color[%u][2]: %f, %f, %f, %f", i, simple_emitter->color[i][2].r, simple_emitter->color[i][2].g, simple_emitter->color[i][2].b, simple_emitter->color[i][2].a);
                        ImGui::Text("colorSection1[%u]: %d", i, simple_emitter->colorSection1[i]);
                        ImGui::Text("colorSection2[%u]: %d", i, simple_emitter->colorSection2[i]);
                        ImGui::Text("colorSection3[%u]: %d", i, simple_emitter->colorSection3[i]);
                        ImGui::Text("colorNumRepeat[%u]: %d", i, simple_emitter->colorNumRepeat[i]);
                        ImGui::Text("colorRepeatStartRand[%u]: %d", i, simple_emitter->colorRepeatStartRand[i]);
                    }
                    ImGui::Text("colorScale: %f", simple_emitter->colorScale);
                    ImGui::Text("initAlpha: %f", simple_emitter->initAlpha);
                    ImGui::Text("diffAlpha21: %f", simple_emitter->diffAlpha21);
                    ImGui::Text("diffAlpha32: %f", simple_emitter->diffAlpha32);
                    ImGui::Text("alphaSection1: %d", simple_emitter->alphaSection1);
                    ImGui::Text("alphaSection2: %d", simple_emitter->alphaSection2);
                    ImGui::Text("texture1ColorBlend: %u", simple_emitter->texture1ColorBlend);
                    ImGui::Text("primitiveColorBlend: %u", simple_emitter->primitiveColorBlend);
                    ImGui::Text("texture1AlphaBlend: %u", simple_emitter->texture1AlphaBlend);
                    ImGui::Text("primitiveAlphaBlend: %u", simple_emitter->primitiveAlphaBlend);
                    ImGui::Text("scaleSection1: %d", simple_emitter->scaleSection1);
                    ImGui::Text("scaleSection2: %d", simple_emitter->scaleSection2);
                    ImGui::Text("scaleRand: %f", simple_emitter->scaleRand);
                    ImGui::Text("baseScale: %f, %f", simple_emitter->baseScale.x, simple_emitter->baseScale.y);
                    ImGui::Text("initScale: %f, %f", simple_emitter->initScale.x, simple_emitter->initScale.y);
                    ImGui::Text("diffScale21: %f, %f", simple_emitter->diffScale21.x, simple_emitter->diffScale21.y);
                    ImGui::Text("diffScale32: %f, %f", simple_emitter->diffScale32.x, simple_emitter->diffScale32.y);
                    ImGui::Text("initRot: %f, %f, %f", simple_emitter->initRot.x, simple_emitter->initRot.y, simple_emitter->initRot.z);
                    ImGui::Text("initRotRand: %f, %f, %f", simple_emitter->initRotRand.x, simple_emitter->initRotRand.y, simple_emitter->initRotRand.z);
                    ImGui::Text("rotVel: %f, %f, %f", simple_emitter->rotVel.x, simple_emitter->rotVel.y, simple_emitter->rotVel.z);
                    ImGui::Text("rotVelRand: %f, %f, %f", simple_emitter->rotVelRand.x, simple_emitter->rotVelRand.y, simple_emitter->rotVelRand.z);
                    ImGui::Text("rotRegist: %f", simple_emitter->rotRegist);
                    ImGui::Text("alphaAddInFade: %f", simple_emitter->alphaAddInFade);
                    ImGui::Text("shaderType: %1u", simple_emitter->shaderType);
                    ImGui::Text("userShaderSetting: %1u", simple_emitter->userShaderSetting);
                    ImGui::Text("shaderUseSoftEdge: %1u", simple_emitter->shaderUseSoftEdge);
                    ImGui::Text("shaderApplyAlphaToRefract: %1u", simple_emitter->shaderApplyAlphaToRefract);
                    ImGui::Text("shaderParam0: %f", simple_emitter->shaderParam0);
                    ImGui::Text("shaderParam1: %f", simple_emitter->shaderParam1);
                    ImGui::Text("softFadeDistance: %f", simple_emitter->softFadeDistance);
                    ImGui::Text("softVolumeParam: %f", simple_emitter->softVolumeParam);
                    for (u32 i = 0; i < 16; i++)
                    {
                        ImGui::Text("userShaderDefine1[%u]: %1u", i, simple_emitter->userShaderDefine1[i]);
                    }
                    for (u32 i = 0; i < 16; i++)
                    {
                        ImGui::Text("userShaderDefine2[%u]: %1u", i, simple_emitter->userShaderDefine2[i]);
                    }
                    ImGui::Text("userShaderFlag: %u", simple_emitter->userShaderFlag);
                    ImGui::Text("userShaderSwitchFlag: %u", simple_emitter->userShaderSwitchFlag);
                    for (u32 i = 0; i < 32; i++)
                    {
                        ImGui::Text("userShaderParam[%u]: %f", i, simple_emitter->userShaderParam.param[i]);
                    }

                    break;
                }
            }

            ImGui::Separator();
        }

        ImGui::End();
    }
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
}

void Editor::calc_()
{
    ImGuiUtil::newFrame();

    calcViewUi_();
    drawUiEmitterSelection_();
    drawUiEmitterEdit_();

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
