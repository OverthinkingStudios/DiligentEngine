#include "ewCamera.h"

#include <cmath>

#include "glm/gtc/matrix_transform.hpp"

namespace ew
{

Camera::SharedPtr Camera::create()
{
    return std::make_shared<Camera>();
}

void Camera::setDepthRange(float zNear, float zFar)
{
    m_Data.nearPlane = zNear;
    m_Data.farPlane  = zFar;
}

float Camera::getFovY() const
{
    // Film-back convention: the FOV follows from focal length over film
    // height in mm, never from a pixel height.
    return 2.f * std::atan(0.5f * m_Data.frameHeight / m_Data.focalLength);
}

glm::mat4 Camera::getViewMatrix() const
{
    // Right-handed lookAt, up = +Y. All CPU code that decomposes the view
    // matrix (atmosphere basis, unProject picking) assumes exactly
    // glm::lookAt semantics.
    return glm::lookAt(m_Data.position, m_Data.target, float3{0.f, 1.f, 0.f});
}

glm::mat4 Camera::getProjMatrix() const
{
    // Explicit RH + [0,1] depth regardless of glm's configured default
    // (GLM_FORCE_DEPTH_ZERO_TO_ONE is also defined project-wide).
    return glm::perspectiveRH_ZO(getFovY(), m_Data.aspectRatio, m_Data.nearPlane, m_Data.farPlane);
}

glm::mat4 Camera::getViewProjMatrix() const
{
    return getProjMatrix() * getViewMatrix();
}

} // namespace ew
