#include "ShaderGridRenderer.hpp"

#include "MapHelper.hpp"
#include "Shader.h"
#include "Errors.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>

namespace Diligent
{

namespace
{

constexpr char kFullscreenVS[] = R"(
struct VSOut
{
    float4 Pos : SV_Position;
    float2 UV  : TEXCOORD0;
};

VSOut main(uint VertexID : SV_VertexID)
{
    VSOut Out;
    float2 uv = float2((VertexID << 1) & 2, VertexID & 2);
    Out.UV = uv;
    Out.Pos = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return Out;
}
)";

// Single pixel shader: blue background + optional grid lines (compile-time DRAW_GRID).
constexpr char kGridPS[] = R"(
#ifndef DRAW_GRID
#define DRAW_GRID 1
#endif

cbuffer cbConstants : register(b0)
{
    float4x4 g_InvViewProj;
    float4   g_CameraPos;
    float    g_GridSpacing;
    float3   g_Pad;
};

struct VSOut
{
    float4 Pos : SV_Position;
    float2 UV  : TEXCOORD0;
};

struct PSOut
{
    float4 Color : SV_Target0;
};

static const float3 kBlue = float3(0.12, 0.38, 0.92);
static const float kLineWidth = 0.04;

float GridMask(float2 worldXZ, float spacing)
{
    float2 cell = abs(frac(worldXZ / spacing) - 0.5) * spacing;
    float dist  = min(cell.x, cell.y);
    return 1.0 - smoothstep(0.0, kLineWidth, dist);
}

PSOut main(VSOut In)
{
    PSOut Out;
    Out.Color = float4(kBlue, 1.0);

#if DRAW_GRID
    float2 ndc = float2(In.UV.x * 2.0 - 1.0, 1.0 - In.UV.y * 2.0);

    float4 clipNear = float4(ndc, 0.0, 1.0);
    float4 clipFar  = float4(ndc, 1.0, 1.0);
    float4 worldNear = mul(g_InvViewProj, clipNear);
    float4 worldFar  = mul(g_InvViewProj, clipFar);
    worldNear.xyz /= worldNear.w;
    worldFar.xyz  /= worldFar.w;

    float3 rayDir = normalize(worldFar.xyz - worldNear.xyz);
    if (abs(rayDir.y) >= 1e-5)
    {
        float t = -g_CameraPos.y / rayDir.y;
        if (t > 0.0)
        {
            float2 hitXZ = (g_CameraPos.xyz + rayDir * t).xz;
            if (GridMask(hitXZ, g_GridSpacing) > 0.5)
                Out.Color = float4(0.0, 0.0, 0.0, 1.0);
        }
    }
#endif

    return Out;
}
)";

struct ShaderConstants
{
    float InvViewProj[16];
    float CameraPos[4];
    float GridSpacing;
    float Pad[3];
};

RefCntAutoPtr<IShader> CreateShader(IRenderDevice* pDevice, SHADER_TYPE type, const char* name, const char* source, ShaderMacroArray macros = {})
{
    ShaderCreateInfo ShaderCI;
    ShaderCI.SourceLanguage  = SHADER_SOURCE_LANGUAGE_HLSL;
    ShaderCI.Desc.ShaderType = type;
    ShaderCI.Desc.Name       = name;
    ShaderCI.EntryPoint      = "main";
    ShaderCI.Source          = source;
    ShaderCI.Macros          = macros;

    RefCntAutoPtr<IShader> pShader;
    pDevice->CreateShader(ShaderCI, &pShader);
    return pShader;
}

IShaderResourceVariable* FindConstantBufferVariable(IPipelineState* pPSO, IShader* pPS)
{
    if (pPSO)
    {
        if (IShaderResourceVariable* pVar = pPSO->GetStaticVariableByName(SHADER_TYPE_PIXEL, "cbConstants"))
            return pVar;
        if (IShaderResourceVariable* pVar = pPSO->GetStaticVariableByIndex(SHADER_TYPE_PIXEL, 0))
            return pVar;
    }

    if (pPS)
    {
        for (Uint32 i = 0; i < pPS->GetResourceCount(); ++i)
        {
            ShaderResourceDesc desc;
            pPS->GetResourceDesc(i, desc);
            if (desc.Type == SHADER_RESOURCE_TYPE_CONSTANT_BUFFER && desc.Name)
            {
                if (IShaderResourceVariable* pVar = pPSO->GetStaticVariableByName(SHADER_TYPE_PIXEL, desc.Name))
                    return pVar;
            }
        }
    }

    return nullptr;
}

} // namespace

