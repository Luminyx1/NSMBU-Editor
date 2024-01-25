#pragma once

#include <gfx/lyr/rio_Drawable.h>
#include <task/rio_Task.h>
#include <gfx/rio_Projection.h>
#include <gpu/rio_RenderBuffer.h>
#include <gpu/rio_RenderTarget.h>

#include <nw/math.h>

class Editor : public rio::ITask, public rio::lyr::IDrawable
{
public:
    Editor();

    void renderForeground(const rio::lyr::DrawInfo& drawInfo);
    void renderBackground(const rio::lyr::DrawInfo& drawInfo);

private:
    void createRenderBuffer_(s32 width, s32 height);
    void resizeView_(s32 width, s32 height);

    void initEftSystem_();
    void calcEftSystem_();
    void drawEftSystem_(const nw::math::MTX44& proj, const nw::math::MTX34& view, const nw::math::VEC3& camPos, f32 zNear, f32 zFar);
    void changeEftEmitterSet_();

    void calcViewUi_();
    void drawUiEmitterSelection_();
    void drawUiEmitterEdit_();

    void bindViewRenderBuffer_();
    void unbindViewRenderBuffer_();

#if RIO_IS_WIN
    void resize_(s32 width, s32 height);
    static void onResizeCallback_(s32 width, s32 height);
#endif // RIO_IS_WIN

    void prepare_() override;
    void exit_() override;
    void calc_() override;

    u8*                     mPtclFile;
    u32                     mPrevEmitterSet;
    u32                     mCurrentEmitterSet;
    bool                    mLoopEmitterSet;
    rio::BaseVec2f          mViewPos;
    rio::BaseVec2i          mViewSize;
    rio::OrthoProjection    mProjection;
    bool                    mViewResized;
    bool                    mViewHovered;
    bool                    mViewFocused;
    rio::Texture2D         *mpColorTexture,
                           *mpDepthTexture;
    rio::RenderTargetColor  mColorTarget;
    rio::RenderTargetDepth  mDepthTarget;
    rio::RenderBuffer       mRenderBuffer;
};
