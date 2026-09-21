#pragma once

#include <vector>
#include <algorithm>
#include <cmath>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <windows.h>

#include "../ICollisionSolver.h"
#include "../CollisionTypes.h"
#include "../NarrowPhase.h"
#include "../Resolution.h"
#include "AABB.h"
#include "BVHNode.h"
#include "IBVHVisualizer.h"

//=============================================================================
// BVHMTSolver - Multi-Threaded AABB Bounding Volume Hierarchy Collision Solver
//
// Architecture:
// 1. Broad Phase: Top-Down Longest-Axis Object Median Split Tree Build
// 2. Narrow Phase: 100% Lock-Free Parallel Query-Against-Tree Traversal
//    - Flat FBVHNode tree buffer is 100% read-only in L2/L3 cache
//    - Spheres are partitioned evenly among worker threads
//    - Each thread maintains a thread-local L1 traversal stack (stack[64])
//    - Unique collision pairs (i < j) collected into thread-local buffers
// 3. Resolution Phase: Standard impulse-based sphere collision resolution
//=============================================================================
class BVHMTSolver : public ICollisionSolver, public IBVHVisualizer
{
public:
    BVHMTSolver(int threadCount = 0)
    {
        QueryPerformanceFrequency(&m_TimerFreq);
        m_Manifolds.reserve(512);
        m_Nodes.reserve(2048);
        m_SphereIndices.reserve(1024);
        m_SphereBounds.reserve(1024);

        unsigned int hwThreads = std::thread::hardware_concurrency();
        int initialCount = (threadCount > 0) ? threadCount : (hwThreads > 0 ? static_cast<int>(hwThreads) : 4);
        InitThreadPool(initialCount);
    }

    ~BVHMTSolver() override
    {
        ShutdownThreadPool();
    }

    //-------------------------------------------------------------------------
    // Thread Pool Lifecycle
    //-------------------------------------------------------------------------
    void SetThreadCount(int threadCount) override
    {
        if (threadCount <= 0 || threadCount == m_ThreadCount)
            return;

        ShutdownThreadPool();
        InitThreadPool(threadCount);
    }

    void InitThreadPool(int threadCount)
    {
        m_ThreadCount = threadCount;
        m_ThreadManifolds.clear();
        m_ThreadManifolds.resize(m_ThreadCount);
        for (auto& vec : m_ThreadManifolds)
        {
            vec.reserve(512);
        }

        m_ThreadCandidates.assign(m_ThreadCount, 0);

        m_Stop           = false;
        m_Iteration      = 0;
        m_CompletedCount = 0;
        m_Workers.clear();

        for (int t = 1; t < m_ThreadCount; ++t)
        {
            m_Workers.emplace_back(&BVHMTSolver::WorkerLoop, this, t);
        }
    }

    void ShutdownThreadPool()
    {
        {
            std::unique_lock<std::mutex> lock(m_Mutex);
            m_Stop = true;
        }
        m_CvStart.notify_all();

        for (std::thread& worker : m_Workers)
        {
            if (worker.joinable())
            {
                worker.join();
            }
        }
        m_Workers.clear();
    }

