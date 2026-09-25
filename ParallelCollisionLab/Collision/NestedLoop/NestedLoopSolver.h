#pragma once

#include <vector>
#include <windows.h>
#include "../ICollisionSolver.h"
#include "../Resolution.h"

//=============================================================================
// NestedLoopSolver - Naive O(n^2) nested loop collision solver
//=============================================================================
class NestedLoopSolver : public ICollisionSolver
{
public:
    NestedLoopSolver()
    {
        QueryPerformanceFrequency(&TimerFrequency);
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

        Manifolds.clear();
        for (int i = 0; i < Count; ++i)
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
                    Manifolds.push_back({ i, j, Normal, RadiusSum - Dist });
                }
            }
        }
        LastStats.ActualCollisionCount = static_cast<uint64_t>(Manifolds.size());
        QueryPerformanceCounter(&TimerNarrow);

        ResolveCollisions(Spheres, Manifolds);

        const double ToMilliseconds = 1000.0 / static_cast<double>(TimerFrequency.QuadPart);
        LastStats.BroadPhaseTimeMs   = static_cast<double>(TimerBroad.QuadPart - TimerStart.QuadPart) * ToMilliseconds;
        LastStats.NarrowPhaseTimeMs  = static_cast<double>(TimerNarrow.QuadPart - TimerBroad.QuadPart) * ToMilliseconds;
        LastStats.TotalSolveTimeMs   = static_cast<double>(TimerNarrow.QuadPart - TimerStart.QuadPart) * ToMilliseconds;
    }

    const wchar_t* GetName()          const override { return L"NestedLoop (ST)"; }
    const wchar_t* GetAlgorithmName() const override { return L"Nested Loop"; }
    const wchar_t* GetExecutionMode() const override { return L"Single Thread"; }
    int            GetThreadCount()   const override { return 1; }

    const FCollisionStats& GetLastStats() const override { return LastStats; }
    const std::vector<FCollisionManifold>& GetManifolds() const { return Manifolds; }

private:
    LARGE_INTEGER                   TimerFrequency = {};
    FCollisionStats                 LastStats      = {};
    std::vector<FCollisionManifold> Manifolds;
};
