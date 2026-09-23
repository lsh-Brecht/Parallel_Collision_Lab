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
    void Init(int InitialSpheres, float InBoxHalfSize)
    {
        BoxHalfSize = InBoxHalfSize;

        int hwThreads = static_cast<int>(std::thread::hardware_concurrency());
        ConfiguredThreads = (hwThreads > 0) ? hwThreads : 4;

        Solvers.clear();
        Solvers.push_back(std::make_unique<NestedLoopSolver>());
        Solvers.push_back(std::make_unique<NestedLoopMTSolver>(ConfiguredThreads));
        Solvers.push_back(std::make_unique<UniformGridSolver>(BoxHalfSize));
        Solvers.push_back(std::make_unique<UniformGridMTSolver>(ConfiguredThreads, BoxHalfSize));
        Solvers.push_back(std::make_unique<BVHSolver>());
        Solvers.push_back(std::make_unique<BVHMTSolver>(ConfiguredThreads));
        ActiveSolverIndex = 0;

        Spheres = CreateSpheres(InitialSpheres, BoxHalfSize);
        RebuildActiveVisualizer();
    }

    double Update(float DeltaTime, bool bIsPaused, int64_t TimerFrequency)
    {
        if (bIsPaused)
            return 0.0;

        LARGE_INTEGER t0, t1;
        QueryPerformanceCounter(&t0);

        for (FSphere& s : Spheres)
        {
            s.Update(DeltaTime);
            s.BoxCollisionCheck(BoxHalfSize);
        }

        ICollisionSolver* activeSolver = GetActiveSolver();
        if (activeSolver)
        {
            activeSolver->Solve(Spheres);
        }

        for (FSphere& s : Spheres)
        {
            s.BoxCollisionCheck(BoxHalfSize);
        }

        QueryPerformanceCounter(&t1);
        return FTimer::GetElapsedMs(t0, t1, TimerFrequency);
    }

    void ResetSpheres()
    {
        Spheres = CreateSpheres(static_cast<int>(Spheres.size()), BoxHalfSize, bMultiScaleSpheres);
        BenchmarkReport.bValid = false;
        RebuildActiveVisualizer();
    }

    void ToggleSphereCount()
    {
        int count = (Spheres.size() == MAX_SPHERES) ? MIN_SPHERES : MAX_SPHERES;
        Spheres = CreateSpheres(count, BoxHalfSize, bMultiScaleSpheres);
        BenchmarkReport.bValid = false;
        RebuildActiveVisualizer();
    }

    void ToggleSphereSizeMode()
    {
        bMultiScaleSpheres = !bMultiScaleSpheres;
        Spheres = CreateSpheres(static_cast<int>(Spheres.size()), BoxHalfSize, bMultiScaleSpheres);
        BenchmarkReport.bValid = false;
        RebuildActiveVisualizer();
    }

    bool IsMultiScaleSpheres() const { return bMultiScaleSpheres; }

    void AddSpheres(int Delta)
    {
        int count = (std::min)(static_cast<int>(Spheres.size()) + Delta, MAX_SPHERES);
        Spheres = CreateSpheres(count, BoxHalfSize, bMultiScaleSpheres);
        BenchmarkReport.bValid = false;
        RebuildActiveVisualizer();
    }

    void SubSpheres(int Delta)
    {
        int count = (std::max)(static_cast<int>(Spheres.size()) - Delta, MIN_SPHERES);
        Spheres = CreateSpheres(count, BoxHalfSize, bMultiScaleSpheres);
        BenchmarkReport.bValid = false;
        RebuildActiveVisualizer();
    }

    void AdjustThreadCount(int Delta)
    {
        ConfiguredThreads = (std::max)(1, (std::min)(64, ConfiguredThreads + Delta));
        for (auto& s : Solvers)
        {
            s->SetThreadCount(ConfiguredThreads);
        }
        BenchmarkReport.bValid = false;
    }

    void SelectSolver(size_t InIndex)
    {
        if (InIndex < Solvers.size())
        {
            ActiveSolverIndex = InIndex;
            RebuildActiveVisualizer();
        }
    }

    void CycleSolver()
    {
        if (!Solvers.empty())
        {
            ActiveSolverIndex = (ActiveSolverIndex + 1) % Solvers.size();
            RebuildActiveVisualizer();
        }
    }

    void RebuildActiveVisualizer()
    {
        ICollisionSolver* activeSolver = GetActiveSolver();
        if (auto* bvh = dynamic_cast<IBVHVisualizer*>(activeSolver))
        {
            bvh->BuildBVH(Spheres);
        }
        else if (auto* grid = dynamic_cast<IUniformGridVisualizer*>(activeSolver))
        {
            grid->BuildGrid(Spheres);
        }
    }

    FBenchmarkReport RunBenchmarkSuite()
    {
        BenchmarkReport = RunBenchmark(Solvers, Spheres, BoxHalfSize);
        return BenchmarkReport;
    }

    const std::vector<FSphere>& GetSpheres() const { return Spheres; }
    std::vector<FSphere>&       GetSpheres()       { return Spheres; }
    int                         GetSphereCount() const { return static_cast<int>(Spheres.size()); }
    int                         GetThreadCount() const { return ConfiguredThreads; }
    float                       GetBoxHalfSize() const { return BoxHalfSize; }

    ICollisionSolver* GetActiveSolver() const
    {
        return (ActiveSolverIndex < Solvers.size()) ? Solvers[ActiveSolverIndex].get() : nullptr;
    }

    const std::vector<std::unique_ptr<ICollisionSolver>>& GetSolvers() const
    {
        return Solvers;
    }

private:
    std::vector<FSphere>                           Spheres;
    std::vector<std::unique_ptr<ICollisionSolver>> Solvers;
    size_t                                         ActiveSolverIndex   = 0;
    int                                            ConfiguredThreads   = 4;
    float                                          BoxHalfSize         = 2.0f;
    bool                                           bMultiScaleSpheres  = false;
    FBenchmarkReport                               BenchmarkReport;
};
