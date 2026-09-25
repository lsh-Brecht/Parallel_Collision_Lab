#pragma once

#include <vector>
#include <cmath>
#include <algorithm>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <windows.h>
#include "../ICollisionSolver.h"
#include "../Resolution.h"
#include "IUniformGridVisualizer.h"

//=============================================================================
// UniformGridMTSolver - Multi-threaded Spatial Partitioning Collision Solver
//=============================================================================
class UniformGridMTSolver : public ICollisionSolver, public FUniformGridBase
{
public:
    UniformGridMTSolver(int InThreadCount = 0, float InBoxHalfSize = 2.0f)
    {
        BoxHalfSize = InBoxHalfSize;
        QueryPerformanceFrequency(&TimerFrequency);
        Manifolds.reserve(512);
        ActiveCells.reserve(1024);

        unsigned int HwThreads = std::thread::hardware_concurrency();
        int InitialCount = (InThreadCount > 0) ? InThreadCount : (HwThreads > 0 ? static_cast<int>(HwThreads) : 4);
        InitThreadPool(InitialCount);
    }

    ~UniformGridMTSolver() override
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
            vec.reserve(512);
        }

        PerThreadCandidates.assign(WorkerThreadCount, 0);

        bStopWorkers         = false;
        CurrentIteration      = 0;
        CompletedWorkerCount = 0;
        WorkerThreads.clear();

        for (int t = 1; t < WorkerThreadCount; ++t)
        {
            WorkerThreads.emplace_back(&UniformGridMTSolver::WorkerLoop, this, t);
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
        BuildGrid(Spheres);
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
        uint64_t TotalCandidates = 0;
        for (int t = 0; t < WorkerThreadCount; ++t)
        {
            TotalManifolds  += PerThreadManifolds[t].size();
            TotalCandidates += PerThreadCandidates[t];
        }

        Manifolds.clear();
        Manifolds.reserve(TotalManifolds);
        for (int t = 0; t < WorkerThreadCount; ++t)
        {
            Manifolds.insert(Manifolds.end(), PerThreadManifolds[t].begin(), PerThreadManifolds[t].end());
        }

        LastStats.CandidatePairCount   = TotalCandidates;
        LastStats.ActualCollisionCount = static_cast<uint64_t>(Manifolds.size());
        QueryPerformanceCounter(&TimerNarrow);

        ResolveCollisions(Spheres, Manifolds);

        const double ToMilliseconds = 1000.0 / static_cast<double>(TimerFrequency.QuadPart);
        LastStats.BroadPhaseTimeMs   = static_cast<double>(TimerBroad.QuadPart - TimerStart.QuadPart) * ToMilliseconds;
        LastStats.NarrowPhaseTimeMs  = static_cast<double>(TimerNarrow.QuadPart - TimerBroad.QuadPart) * ToMilliseconds;
        LastStats.TotalSolveTimeMs   = static_cast<double>(TimerNarrow.QuadPart - TimerStart.QuadPart) * ToMilliseconds;
    }

    const wchar_t* GetName()          const override { return L"UniformGrid (MT)"; }
    const wchar_t* GetAlgorithmName() const override { return L"Uniform Grid"; }
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

    void DoNarrowPhaseChunk(int ThreadIndex)
    {
        if (!CurrentSpheres) return;
        const std::vector<FSphere>& Spheres = *CurrentSpheres;

        struct FOffset { int x, y, z; };
        static const FOffset FORWARD_NEIGHBORS[13] = {
            { +1,  0,  0 },
            { -1, +1,  0 }, {  0, +1,  0 }, { +1, +1,  0 },
            { -1, -1, +1 }, {  0, -1, +1 }, { +1, -1, +1 },
            { -1,  0, +1 }, {  0,  0, +1 }, { +1,  0, +1 },
            { -1, +1, +1 }, {  0, +1, +1 }, { +1, +1, +1 }
        };

        const int DimXY = DimX * DimY;
        const size_t NumActive = ActiveCells.size();
        std::vector<FCollisionManifold>& LocalManifolds = PerThreadManifolds[ThreadIndex];
        uint64_t CandidatePairs = 0;

        for (size_t idx = static_cast<size_t>(ThreadIndex); idx < NumActive; idx += static_cast<size_t>(WorkerThreadCount))
        {
            int CellID = ActiveCells[idx];

            int cz  = CellID / DimXY;
            int rem = CellID % DimXY;
            int cy  = rem / DimX;
            int cx  = rem % DimX;

            for (int i = CellHead[CellID]; i != -1; i = SphereNext[i])
            {
                const FVector3 PosA    = Spheres[i].Center;
                const float    RadiusA = Spheres[i].Radius;

                for (int j = SphereNext[i]; j != -1; j = SphereNext[j])
                {
                    CandidatePairs++;
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

            for (const auto& Offset : FORWARD_NEIGHBORS)
            {
                int nx = cx + Offset.x;
                int ny = cy + Offset.y;
                int nz = cz + Offset.z;

                if (nx < 0 || nx >= DimX || ny < 0 || ny >= DimY || nz < 0 || nz >= DimZ)
                {
                    continue;
                }

                int NCellID = nx + DimX * (ny + DimY * nz);
                if (CellHead[NCellID] == -1)
                {
                    continue;
                }

                for (int i = CellHead[CellID]; i != -1; i = SphereNext[i])
                {
                    const FVector3 PosA    = Spheres[i].Center;
                    const float    RadiusA = Spheres[i].Radius;

                    for (int j = CellHead[NCellID]; j != -1; j = SphereNext[j])
                    {
                        CandidatePairs++;
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
        }

        PerThreadCandidates[ThreadIndex] = CandidatePairs;
    }

private:
    LARGE_INTEGER                   TimerFrequency = {};
    int                             WorkerThreadCount = 4;
    FCollisionStats                 LastStats       = {};
    std::vector<FCollisionManifold> Manifolds;

    std::vector<std::vector<FCollisionManifold>> PerThreadManifolds;
    std::vector<uint64_t>                        PerThreadCandidates;
    const std::vector<FSphere>*                  CurrentSpheres = nullptr;

    std::vector<std::thread> WorkerThreads;
    std::mutex               SyncMutex;
    std::condition_variable  CvStart;
    std::condition_variable  CvDone;
    int                      CurrentIteration     = 0;
    int                      CompletedWorkerCount = 0;
    bool                     bStopWorkers         = false;
};
