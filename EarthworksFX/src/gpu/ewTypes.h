#pragma once

// ---------------------------------------------------------------------------
// ew:: shared math aliases, input events and small helpers.
//
// Core math is glm (right-handed, GLM_FORCE_DEPTH_ZERO_TO_ONE is a PUBLIC
// compile definition - see EarthworksFX/CMakeLists.txt); Diligent's BasicMath
// types only appear at the host-app seam (FirstPersonCamera), converted
// explicitly with toDiligent()/toGlm().
// ---------------------------------------------------------------------------

#include <cstdint>

#ifndef GLM_ENABLE_EXPERIMENTAL
#    define GLM_ENABLE_EXPERIMENTAL
#endif
#include "glm/glm.hpp"

#include "BasicMath.hpp"

namespace ew
{

using float2 = glm::vec2;
using float3 = glm::vec3;
using float4 = glm::vec4;
using uint2  = glm::uvec2;
using uint3  = glm::uvec3;
using uint4  = glm::uvec4;
using int2   = glm::ivec2;
using int4   = glm::ivec4;
using uint   = std::uint32_t;

// --- glm <-> Diligent seam conversions (host app boundary only) -------------

inline Diligent::float3 toDiligent(const glm::vec3& v) { return Diligent::float3{v.x, v.y, v.z}; }
inline glm::vec3 toGlm(const Diligent::float3& v) { return glm::vec3{v.x, v.y, v.z}; }

// --- FBO clear selection -----------------------------------------------------

enum class FboAttachmentType : uint32_t
{
    Color   = 1,
    Depth   = 2,
    Stencil = 4,
    All     = Color | Depth | Stencil,
};

inline FboAttachmentType operator|(FboAttachmentType a, FboAttachmentType b)
{
    return static_cast<FboAttachmentType>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline bool hasAttachment(FboAttachmentType value, FboAttachmentType flag)
{
    return (static_cast<uint32_t>(value) & static_cast<uint32_t>(flag)) != 0;
}

// --- input events (consumed by the renderer) ---------------------------------

namespace Input
{
enum class Modifier
{
    Ctrl  = 1u << 0,
    Shift = 1u << 1,
    Alt   = 1u << 2,
};

enum class Key
{
    A,
    B,
    C,
    D,
    F,
    G,
    H,
    J,
    K,
    M,
    N,
    O,
    Q,
    R,
    S,
    T,
    V,
    X,
    Y,
    W,
    Del,
    Escape,
    Space,
    Enter,
    Up,
    Down,
    Left,
    Right,
    Key0,
    Key1,
    Key2,
    Key3,
    Key4,
    Key5,
    Key6,
    Key7,
    LeftControl,
    LeftShift,
};

enum class MouseButton
{
    Left,
    Right,
    Middle,
};
} // namespace Input

struct KeyboardEvent
{
    enum class Type
    {
        KeyPressed,
        KeyReleased,
    };

    Type       type      = Type::KeyPressed;
    Input::Key key       = Input::Key::D;
    uint32_t   modifiers = 0;

    bool hasModifier(Input::Modifier mod) const
    {
        return (modifiers & static_cast<uint32_t>(mod)) != 0;
    }
};

struct MouseEvent
{
    enum class Type
    {
        Move,
        ButtonDown,
        ButtonUp,
        Wheel,
    };

    enum class Buttons
    {
        Left   = 1,
        Right  = 2,
        Middle = 4,
    };

    Type               type    = Type::Move;
    Buttons            buttons = Buttons::Left;
    Input::MouseButton button  = Input::MouseButton::Left;
    // Normalized [0,1] window coordinates - the host divides out the pixel
    // size before dispatching the event.
    float2 pos{};
    float2 wheelDelta{};
};

} // namespace ew
