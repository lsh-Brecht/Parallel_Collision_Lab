#pragma once

#include <vector>
#include "../ICollisionSolver.h"
#include "../CollisionTypes.h"
#include "../NarrowPhase.h"
#include "../Resolution.h"

//=============================================================================
// NestedLoopSolver - Naive O(n^2) nested loop collision solver
// Optimized: Zero-allocation pair traversal with L1/L2 cache-friendly streaming.
// Decouples detection and resolution without memory bandwidth thrashing.
//=============================================================================
class NestedLoopSolver : public ICollisionSolver
{
public:
    void Solve(std::vector<FSphere>& spheres) override
    {
        const int count = static_cast<int>(spheres.size());
        if (count < 2) return;

        // --------------------------------------------------------------------
        // 1. Broad Phase: Mathematical pair count (N * (N - 1) / 2)
        // Avoid materializing all pairs into memory, which would allocate 4+ MB
        // per frame, evicting the entire L1/L2 CPU cache and stalling memory bus.
        // --------------------------------------------------------------------
        m_CandidatePairCount = static_cast<uint64_t>(count) * (count - 1) / 2;

        // --------------------------------------------------------------------
        // 2. Narrow Phase: Cache-friendly collision detection
        // Stream spheres array with outer sphere cached in CPU registers.
        // Only actual colliding manifolds are collected for resolution.
        // --------------------------------------------------------------------
        m_Manifolds.clear();

        for (int i = 0; i < count; ++i)
        {
            // Cache sphere A in CPU registers for the entire inner loop
            const FVector3 posA = spheres[i].Center;
            const float    radA = spheres[i].Radius;

            for (int j = i + 1; j < count; ++j)
            {
                const FVector3 diff = posA - spheres[j].Center;
                const float distSq  = diff.LengthSq();
                const float radSum  = radA + spheres[j].Radius;

                // Squared distance fast rejection avoids expensive sqrtf
                if (distSq < radSum * radSum)
                {
                    const float dist = sqrtf(distSq);
                    const FVector3 normal = (dist > 1e-6f) ? diff * (1.0f / dist) : FVector3(1.0f, 0.0f, 0.0f);
                    m_Manifolds.push_back({ i, j, normal, radSum - dist });
                }
            }
        }

        // --------------------------------------------------------------------
        // 3. Resolution: Deterministic, sequential impulse & position updates
        // --------------------------------------------------------------------
        ResolveCollisions(spheres, m_Manifolds);
    }

    const wchar_t* GetName() const override { return L"NestedLoop (ST)"; }

    // Accessors for metrics & profiling
    uint64_t GetCandidatePairCount() const { return m_CandidatePairCount; }
    const std::vector<FCollisionManifold>& GetManifolds() const { return m_Manifolds; }

private:
    uint64_t                        m_CandidatePairCount = 0;
    std::vector<FCollisionManifold> m_Manifolds;
};
