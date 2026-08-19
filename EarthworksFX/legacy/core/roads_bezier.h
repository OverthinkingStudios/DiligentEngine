#pragma once

#include "Falcor.h"
#include "terrafector.h"
using namespace Falcor;

#include "cereal/cereal.hpp"
#include "cereal/types/map.hpp"
#include "cereal/types/vector.hpp"
#include "cereal/types/list.hpp"
#include "cereal/types/array.hpp"
#include "cereal/types/string.hpp"







struct splinePoint;
enum bezierSide { left, middle, right };
enum nameNames { innerShoulder, innerTurn, lane1, lane2, lane3, outerTurn, outerShoulder, side, clearing };
enum e_constraint { none, camber, fixed };
struct cubicDouble
{
    cubicDouble() { ; }
    cubicDouble(splinePoint a, splinePoint b);
    cubicDouble(splinePoint a, splinePoint b, bool bRight);
    cubicDouble(splinePoint a, splinePoint b, bool bRight, float w0, float w1);
    cubicDouble(splinePoint a, splinePoint b, bool bRight, float2 A, float2 D);
    float4 data[2][4];	//[inner, outer][p0, p1, p2, p3]


    template<class Archive>
    void serialize(Archive& archive, std::uint32_t const version)
    {
        if (version == 100) {
            for (int j = 0; j < 2; j++) {
                for (int i = 0; i < 4; i++) {
                    archive_float4(data[j][i]);
                }
            }
        }
    }
};
CEREAL_CLASS_VERSION(cubicDouble, 100);



struct bezierPoint
{
    glm::vec3 forward() { return pos + tangentForward * dst_Forward; }
    glm::vec3 backward() { return pos - tangentBack * dst_Back; }

    glm::vec4 forward_uv() { return glm::vec4(pos + tangentForward * dst_Forward, u_forward); }
    glm::vec4 pos_uv() { return glm::vec4(pos, u); }
    glm::vec4 backward_uv() { return glm::vec4(pos - tangentBack * dst_Back, u_back); }

    glm::vec4 forward_uv(float dst) { return glm::vec4(pos + tangentForward * dst, u_forward); }
    glm::vec4 backward_uv(float dst) { return glm::vec4(pos - tangentBack * dst, u_back); }

    glm::vec3 pos = float3(0, 0, 0);
    glm::vec3 tangentForward = float3(1, 0, 0);
    glm::vec3 tangentBack = float3(1, 0, 0);
    float dst_Forward = 5.0f;
    float dst_Back = 5.0f;
    float u = 0.0f;				// as in UV
    float u_back;
    float u_forward;

    // temporaty variables
    float length = 0.0f;
    float totalLength = 0.0f;
    float3 pullBack = float3(0, 0, 0);
    float3 pullForward = float3(0, 0, 0);

    template<class Archive>
    void serialize(Archive& archive, std::uint32_t const version)
    {
        if (version >= 100) {
            archive_float3(pos);
            archive_float3(tangentForward);
            archive(CEREAL_NVP(dst_Forward), CEREAL_NVP(dst_Back));
            archive(CEREAL_NVP(u_back), CEREAL_NVP(u), CEREAL_NVP(u_forward));
            archive_float3(tangentBack);
        }
    }
};
CEREAL_CLASS_VERSION(bezierPoint, 100);



struct laneDescription
{
    float laneWidth = 0.0f;
    int type = 0;				// this is really a material rework when we get there
    float2 percentage;			// % across the width for their points (inner, outer)

    template<class Archive>
    void serialize(Archive& archive, std::uint32_t const version)
    {
        if (version >= 100) {
            archive(CEREAL_NVP(laneWidth));
            archive(CEREAL_NVP(type));
            archive_float2(percentage);
        }
    }
};
CEREAL_CLASS_VERSION(laneDescription, 100);




struct splinePoint
{
    splinePoint();

    void addHeight(float h);
    void addTangent(float _t);
    void solveMiddlePos();
    void setAnchor(glm::vec3 _P, glm::vec3 _N, uint _lod);
    void forceAnchor(glm::vec3 _P, glm::vec3 _N, uint _lod);

    // anchor from mouse edit ----------------------------------------------
    bool selected = true;
    bool bAutoGenerate = true;
    glm::vec3 anchor;
    glm::vec3 anchorNormal;					// but just controls banking through perpendicular, pitch is ignored
    float height_AboveAnchor = 0.0f;
    float tangent_Offset = 0.0f;
    float perpendicular_Offset = 0.0f;

