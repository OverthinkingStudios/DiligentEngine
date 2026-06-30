/***************************************************************************
 # Copyright (c) 2015-22, NVIDIA CORPORATION. All rights reserved.
 #
 # Redistribution and use in source and binary forms, with or without
 # modification, are permitted provided that the following conditions
 # are met:
 #  * Redistributions of source code must retain the above copyright
 #    notice, this list of conditions and the following disclaimer.
 #  * Redistributions in binary form must reproduce the above copyright
 #    notice, this list of conditions and the following disclaimer in the
 #    documentation and/or other materials provided with the distribution.
 #  * Neither the name of NVIDIA CORPORATION nor the names of its
 #    contributors may be used to endorse or promote products derived
 #    from this software without specific prior written permission.
 #
 # THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS "AS IS" AND ANY
 # EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 # IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 # PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 # CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 # EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 # PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 # PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 # OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 # (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 # OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 **************************************************************************/
#pragma once
#include "Falcor.h"
#include "earthworksScene.h"
//#include "RenderGraph/RenderGraph.h"
//#include "Utils/Video/VideoEncoderUI.h"



using namespace Falcor;

inline float3 global_sun_direction = float3(0.96592582628906f, -0.2588190451f, 0.f);



class Earthworks_4 : public IRenderer
{
public:
    void onLoad(RenderContext* _renderContext) override;
    void onFrameUpdate(RenderContext* _renderContext);
    void onFrameRender(RenderContext* _renderContext, const Fbo::SharedPtr& pTargetFbo) override;
    void onRenderOverlay(RenderContext* _renderContext, const Fbo::SharedPtr& pTargetFbo);
    void onShutdown() override;
    void onResizeSwapChain(uint32_t _width, uint32_t _height) override;
    bool onKeyEvent(const KeyboardEvent& keyEvent) override;
    bool onMouseEvent(const MouseEvent& mouseEvent) override;
    void initGui(Gui* _gui);
    void onGuiRender(Gui* _gui) override;
    void onGuiMenubar(Gui* _gui);

    void loadColorCube(std::string name);

    // --- DiligentEngine bring-up: let the host app drive the debug grid and
    // read the camera without owning an ImGui window inside the core renderer.
    bool&                    debugGridEnabled() { return showDebugGrid; }
    const Camera::SharedPtr& getCamera() const { return camera; }

private:
    void guiStyle();

    // --- debug orientation / movement grid (DiligentEngine bring-up aid) ---
    void                        renderDebugGrid(RenderContext* _renderContext, const Fbo::SharedPtr& pTargetFbo);
    bool                        showDebugGrid = true;
    GraphicsProgram::SharedPtr  debugGridProgram;
    GraphicsState::SharedPtr    debugGridState;
    GraphicsVars::SharedPtr     debugGridVars;
    Vao::SharedPtr              debugGridVao;

    GraphicsState::Viewport     viewport3d;
    float2                      screenSize;
    float2                      screenMouseScale;
    float2                      screenMouseOffset;
    Fbo::SharedPtr		        hdrFbo;
    Fbo::SharedPtr		        tonemappedFbo;
    Texture::SharedPtr	        hdrPreviousFrame;
    

    GraphicsState::SharedPtr    graphicsState;
    Camera::SharedPtr	        camera;
    
   terrainManager              terrain;
    atmosphereAndFog            atmosphere;     // make directly available in terrain include there

    bool showAbout = false;
    Texture::SharedPtr aboutTex;
    bool showGui = true;
    bool firstGUI = true;
    BlendState::SharedPtr overlayBlendstate;

    earthworksScene scene;

    struct
    {
        pixelShader         tonemapper;
        Texture::SharedPtr	colorCube;
    } postProcess;

    /*
    struct
    {
        BarrierThrd glider_barrier; // create barrier for threads
    } glider; */

    struct {
        bool    vsync = true;
        bool    minimal = true;
    } refresh;

    struct {
        int left_width = 300;
        int right_width = 300;
        int bottom_width = 500; // damn should have been called height
        int header_height = 10;
    } layout;

    template<class Archive>
    void serialize(Archive& _archive, std::uint32_t const _version)
    {
        _archive(CEREAL_NVP(refresh.vsync));
        _archive(CEREAL_NVP(refresh.minimal));

        if (_version >= 101)
        {
            _archive(CEREAL_NVP(layout.left_width));
            _archive(CEREAL_NVP(layout.right_width));
            _archive(CEREAL_NVP(layout.bottom_width));
        }
    }
};
CEREAL_CLASS_VERSION(Earthworks_4, 101);

