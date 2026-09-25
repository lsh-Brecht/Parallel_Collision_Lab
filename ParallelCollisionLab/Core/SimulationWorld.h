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

    double Update(float DeltaTime, bool bIsPaused, int64_t TimerFrequency, const FVector3& PlayerInput = FVector3(0.0f, 0.0f, 0.0f))
    {
        if (bIsPaused)
            return 0.0;

        LARGE_INTEGER t0, t1;
        QueryPerformanceCounter(&t0);

        if (!Spheres.empty() && PlayerInput.LengthSq() > 0.001f)
        {
            ApplyPlayerAcceleration(PlayerInput, DeltaTime);
        }

        for (FSphere& s : Spheres)
        {
            s.Update(DeltaTime, bDampingEnabled,
                     Config::DEFAULT_DAMPING_FACTOR,
                     Config::SLEEP_VELOCITY_THRESHOLD,
                     Config::SLEEP_TIME_REQUIRED,
                     Config::COLOR_SLEEPING,
                     Config::COLOR_LERP_SPEED);
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
        RebuildActiveVisualizer();
    }

    void ToggleSphereCount()
    {
        int count = (Spheres.size() == MAX_SPHERES) ? MIN_SPHERES : MAX_SPHERES;
        Spheres = CreateSpheres(count, BoxHalfSize, bMultiScaleSpheres);
        RebuildActiveVisualizer();
    }

    void ToggleSphereSizeMode()
    {
        bMultiScaleSpheres = !bMultiScaleSpheres;
        Spheres = CreateSpheres(static_cast<int>(Spheres.size()), BoxHalfSize, bMultiScaleSpheres);
        RebuildActiveVisualizer();
    }

    bool IsMultiScaleSpheres()   const { return bMultiScaleSpheres; }
    void ToggleDamping()               { bDampingEnabled = !bDampingEnabled; }
    bool IsDampingEnabled()      const { return bDampingEnabled; }
    size_t GetSleepingSphereCount() const
    {
        size_t count = 0;
        for (const auto& s : Spheres)
        {
            if (s.bIsSleeping) count++;
        }
        return count;
    }

    void PromoteToPlanet(int32_t SphereId, EPlanetType Type)
    {
        if (SphereId < 0 || SphereId >= static_cast<int32_t>(Spheres.size())) return;
        FSphere& s = Spheres[SphereId];
        s.PlanetType = Type;
        float scale = cbrtf((float)MIN_SPHERES) / cbrtf((float)Spheres.size());
        s.Radius = PLANET_BASE_RADIUS * scale;
        s.Mass = s.Radius * s.Radius * s.Radius;
        s.Color = FVector4(1.0f, 1.0f, 1.0f, 1.0f);
        s.BaseColor = s.Color;
        s.WakeUp();
    }

    void DemotePlanet(int32_t SphereId)
    {
        if (SphereId < 0 || SphereId >= static_cast<int32_t>(Spheres.size())) return;
        FSphere& s = Spheres[SphereId];
        s.PlanetType = EPlanetType::None;
        float scale = cbrtf((float)MIN_SPHERES) / cbrtf((float)Spheres.size());
        s.Radius = RandF(0.08f, 0.22f) * scale;
        s.Mass = s.Radius * s.Radius * s.Radius;
        float ColorRand = RandF(0.0f, 340.0f);
        ColorRand = (ColorRand > 230.0f) ? ColorRand + 20.0f : ColorRand;
        s.Color = HSVtoRGB(ColorRand, RandF(0.85f, 1.0f), RandF(0.85f, 1.0f));
        s.BaseColor = s.Color;
        s.WakeUp();
    }

    void ApplySphereAcceleration(int32_t SphereId, const FVector3& InDir, float DeltaTime)
    {
        if (SphereId < 0 || SphereId >= static_cast<int32_t>(Spheres.size())) return;
        FSphere& s = Spheres[SphereId];

        if (InDir.LengthSq() > 0.001f)
        {
            FVector3 dir = InDir.Normalize();
            s.Velocity += dir * (Config::EARTH_ACCELERATION * DeltaTime);
            s.WakeUp();
        }

        float speedSq = s.Velocity.LengthSq();
        if (speedSq > Config::EARTH_MAX_SPEED * Config::EARTH_MAX_SPEED)
        {
            s.Velocity = s.Velocity.Normalize() * Config::EARTH_MAX_SPEED;
        }
    }

    void ApplyPlayerAcceleration(const FVector3& InDir, float DeltaTime)
    {
        ApplySphereAcceleration(0, InDir, DeltaTime);
    }

    void AddSpheres(int Delta)
    {
        int count = (std::min)(static_cast<int>(Spheres.size()) + Delta, MAX_SPHERES);
        Spheres = CreateSpheres(count, BoxHalfSize, bMultiScaleSpheres);
        RebuildActiveVisualizer();
    }

    void SubSpheres(int Delta)
    {
        int count = (std::max)(static_cast<int>(Spheres.size()) - Delta, MIN_SPHERES);
        Spheres = CreateSpheres(count, BoxHalfSize, bMultiScaleSpheres);
        RebuildActiveVisualizer();
    }

    void AdjustThreadCount(int Delta)
    {
        ConfiguredThreads = (std::max)(1, (std::min)(64, ConfiguredThreads + Delta));
        for (auto& s : Solvers)
        {
            s->SetThreadCount(ConfiguredThreads);
        }
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
        return RunBenchmark(Solvers, Spheres, BoxHalfSize);
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
    bool                                           bDampingEnabled     = false;
};
