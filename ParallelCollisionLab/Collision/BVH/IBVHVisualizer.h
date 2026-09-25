#pragma once

#include <vector>
#include <algorithm>
#include "../../Core/Common.h"
#include "../../Core/Sphere.h"
#include "AABB.h"
#include "BVHNode.h"

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

    virtual void BuildBVH(const std::vector<FSphere>& Spheres) = 0;

    virtual void GenerateVisualizerLineGroups(std::vector<FBVHLineGroup>& OutGroups) const = 0;

    virtual int  GetVisualizerDepth() const = 0;
    virtual int  GetMaxTreeDepth()    const = 0;
    virtual void SetVisualizerDepth(int Depth) = 0;
    virtual void IncrementVisualizerDepth() = 0;
    virtual void DecrementVisualizerDepth() = 0;

    virtual EBVHVisualizerMode GetVisualizerMode() const = 0;
    virtual void               CycleVisualizerMode() = 0;
    virtual const wchar_t*     GetVisualizerModeName() const = 0;

    static void AppendAABBWireframe(std::vector<FVertexSimple>& Lines, const FAABB& Box)
    {
        const float x0 = Box.Min.x, y0 = Box.Min.y, z0 = Box.Min.z;
        const float x1 = Box.Max.x, y1 = Box.Max.y, z1 = Box.Max.z;

        FVertexSimple v0 = { x0, y0, z0 };
        FVertexSimple v1 = { x1, y0, z0 };
        FVertexSimple v2 = { x1, y1, z0 };
        FVertexSimple v3 = { x0, y1, z0 };
        FVertexSimple v4 = { x0, y0, z1 };
        FVertexSimple v5 = { x1, y0, z1 };
        FVertexSimple v6 = { x1, y1, z1 };
        FVertexSimple v7 = { x0, y1, z1 };

        Lines.push_back(v0); Lines.push_back(v1);
        Lines.push_back(v1); Lines.push_back(v2);
        Lines.push_back(v2); Lines.push_back(v3);
        Lines.push_back(v3); Lines.push_back(v0);

        Lines.push_back(v4); Lines.push_back(v5);
        Lines.push_back(v5); Lines.push_back(v6);
        Lines.push_back(v6); Lines.push_back(v7);
        Lines.push_back(v7); Lines.push_back(v4);

        Lines.push_back(v0); Lines.push_back(v4);
        Lines.push_back(v1); Lines.push_back(v5);
        Lines.push_back(v2); Lines.push_back(v6);
        Lines.push_back(v3); Lines.push_back(v7);
    }

    static FVector4 GetDepthColor(int Depth)
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
        return PALETTE[Depth % count];
    }
};

//=============================================================================
// FBVHVisualizerBase - Common visualizer state & tree traversal implementation
//=============================================================================
class FBVHVisualizerBase : public IBVHVisualizer
{
public:
    virtual ~FBVHVisualizerBase() = default;

    void GenerateVisualizerLineGroups(std::vector<FBVHLineGroup>& OutGroups) const override
    {
        OutGroups.clear();
        if (Nodes.empty()) return;

        const int TargetDepth = (std::max)(0, (std::min)(VisualizerDepth, MaxTreeDepth));

        if (VisualizerMode == EBVHVisualizerMode::SingleLevel)
        {
            OutGroups.resize(1);
            OutGroups[0].Color = IBVHVisualizer::GetDepthColor(TargetDepth);
            CollectSingleLevelLines(0, 0, TargetDepth, OutGroups[0].Lines);
        }
        else if (VisualizerMode == EBVHVisualizerMode::LOD)
        {
            OutGroups.resize(TargetDepth + 1);
            for (int d = 0; d <= TargetDepth; ++d)
            {
                OutGroups[d].Color = IBVHVisualizer::GetDepthColor(d);
            }
            CollectLODLines(0, 0, TargetDepth, OutGroups);
        }
        else // LeafOnly
        {
            OutGroups.resize(1);
            OutGroups[0].Color = FVector4(0.20f, 0.95f, 0.40f, 1.0f);
            CollectLeafLines(0, OutGroups[0].Lines);
        }
    }