    uint constraint = e_constraint::none;
    float camber = 0.0f;
    glm::vec3 perpendicular;
    uint lidarLod;

    float widthLeft, widthRight;

    std::array<bezierPoint, 3> bezier;

    // Now for lane data,       innerShoulder, innerTurn, lane, lane, lane, outerTurn, outerShoulder, side, clearing
    std::array<laneDescription, 9> lanesLeft;
    std::array<laneDescription, 9> lanesRight;

    enum matName { verge, sidewalk, gutter, tarmac, shoulder, outerTurn, lane2, lane1, innerTurn, innerShoulder, rubberOuter, rubberLane3, rubberLane2, rubberLane1, rubberInner };
    std::array<int, 15>	matLeft = { -1, -1, -1, -1 , -1, -1 , -1, -1 , -1, -1 , -1, -1 , -1, -1 , -1 };
    std::array<int, 2>	matCenter = { -1, -1 };
    std::array<int, 15>	matRight{ -1, -1, -1, -1 , -1, -1 , -1, -1 , -1, -1 , -1, -1 , -1, -1 , -1 };
    void setMaterialIndex(uint _side, uint _slot, uint _index);
    int getMaterialIndex(uint _side, uint _slot);


    bool isBridge = false;
    std::string bridgeName;

    bool isAIonly = false;		// Norschleife

    float T = 1.0f;	// tension
    float C = 1.0f;	// continuity
    float B = 0.5f; // bias

    int centerlineType = 1;						// FIXME enum - the type of line in the middle

    float curvature = 0.05f;		// 5 cm default

    // Temporary for realistic rendering ----------------- save indicis
    std::array<uint, 9> left_Lane_Idx;
    std::array<uint, 9> right_Lane_Idx;
    std::array<uint, 2> left_Geom_Idx;
    std::array<uint, 2> right_Geom_Idx;


    template<class Archive>
    void serialize(Archive& archive, std::uint32_t const version)
    {
        archive(CEREAL_NVP(bAutoGenerate));
        archive_float3(anchor);
        archive_float3(anchorNormal);
        archive(CEREAL_NVP(height_AboveAnchor));
        archive(CEREAL_NVP(widthLeft), CEREAL_NVP(widthRight));	// unnesesary
        archive(CEREAL_NVP(left), CEREAL_NVP(middle), CEREAL_NVP(right));
        archive(CEREAL_NVP(bezier));
        archive(CEREAL_NVP(lanesLeft), CEREAL_NVP(lanesRight));
        archive(CEREAL_NVP(centerlineType));

        archive(CEREAL_NVP(constraint));
        archive(CEREAL_NVP(camber));
        archive_float3(perpendicular);
        archive(CEREAL_NVP(lidarLod));

        archive(CEREAL_NVP(isBridge));
        archive(CEREAL_NVP(bridgeName));
        archive(CEREAL_NVP(tangent_Offset));
        archive(CEREAL_NVP(perpendicular_Offset));
        archive(CEREAL_NVP(T));
        archive(CEREAL_NVP(C));
        archive(CEREAL_NVP(B));
        archive(CEREAL_NVP(isAIonly));

        if (version >= 101) {
            archive(CEREAL_NVP(matLeft));
            archive(CEREAL_NVP(matCenter));
            archive(CEREAL_NVP(matRight));
        }

        if (version >= 102) {
            archive(CEREAL_NVP(curvature));
        }
    }

    // this is for load and ssave of road types - expand with materials
    template<class Archive>
    void serializeType(Archive& archive)
    {
        archive(CEREAL_NVP(lanesLeft), CEREAL_NVP(lanesRight));
        archive(CEREAL_NVP(centerlineType));
        archive(CEREAL_NVP(perpendicular_Offset));	// since this allows one way roads
    }
};
CEREAL_CLASS_VERSION(splinePoint, 102);



enum bezier_edge { center, outside };
struct bezierLayer
{
    bezierLayer() { ; }
    bezierLayer(bezier_edge inner, bezier_edge outer, uint _material, uint bezierIndex, bool _left, float w0, float w1, bool startSeg = false, bool endSeg = false);
    uint A;			// flags, material, index [2][14][16]			combine with rootindex in constant buffer
    uint B;			// w0, w1 [4][14][14]
};



