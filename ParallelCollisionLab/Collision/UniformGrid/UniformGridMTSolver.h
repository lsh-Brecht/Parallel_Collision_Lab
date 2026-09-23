#pragma once

#include <vector>
#include <cmath>
#include <algorithm>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <windows.h>
#include "../ICollisionSolver.h"
#include "../CollisionTypes.h"
#include "../NarrowPhase.h"
#include "../Resolution.h"
#include "IUniformGridVisualizer.h"

//=============================================================================
// UniformGridMTSolver - Multi-threaded Spatial Partitioning Collision Solver
//=============================================================================
class UniformGridMTSolver : public ICollisionSolver, public IUniformGridVisualizer
{
public:
    UniformGridMTSolver(int InThreadCount = 0, float InBoxHalfSize = 2.0f)
        : BoxHalfSize(InBoxHalfSize)
    {
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

    void SetBoxHalfSize(float InBoxHalfSize) { BoxHalfSize = InBoxHalfSize; }

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

    void BuildGrid(const std::vector<FSphere>& Spheres) override
    {
        const int Count = static_cast<int>(Spheres.size());
        if (Count == 0) return;

        float MaxRadius = 0.0f;
        for (int i = 0; i < Count; ++i)
        {
            if (Spheres[i].Radius > MaxRadius)
            {
                MaxRadius = Spheres[i].Radius;
            }
        }

        const float BoxW = BoxHalfSize * 2.0f;

        float TargetCellSize = MaxRadius * 2.0f;
        if (TargetCellSize < 0.05f) TargetCellSize = 0.05f;

        int Dim = static_cast<int>(floorf(BoxW / TargetCellSize));
        if (Dim < 1)  Dim = 1;
        if (Dim > 64) Dim = 64;

        DimX = Dim;
        DimY = Dim;
        DimZ = Dim;

        CellSize = BoxW / static_cast<float>(Dim);

        GridMinX = -BoxHalfSize;
        GridMinY = -BoxHalfSize;
        GridMinZ = -BoxHalfSize;

        int TotalCells = DimX * DimY * DimZ;

        if (static_cast<int>(CellHead.size()) != TotalCells)
        {
            CellHead.assign(TotalCells, -1);
            ActiveCells.clear();
        }
        else
        {
            for (int c : ActiveCells)
            {
                CellHead[c] = -1;
            }
            ActiveCells.clear();
        }

        if (static_cast<int>(SphereNext.size()) < Count)
        {
            SphereNext.resize(Count);
        }

        const float InvCell = 1.0f / CellSize;

        for (int i = 0; i < Count; ++i)
        {
            int cx = static_cast<int>((Spheres[i].Center.x - GridMinX) * InvCell);
            int cy = static_cast<int>((Spheres[i].Center.y - GridMinY) * InvCell);
            int cz = static_cast<int>((Spheres[i].Center.z - GridMinZ) * InvCell);

            if (cx < 0) cx = 0; else if (cx >= DimX) cx = DimX - 1;
            if (cy < 0) cy = 0; else if (cy >= DimY) cy = DimY - 1;
            if (cz < 0) cz = 0; else if (cz >= DimZ) cz = DimZ - 1;

            int CellID = cx + DimX * (cy + DimY * cz);
            if (CellHead[CellID] == -1)
            {
                ActiveCells.push_back(CellID);
            }
            SphereNext[i] = CellHead[CellID];
            CellHead[CellID] = i;
        }
    }

    void Solve(std::vector<FSphere>& Spheres) override
    {
        const int Count = static_cast<int>(Spheres.size());
        LastStats = {};
        if (Count < 2) return;

        LARGE_INTEGER TimerStart, TimerBroad, TimerNarrow, TimerResolve;

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
        QueryPerformanceCounter(&TimerResolve);

        const double ToMilliseconds = 1000.0 / static_cast<double>(TimerFrequency.QuadPart);
        LastStats.BroadPhaseTimeMs   = static_cast<double>(TimerBroad.QuadPart - TimerStart.QuadPart) * ToMilliseconds;
        LastStats.NarrowPhaseTimeMs  = static_cast<double>(TimerNarrow.QuadPart - TimerBroad.QuadPart) * ToMilliseconds;
        LastStats.ResolutionTimeMs   = static_cast<double>(TimerResolve.QuadPart - TimerNarrow.QuadPart) * ToMilliseconds;
        LastStats.TotalSolveTimeMs   = static_cast<double>(TimerResolve.QuadPart - TimerStart.QuadPart) * ToMilliseconds;
    }

    void GenerateActiveCellLines(std::vector<FVertexSimple>& OutLines) const override
    {
        OutLines.clear();
        OutLines.reserve(ActiveCells.size() * 24);

        const int DimXY = DimX * DimY;

        for (int CellID : ActiveCells)
        {
            int cz  = CellID / DimXY;
            int rem = CellID % DimXY;
            int cy  = rem / DimX;
            int cx  = rem % DimX;

            float x0 = GridMinX + static_cast<float>(cx) * CellSize;
            float y0 = GridMinY + static_cast<float>(cy) * CellSize;
            float z0 = GridMinZ + static_cast<float>(cz) * CellSize;
            float x1 = x0 + CellSize;
            float y1 = y0 + CellSize;
            float z1 = z0 + CellSize;

            FVertexSimple v0 = { x0, y0, z0 };
            FVertexSimple v1 = { x1, y0, z0 };
            FVertexSimple v2 = { x1, y1, z0 };
            FVertexSimple v3 = { x0, y1, z0 };
            FVertexSimple v4 = { x0, y0, z1 };
            FVertexSimple v5 = { x1, y0, z1 };
            FVertexSimple v6 = { x1, y1, z1 };
            FVertexSimple v7 = { x0, y1, z1 };

            OutLines.push_back(v0); OutLines.push_back(v1);
            OutLines.push_back(v1); OutLines.push_back(v2);
            OutLines.push_back(v2); OutLines.push_back(v3);
            OutLines.push_back(v3); OutLines.push_back(v0);

            OutLines.push_back(v4); OutLines.push_back(v5);
            OutLines.push_back(v5); OutLines.push_back(v6);
            OutLines.push_back(v6); OutLines.push_back(v7);
            OutLines.push_back(v7); OutLines.push_back(v4);

            OutLines.push_back(v0); OutLines.push_back(v4);
            OutLines.push_back(v1); OutLines.push_back(v5);
            OutLines.push_back(v2); OutLines.push_back(v6);
            OutLines.push_back(v3); OutLines.push_back(v7);
        }
    }

    void GenerateFloorAndWallGridLines(std::vector<FVertexSimple>& OutLines, float InBoxHalfSize = 2.0f) const override
    {
        OutLines.clear();
        if (CellSize <= 0.001f || DimX < 1) return;

        const float Eps = 0.002f;
        const float FloorY = -InBoxHalfSize + Eps;
        const float BackZ  =  InBoxHalfSize - Eps;

        for (int i = 0; i <= DimX; ++i)
        {
            float x = -InBoxHalfSize + static_cast<float>(i) * CellSize;
            OutLines.push_back({ x, FloorY, -InBoxHalfSize });
            OutLines.push_back({ x, FloorY,  InBoxHalfSize });
        }
        for (int k = 0; k <= DimZ; ++k)
        {
            float z = -InBoxHalfSize + static_cast<float>(k) * CellSize;
            OutLines.push_back({ -InBoxHalfSize, FloorY, z });
            OutLines.push_back({  InBoxHalfSize, FloorY, z });
        }

        // Back wall grid (z = InBoxHalfSize)
        for (int i = 0; i <= DimX; ++i)
        {
            float x = -InBoxHalfSize + static_cast<float>(i) * CellSize;
            OutLines.push_back({ x, -InBoxHalfSize, BackZ });
            OutLines.push_back({ x,  InBoxHalfSize, BackZ });
        }
        for (int j = 0; j <= DimY; ++j)
        {
            float y = -InBoxHalfSize + static_cast<float>(j) * CellSize;
            OutLines.push_back({ -InBoxHalfSize, y, BackZ });
            OutLines.push_back({  InBoxHalfSize, y, BackZ });
        }
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

    float BoxHalfSize = 2.0f;
    float GridMinX    = -2.0f;
    float GridMinY    = -2.0f;
    float GridMinZ    = -2.0f;
    float CellSize    = 0.1f;
    int   DimX        = 1;
    int   DimY        = 1;
    int   DimZ        = 1;

    std::vector<int> CellHead;
    std::vector<int> SphereNext;
    std::vector<int> ActiveCells;

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
