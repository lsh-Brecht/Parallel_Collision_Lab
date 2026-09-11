#pragma once

#include <vector>
#include <windows.h>
#include "../ICollisionSolver.h"
#include "../CollisionTypes.h"
#include "../NarrowPhase.h"
#include "../Resolution.h"

//=============================================================================
// NestedLoopSolver - Naive O(n^2) nested loop collision solver
//=============================================================================
class NestedLoopSolver : public ICollisionSolver
{
public:
    NestedLoopSolver()
    {
        QueryPerformanceFrequency(&m_TimerFreq);
    }

    void Solve(std::vector<FSphere>& spheres) override
    {
        const int count = static_cast<int>(spheres.size());
        m_Stats = {};
        if (count < 2) return;

        LARGE_INTEGER t0, t1, t2, t3;

        // Broad Phase
        QueryPerformanceCounter(&t0);
        m_Stats.CandidatePairCount = static_cast<uint64_t>(count) * (count - 1) / 2;
        QueryPerformanceCounter(&t1);

        // Narrow Phase
        m_Manifolds.clear();
        for (int i = 0; i < count; ++i)
        {
            const FVector3 posA = spheres[i].Center;
            const float    radA = spheres[i].Radius;

            for (int j = i + 1; j < count; ++j)
            {
                const FVector3 diff = posA - spheres[j].Center;
                const float distSq  = diff.LengthSq();
                const float radSum  = radA + spheres[j].Radius;

                if (distSq < radSum * radSum)
                {
                    const float dist = sqrtf(distSq);
                    const FVector3 normal = (dist > 1e-6f) ? diff * (1.0f / dist) : FVector3(1.0f, 0.0f, 0.0f);
                    m_Manifolds.push_back({ i, j, normal, radSum - dist });
                }
            }
        }
        m_Stats.ActualCollisionCount = static_cast<uint64_t>(m_Manifolds.size());
        QueryPerformanceCounter(&t2);

        // Resolution
        ResolveCollisions(spheres, m_Manifolds);
        QueryPerformanceCounter(&t3);

        const double toMs = 1000.0 / static_cast<double>(m_TimerFreq.QuadPart);
        m_Stats.BroadPhaseTimeMs   = static_cast<double>(t1.QuadPart - t0.QuadPart) * toMs;
        m_Stats.NarrowPhaseTimeMs  = static_cast<double>(t2.QuadPart - t1.QuadPart) * toMs;
        m_Stats.ResolutionTimeMs   = static_cast<double>(t3.QuadPart - t2.QuadPart) * toMs;
        m_Stats.TotalSolveTimeMs   = static_cast<double>(t3.QuadPart - t0.QuadPart) * toMs;
    }

    const wchar_t* GetName()          const override { return L"NestedLoop (ST)"; }
    const wchar_t* GetAlgorithmName() const override { return L"Nested Loop (Naive)"; }
    const wchar_t* GetExecutionMode() const override { return L"Single Thread"; }
    int            GetThreadCount()   const override { return 1; }

    const FCollisionStats& GetLastStats() const override { return m_Stats; }
    const std::vector<FCollisionManifold>& GetManifolds() const { return m_Manifolds; }

private:
    LARGE_INTEGER                   m_TimerFreq          = {};
    FCollisionStats                 m_Stats              = {};
    std::vector<FCollisionManifold> m_Manifolds;
};
