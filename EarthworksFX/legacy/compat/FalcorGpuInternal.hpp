#pragma once

#include "Falcor.h"

#include "EngineFactory.h"
#include "GraphicsTypes.h"
#include "PipelineState.h"
#include "ShaderResourceVariable.h"
#include "MapHelper.hpp"

namespace Falcor
{
struct ComputeVarsData;
struct GraphicsVarsData;

namespace Gpu
{
class Internal
{
public:
    static GraphicsVarsData* GfxData(GraphicsVars& vars);
    static ComputeVarsData*  CmpData(ComputeVars& vars);
    static std::shared_ptr<ShaderVar::Node> CmpBlockRoot(ComputeVars& vars);
    static ShaderVar GfxRoot(GraphicsVars& vars);
    static ShaderVar CmpRoot(ComputeVars& vars);

    static void RegisterGraphicsProgram(GraphicsProgram* pProgram, const GraphicsProgram::Desc& desc,
                                        const Program::DefineList& defines);
    static void RegisterComputeProgram(ComputeProgram* pProgram, const std::filesystem::path& path,
                                       const std::string& csEntry, const Program::DefineList& defines);
};

void OnSetFalcorDevice(Diligent::IRenderDevice* pDevice,
                       Diligent::IDeviceContext* pContext,
                       Diligent::ISwapChain* pSwapChain,
                       Diligent::IEngineFactory* pFactory);

void RebuildShaderSearchPaths();

GraphicsProgram::SharedPtr CompileGraphicsProgram(const GraphicsProgram::Desc& desc,
                                                  const Program::DefineList& defines);

ComputeProgram::SharedPtr CompileComputeProgram(const std::filesystem::path& path,
                                                 const std::string& csEntry,
                                                 const Program::DefineList& defines);

void DrawInstanced(Diligent::IDeviceContext* pCtx,
                   GraphicsState* pState,
                   GraphicsVars* pVars,
                   uint32_t vertexCount,
                   uint32_t instanceCount,
                   uint32_t startVertex,
                   uint32_t startInstance);

void DrawIndexedInstanced(Diligent::IDeviceContext* pCtx,
                          GraphicsState* pState,
                          GraphicsVars* pVars,
                          uint32_t indexCount,
                          uint32_t instanceCount,
                          uint32_t startIndex,
                          int32_t baseVertex,
                          uint32_t startInstance);

void DrawIndirect(Diligent::IDeviceContext* pCtx,
                  GraphicsState* pState,
                  GraphicsVars* pVars,
                  uint32_t numArgs,
                  Buffer* pArgBuffer,
                  uint64_t argBufferOffset,
                  Buffer* pCountBuffer,
                  uint64_t countBufferOffset);

void Dispatch(Diligent::IDeviceContext* pCtx,
              ComputeState* pState,
              ComputeVars* pVars,
              const uint3& groups);

void DispatchIndirect(Diligent::IDeviceContext* pCtx,
                      ComputeState* pState,
                      ComputeVars* pVars,
                      const Buffer* pArgBuffer,
                      uint64_t argBufferOffset);

void ClearFbo(Diligent::IDeviceContext* pCtx,
              Fbo* pFbo,
              const float4& color,
              float depth,
              uint8_t stencil,
              FboAttachmentType attachments);

void Blit(Diligent::IDeviceContext* pCtx,
          Diligent::ITextureView* pSrc,
          Diligent::ITextureView* pDst,
          const glm::vec4& srcRect,
          const glm::vec4& dstRect,
          Sampler::Filter filter,
          BlendState::SharedPtr blend);

void RegisterVao(const Vao* pVao, Vao::Topology topology, const Vao::BufferVec& vbos,
                 const Buffer::SharedPtr& ib, ResourceFormat indexFormat);

} // namespace Gpu
} // namespace Falcor