    //-------------------------------------------------------------------------
    // ICollisionSolver Interface
    //-------------------------------------------------------------------------
    void Solve(std::vector<FSphere>& spheres) override
    {
        const int count = static_cast<int>(spheres.size());
        m_Stats = {};
        if (count < 2) return;

        m_CurrentSpheres = &spheres;

        LARGE_INTEGER t0, t1, t2, t3;

        QueryPerformanceCounter(&t0);
        BuildBVH(spheres);
        QueryPerformanceCounter(&t1);

        for (auto& vec : m_ThreadManifolds)
        {
            vec.clear();
        }
        std::fill(m_ThreadCandidates.begin(), m_ThreadCandidates.end(), 0ULL);

        if (m_Workers.empty())
        {
            DoQueryChunk(0, 0, count);
        }
        else
        {
            m_Phase = EBVHMTParticlePhase::QuerySpheres;
            {
                std::unique_lock<std::mutex> lock(m_Mutex);
                m_CompletedCount = 0;
                m_Iteration++;
            }
            m_CvStart.notify_all();

            int chunkSize = (count + m_ThreadCount - 1) / m_ThreadCount;
            int end0      = (std::min)(chunkSize, count);
            DoQueryChunk(0, 0, end0);

            {
                std::unique_lock<std::mutex> lock(m_Mutex);
                m_CvDone.wait(lock, [&]() {
                    return m_CompletedCount >= static_cast<int>(m_Workers.size());
                });
            }
        }

        m_Manifolds.clear();
        size_t totalManifolds = 0;
        uint64_t totalCandidates = 0;
        for (int t = 0; t < m_ThreadCount; ++t)
        {
            totalManifolds  += m_ThreadManifolds[t].size();
            totalCandidates += m_ThreadCandidates[t];
        }

        m_Manifolds.reserve(totalManifolds);
        for (int t = 0; t < m_ThreadCount; ++t)
        {
            m_Manifolds.insert(m_Manifolds.end(),
                               m_ThreadManifolds[t].begin(),
                               m_ThreadManifolds[t].end());
        }

        m_Stats.CandidatePairCount   = totalCandidates;
        m_Stats.ActualCollisionCount = static_cast<uint64_t>(m_Manifolds.size());
        QueryPerformanceCounter(&t2);

        ResolveCollisions(spheres, m_Manifolds);
        QueryPerformanceCounter(&t3);

        const double toMs = 1000.0 / static_cast<double>(m_TimerFreq.QuadPart);
        m_Stats.BroadPhaseTimeMs  = static_cast<double>(t1.QuadPart - t0.QuadPart) * toMs;
        m_Stats.NarrowPhaseTimeMs = static_cast<double>(t2.QuadPart - t1.QuadPart) * toMs;
        m_Stats.ResolutionTimeMs  = static_cast<double>(t3.QuadPart - t2.QuadPart) * toMs;
        m_Stats.TotalSolveTimeMs  = static_cast<double>(t3.QuadPart - t0.QuadPart) * toMs;
    }

