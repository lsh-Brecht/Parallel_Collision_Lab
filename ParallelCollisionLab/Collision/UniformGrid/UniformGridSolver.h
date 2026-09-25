#pragma once

#include <vector>
#include <cmath>
#include <algorithm>
#include <windows.h>
#include "../ICollisionSolver.h"
#include "../Resolution.h"
#include "IUniformGridVisualizer.h"

//=============================================================================
// UniformGridSolver - Spatial partitioning grid collision solver (Single Thread)
//=============================================================================
class UniformGridSolver : public ICollisionSolver, public FUniformGridBase
{
public:
    UniformGridSolver(float InBoxHalfSize = 2.0f)
    {
        BoxHalfSize = InBoxHalfSize;
        QueryPerformanceFrequency(&TimerFrequency);
        Manifolds.reserve(512);
        ActiveCells.reserve(1024);
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

        Manifolds.clear();
        uint64_t CandidatePairs = 0;

        struct FOffset { int x, y, z; };
        static const FOffset FORWARD_NEIGHBORS[13] = {
            { +1,  0,  0 },
            { -1, +1,  0 }, {  0, +1,  0 }, { +1, +1,  0 },
            { -1, -1, +1 }, {  0, -1, +1 }, { +1, -1, +1 },
            { -1,  0, +1 }, {  0,  0, +1 }, { +1,  0, +1 },
            { -1, +1, +1 }, {  0, +1, +1 }, { +1, +1, +1 }
        };

        const int DimXY = DimX * DimY;

        for (int CellID : ActiveCells)
        {
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
                        Manifolds.push_back({ i, j, Normal, RadiusSum - Dist });
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
                            Manifolds.push_back({ i, j, Normal, RadiusSum - Dist });
                        }
                    }
                }
            }
        }

        LastStats.CandidatePairCount   = CandidatePairs;
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

    const wchar_t* GetName()          const override { return L"UniformGrid (ST)"; }
    const wchar_t* GetAlgorithmName() const override { return L"Uniform Grid"; }
    const wchar_t* GetExecutionMode() const override { return L"Single Thread"; }
    int            GetThreadCount()   const override { return 1; }

    const FCollisionStats& GetLastStats() const override { return LastStats; }
    const std::vector<FCollisionManifold>& GetManifolds() const { return Manifolds; }

private:
    LARGE_INTEGER                   TimerFrequency = {};
    FCollisionStats                 LastStats      = {};
    std::vector<FCollisionManifold> Manifolds;
};
