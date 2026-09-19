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
        QueryPerformanceFrequency(&m_TimerFreq);
        m_Manifolds.reserve(512);
        m_Nodes.reserve(2048);
        m_SphereIndices.reserve(1024);
        m_SphereBounds.reserve(1024);
    }

    //-------------------------------------------------------------------------
    // ICollisionSolver Interface
    //-------------------------------------------------------------------------
    void Solve(std::vector<FSphere>& spheres) override
    {
        const int count = static_cast<int>(spheres.size());
        m_Stats = {};
        if (count < 2) return;

        LARGE_INTEGER t0, t1, t2, t3;

        // 1. Broad Phase: Build BVH tree
        QueryPerformanceCounter(&t0);
        BuildBVH(spheres);
        QueryPerformanceCounter(&t1);

        // 2. Narrow Phase: Stack-based query-against-tree
        m_Manifolds.clear();
        uint64_t candidatePairs = 0;

        // Local static stack for tree traversal (max depth <= 16, stack of 64 is plenty)
        int stack[64];

        for (int i = 0; i < count; ++i)
        {
            const FVector3 posA = spheres[i].Center;
            const float    radA = spheres[i].Radius;
            const FAABB&   boxA = m_SphereBounds[i];

            int stackPtr = 0;
            stack[stackPtr++] = 0; // Root node

            while (stackPtr > 0)
            {
                int currIdx = stack[--stackPtr];
                const FBVHNode& node = m_Nodes[currIdx];

                if (!boxA.Intersects(node.Bounds))
                {
                    continue; // Prune non-overlapping subtree
                }

                if (node.IsLeaf())
                {
                    int j = node.GetSphereIndex();
                    // Guarantee strictly unique pairs and avoid self-collision
                    if (i < j)
                    {
                        candidatePairs++;
                        const FVector3 diff   = posA - spheres[j].Center;
                        const float    distSq = diff.LengthSq();
                        const float    radSum = radA + spheres[j].Radius;

                        if (distSq < radSum * radSum)
                        {
                            const float dist = sqrtf(distSq);
                            const FVector3 normal = (dist > 1e-6f) ? diff * (1.0f / dist) : FVector3(1.0f, 0.0f, 0.0f);
                            m_Manifolds.push_back({ i, j, normal, radSum - dist });
                        }
                    }
                }
                else
                {
                    // Push children onto traversal stack
                    stack[stackPtr++] = node.RightChild;
                    stack[stackPtr++] = node.LeftChild;
                }
            }
        }

        m_Stats.CandidatePairCount   = candidatePairs;
        m_Stats.ActualCollisionCount = static_cast<uint64_t>(m_Manifolds.size());
        QueryPerformanceCounter(&t2);

        // 3. Resolution Phase
        ResolveCollisions(spheres, m_Manifolds);
        QueryPerformanceCounter(&t3);

        const double toMs = 1000.0 / static_cast<double>(m_TimerFreq.QuadPart);
        m_Stats.BroadPhaseTimeMs  = static_cast<double>(t1.QuadPart - t0.QuadPart) * toMs;
        m_Stats.NarrowPhaseTimeMs = static_cast<double>(t2.QuadPart - t1.QuadPart) * toMs;
        m_Stats.ResolutionTimeMs  = static_cast<double>(t3.QuadPart - t2.QuadPart) * toMs;
        m_Stats.TotalSolveTimeMs  = static_cast<double>(t3.QuadPart - t0.QuadPart) * toMs;
    }

    const wchar_t* GetName()          const override { return L"BVH (ST)"; }
    const wchar_t* GetAlgorithmName() const override { return L"BVH (Median Split)"; }
    const wchar_t* GetExecutionMode() const override { return L"Single Thread"; }
    int            GetThreadCount()   const override { return 1; }
    void           SetThreadCount(int) override {}

    const FCollisionStats& GetLastStats() const override { return m_Stats; }
    const std::vector<FCollisionManifold>& GetManifolds() const { return m_Manifolds; }

    //-------------------------------------------------------------------------
    // IBVHVisualizer Interface
    //-------------------------------------------------------------------------
    void BuildBVH(const std::vector<FSphere>& spheres) override
    {
        const int count = static_cast<int>(spheres.size());
        if (count == 0)
        {
            m_Nodes.clear();
            m_MaxDepth = 0;
            return;
        }

        // 1. Prepare indices and sphere bounding boxes
        m_SphereIndices.resize(count);
        m_SphereBounds.resize(count);
        for (int i = 0; i < count; ++i)
        {
            m_SphereIndices[i] = i;
            m_SphereBounds[i]  = FAABB::FromSphere(spheres[i].Center, spheres[i].Radius);
        }

        // 2. Pre-allocate contiguous flat node vector (2N - 1 max nodes)
        m_Nodes.clear();
        m_Nodes.reserve(count * 2);
        m_MaxDepth = 0;

        // 3. Recursive top-down median split
        BuildSubtree(spheres, 0, count, 0);
    }

    void GenerateVisualizerLineGroups(std::vector<FBVHLineGroup>& outGroups) const override
    {
        outGroups.clear();
        if (m_Nodes.empty()) return;

        const int targetDepth = (std::max)(0, (std::min)(m_VisualizerDepth, m_MaxDepth));

        if (m_VisualizerMode == EBVHVisualizerMode::SingleLevel)
        {
            outGroups.resize(1);
            outGroups[0].Color = IBVHVisualizer::GetDepthColor(targetDepth);
            CollectSingleLevelLines(0, 0, targetDepth, outGroups[0].Lines);
        }
        else if (m_VisualizerMode == EBVHVisualizerMode::LOD)
        {
            outGroups.resize(targetDepth + 1);
            for (int d = 0; d <= targetDepth; ++d)
            {
                outGroups[d].Color = IBVHVisualizer::GetDepthColor(d);
            }
            CollectLODLines(0, 0, targetDepth, outGroups);
        }
        else // LeafOnly
        {
            outGroups.resize(1);
            outGroups[0].Color = FVector4(0.20f, 0.95f, 0.40f, 1.0f); // Vibrant Leaf Green
            CollectLeafLines(0, outGroups[0].Lines);
        }
    }

    int  GetVisualizerDepth() const override { return m_VisualizerDepth; }
    int  GetMaxTreeDepth()    const override { return m_MaxDepth; }
    void SetVisualizerDepth(int depth) override
    {
        m_VisualizerDepth = (std::max)(0, (std::min)(depth, m_MaxDepth));
    }
    void IncrementVisualizerDepth() override
    {
        if (m_VisualizerDepth < m_MaxDepth) m_VisualizerDepth++;
    }
    void DecrementVisualizerDepth() override
    {
        if (m_VisualizerDepth > 0) m_VisualizerDepth--;
    }

    EBVHVisualizerMode GetVisualizerMode() const override { return m_VisualizerMode; }
    void CycleVisualizerMode() override
    {
        if (m_VisualizerMode == EBVHVisualizerMode::LOD)
            m_VisualizerMode = EBVHVisualizerMode::SingleLevel;
        else if (m_VisualizerMode == EBVHVisualizerMode::SingleLevel)
            m_VisualizerMode = EBVHVisualizerMode::LeafOnly;
        else
            m_VisualizerMode = EBVHVisualizerMode::LOD;
    }
    const wchar_t* GetVisualizerModeName() const override
    {
        switch (m_VisualizerMode)
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
    int BuildSubtree(const std::vector<FSphere>& spheres, int start, int end, int currentDepth)
    {
        m_MaxDepth = (std::max)(m_MaxDepth, currentDepth);

        const int nodeIdx = static_cast<int>(m_Nodes.size());
        m_Nodes.emplace_back(); // Allocate node in contiguous array

        const int count = end - start;
        if (count == 1)
        {
            // Leaf node: stores direct sphere index
            int sIdx = m_SphereIndices[start];
            m_Nodes[nodeIdx].SetLeaf(sIdx, m_SphereBounds[sIdx]);
            return nodeIdx;
        }

        // Calculate centroid bounds and total envelope bounds
        FAABB centroidBounds;
        FAABB totalBounds;
        for (int i = start; i < end; ++i)
        {
            int sIdx = m_SphereIndices[i];
            centroidBounds.ExpandBy(spheres[sIdx].Center);
            totalBounds.ExpandBy(m_SphereBounds[sIdx]);
        }

        const int axis = centroidBounds.GetLongestAxis();
        const int mid  = start + count / 2;

        // O(count) median partitioning using std::nth_element
        std::nth_element(
            m_SphereIndices.begin() + start,
            m_SphereIndices.begin() + mid,
            m_SphereIndices.begin() + end,
            [&spheres, axis](int a, int b) {
                const FVector3& ca = spheres[a].Center;
                const FVector3& cb = spheres[b].Center;
                if (axis == 0) return ca.x < cb.x;
                if (axis == 1) return ca.y < cb.y;
                return ca.z < cb.z;
            }
        );

        // Recursively build children subtrees
        int leftChild  = BuildSubtree(spheres, start, mid, currentDepth + 1);
        int rightChild = BuildSubtree(spheres, mid, end, currentDepth + 1);

        // Internal node: store child indices and unified bounding box
        m_Nodes[nodeIdx].SetInternal(leftChild, rightChild, totalBounds);
        return nodeIdx;
    }

    //-------------------------------------------------------------------------
    // Visualizer Wireframe Collectors
    //-------------------------------------------------------------------------
    void CollectSingleLevelLines(int nodeIdx, int currentDepth, int targetDepth, std::vector<FVertexSimple>& lines) const
    {
        if (currentDepth == targetDepth)
        {
            IBVHVisualizer::AppendAABBWireframe(lines, m_Nodes[nodeIdx].Bounds);
            return;
        }

        if (!m_Nodes[nodeIdx].IsLeaf())
        {
            CollectSingleLevelLines(m_Nodes[nodeIdx].LeftChild,  currentDepth + 1, targetDepth, lines);
            CollectSingleLevelLines(m_Nodes[nodeIdx].RightChild, currentDepth + 1, targetDepth, lines);
        }
    }

    void CollectLODLines(int nodeIdx, int currentDepth, int maxDepth, std::vector<FBVHLineGroup>& outGroups) const
    {
        if (currentDepth <= maxDepth)
        {
            IBVHVisualizer::AppendAABBWireframe(outGroups[currentDepth].Lines, m_Nodes[nodeIdx].Bounds);
        }

        if (currentDepth < maxDepth && !m_Nodes[nodeIdx].IsLeaf())
        {
            CollectLODLines(m_Nodes[nodeIdx].LeftChild,  currentDepth + 1, maxDepth, outGroups);
            CollectLODLines(m_Nodes[nodeIdx].RightChild, currentDepth + 1, maxDepth, outGroups);
        }
    }

    void CollectLeafLines(int nodeIdx, std::vector<FVertexSimple>& lines) const
    {
        if (m_Nodes[nodeIdx].IsLeaf())
        {
            IBVHVisualizer::AppendAABBWireframe(lines, m_Nodes[nodeIdx].Bounds);
        }
        else
        {
            CollectLeafLines(m_Nodes[nodeIdx].LeftChild,  lines);
            CollectLeafLines(m_Nodes[nodeIdx].RightChild, lines);
        }
    }

private:
    LARGE_INTEGER                   m_TimerFreq        = {};
    FCollisionStats                 m_Stats            = {};
    std::vector<FCollisionManifold> m_Manifolds;

    // Contiguous Flat BVH Array
    std::vector<FBVHNode>           m_Nodes;
    std::vector<int>                m_SphereIndices;
    std::vector<FAABB>              m_SphereBounds;
    int                             m_MaxDepth         = 0;

    // Visualizer State
    int                             m_VisualizerDepth  = 3; // Default visible depth level
    EBVHVisualizerMode              m_VisualizerMode   = EBVHVisualizerMode::LOD;
};