    const wchar_t* GetName()          const override { return L"BVH (MT)"; }
    const wchar_t* GetAlgorithmName() const override { return L"BVH (Median Split)"; }
    const wchar_t* GetExecutionMode() const override { return L"Multi Thread"; }
    int            GetThreadCount()   const override { return m_ThreadCount; }

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
            m_VisualizerDepth = 0;
            return;
        }

        m_CurrentSpheres = const_cast<std::vector<FSphere>*>(&spheres);
        m_SphereIndices.resize(count);
        m_SphereBounds.resize(count);
        for (int i = 0; i < count; ++i)
        {
            m_SphereIndices[i] = i;
            m_SphereBounds[i]  = FAABB::FromSphere(spheres[i].Center, spheres[i].Radius);
        }

        const int totalNodes = 2 * count - 1;
        m_Nodes.resize(totalNodes);
        m_MaxDepth = 0;

        int numTasks = 1;
        int forkDepth = 0;
        if (m_ThreadCount >= 8 && count >= 2048)
        {
            numTasks = 8;
            forkDepth = 3;
        }
        else if (m_ThreadCount >= 4 && count >= 1024)
        {
            numTasks = 4;
            forkDepth = 2;
        }

        if (numTasks <= 1 || m_Workers.empty())
        {
            int freeNode = 0;
            BuildSubtreeInPlace(spheres, 0, count, 0, freeNode, m_MaxDepth);
            if (m_VisualizerDepth > m_MaxDepth)
            {
                m_VisualizerDepth = m_MaxDepth;
            }
            return;
        }

        m_BuildTasks.clear();
        m_BuildTasks.reserve(numTasks);

        int upperNodeCount = (1 << forkDepth) - 1;
        int nextTaskOffset = upperNodeCount;
        int upperFreeNode  = 0;

        BuildUpperTreeInPlace(spheres, 0, count, 0, forkDepth, upperFreeNode, nextTaskOffset);

        m_Phase = EBVHMTParticlePhase::BuildSubtrees;
        {
            std::unique_lock<std::mutex> lock(m_Mutex);
            m_CompletedCount = 0;
            m_Iteration++;
        }
        m_CvStart.notify_all();

        ExecuteBuildTasks(0);

        {
            std::unique_lock<std::mutex> lock(m_Mutex);
            m_CvDone.wait(lock, [&]() {
                return m_CompletedCount >= static_cast<int>(m_Workers.size());
            });
        }

        for (const auto& task : m_BuildTasks)
        {
            if (task.LocalMaxDepth > m_MaxDepth)
            {
                m_MaxDepth = task.LocalMaxDepth;
            }
        }

        if (m_VisualizerDepth > m_MaxDepth)
        {
            m_VisualizerDepth = m_MaxDepth;
        }
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
        else
        {
            outGroups.resize(1);
            outGroups[0].Color = FVector4(0.20f, 0.95f, 0.40f, 1.0f);
            CollectLeafLines(0, outGroups[0].Lines);
        }
    }

    int  GetVisualizerDepth() const override
    {
        return (std::max)(0, (std::min)(m_VisualizerDepth, m_MaxDepth));
    }
    int  GetMaxTreeDepth()    const override { return m_MaxDepth; }
    void SetVisualizerDepth(int depth) override
    {
        m_VisualizerDepth = (std::max)(0, (std::min)(depth, m_MaxDepth));
    }
    void IncrementVisualizerDepth() override
    {
        if (m_VisualizerDepth > m_MaxDepth)
        {
            m_VisualizerDepth = m_MaxDepth;
        }
        else if (m_VisualizerDepth < m_MaxDepth)
        {
            m_VisualizerDepth++;
        }
    }
    void DecrementVisualizerDepth() override
    {
        if (m_VisualizerDepth > m_MaxDepth)
        {
            m_VisualizerDepth = m_MaxDepth;
        }
        if (m_VisualizerDepth > 0)
        {
            m_VisualizerDepth--;
        }
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
        case EBVHVisualizerMode::LOD:         return L"LOD (Accumulated)";
        case EBVHVisualizerMode::SingleLevel: return L"Single Level";
        case EBVHVisualizerMode::LeafOnly:    return L"Leaves Only";
        default:                              return L"Unknown";
        }
    }

