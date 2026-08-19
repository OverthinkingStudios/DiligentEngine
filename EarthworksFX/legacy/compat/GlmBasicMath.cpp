#include "GlmBasicMath.h"

Diligent::float3 operator+(const Diligent::float3& a, const glm::vec3& b)
{
    return a + Diligent::float3(b);
}

glm::vec3 operator+(const glm::vec3& a, const Diligent::float3& b)
{
    return a + glm::vec3(b);
}

Diligent::float3 operator-(const Diligent::float3& a, const glm::vec3& b)
{
    return a - Diligent::float3(b);
}

glm::vec3 operator-(const glm::vec3& a, const Diligent::float3& b)
{
    return a - glm::vec3(b);
}

Diligent::float3 operator*(const Diligent::float3& a, const glm::vec3& b)
{
    return a * Diligent::float3(b);
}

glm::vec3 operator*(const glm::vec3& a, const Diligent::float3& b)
{
    return a * glm::vec3(b);
}

Diligent::float3 operator/(const Diligent::float3& a, const glm::vec3& b)
{
    return a / Diligent::float3(b);
}

glm::vec3 operator/(const glm::vec3& a, const Diligent::float3& b)
{
    return a / glm::vec3(b);
}

Diligent::float3& operator+=(Diligent::float3& a, const glm::vec3& b)
{
    a.x += b.x;
    a.y += b.y;
    a.z += b.z;
    return a;
}

Diligent::float3& operator-=(Diligent::float3& a, const glm::vec3& b)
{
    a.x -= b.x;
    a.y -= b.y;
    a.z -= b.z;
    return a;
}

Diligent::float3& operator*=(Diligent::float3& a, const glm::vec3& b)
{
    a.x *= b.x;
    a.y *= b.y;
    a.z *= b.z;
    return a;
}

Diligent::float3& operator/=(Diligent::float3& a, const glm::vec3& b)
{
    a.x /= b.x;
    a.y /= b.y;
    a.z /= b.z;
    return a;
}

glm::vec3& operator+=(glm::vec3& a, const Diligent::float3& b)
{
    a.x += b.x;
    a.y += b.y;
    a.z += b.z;
    return a;
}

glm::vec3& operator-=(glm::vec3& a, const Diligent::float3& b)
{
    a.x -= b.x;
    a.y -= b.y;
    a.z -= b.z;
    return a;
}

glm::vec3& operator*=(glm::vec3& a, const Diligent::float3& b)
{
    a.x *= b.x;
    a.y *= b.y;
    a.z *= b.z;
    return a;
}

glm::vec3& operator/=(glm::vec3& a, const Diligent::float3& b)
{
    a.x /= b.x;
    a.y /= b.y;
    a.z /= b.z;
    return a;
}

bool operator==(const Diligent::float3& a, const glm::vec3& b)
{
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

bool operator==(const glm::vec3& a, const Diligent::float3& b)
{
    return b == a;
}

bool operator!=(const Diligent::float3& a, const glm::vec3& b)
{
    return !(a == b);
}

bool operator!=(const glm::vec3& a, const Diligent::float3& b)
{
    return !(a == b);
}
