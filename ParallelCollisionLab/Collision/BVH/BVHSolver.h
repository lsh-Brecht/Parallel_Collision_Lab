#pragma once

#include <vector>
#include <algorithm>
#include <cmath>
#include <windows.h>

#include "../ICollisionSolver.h"
#include "../CollisionTypes.h"
#include "../NarrowPhase.h"
#include "../Resolution.h"
#include "AABB.h"
#include "BVHNode.h"
#include "IBVHVisualizer.h"

//=============================================================================
// BVHSolver - Single-Threaded AABB Bounding Volume Hierarchy Collision Solver
//
// Algorithm:
// 1. Broad Phase: Top-Down Longest-Axis Object Median Split (Zero-allocation Full Rebuild)
// 2. Narrow Phase: Stack-based Iterative Query-Against-Tree (i < j unique pairs)
// 3. Resolution Phase: Standard impulse-based sphere collision resolution
//=============================================================================
class BVHSolver : public ICollisionSolver, public IBVHVisualizer
{
public:
    BVHSolver()
    {
        QueryPerformanceFrequency(&TimerFrequency);
        Manifolds.reserve(512);
        Nodes.reserve(2048);
        SphereIndices.reserve(1024);
        SphereBounds.reserve(1024);
    }

    //-------------------------------------------------------------------------
    // ICollisionSolver Interface
    //-------------------------------------------------------------------------
    void Solve(std::vector<FSphere>& Spheres) override
    {
        const int Count = static_cast<int>(Spheres.size());
        LastStats = {};
        if (Count < 2) return;

        LARGE_INTEGER TimerStart, TimerBroad, TimerNarrow, TimerResolve;

        QueryPerformanceCounter(&TimerStart);
        BuildBVH(Spheres);
        QueryPerformanceCounter(&TimerBroad);

        Manifolds.clear();
        uint64_t CandidatePairs = 0;

        int Stack[64];

        for (int i = 0; i < Count; ++i)
        {
            const FVector3 PosA    = Spheres[i].Center;
            const float    RadiusA = Spheres[i].Radius;
            const FAABB&   BoxA    = SphereBounds[i];

            int StackPtr = 0;
            Stack[StackPtr++] = 0;

            while (StackPtr > 0)
            {
                int CurrIdx = Stack[--StackPtr];
                const FBVHNode& Node = Nodes[CurrIdx];

                if (!BoxA.Intersects(Node.Bounds))
                {
                    continue;
                }

                if (Node.IsLeaf())
                {
                    int j = Node.GetSphereIndex();
                    if (i < j)
                    {
                        CandidatePairs++;
                        const FVector3 Diff      = PosA - Spheres[j].Center;
                        const float    DistSq    = Diff.LengthSq();
                        const float    RadiusSum = RadiusA + Spheres[j].Radius;

                        if (DistSq < RadiusSum * RadiusSum)
                        {
                            const float Dist = sqrtf(DistSq);
                            const FVector3 Normal = (Dist > 1e-6f) ? Diff * (1.0f / Dist) : FVector3(1.0f, 0.0f, 0.0f);
                            Manifolds.push_back({ i, j, Normal, RadiusSum - Dist });
                        }
                    }
                }
                else
                {
                    Stack[StackPtr++] = Node.RightChild;
                    Stack[StackPtr++] = Node.LeftChild;
                }
            }
        }

        LastStats.CandidatePairCount   = CandidatePairs;
        LastStats.ActualCollisionCount = static_cast<uint64_t>(Manifolds.size());
        QueryPerformanceCounter(&TimerNarrow);

        ResolveCollisions(Spheres, Manifolds);
        QueryPerformanceCounter(&TimerResolve);

        const double ToMilliseconds = 1000.0 / static_cast<double>(TimerFrequency.QuadPart);
        LastStats.BroadPhaseTimeMs  = static_cast<double>(TimerBroad.QuadPart - TimerStart.QuadPart) * ToMilliseconds;
        LastStats.NarrowPhaseTimeMs = static_cast<double>(TimerNarrow.QuadPart - TimerBroad.QuadPart) * ToMilliseconds;
        LastStats.ResolutionTimeMs  = static_cast<double>(TimerResolve.QuadPart - TimerNarrow.QuadPart) * ToMilliseconds;
        LastStats.TotalSolveTimeMs  = static_cast<double>(TimerResolve.QuadPart - TimerStart.QuadPart) * ToMilliseconds;
    }

    const wchar_t* GetName()          const override { return L"BVH (ST)"; }
    const wchar_t* GetAlgorithmName() const override { return L"BVH (Median Split)"; }
    const wchar_t* GetExecutionMode() const override { return L"Single Thread"; }
    int            GetThreadCount()   const override { return 1; }
    void           SetThreadCount(int) override {}

