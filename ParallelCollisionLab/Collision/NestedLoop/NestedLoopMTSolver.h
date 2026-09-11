#pragma once

#include <vector>
#include <windows.h>
#include <thread>
#include "../ICollisionSolver.h"
#include "../CollisionTypes.h"
#include "NestedLoopSolver.h"

//=============================================================================
// NestedLoopMTSolver - Multi-threaded Naive O(n^2) solver (Stub for Step 2)
//=============================================================================
class NestedLoopMTSolver : public ICollisionSolver
{
public:
    NestedLoopMTSolver(int threadCount = 0)
    {
        unsigned int hwThreads = std::thread::hardware_concurrency();
        m_ThreadCount = (threadCount > 0) ? threadCount : (hwThreads > 0 ? static_cast<int>(hwThreads) : 4);
    }

    void Solve(std::vector<FSphere>& spheres) override
    {
        // Fallback to ST implementation until MT implementation in Step 2
        m_FallbackSolver.Solve(spheres);
        m_Stats = m_FallbackSolver.GetLastStats();
    }

    const wchar_t* GetName()          const override { return L"NestedLoop (MT)"; }
    const wchar_t* GetAlgorithmName() const override { return L"Nested Loop (Naive)"; }
    const wchar_t* GetExecutionMode() const override { return L"Multi Thread (Pending)"; }
    int            GetThreadCount()   const override { return m_ThreadCount; }

    const FCollisionStats& GetLastStats() const override { return m_Stats; }

private:
    int              m_ThreadCount = 4;
    NestedLoopSolver m_FallbackSolver;
    FCollisionStats  m_Stats;
};
