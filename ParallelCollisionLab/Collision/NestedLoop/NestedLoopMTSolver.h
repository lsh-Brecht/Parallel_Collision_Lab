#pragma once

#include <vector>
#include <windows.h>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <algorithm>
#include <cmath>
#include "../ICollisionSolver.h"
#include "../Resolution.h"

//=============================================================================
// NestedLoopMTSolver - Multi-threaded Naive O(n^2) collision solver
//=============================================================================
class NestedLoopMTSolver : public ICollisionSolver
{
public:
    NestedLoopMTSolver(int InThreadCount = 0)
    {
        QueryPerformanceFrequency(&TimerFrequency);

        unsigned int HwThreads = std::thread::hardware_concurrency();
        int InitialCount = (InThreadCount > 0) ? InThreadCount : (HwThreads > 0 ? static_cast<int>(HwThreads) : 4);
        InitThreadPool(InitialCount);
    }

    ~NestedLoopMTSolver() override
    {
        ShutdownThreadPool();
    }

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
            vec.reserve(256);
        }

        bStopWorkers = false;
        CurrentIteration = 0;
        CompletedWorkerCount = 0;
        WorkerThreads.clear();

        // Spawn background worker threads (main thread acts as worker 0)
        for (int t = 1; t < WorkerThreadCount; ++t)
        {
            WorkerThreads.emplace_back(&NestedLoopMTSolver::WorkerLoop, this, t);
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

    void Solve(std::vector<FSphere>& Spheres) override
    {
        const int Count = static_cast<int>(Spheres.size());
        LastStats = {};
        if (Count < 2) return;

        LARGE_INTEGER TimerStart, TimerBroad, TimerNarrow;

        QueryPerformanceCounter(&TimerStart);
        LastStats.CandidatePairCount = static_cast<uint64_t>(Count) * (Count - 1) / 2;
        QueryPerformanceCounter(&TimerBroad);

        CurrentSpheres = &Spheres;
        for (auto& vec : PerThreadManifolds)
        {
            vec.clear();
        }

        if (WorkerThreads.empty())
        {
            DoNarrowPhaseChunk(0);
        }
        else
        {
            {
                std::unique_lock<std::mutex> Lock(SyncMutex);
                CompletedWorkerCount = 0;
                CurrentIteration++;
            }
            CvStart.notify_all();

            DoNarrowPhaseChunk(0);

            {
                std::unique_lock<std::mutex> Lock(SyncMutex);
                CvDone.wait(Lock, [&]() {
                    return CompletedWorkerCount >= static_cast<int>(WorkerThreads.size());
                });
            }
        }

        size_t TotalManifolds = 0;
        for (int t = 0; t < WorkerThreadCount; ++t)
        {
            TotalManifolds += PerThreadManifolds[t].size();
        }
        Manifolds.clear();
        Manifolds.reserve(TotalManifolds);
        for (int t = 0; t < WorkerThreadCount; ++t)
        {
            Manifolds.insert(Manifolds.end(), PerThreadManifolds[t].begin(), PerThreadManifolds[t].end());
        }
        LastStats.ActualCollisionCount = static_cast<uint64_t>(Manifolds.size());
        QueryPerformanceCounter(&TimerNarrow);

        ResolveCollisions(Spheres, Manifolds);

        const double ToMilliseconds = 1000.0 / static_cast<double>(TimerFrequency.QuadPart);
        LastStats.BroadPhaseTimeMs   = static_cast<double>(TimerBroad.QuadPart - TimerStart.QuadPart) * ToMilliseconds;
        LastStats.NarrowPhaseTimeMs  = static_cast<double>(TimerNarrow.QuadPart - TimerBroad.QuadPart) * ToMilliseconds;
        LastStats.TotalSolveTimeMs   = static_cast<double>(TimerNarrow.QuadPart - TimerStart.QuadPart) * ToMilliseconds;
    }

    const wchar_t* GetName()          const override { return L"NestedLoop (MT)"; }
    const wchar_t* GetAlgorithmName() const override { return L"Nested Loop"; }
    const wchar_t* GetExecutionMode() const override { return L"Multi Thread"; }
    int            GetThreadCount()   const override { return WorkerThreadCount; }

    const FCollisionStats& GetLastStats() const override { return LastStats; }
    const std::vector<FCollisionManifold>& GetManifolds() const { return Manifolds; }

private:
    void WorkerLoop(int ThreadIndex)
    {
        int LastIteration = 0;
        while (true)
        {
            {
                std::unique_lock<std::mutex> Lock(SyncMutex);
                CvStart.wait(Lock, [&]() {
                    return bStopWorkers || (CurrentIteration > LastIteration);
                });

                if (bStopWorkers) break;
                LastIteration = CurrentIteration;
            }

            DoNarrowPhaseChunk(ThreadIndex);

            {
                std::unique_lock<std::mutex> Lock(SyncMutex);
                CompletedWorkerCount++;
                if (CompletedWorkerCount == static_cast<int>(WorkerThreads.size()))
                {
                    CvDone.notify_one();
                }
            }
        }
    }

    static int GetSplitIndex(int Count, int ThreadIndex, int TotalThreads)
    {
        if (ThreadIndex <= 0) return 0;
        if (ThreadIndex >= TotalThreads) return Count;
        double Fraction = static_cast<double>(ThreadIndex) / static_cast<double>(TotalThreads);
        double Root = std::sqrt(1.0 - Fraction);
        int Index = static_cast<int>(std::round(Count * (1.0 - Root)));
        if (Index < 0) Index = 0;
        if (Index > Count) Index = Count;
        return Index;
    }

    void DoNarrowPhaseChunk(int ThreadIndex)
    {
        if (!CurrentSpheres) return;
        const std::vector<FSphere>& Spheres = *CurrentSpheres;
        const int Count = static_cast<int>(Spheres.size());

        const int StartI = GetSplitIndex(Count, ThreadIndex, WorkerThreadCount);
        const int EndI   = GetSplitIndex(Count, ThreadIndex + 1, WorkerThreadCount);

        std::vector<FCollisionManifold>& LocalManifolds = PerThreadManifolds[ThreadIndex];

        for (int i = StartI; i < EndI; ++i)
        {
            const FVector3 PosA    = Spheres[i].Center;
            const float    RadiusA = Spheres[i].Radius;

            for (int j = i + 1; j < Count; ++j)
            {
                const FVector3 Diff      = PosA - Spheres[j].Center;
                const float    DistSq    = Diff.LengthSq();
                const float    RadiusSum = RadiusA + Spheres[j].Radius;

                if (DistSq < RadiusSum * RadiusSum)
                {
                    const float Dist = sqrtf(DistSq);
                    const FVector3 Normal = (Dist > 1e-6f) ? Diff * (1.0f / Dist) : FVector3(1.0f, 0.0f, 0.0f);
                    LocalManifolds.push_back({ i, j, Normal, RadiusSum - Dist });
                }
            }
        }
    }

private:
    LARGE_INTEGER                   TimerFrequency       = {};
    int                             WorkerThreadCount    = 4;
    FCollisionStats                 LastStats            = {};
    std::vector<FCollisionManifold> Manifolds;

    std::vector<std::vector<FCollisionManifold>> PerThreadManifolds;
    const std::vector<FSphere>*                  CurrentSpheres       = nullptr;

    std::vector<std::thread>        WorkerThreads;
    std::mutex                      SyncMutex;
    std::condition_variable         CvStart;
    std::condition_variable         CvDone;
    int                             CurrentIteration     = 0;
    int                             CompletedWorkerCount = 0;
    bool                            bStopWorkers         = false;
};
