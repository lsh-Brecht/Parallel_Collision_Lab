#pragma once

#include <vector>
#include "../ICollisionSolver.h"
#include "../CollisionTypes.h"
#include "../NestedLoop/NestedLoopSolver.h"

//=============================================================================
// UniformGridSolver - Spatial partitioning grid solver (Stub for Step 3)
//=============================================================================
class UniformGridSolver : public ICollisionSolver
{
public:
    void Solve(std::vector<FSphere>& spheres) override
    {
        // Fallback to ST implementation until Grid implementation
        m_FallbackSolver.Solve(spheres);
        m_Stats = m_FallbackSolver.GetLastStats();
    }

    const wchar_t* GetName()          const override { return L"UniformGrid (ST)"; }
    const wchar_t* GetAlgorithmName() const override { return L"Uniform Grid"; }
    const wchar_t* GetExecutionMode() const override { return L"Single Thread (Pending)"; }
    int            GetThreadCount()   const override { return 1; }

    const FCollisionStats& GetLastStats() const override { return m_Stats; }

private:
    NestedLoopSolver m_FallbackSolver;
    FCollisionStats  m_Stats;
};
