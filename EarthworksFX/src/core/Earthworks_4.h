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
#include "FalcorCompat.hpp"
#include "terrain.h"
#include "atmosphere.h"





using namespace Falcor;

extern float3 global_sun_direction;



class Earthworks_4
{
public:
    void onLoad(RenderContext* _renderContext);
    void onFrameUpdate(RenderContext* _renderContext);
    void onFrameRender(RenderContext* _renderContext, const Fbo::SharedPtr& pTargetFbo);
    void onRenderOverlay(RenderContext* _renderContext, const Fbo::SharedPtr& pTargetFbo);
    void onShutdown();
    void onResizeSwapChain(uint32_t _width, uint32_t _height);
    bool onKeyEvent(const KeyboardEvent& keyEvent);
    bool onMouseEvent(const MouseEvent& mouseEvent);
    void onGuiRender(Gui* _gui);
    void onGuiMenubar(Gui* _gui);

    void loadColorCube(std::string name);
    FILE* logFile;

private:
    void guiStyle();

    GraphicsState::Viewport     viewport3d;
    float2                      screenSize;
    float2                      screenMouseScale;
    float2                      screenMouseOffset;
    Fbo::SharedPtr		        hdrFbo;
    Fbo::SharedPtr		        tonemappedFbo;
    Texture::SharedPtr	        hdrHalfCopy;
    Texture::SharedPtr	        colorCube;

    GraphicsState::SharedPtr    graphicsState;
    Camera::SharedPtr	        camera;
    pixelShader		            tonemapper;
    terrainManager              terrain;
    BarrierThrd glider_barrier; // create barrier for threads
    atmosphereAndFog            atmosphere; // make directly available in terrain include there
    bool showAbout = false;
    Texture::SharedPtr aboutTex;
    int slowTimer = 0;      // this is used to alow donw framerate
    bool showEditGui = true;
    bool hideGui = false;
    BlendState::SharedPtr overlayBlendstate;
        

    struct {
        bool    vsync = true;
        bool    minimal = true;
    } refresh;


    template<class Archive>
    void serialize(Archive& _archive, std::uint32_t const _version)
    {
        _archive(CEREAL_NVP(refresh.vsync));
        _archive(CEREAL_NVP(refresh.minimal));
    }
};
CEREAL_CLASS_VERSION(Earthworks_4, 100);

