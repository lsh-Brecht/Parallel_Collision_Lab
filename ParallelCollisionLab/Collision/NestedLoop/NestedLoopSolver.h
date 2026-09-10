#pragma once

#include <vector>
#include "../ICollisionSolver.h"
#include "../CollisionTypes.h"
#include "../NarrowPhase.h"
#include "../Resolution.h"

//=============================================================================
// NestedLoopSolver - Naive O(n^2) nested loop collision solver
// Pipeline: Broad Phase -> Narrow Phase -> Resolution
//=============================================================================
class NestedLoopSolver : public ICollisionSolver
{
public:
    void Solve(std::vector<FSphere>& spheres) override
    {
        const int count = static_cast<int>(spheres.size());
        if (count < 2) return;

        // --------------------------------------------------------------------
        // 1. Broad Phase: Generate all possible sphere pairs (O(N^2))
        // --------------------------------------------------------------------
        m_CandidatePairs.clear();
        m_CandidatePairs.reserve(count * (count - 1) / 2);

        for (int i = 0; i < count; ++i)
        {
            for (int j = i + 1; j < count; ++j)
            {
                m_CandidatePairs.push_back({ i, j });
            }
        }

        // --------------------------------------------------------------------
        // 2. Narrow Phase: Perform precise sphere-sphere collision checks
        // --------------------------------------------------------------------
        m_Manifolds.clear();
        m_Manifolds.reserve(m_CandidatePairs.size() / 8);

        for (const FCollisionPair& pair : m_CandidatePairs)
        {
            FCollisionManifold manifold;
            if (CheckSphereSphere(spheres[pair.IndexA], spheres[pair.IndexB], pair.IndexA, pair.IndexB, manifold))
            {
                m_Manifolds.push_back(manifold);
            }
        }

        // --------------------------------------------------------------------
        // 3. Resolution: Apply velocity impulse and position correction
        // --------------------------------------------------------------------
        ResolveCollisions(spheres, m_Manifolds);
    }

    const wchar_t* GetName() const override { return L"NestedLoop (ST)"; }

    // Accessors for metrics & profiling
    const std::vector<FCollisionPair>& GetCandidatePairs() const { return m_CandidatePairs; }
    const std::vector<FCollisionManifold>& GetManifolds() const { return m_Manifolds; }

private:
    std::vector<FCollisionPair>     m_CandidatePairs;
    std::vector<FCollisionManifold> m_Manifolds;
};
