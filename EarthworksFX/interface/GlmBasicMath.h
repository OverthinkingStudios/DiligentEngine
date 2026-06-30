#pragma once

#include "BasicMath.hpp"

#ifndef GLM_ENABLE_EXPERIMENTAL
#    define GLM_ENABLE_EXPERIMENTAL
#endif
#include "glm/glm.hpp"

Diligent::float3 operator+(const Diligent::float3& a, const glm::vec3& b);
glm::vec3 operator+(const glm::vec3& a, const Diligent::float3& b);

Diligent::float3 operator-(const Diligent::float3& a, const glm::vec3& b);
glm::vec3 operator-(const glm::vec3& a, const Diligent::float3& b);

Diligent::float3 operator*(const Diligent::float3& a, const glm::vec3& b);
glm::vec3 operator*(const glm::vec3& a, const Diligent::float3& b);

Diligent::float3 operator/(const Diligent::float3& a, const glm::vec3& b);
glm::vec3 operator/(const glm::vec3& a, const Diligent::float3& b);

Diligent::float3& operator+=(Diligent::float3& a, const glm::vec3& b);
Diligent::float3& operator-=(Diligent::float3& a, const glm::vec3& b);
Diligent::float3& operator*=(Diligent::float3& a, const glm::vec3& b);
Diligent::float3& operator/=(Diligent::float3& a, const glm::vec3& b);

glm::vec3& operator+=(glm::vec3& a, const Diligent::float3& b);
glm::vec3& operator-=(glm::vec3& a, const Diligent::float3& b);
glm::vec3& operator*=(glm::vec3& a, const Diligent::float3& b);
glm::vec3& operator/=(glm::vec3& a, const Diligent::float3& b);

bool operator==(const Diligent::float3& a, const glm::vec3& b);
bool operator==(const glm::vec3& a, const Diligent::float3& b);

bool operator!=(const Diligent::float3& a, const glm::vec3& b);
bool operator!=(const glm::vec3& a, const Diligent::float3& b);
