#pragma once

#include <vector>
#include "../../Core/Common.h"
#include "../../Core/Sphere.h"
#include "AABB.h"

//=============================================================================
// EBVHVisualizerMode - Display modes for hierarchical bounding boxes
//=============================================================================
enum class EBVHVisualizerMode
{
    LOD,         // Cumulative boxes from depth 0 up to selected depth
    SingleLevel, // Only boxes at the selected depth
    LeafOnly     // Only leaf bounding boxes (individual spheres)
};

//=============================================================================
// FBVHLineGroup - Batch of wireframe line vertices sharing a depth color
//=============================================================================
struct FBVHLineGroup
{
    FVector4                   Color;
    std::vector<FVertexSimple> Lines;
};

//=============================================================================
// IBVHVisualizer - Interface for hierarchical 3D BVH wireframe inspection
//=============================================================================
class IBVHVisualizer
{
public:
    virtual ~IBVHVisualizer() = default;

    virtual void BuildBVH(const std::vector<FSphere>& spheres) = 0;

    virtual void GenerateVisualizerLineGroups(std::vector<FBVHLineGroup>& outGroups) const = 0;

    virtual int  GetVisualizerDepth() const = 0;
    virtual int  GetMaxTreeDepth()    const = 0;
    virtual void SetVisualizerDepth(int depth) = 0;
    virtual void IncrementVisualizerDepth() = 0;
    virtual void DecrementVisualizerDepth() = 0;

    virtual EBVHVisualizerMode GetVisualizerMode() const = 0;
    virtual void               CycleVisualizerMode() = 0;
    virtual const wchar_t*     GetVisualizerModeName() const = 0;

    static void AppendAABBWireframe(std::vector<FVertexSimple>& lines, const FAABB& aabb)
    {
        const float x0 = aabb.Min.x, y0 = aabb.Min.y, z0 = aabb.Min.z;
        const float x1 = aabb.Max.x, y1 = aabb.Max.y, z1 = aabb.Max.z;

        FVertexSimple v0 = { x0, y0, z0 };
        FVertexSimple v1 = { x1, y0, z0 };
        FVertexSimple v2 = { x1, y1, z0 };
        FVertexSimple v3 = { x0, y1, z0 };
        FVertexSimple v4 = { x0, y0, z1 };
        FVertexSimple v5 = { x1, y0, z1 };
        FVertexSimple v6 = { x1, y1, z1 };
        FVertexSimple v7 = { x0, y1, z1 };

        lines.push_back(v0); lines.push_back(v1);
        lines.push_back(v1); lines.push_back(v2);
        lines.push_back(v2); lines.push_back(v3);
        lines.push_back(v3); lines.push_back(v0);

        lines.push_back(v4); lines.push_back(v5);
        lines.push_back(v5); lines.push_back(v6);
        lines.push_back(v6); lines.push_back(v7);
        lines.push_back(v7); lines.push_back(v4);

        lines.push_back(v0); lines.push_back(v4);
        lines.push_back(v1); lines.push_back(v5);
        lines.push_back(v2); lines.push_back(v6);
        lines.push_back(v3); lines.push_back(v7);
    }

    static FVector4 GetDepthColor(int depth)
    {
        static const FVector4 PALETTE[] = {
            FVector4(1.00f, 0.25f, 0.25f, 1.0f),
            FVector4(1.00f, 0.55f, 0.05f, 1.0f),
            FVector4(1.00f, 0.88f, 0.10f, 1.0f),
            FVector4(0.65f, 0.95f, 0.15f, 1.0f),
            FVector4(0.15f, 0.95f, 0.35f, 1.0f),
            FVector4(0.10f, 0.88f, 0.95f, 1.0f),
            FVector4(0.25f, 0.60f, 1.00f, 1.0f),
            FVector4(0.40f, 0.35f, 1.00f, 1.0f),
            FVector4(0.75f, 0.25f, 0.95f, 1.0f),
            FVector4(0.95f, 0.20f, 0.65f, 1.0f)
        };
        const int count = sizeof(PALETTE) / sizeof(PALETTE[0]);
        return PALETTE[depth % count];
    }
};
