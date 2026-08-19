#pragma once

// ---------------------------------------------------------------------------
// ew::Camera - the Earthworks scene camera, glm-native.
//
// Conventions, all LOAD-BEARING:
//   * RIGHT-handed: getViewMatrix() == glm::lookAt(pos, target, +Y).
//   * Depth range [0,1]: perspectiveRH_ZO explicitly; the matching
//     GLM_FORCE_DEPTH_ZERO_TO_ONE PUBLIC compile definition keeps
//     glm::unProject / ortho helpers consistent.
//   * Film-back FOV: fovY = 2*atan(0.5*frameHeight/focalLength) with
//     frameHeight = 24 mm, focal 15 mm -> ~77.3 deg. frameHeight is NOT a
//     pixel height. The FOV feeds the terrain lod_Pix split heuristic -
//     changing it skews LOD everywhere.
//   * Matrices reach shaders as glm::transpose(proj*view) so that HLSL
//     mul(float4(pos,1), M) with default column-major cbuffer packing yields
//     clip = P*V*pos.
//
// CameraData is NOT binary-compatible with the legacy camera.bin, which stored
// Diligent float4x4 members. Camera state is not persisted here; version the
// file if persistence returns.
// ---------------------------------------------------------------------------

#include <memory>

#include "ewTypes.h"

namespace ew
{

struct CameraData
{
    float3 position{0.f, 0.f, 0.f};
    float3 target{0.f, 0.f, 100.f};
    float  focalLength = 15.f;  // mm
    float  frameHeight = 24.f;  // mm film back - NOT pixels
    float  nearPlane   = 0.1f;
    float  farPlane    = 40000.f;
    float  aspectRatio = 16.f / 9.f;
};

class Camera
{
public:
    using SharedPtr = std::shared_ptr<Camera>;

    static SharedPtr create();

    void setDepthRange(float zNear, float zFar);
    void setNearPlane(float zNear) { m_Data.nearPlane = zNear; }
    void setFarPlane(float zFar) { m_Data.farPlane = zFar; }
    void setAspectRatio(float aspect) { m_Data.aspectRatio = aspect; }
    void setFocalLength(float focalMm) { m_Data.focalLength = focalMm; }
    void setPosition(const float3& pos) { m_Data.position = pos; }
    void setTarget(const float3& target) { m_Data.target = target; }

    /// glm convention (column-vector math). Upload to HLSL as
    /// glm::transpose(getViewProjMatrix()) - see header comment.
    glm::mat4 getViewMatrix() const;
    glm::mat4 getProjMatrix() const;
    glm::mat4 getViewProjMatrix() const; // proj * view

    float getFovY() const; // radians, film-back convention

    const float3& getPosition() const { return m_Data.position; }
    const float3& getTarget() const { return m_Data.target; }

    float getFocalLength() const { return m_Data.focalLength; }
    float getFrameHeight() const { return m_Data.frameHeight; }
    float getAspectRatio() const { return m_Data.aspectRatio; }
    float getNearPlane() const { return m_Data.nearPlane; }
    float getFarPlane() const { return m_Data.farPlane; }

    CameraData&       getData() { return m_Data; }
    const CameraData& getData() const { return m_Data; }

private:
    CameraData m_Data{};
};

} // namespace ew