    const FCollisionStats& GetLastStats() const override { return LastStats; }
    const std::vector<FCollisionManifold>& GetManifolds() const { return Manifolds; }

    //-------------------------------------------------------------------------
    // IBVHVisualizer Interface
    //-------------------------------------------------------------------------
    void BuildBVH(const std::vector<FSphere>& Spheres) override
    {
        const int Count = static_cast<int>(Spheres.size());
        if (Count == 0)
        {
            Nodes.clear();
            MaxTreeDepth = 0;
            return;
        }

        SphereIndices.resize(Count);
        SphereBounds.resize(Count);
        for (int i = 0; i < Count; ++i)
        {
            SphereIndices[i] = i;
            SphereBounds[i]  = FAABB::FromSphere(Spheres[i].Center, Spheres[i].Radius);
        }

        Nodes.clear();
        Nodes.reserve(Count * 2);
        MaxTreeDepth = 0;

        BuildSubtree(Spheres, 0, Count, 0);

        if (VisualizerDepth > MaxTreeDepth)
        {
            VisualizerDepth = MaxTreeDepth;
        }
    }

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
            OutGroups[0].Color = FVector4(0.20f, 0.95f, 0.40f, 1.0f); // Vibrant Leaf Green
            CollectLeafLines(0, OutGroups[0].Lines);
        }
    }

    int  GetVisualizerDepth() const override
    {
        return (std::max)(0, (std::min)(VisualizerDepth, MaxTreeDepth));
    }
    int  GetMaxTreeDepth()    const override { return MaxTreeDepth; }
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

private:
    //-------------------------------------------------------------------------
    // Recursive Top-Down Object Median Split Builder
    //-------------------------------------------------------------------------
    int BuildSubtree(const std::vector<FSphere>& Spheres, int Start, int End, int CurrentDepth)
    {
        MaxTreeDepth = (std::max)(MaxTreeDepth, CurrentDepth);

        const int NodeIdx = static_cast<int>(Nodes.size());
        Nodes.emplace_back();

        const int Count = End - Start;
        if (Count == 1)
        {
            int sIdx = SphereIndices[Start];
            Nodes[NodeIdx].SetLeaf(sIdx, SphereBounds[sIdx]);
            return NodeIdx;
        }

        FAABB CentroidBounds;
        FAABB TotalBounds;
        for (int i = Start; i < End; ++i)
        {
            int sIdx = SphereIndices[i];
            CentroidBounds.ExpandBy(Spheres[sIdx].Center);
            TotalBounds.ExpandBy(SphereBounds[sIdx]);
        }

        const int Axis = CentroidBounds.GetLongestAxis();
        const int Mid  = Start + Count / 2;

        int* pBegin = SphereIndices.data() + Start;
        int* pMid   = SphereIndices.data() + Mid;
        int* pEnd   = SphereIndices.data() + End;
        const FSphere* pSpheres = Spheres.data();

        if (Axis == 0)
        {
            std::nth_element(pBegin, pMid, pEnd, [pSpheres](int a, int b) {
                return pSpheres[a].Center.x < pSpheres[b].Center.x;
            });
        }
        else if (Axis == 1)
        {
            std::nth_element(pBegin, pMid, pEnd, [pSpheres](int a, int b) {
                return pSpheres[a].Center.y < pSpheres[b].Center.y;
            });
        }
        else
        {
            std::nth_element(pBegin, pMid, pEnd, [pSpheres](int a, int b) {
                return pSpheres[a].Center.z < pSpheres[b].Center.z;
            });
        }

        int LeftChild  = BuildSubtree(Spheres, Start, Mid, CurrentDepth + 1);
        int RightChild = BuildSubtree(Spheres, Mid, End, CurrentDepth + 1);

        Nodes[NodeIdx].SetInternal(LeftChild, RightChild, TotalBounds);
        return NodeIdx;
    }

    //-------------------------------------------------------------------------
    // Visualizer Wireframe Collectors
    //-------------------------------------------------------------------------
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

private:
    LARGE_INTEGER                   TimerFrequency   = {};
    FCollisionStats                 LastStats       = {};
    std::vector<FCollisionManifold> Manifolds;

    std::vector<FBVHNode>           Nodes;
    std::vector<int>                SphereIndices;
    std::vector<FAABB>              SphereBounds;
    int                             MaxTreeDepth     = 0;

    int                             VisualizerDepth  = 3;
    EBVHVisualizerMode              VisualizerMode   = EBVHVisualizerMode::LOD;
};
