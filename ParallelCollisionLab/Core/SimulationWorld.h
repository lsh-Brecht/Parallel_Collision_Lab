#pragma once

#include <vector>
#include <memory>
#include <algorithm>
#include <thread>
#include <windows.h>

#include "Sphere.h"
#include "Timer.h"
#include "AppConfig.h"
#include "CPUInfo.h"
#include "../Collision/ICollisionSolver.h"
#include "../Collision/NestedLoop/NestedLoopSolver.h"
#include "../Collision/NestedLoop/NestedLoopMTSolver.h"
#include "../Collision/UniformGrid/UniformGridSolver.h"
#include "../Collision/UniformGrid/UniformGridMTSolver.h"
#include "../Collision/BVH/BVHSolver.h"
#include "../Collision/BVH/BVHMTSolver.h"
#include "../Collision/Benchmark.h"

//=============================================================================
// FSimulationWorld - Manages spheres, physics integration, wall collisions,
// and collision solver lifecycle
//=============================================================================
class FSimulationWorld
{
public:
    void Init(int initialSpheres, float boxHalfSize)
    {
        m_BoxHalfSize = boxHalfSize;

        int hwThreads = static_cast<int>(std::thread::hardware_concurrency());
        m_ConfiguredThreads = (hwThreads > 0) ? hwThreads : 4;

        m_Solvers.clear();
        m_Solvers.push_back(std::make_unique<NestedLoopSolver>());
        m_Solvers.push_back(std::make_unique<NestedLoopMTSolver>(m_ConfiguredThreads));
        m_Solvers.push_back(std::make_unique<UniformGridSolver>(m_BoxHalfSize));
        m_Solvers.push_back(std::make_unique<UniformGridMTSolver>(m_ConfiguredThreads, m_BoxHalfSize));
        m_Solvers.push_back(std::make_unique<BVHSolver>());
        m_Solvers.push_back(std::make_unique<BVHMTSolver>(m_ConfiguredThreads));
        m_CurrentSolverIdx = 0;

        m_Spheres = CreateSpheres(initialSpheres, m_BoxHalfSize);
        RebuildActiveVisualizer();
    }

    double Update(float dt, bool bPaused, int64_t timerFreq)
    {
        if (bPaused)
            return 0.0;

        LARGE_INTEGER t0, t1;
        QueryPerformanceCounter(&t0);

        for (FSphere& s : m_Spheres)
        {
            s.Update(dt);
            s.BoxCollisionCheck(m_BoxHalfSize);
        }

        ICollisionSolver* activeSolver = GetActiveSolver();
        if (activeSolver)
        {
            activeSolver->Solve(m_Spheres);
        }

        QueryPerformanceCounter(&t1);
        return FTimer::GetElapsedMs(t0, t1, timerFreq);
    }

    void ResetSpheres()
    {
        m_Spheres = CreateSpheres(static_cast<int>(m_Spheres.size()), m_BoxHalfSize, m_bMultiScaleSpheres);
        m_BenchmarkReport.bValid = false;
        RebuildActiveVisualizer();
    }

    void ToggleSphereCount()
    {
        int count = (m_Spheres.size() == MAX_SPHERES) ? MIN_SPHERES : MAX_SPHERES;
        m_Spheres = CreateSpheres(count, m_BoxHalfSize, m_bMultiScaleSpheres);
        m_BenchmarkReport.bValid = false;
        RebuildActiveVisualizer();
    }

    void ToggleSphereSizeMode()
    {
        m_bMultiScaleSpheres = !m_bMultiScaleSpheres;
        m_Spheres = CreateSpheres(static_cast<int>(m_Spheres.size()), m_BoxHalfSize, m_bMultiScaleSpheres);
        m_BenchmarkReport.bValid = false;
        RebuildActiveVisualizer();
    }

    bool IsMultiScaleSpheres() const { return m_bMultiScaleSpheres; }

    void AddSpheres(int delta)
    {
        int count = (std::min)(static_cast<int>(m_Spheres.size()) + delta, MAX_SPHERES);
        m_Spheres = CreateSpheres(count, m_BoxHalfSize, m_bMultiScaleSpheres);
        m_BenchmarkReport.bValid = false;
        RebuildActiveVisualizer();
    }

    void SubSpheres(int delta)
    {
        int count = (std::max)(static_cast<int>(m_Spheres.size()) - delta, MIN_SPHERES);
        m_Spheres = CreateSpheres(count, m_BoxHalfSize, m_bMultiScaleSpheres);
        m_BenchmarkReport.bValid = false;
        RebuildActiveVisualizer();
    }

    void AdjustThreadCount(int delta)
    {
        m_ConfiguredThreads = (std::max)(1, (std::min)(64, m_ConfiguredThreads + delta));
        for (auto& s : m_Solvers)
        {
            s->SetThreadCount(m_ConfiguredThreads);
        }
        m_BenchmarkReport.bValid = false;
    }

    void SelectSolver(size_t idx)
    {
        if (idx < m_Solvers.size())
        {
            m_CurrentSolverIdx = idx;
            RebuildActiveVisualizer();
        }
    }

    void CycleSolver()
    {
        if (!m_Solvers.empty())
        {
            m_CurrentSolverIdx = (m_CurrentSolverIdx + 1) % m_Solvers.size();
            RebuildActiveVisualizer();
        }
    }

    void RebuildActiveVisualizer()
    {
        ICollisionSolver* activeSolver = GetActiveSolver();
        if (auto* bvh = dynamic_cast<IBVHVisualizer*>(activeSolver))
        {
            bvh->BuildBVH(m_Spheres);
        }
        else if (auto* grid = dynamic_cast<IUniformGridVisualizer*>(activeSolver))
        {
            grid->BuildGrid(m_Spheres);
        }
    }

    FBenchmarkReport RunBenchmarkSuite()
    {
        m_BenchmarkReport = RunBenchmark(m_Solvers, m_Spheres, m_BoxHalfSize);
        return m_BenchmarkReport;
    }

    const std::vector<FSphere>& GetSpheres() const { return m_Spheres; }
    std::vector<FSphere>&       GetSpheres()       { return m_Spheres; }
    int                         GetSphereCount() const { return static_cast<int>(m_Spheres.size()); }
    int                         GetThreadCount() const { return m_ConfiguredThreads; }
    float                       GetBoxHalfSize() const { return m_BoxHalfSize; }

    ICollisionSolver* GetActiveSolver() const
    {
        return (m_CurrentSolverIdx < m_Solvers.size()) ? m_Solvers[m_CurrentSolverIdx].get() : nullptr;
    }

    const std::vector<std::unique_ptr<ICollisionSolver>>& GetSolvers() const
    {
        return m_Solvers;
    }

private:
    std::vector<FSphere>                           m_Spheres;
    std::vector<std::unique_ptr<ICollisionSolver>> m_Solvers;
    size_t                                         m_CurrentSolverIdx  = 0;
    int                                            m_ConfiguredThreads = 4;
    float                                          m_BoxHalfSize       = 2.0f;
    bool                                           m_bMultiScaleSpheres = false;
    FBenchmarkReport                               m_BenchmarkReport;
};