private:
    enum class EBVHMTParticlePhase
    {
        BuildSubtrees,
        QuerySpheres
    };

    struct FSubtreeBuildTask
    {
        int StartSphere;
        int EndSphere;
        int NodeOffset;
        int CurrentDepth;
        int LocalMaxDepth = 0;
    };

    void WorkerLoop(int threadIdx)
    {
        int lastIteration = 0;
        while (true)
        {
            {
                std::unique_lock<std::mutex> lock(m_Mutex);
                m_CvStart.wait(lock, [&]() {
                    return m_Stop || m_Iteration > lastIteration;
                });

                if (m_Stop) break;
                lastIteration = m_Iteration;
            }

            if (m_Phase == EBVHMTParticlePhase::BuildSubtrees)
            {
                ExecuteBuildTasks(threadIdx);
            }
            else // QuerySpheres
            {
                const int count = static_cast<int>(m_CurrentSpheres->size());
                int chunkSize   = (count + m_ThreadCount - 1) / m_ThreadCount;
                int startIdx    = threadIdx * chunkSize;
                int endIdx      = (std::min)(startIdx + chunkSize, count);

                if (startIdx < count)
                {
                    DoQueryChunk(threadIdx, startIdx, endIdx);
                }
            }

            {
                std::unique_lock<std::mutex> lock(m_Mutex);
                m_CompletedCount++;
                if (m_CompletedCount >= static_cast<int>(m_Workers.size()))
                {
                    m_CvDone.notify_one();
                }
            }
        }
    }

    void ExecuteBuildTasks(int threadIdx)
    {
        const auto& spheres = *m_CurrentSpheres;
        int numTasks = static_cast<int>(m_BuildTasks.size());
        for (int k = threadIdx; k < numTasks; k += m_ThreadCount)
        {
            auto& task = m_BuildTasks[k];
            int freeNode = task.NodeOffset;
            BuildSubtreeInPlace(spheres, task.StartSphere, task.EndSphere, task.CurrentDepth,
                                freeNode, task.LocalMaxDepth);
        }
    }

    void DoQueryChunk(int threadIdx, int startIdx, int endIdx)
    {
        const auto& spheres   = *m_CurrentSpheres;
        auto& localManifolds  = m_ThreadManifolds[threadIdx];
        uint64_t candidates   = 0;

        int stack[64];

        for (int i = startIdx; i < endIdx; ++i)
        {
            const FVector3 posA = spheres[i].Center;
            const float    radA = spheres[i].Radius;
            const FAABB&   boxA = m_SphereBounds[i];

            int stackPtr = 0;
            stack[stackPtr++] = 0;

            while (stackPtr > 0)
            {
                int currIdx = stack[--stackPtr];
                const FBVHNode& node = m_Nodes[currIdx];

                if (!boxA.Intersects(node.Bounds))
                {
                    continue;
                }

                if (node.IsLeaf())
                {
                    int j = node.GetSphereIndex();
                    if (i < j)
                    {
                        candidates++;
                        const FVector3 diff   = posA - spheres[j].Center;
                        const float    distSq = diff.LengthSq();
                        const float    radSum = radA + spheres[j].Radius;

                        if (distSq < radSum * radSum)
                        {
                            const float dist   = sqrtf(distSq);
                            const FVector3 normal = (dist > 1e-6f) ? diff * (1.0f / dist) : FVector3(1.0f, 0.0f, 0.0f);
                            localManifolds.push_back({ i, j, normal, radSum - dist });
                        }
                    }
                }
                else
                {
                    stack[stackPtr++] = node.RightChild;
                    stack[stackPtr++] = node.LeftChild;
                }
            }
        }

        m_ThreadCandidates[threadIdx] = candidates;
    }

    int BuildSubtreeInPlace(const std::vector<FSphere>& spheres,
                            int start, int end, int currentDepth,
                            int& freeNode, int& maxDepth)
    {
        if (currentDepth > maxDepth) maxDepth = currentDepth;

        int nodeIdx = freeNode++;
        int count   = end - start;

        if (count == 1)
        {
            int sIdx = m_SphereIndices[start];
            m_Nodes[nodeIdx].SetLeaf(sIdx, m_SphereBounds[sIdx]);
            return nodeIdx;
        }

        FAABB centroidBounds;
        FAABB totalBounds;
        for (int i = start; i < end; ++i)
        {
            int sIdx = m_SphereIndices[i];
            centroidBounds.ExpandBy(spheres[sIdx].Center);
            totalBounds.ExpandBy(m_SphereBounds[sIdx]);
        }

        int axis = centroidBounds.GetLongestAxis();
        int mid  = start + count / 2;

        int* pBegin = m_SphereIndices.data() + start;
        int* pMid   = m_SphereIndices.data() + mid;
        int* pEnd   = m_SphereIndices.data() + end;
        const FSphere* pSpheres = spheres.data();

        if (axis == 0)
        {
            std::nth_element(pBegin, pMid, pEnd, [pSpheres](int a, int b) {
                return pSpheres[a].Center.x < pSpheres[b].Center.x;
            });
        }
        else if (axis == 1)
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

        int leftChild  = BuildSubtreeInPlace(spheres, start, mid, currentDepth + 1, freeNode, maxDepth);
        int rightChild = BuildSubtreeInPlace(spheres, mid,   end, currentDepth + 1, freeNode, maxDepth);

        m_Nodes[nodeIdx].SetInternal(leftChild, rightChild, totalBounds);
        return nodeIdx;
    }

    void BuildUpperTreeInPlace(const std::vector<FSphere>& spheres,
                               int start, int end, int currentDepth, int forkDepth,
                               int& upperFreeNode, int& nextTaskOffset)
    {
        if (currentDepth > m_MaxDepth) m_MaxDepth = currentDepth;

        int nodeIdx = upperFreeNode++;
        int count   = end - start;

        FAABB centroidBounds;
        FAABB totalBounds;
        for (int i = start; i < end; ++i)
        {
            int sIdx = m_SphereIndices[i];
            centroidBounds.ExpandBy(spheres[sIdx].Center);
            totalBounds.ExpandBy(m_SphereBounds[sIdx]);
        }

        int axis = centroidBounds.GetLongestAxis();
        int mid  = start + count / 2;

        int* pBegin = m_SphereIndices.data() + start;
        int* pMid   = m_SphereIndices.data() + mid;
        int* pEnd   = m_SphereIndices.data() + end;
        const FSphere* pSpheres = spheres.data();

        if (axis == 0)
        {
            std::nth_element(pBegin, pMid, pEnd, [pSpheres](int a, int b) {
                return pSpheres[a].Center.x < pSpheres[b].Center.x;
            });
        }
        else if (axis == 1)
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

        int leftChildIdx  = -1;
        int rightChildIdx = -1;

        if (currentDepth + 1 == forkDepth)
        {
            int leftCount = mid - start;
            int leftNodeOffset = nextTaskOffset;
            nextTaskOffset += (2 * leftCount - 1);
            m_BuildTasks.push_back({ start, mid, leftNodeOffset, currentDepth + 1, currentDepth + 1 });
            leftChildIdx = leftNodeOffset;

            int rightCount = end - mid;
            int rightNodeOffset = nextTaskOffset;
            nextTaskOffset += (2 * rightCount - 1);
            m_BuildTasks.push_back({ mid, end, rightNodeOffset, currentDepth + 1, currentDepth + 1 });
            rightChildIdx = rightNodeOffset;
        }
        else
        {
            leftChildIdx  = upperFreeNode;
            BuildUpperTreeInPlace(spheres, start, mid, currentDepth + 1, forkDepth,
                                  upperFreeNode, nextTaskOffset);

            rightChildIdx = upperFreeNode;
            BuildUpperTreeInPlace(spheres, mid, end, currentDepth + 1, forkDepth,
                                  upperFreeNode, nextTaskOffset);
        }

        m_Nodes[nodeIdx].SetInternal(leftChildIdx, rightChildIdx, totalBounds);
    }

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

    void CollectLODLines(int nodeIdx, int currentDepth, int targetDepth, std::vector<FBVHLineGroup>& outGroups) const
    {
        if (currentDepth <= targetDepth && currentDepth < static_cast<int>(outGroups.size()))
        {
            IBVHVisualizer::AppendAABBWireframe(outGroups[currentDepth].Lines, m_Nodes[nodeIdx].Bounds);
        }

        if (currentDepth < targetDepth && !m_Nodes[nodeIdx].IsLeaf())
        {
            CollectLODLines(m_Nodes[nodeIdx].LeftChild,  currentDepth + 1, targetDepth, outGroups);
            CollectLODLines(m_Nodes[nodeIdx].RightChild, currentDepth + 1, targetDepth, outGroups);
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

    std::vector<FBVHNode>           m_Nodes;
    std::vector<int>                m_SphereIndices;
    std::vector<FAABB>              m_SphereBounds;
    int                             m_MaxDepth         = 0;

    int                             m_VisualizerDepth  = 3;
    EBVHVisualizerMode              m_VisualizerMode   = EBVHVisualizerMode::LOD;

    // Multi-threading state
    int                             m_ThreadCount      = 1;
    std::vector<std::thread>        m_Workers;
    std::mutex                      m_Mutex;
    std::condition_variable         m_CvStart;
    std::condition_variable         m_CvDone;
    bool                            m_Stop             = false;
    int                             m_Iteration        = 0;
    int                             m_CompletedCount   = 0;

    std::vector<FSphere>*           m_CurrentSpheres   = nullptr;
    std::vector<std::vector<FCollisionManifold>> m_ThreadManifolds;
    std::vector<uint64_t>           m_ThreadCandidates;

    EBVHMTParticlePhase             m_Phase            = EBVHMTParticlePhase::QuerySpheres;
    std::vector<FSubtreeBuildTask>  m_BuildTasks;
};
