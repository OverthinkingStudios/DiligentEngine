/*
 *  Copyright 2019-2022 Diligent Graphics LLC
 *  Copyright 2015-2019 Egor Yusov
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#pragma once

#include <algorithm>

#include "BasicMath.hpp"
#include "InputController.hpp"
#include "GraphicsTypes.h"

namespace Diligent
{

class FirstPersonCamera
{
public:
    void Update(InputController& Controller, float ElapsedTime);
    void SetRotation(float Yaw, float Pitch);
    void SetLookAt(const float3& LookAt);
    void SetMoveSpeed(float MoveSpeed) { m_fMoveSpeed = MoveSpeed; }
    void SetRotationSpeed(float RotationSpeed) { m_fRotationSpeed = RotationSpeed; }
    void SetPos(const float3& Pos) { m_Pos = Pos; }

    /// AspectRatio = width / height accounting for surface pretransform
    /// (i.e. logical width / logical height)
    void SetProjAttribs(Float32           NearClipPlane,
                        Float32           FarClipPlane,
                        Float32           AspectRatio,
                        Float32           FOV,
                        SURFACE_TRANSFORM SrfPreTransform,
                        bool              IsGL);
    void SetSpeedUpScales(Float32 SpeedUpScale, Float32 SuperSpeedUpScale);

    void SetWheelMoveSpeedFactor(float Factor) { m_fWheelMoveSpeedFactor = Factor; }
    void SetMoveSpeedLimits(float MinSpeed, float MaxSpeed)
    {
        m_fMinMoveSpeed = MinSpeed;
        m_fMaxMoveSpeed = MaxSpeed;
        m_fMoveSpeed    = std::min(std::max(m_fMoveSpeed, MinSpeed), MaxSpeed);
    }

    // clang-format off
    const float4x4& GetViewMatrix()  const { return m_ViewMatrix;  }
    const float4x4& GetWorldMatrix() const { return m_WorldMatrix; }
    const float4x4& GetProjMatrix()  const { return m_ProjMatrix;  }

    float3 GetWorldRight() const { return float3(m_ViewMatrix._11, m_ViewMatrix._21, m_ViewMatrix._31); }
    float3 GetWorldUp()    const { return float3(m_ViewMatrix._12, m_ViewMatrix._22, m_ViewMatrix._32); }
    float3 GetWorldAhead() const { return float3(m_ViewMatrix._13, m_ViewMatrix._23, m_ViewMatrix._33); }
    // clang-format on

    float3 GetPos() const { return m_Pos; }
    float  GetMoveSpeed() const { return m_fMoveSpeed; }
    float  GetCurrentSpeed() const { return m_fCurrentSpeed; }

    struct ProjectionAttribs
    {
        Float32           NearClipPlane = 1.f;
        Float32           FarClipPlane  = 1000.f;
        Float32           AspectRatio   = 1.f;
        Float32           FOV           = PI_F / 4.f;
        SURFACE_TRANSFORM PreTransform  = SURFACE_TRANSFORM_IDENTITY;
        bool              IsGL          = false;
    };
    const ProjectionAttribs& GetProjAttribs() { return m_ProjAttribs; }

    void SetReferenceAxes(const float3& ReferenceRightAxis, const float3& ReferenceUpAxis, bool IsRightHanded = false);

    void SetHandness(bool IsRightHanded)
    {
        m_fHandness = IsRightHanded ? +1.f : -1.f;
    }

protected:
    float4x4 GetReferenceRotiation() const;

    ProjectionAttribs m_ProjAttribs;

    MouseState m_LastMouseState;

    float3 m_ReferenceRightAxis = float3{1, 0, 0};
    float3 m_ReferenceUpAxis    = float3{0, 1, 0};
    float3 m_ReferenceAheadAxis = float3{0, 0, 1};

    float3 m_Pos;

    float4x4 m_ViewMatrix;
    float4x4 m_WorldMatrix;
    float4x4 m_ProjMatrix;
    float    m_fRotationSpeed         = 0.01f;
    float    m_fMoveSpeed             = 10.f;
    float    m_fMinMoveSpeed          = 0.1f;
    float    m_fMaxMoveSpeed          = 10000.f;
    float    m_fWheelMoveSpeedFactor  = 1.1f;
    float    m_fCurrentSpeed          = 0.f;

    float m_fYawAngle          = 0;
    float m_fPitchAngle        = 0;
    float m_fSpeedUpScale      = 4.f;
    float m_fSuperSpeedUpScale = 10.f;
    float m_fHandness          = 1.f;
};

} // namespace Diligent