struct ShaderGridRenderer::CameraState
{
    glm::vec3 Pos{0.f, 3.f, 8.f};
    float     Yaw   = glm::pi<float>();
    float     Pitch = -0.2f;
    float     LastMouseX = 0.f;
    float     LastMouseY = 0.f;
    bool      HasLastMouse = false;

    void Update(InputController& Input, double ElapsedSeconds, float Aspect)
    {
        (void)Aspect;

        const float dt        = static_cast<float>(ElapsedSeconds);
        const float moveSpeed = 12.f;

        const glm::vec3 forward{std::cos(Pitch) * std::sin(Yaw), std::sin(Pitch), std::cos(Pitch) * std::cos(Yaw)};
        const glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3{0.f, 1.f, 0.f}));
        const glm::vec3 up    = glm::vec3{0.f, 1.f, 0.f};

        glm::vec3 move{};
        if (Input.IsKeyDown(InputKeys::MoveForward))
            move += forward;
        if (Input.IsKeyDown(InputKeys::MoveBackward))
            move -= forward;
        if (Input.IsKeyDown(InputKeys::MoveLeft))
            move -= right;
        if (Input.IsKeyDown(InputKeys::MoveRight))
            move += right;
        if (Input.IsKeyDown(InputKeys::MoveUp))
            move += up;
        if (Input.IsKeyDown(InputKeys::MoveDown))
            move -= up;

        if (glm::length(move) > 0.f)
            Pos += glm::normalize(move) * moveSpeed * dt;

        const MouseState& mouse = Input.GetMouseState();
        if (!HasLastMouse)
        {
            LastMouseX   = mouse.PosX;
            LastMouseY   = mouse.PosY;
            HasLastMouse = true;
        }

        if (mouse.ButtonFlags & MouseState::BUTTON_FLAG_RIGHT)
        {
            const float dx = mouse.PosX - LastMouseX;
            const float dy = mouse.PosY - LastMouseY;
            constexpr float kLookSpeed = 0.003f;
            Yaw += dx * kLookSpeed;
            Pitch = std::clamp(Pitch - dy * kLookSpeed, -1.45f, 1.45f);
        }

        LastMouseX = mouse.PosX;
        LastMouseY = mouse.PosY;
    }

    ShaderConstants BuildConstants(float Aspect) const
    {
        const glm::vec3 forward{std::cos(Pitch) * std::sin(Yaw), std::sin(Pitch), std::cos(Pitch) * std::cos(Yaw)};
        const glm::mat4 view        = glm::lookAt(Pos, Pos + forward, glm::vec3{0.f, 1.f, 0.f});
        const glm::mat4 proj        = glm::perspective(glm::radians(60.f), Aspect, 0.1f, 10000.f);
        const glm::mat4 invViewProj = glm::inverse(proj * view);

        ShaderConstants constants{};
        std::memcpy(constants.InvViewProj, glm::value_ptr(invViewProj), sizeof(constants.InvViewProj));
        constants.CameraPos[0] = Pos.x;
        constants.CameraPos[1] = Pos.y;
        constants.CameraPos[2] = Pos.z;
        constants.GridSpacing  = 1.f;
        return constants;
    }
};

ShaderGridRenderer::ShaderGridRenderer()
    : m_pCamera{std::make_unique<CameraState>()}
{
}

ShaderGridRenderer::~ShaderGridRenderer() = default;

