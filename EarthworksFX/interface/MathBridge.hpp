#pragma once
// glm <-> Diligent math conversions. Per the guardrails, glm is only allowed to
// touch the Diligent side through this header.

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "BasicMath.hpp"

namespace earthworksfx
{

// glm is column-major; Diligent::float4x4 is row-major. Transpose on convert.
inline Diligent::float4x4 ToDiligent(const glm::mat4& m)
{
    Diligent::float4x4 r;
    for (int row = 0; row < 4; ++row)
        for (int col = 0; col < 4; ++col)
            r.m[row][col] = m[col][row];
    return r;
}

inline glm::mat4 ToGlm(const Diligent::float4x4& m)
{
    glm::mat4 r;
    for (int row = 0; row < 4; ++row)
        for (int col = 0; col < 4; ++col)
            r[col][row] = m.m[row][col];
    return r;
}

inline Diligent::float3 ToDiligent(const glm::vec3& v)
{
    return Diligent::float3{v.x, v.y, v.z};
}

inline glm::vec3 ToGlm(const Diligent::float3& v)
{
    return glm::vec3{v.x, v.y, v.z};
}

} // namespace earthworksfx
