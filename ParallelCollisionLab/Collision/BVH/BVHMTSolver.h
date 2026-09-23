#pragma once

#include <vector>
#include <algorithm>
#include <cmath>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <windows.h>

#include "../ICollisionSolver.h"
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
    BVHMTSolver(int InThreadCount = 0)
    {
        QueryPerformanceFrequency(&TimerFrequency);
        Manifolds.reserve(512);
        Nodes.reserve(2048);
        SphereIndices.reserve(1024);
        SphereBounds.reserve(1024);

        unsigned int HwThreads = std::thread::hardware_concurrency();
        int InitialCount = (InThreadCount > 0) ? InThreadCount : (HwThreads > 0 ? static_cast<int>(HwThreads) : 4);
        InitThreadPool(InitialCount);
    }

    ~BVHMTSolver() override
    {
        ShutdownThreadPool();
    }

    //-------------------------------------------------------------------------
    // Thread Pool Lifecycle
    //-------------------------------------------------------------------------
    void SetThreadCount(int InThreadCount) override
    {
        if (InThreadCount <= 0 || InThreadCount == WorkerThreadCount)
            return;

        ShutdownThreadPool();
        InitThreadPool(InThreadCount);
    }

    void InitThreadPool(int InThreadCount)
    {
        WorkerThreadCount = InThreadCount;
        PerThreadManifolds.clear();
        PerThreadManifolds.resize(WorkerThreadCount);
        for (auto& vec : PerThreadManifolds)
        {
            vec.reserve(512);
        }

        PerThreadCandidates.assign(WorkerThreadCount, 0);

        bStopWorkers         = false;
        CurrentIteration     = 0;
        CompletedWorkerCount = 0;
        WorkerThreads.clear();

        for (int t = 1; t < WorkerThreadCount; ++t)
        {
            WorkerThreads.emplace_back(&BVHMTSolver::WorkerLoop, this, t);
        }
    }

    void ShutdownThreadPool()
    {
        {
            std::unique_lock<std::mutex> Lock(SyncMutex);
            bStopWorkers = true;
        }
        CvStart.notify_all();

        for (std::thread& Worker : WorkerThreads)
        {
            if (Worker.joinable())
            {
                Worker.join();
            }
        }
        WorkerThreads.clear();
    }

    //-------------------------------------------------------------------------
    // ICollisionSolver Interface
    //-------------------------------------------------------------------------
    void Solve(std::vector<FSphere>& Spheres) override
    {
        const int Count = static_cast<int>(Spheres.size());
        LastStats = {};
        if (Count < 2) return;

        CurrentSpheres = &Spheres;

        LARGE_INTEGER TimerStart, TimerBroad, TimerNarrow, TimerResolve;

        QueryPerformanceCounter(&TimerStart);
        BuildBVH(Spheres);
        QueryPerformanceCounter(&TimerBroad);

        for (auto& vec : PerThreadManifolds)
        {
            vec.clear();
        }
        std::fill(PerThreadCandidates.begin(), PerThreadCandidates.end(), 0ULL);

        if (WorkerThreads.empty())
        {
            DoQueryChunk(0, 0, Count);
        }
        else
        {
            CurrentPhase = EBVHMTParticlePhase::QuerySpheres;
            {
                std::unique_lock<std::mutex> Lock(SyncMutex);
                CompletedWorkerCount = 0;
                CurrentIteration++;
            }
            CvStart.notify_all();

            int ChunkSize = (Count + WorkerThreadCount - 1) / WorkerThreadCount;
            int End0      = (std::min)(ChunkSize, Count);
            DoQueryChunk(0, 0, End0);

            {
                std::unique_lock<std::mutex> Lock(SyncMutex);
                CvDone.wait(Lock, [&]() {
                    return CompletedWorkerCount >= static_cast<int>(WorkerThreads.size());
                });
            }
        }

        Manifolds.clear();
        size_t TotalManifolds = 0;
        uint64_t TotalCandidates = 0;
        for (int t = 0; t < WorkerThreadCount; ++t)
        {
            TotalManifolds  += PerThreadManifolds[t].size();
            TotalCandidates += PerThreadCandidates[t];
        }

        Manifolds.reserve(TotalManifolds);
        for (int t = 0; t < WorkerThreadCount; ++t)
        {
            Manifolds.insert(Manifolds.end(),
                             PerThreadManifolds[t].begin(),
                             PerThreadManifolds[t].end());
        }

        LastStats.CandidatePairCount   = TotalCandidates;
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

    const wchar_t* GetName()          const override { return L"BVH (MT)"; }
    const wchar_t* GetAlgorithmName() const override { return L"BVH (Median Split)"; }
    const wchar_t* GetExecutionMode() const override { return L"Multi Thread"; }
    int            GetThreadCount()   const override { return WorkerThreadCount; }

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
            VisualizerDepth = 0;
            return;
        }

        CurrentSpheres = const_cast<std::vector<FSphere>*>(&Spheres);
        SphereIndices.resize(Count);
        SphereBounds.resize(Count);
        for (int i = 0; i < Count; ++i)
        {
            SphereIndices[i] = i;
            SphereBounds[i]  = FAABB::FromSphere(Spheres[i].Center, Spheres[i].Radius);
        }

        const int TotalNodes = 2 * Count - 1;
        Nodes.resize(TotalNodes);
        MaxTreeDepth = 0;

        int NumTasks = 1;
        int ForkDepth = 0;
        if (WorkerThreadCount >= 8 && Count >= 2048)
        {
            NumTasks = 8;
            ForkDepth = 3;
        }
        else if (WorkerThreadCount >= 4 && Count >= 1024)
        {
            NumTasks = 4;
            ForkDepth = 2;
        }

        if (NumTasks <= 1 || WorkerThreads.empty())
        {
            int FreeNode = 0;
            BuildSubtreeInPlace(Spheres, 0, Count, 0, FreeNode, MaxTreeDepth);
            if (VisualizerDepth > MaxTreeDepth)
            {
                VisualizerDepth = MaxTreeDepth;
            }
            return;
        }

        SubtreeBuildTasks.clear();
        SubtreeBuildTasks.reserve(NumTasks);

        int UpperNodeCount = (1 << ForkDepth) - 1;
        int NextTaskOffset = UpperNodeCount;
        int UpperFreeNode  = 0;

        BuildUpperTreeInPlace(Spheres, 0, Count, 0, ForkDepth, UpperFreeNode, NextTaskOffset);

        CurrentPhase = EBVHMTParticlePhase::BuildSubtrees;
        {
            std::unique_lock<std::mutex> Lock(SyncMutex);
            CompletedWorkerCount = 0;
            CurrentIteration++;
        }
        CvStart.notify_all();

        ExecuteBuildTasks(0);

        {
            std::unique_lock<std::mutex> Lock(SyncMutex);
            CvDone.wait(Lock, [&]() {
                return CompletedWorkerCount >= static_cast<int>(WorkerThreads.size());
            });
        }

        for (const auto& Task : SubtreeBuildTasks)
        {
            if (Task.LocalMaxDepth > MaxTreeDepth)
            {
                MaxTreeDepth = Task.LocalMaxDepth;
            }
        }

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
        else
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

    void WorkerLoop(int ThreadIndex)
    {
        int LastIteration = 0;
        while (true)
        {
            {
                std::unique_lock<std::mutex> Lock(SyncMutex);
                CvStart.wait(Lock, [&]() {
                    return bStopWorkers || CurrentIteration > LastIteration;
                });

                if (bStopWorkers) break;
                LastIteration = CurrentIteration;
            }

            if (CurrentPhase == EBVHMTParticlePhase::BuildSubtrees)
            {
                ExecuteBuildTasks(ThreadIndex);
            }
            else // QuerySpheres
            {
                const int Count     = static_cast<int>(CurrentSpheres->size());
                int ChunkSize       = (Count + WorkerThreadCount - 1) / WorkerThreadCount;
                int StartIdx        = ThreadIndex * ChunkSize;
                int EndIdx          = (std::min)(StartIdx + ChunkSize, Count);

                if (StartIdx < Count)
                {
                    DoQueryChunk(ThreadIndex, StartIdx, EndIdx);
                }
            }

            {
                std::unique_lock<std::mutex> Lock(SyncMutex);
                CompletedWorkerCount++;
                if (CompletedWorkerCount >= static_cast<int>(WorkerThreads.size()))
                {
                    CvDone.notify_one();
                }
            }
        }
    }

    void ExecuteBuildTasks(int ThreadIndex)
    {
        const auto& Spheres = *CurrentSpheres;
        int NumTasks = static_cast<int>(SubtreeBuildTasks.size());
        for (int k = ThreadIndex; k < NumTasks; k += WorkerThreadCount)
        {
            auto& Task = SubtreeBuildTasks[k];
            int FreeNode = Task.NodeOffset;
            BuildSubtreeInPlace(Spheres, Task.StartSphere, Task.EndSphere, Task.CurrentDepth,
                                FreeNode, Task.LocalMaxDepth);
        }
    }

    void DoQueryChunk(int ThreadIndex, int StartIdx, int EndIdx)
    {
        const auto& Spheres          = *CurrentSpheres;
        auto& LocalManifolds         = PerThreadManifolds[ThreadIndex];
        uint64_t Candidates          = 0;

        int Stack[64];

        for (int i = StartIdx; i < EndIdx; ++i)
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
                        Candidates++;
                        const FVector3 Diff      = PosA - Spheres[j].Center;
                        const float    DistSq    = Diff.LengthSq();
                        const float    RadiusSum = RadiusA + Spheres[j].Radius;

                        if (DistSq < RadiusSum * RadiusSum)
                        {
                            const float Dist      = sqrtf(DistSq);
                            const FVector3 Normal = (Dist > 1e-6f) ? Diff * (1.0f / Dist) : FVector3(1.0f, 0.0f, 0.0f);
                            LocalManifolds.push_back({ i, j, Normal, RadiusSum - Dist });
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

        PerThreadCandidates[ThreadIndex] = Candidates;
    }

    int BuildSubtreeInPlace(const std::vector<FSphere>& Spheres,
                            int Start, int End, int CurrentDepth,
                            int& FreeNode, int& InMaxDepth)
    {
        if (CurrentDepth > InMaxDepth) InMaxDepth = CurrentDepth;

        int NodeIdx = FreeNode++;
        int Count   = End - Start;

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

        int Axis = CentroidBounds.GetLongestAxis();
        int Mid  = Start + Count / 2;

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

        int LeftChild  = BuildSubtreeInPlace(Spheres, Start, Mid, CurrentDepth + 1, FreeNode, InMaxDepth);
        int RightChild = BuildSubtreeInPlace(Spheres, Mid,   End, CurrentDepth + 1, FreeNode, InMaxDepth);

        Nodes[NodeIdx].SetInternal(LeftChild, RightChild, TotalBounds);
        return NodeIdx;
    }

    void BuildUpperTreeInPlace(const std::vector<FSphere>& Spheres,
                               int Start, int End, int CurrentDepth, int ForkDepth,
                               int& UpperFreeNode, int& NextTaskOffset)
    {
        if (CurrentDepth > MaxTreeDepth) MaxTreeDepth = CurrentDepth;

        int NodeIdx = UpperFreeNode++;
        int Count   = End - Start;

        FAABB CentroidBounds;
        FAABB TotalBounds;
        for (int i = Start; i < End; ++i)
        {
            int sIdx = SphereIndices[i];
            CentroidBounds.ExpandBy(Spheres[sIdx].Center);
            TotalBounds.ExpandBy(SphereBounds[sIdx]);
        }

        int Axis = CentroidBounds.GetLongestAxis();
        int Mid  = Start + Count / 2;

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

        int LeftChildIdx  = -1;
        int RightChildIdx = -1;

        if (CurrentDepth + 1 == ForkDepth)
        {
            int LeftCount = Mid - Start;
            int LeftNodeOffset = NextTaskOffset;
            NextTaskOffset += (2 * LeftCount - 1);
            SubtreeBuildTasks.push_back({ Start, Mid, LeftNodeOffset, CurrentDepth + 1, CurrentDepth + 1 });
            LeftChildIdx = LeftNodeOffset;

            int RightCount = End - Mid;
            int RightNodeOffset = NextTaskOffset;
            NextTaskOffset += (2 * RightCount - 1);
            SubtreeBuildTasks.push_back({ Mid, End, RightNodeOffset, CurrentDepth + 1, CurrentDepth + 1 });
            RightChildIdx = RightNodeOffset;
        }
        else
        {
            LeftChildIdx  = UpperFreeNode;
            BuildUpperTreeInPlace(Spheres, Start, Mid, CurrentDepth + 1, ForkDepth,
                                  UpperFreeNode, NextTaskOffset);

            RightChildIdx = UpperFreeNode;
            BuildUpperTreeInPlace(Spheres, Mid, End, CurrentDepth + 1, ForkDepth,
                                  UpperFreeNode, NextTaskOffset);
        }

        Nodes[NodeIdx].SetInternal(LeftChildIdx, RightChildIdx, TotalBounds);
    }

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

    void CollectLODLines(int NodeIdx, int CurrentDepth, int TargetDepth, std::vector<FBVHLineGroup>& OutGroups) const
    {
        if (CurrentDepth <= TargetDepth && CurrentDepth < static_cast<int>(OutGroups.size()))
        {
            IBVHVisualizer::AppendAABBWireframe(OutGroups[CurrentDepth].Lines, Nodes[NodeIdx].Bounds);
        }

        if (CurrentDepth < TargetDepth && !Nodes[NodeIdx].IsLeaf())
        {
            CollectLODLines(Nodes[NodeIdx].LeftChild,  CurrentDepth + 1, TargetDepth, OutGroups);
            CollectLODLines(Nodes[NodeIdx].RightChild, CurrentDepth + 1, TargetDepth, OutGroups);
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
    LARGE_INTEGER                   TimerFrequency     = {};
    FCollisionStats                 LastStats          = {};
    std::vector<FCollisionManifold> Manifolds;

    std::vector<FBVHNode>           Nodes;
    std::vector<int>                SphereIndices;
    std::vector<FAABB>              SphereBounds;
    int                             MaxTreeDepth       = 0;

    int                             VisualizerDepth    = 3;
    EBVHVisualizerMode              VisualizerMode     = EBVHVisualizerMode::LOD;

    // Multi-threading state
    int                             WorkerThreadCount  = 1;
    std::vector<std::thread>        WorkerThreads;
    std::mutex                      SyncMutex;
    std::condition_variable         CvStart;
    std::condition_variable         CvDone;
    bool                            bStopWorkers       = false;
    int                             CurrentIteration   = 0;
    int                             CompletedWorkerCount = 0;

    std::vector<FSphere>*           CurrentSpheres     = nullptr;
    std::vector<std::vector<FCollisionManifold>> PerThreadManifolds;
    std::vector<uint64_t>           PerThreadCandidates;

    EBVHMTParticlePhase             CurrentPhase       = EBVHMTParticlePhase::QuerySpheres;
    std::vector<FSubtreeBuildTask>  SubtreeBuildTasks;
};