void ShaderGridRenderer::Initialize(IRenderDevice* pDevice, TEXTURE_FORMAT RTVFormat, TEXTURE_FORMAT DSVFormat)
{
    BufferDesc CBDesc;
    CBDesc.Name           = "ShaderGrid constants";
    CBDesc.Size           = sizeof(ShaderConstants);
    CBDesc.Usage          = USAGE_DYNAMIC;
    CBDesc.BindFlags      = BIND_UNIFORM_BUFFER;
    CBDesc.CPUAccessFlags = CPU_ACCESS_WRITE;
    pDevice->CreateBuffer(CBDesc, nullptr, &m_pConstantsCB);
    VERIFY_EXPR(m_pConstantsCB);

    ShaderMacro macros[] = {{"DRAW_GRID", "1"}, {nullptr, nullptr}};
    ShaderMacroArray macroArray{macros, _countof(macros) - 1};

    auto pVS = CreateShader(pDevice, SHADER_TYPE_VERTEX, "ShaderGrid VS", kFullscreenVS);
    auto pPS = CreateShader(pDevice, SHADER_TYPE_PIXEL, "ShaderGrid PS", kGridPS, macroArray);
    VERIFY_EXPR(pVS && pPS);

    GraphicsPipelineStateCreateInfo PSOCreateInfo;
    PSOCreateInfo.PSODesc.Name                                 = "ShaderGrid PSO";
    PSOCreateInfo.PSODesc.PipelineType                         = PIPELINE_TYPE_GRAPHICS;
    PSOCreateInfo.PSODesc.ResourceLayout.DefaultVariableType   = SHADER_RESOURCE_VARIABLE_TYPE_STATIC;

    ShaderResourceVariableDesc vars[] = {
        {SHADER_TYPE_PIXEL, "cbConstants", SHADER_RESOURCE_VARIABLE_TYPE_STATIC},
    };
    PSOCreateInfo.PSODesc.ResourceLayout.Variables    = vars;
    PSOCreateInfo.PSODesc.ResourceLayout.NumVariables   = _countof(vars);

    auto& GP = PSOCreateInfo.GraphicsPipeline;
    GP.NumRenderTargets             = 1;
    GP.RTVFormats[0]                = RTVFormat;
    GP.DSVFormat                    = DSVFormat;
    GP.PrimitiveTopology            = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    GP.RasterizerDesc.CullMode      = CULL_MODE_NONE;
    GP.DepthStencilDesc.DepthEnable = False;

    PSOCreateInfo.pVS = pVS;
    PSOCreateInfo.pPS = pPS;
    pDevice->CreateGraphicsPipelineState(PSOCreateInfo, &m_pPSO);
    VERIFY_EXPR(m_pPSO);

    IShaderResourceVariable* pVar = FindConstantBufferVariable(m_pPSO, pPS);
    if (!pVar)
        LOG_ERROR_AND_THROW("ShaderGridRenderer: cbConstants binding not found");

    pVar->Set(m_pConstantsCB);

    m_pPSO->CreateShaderResourceBinding(&m_pSRB, true);
    VERIFY_EXPR(m_pSRB);
}

void ShaderGridRenderer::UpdateCamera(InputController& Input, double ElapsedSeconds, Uint32 Width, Uint32 Height)
{
    const float aspect = Height > 0 ? static_cast<float>(Width) / static_cast<float>(Height) : 1.f;
    m_pCamera->Update(Input, ElapsedSeconds, aspect);
}

void ShaderGridRenderer::Render(IDeviceContext* pCtx, ITextureView* pRTV, ITextureView* pDSV, Uint32 Width, Uint32 Height)
{
    if (!pCtx || !pRTV || !m_pCamera || !m_pPSO || !m_pSRB)
        return;

    const float aspect = Height > 0 ? static_cast<float>(Width) / static_cast<float>(Height) : 1.f;
    const ShaderConstants constants = m_pCamera->BuildConstants(aspect);

    {
        MapHelper<ShaderConstants> CBData(pCtx, m_pConstantsCB, MAP_WRITE, MAP_FLAG_DISCARD);
        *CBData = constants;
    }

    const float clearColor[] = {0.12f, 0.38f, 0.92f, 1.0f};
    pCtx->ClearRenderTarget(pRTV, clearColor, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    if (pDSV)
        pCtx->ClearDepthStencil(pDSV, CLEAR_DEPTH_FLAG, 1.f, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    pCtx->SetRenderTargets(1, &pRTV, pDSV, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    pCtx->SetPipelineState(m_pPSO);
    pCtx->CommitShaderResources(m_pSRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    DrawAttribs drawAttrs;
    drawAttrs.NumVertices = 3;
    pCtx->Draw(drawAttrs);
}

} // namespace Diligent