    int  GetVisualizerDepth() const override
    {
        return (std::max)(0, (std::min)(VisualizerDepth, MaxTreeDepth));
    }
    int  GetMaxTreeDepth() const override { return MaxTreeDepth; }
    void SetVisualizerDepth(int Depth) override
    {
        VisualizerDepth = (std::max)(0, (std::min)(Depth, MaxTreeDepth));
    }
    void IncrementVisualizerDepth() override
    {
        if (VisualizerDepth > MaxTreeDepth)
        {
            VisualizerDepth = MaxTreeDepth;
        }
        else if (VisualizerDepth < MaxTreeDepth)
        {
            VisualizerDepth++;
        }
    }
    void DecrementVisualizerDepth() override
    {
        if (VisualizerDepth > MaxTreeDepth)
        {
            VisualizerDepth = MaxTreeDepth;
        }
        if (VisualizerDepth > 0)
        {
            VisualizerDepth--;
        }
    }

    EBVHVisualizerMode GetVisualizerMode() const override { return VisualizerMode; }
    void CycleVisualizerMode() override
    {
        if (VisualizerMode == EBVHVisualizerMode::LOD)
            VisualizerMode = EBVHVisualizerMode::SingleLevel;
        else if (VisualizerMode == EBVHVisualizerMode::SingleLevel)
            VisualizerMode = EBVHVisualizerMode::LeafOnly;
        else
            VisualizerMode = EBVHVisualizerMode::LOD;
    }
    const wchar_t* GetVisualizerModeName() const override
    {
        switch (VisualizerMode)
        {
        case EBVHVisualizerMode::LOD:         return L"LOD (0~Depth)";
        case EBVHVisualizerMode::SingleLevel: return L"Single Level";
        case EBVHVisualizerMode::LeafOnly:    return L"Leaves Only";
        default:                              return L"Unknown";
        }
    }

protected:
    void CollectSingleLevelLines(int NodeIdx, int CurrentDepth, int TargetDepth, std::vector<FVertexSimple>& Lines) const
    {
        if (CurrentDepth == TargetDepth)
        {
            IBVHVisualizer::AppendAABBWireframe(Lines, Nodes[NodeIdx].Bounds);
            return;
        }

        if (!Nodes[NodeIdx].IsLeaf())
        {
            CollectSingleLevelLines(Nodes[NodeIdx].LeftChild,  CurrentDepth + 1, TargetDepth, Lines);
            CollectSingleLevelLines(Nodes[NodeIdx].RightChild, CurrentDepth + 1, TargetDepth, Lines);
        }
    }

    void CollectLODLines(int NodeIdx, int CurrentDepth, int MaxDepth, std::vector<FBVHLineGroup>& OutGroups) const
    {
        if (CurrentDepth <= MaxDepth)
        {
            IBVHVisualizer::AppendAABBWireframe(OutGroups[CurrentDepth].Lines, Nodes[NodeIdx].Bounds);
        }

        if (CurrentDepth < MaxDepth && !Nodes[NodeIdx].IsLeaf())
        {
            CollectLODLines(Nodes[NodeIdx].LeftChild,  CurrentDepth + 1, MaxDepth, OutGroups);
            CollectLODLines(Nodes[NodeIdx].RightChild, CurrentDepth + 1, MaxDepth, OutGroups);
        }
    }

    void CollectLeafLines(int NodeIdx, std::vector<FVertexSimple>& Lines) const
    {
        if (Nodes[NodeIdx].IsLeaf())
        {
            IBVHVisualizer::AppendAABBWireframe(Lines, Nodes[NodeIdx].Bounds);
        }
        else
        {
            CollectLeafLines(Nodes[NodeIdx].LeftChild,  Lines);
            CollectLeafLines(Nodes[NodeIdx].RightChild, Lines);
        }
    }

protected:
    std::vector<FBVHNode> Nodes;
    int                   MaxTreeDepth    = 0;
    int                   VisualizerDepth = 3;
    EBVHVisualizerMode    VisualizerMode  = EBVHVisualizerMode::LOD;
};
