#pragma once

#include <vector>
#include <windows.h>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <algorithm>
#include <cmath>
#include "../ICollisionSolver.h"
#include "../CollisionTypes.h"
#include "../NarrowPhase.h"
#include "../Resolution.h"

//=============================================================================
// NestedLoopMTSolver - Multi-threaded Naive O(n^2) collision solver
//=============================================================================
class NestedLoopMTSolver : public ICollisionSolver
{
public:
    NestedLoopMTSolver(int threadCount = 0)
    {
        QueryPerformanceFrequency(&m_TimerFreq);

        unsigned int hwThreads = std::thread::hardware_concurrency();
        int initialCount = (threadCount > 0) ? threadCount : (hwThreads > 0 ? static_cast<int>(hwThreads) : 4);
        InitThreadPool(initialCount);
    }

    ~NestedLoopMTSolver() override
    {
        ShutdownThreadPool();
    }

    void SetThreadCount(int threadCount) override
    {
        if (threadCount <= 0 || threadCount == m_ThreadCount)
            return;

        ShutdownThreadPool();
        InitThreadPool(threadCount);
    }

    void InitThreadPool(int threadCount)
    {
        m_ThreadCount = threadCount;
        m_ThreadManifolds.clear();
        m_ThreadManifolds.resize(m_ThreadCount);
        for (auto& vec : m_ThreadManifolds)
        {
            vec.reserve(256);
        }

        m_Stop = false;
        m_Iteration = 0;
        m_CompletedCount = 0;
        m_Workers.clear();

        // Spawn background worker threads (main thread acts as worker 0)
        for (int t = 1; t < m_ThreadCount; ++t)
        {
            m_Workers.emplace_back(&NestedLoopMTSolver::WorkerLoop, this, t);
        }
    }

    void ShutdownThreadPool()
    {
        {
            std::unique_lock<std::mutex> lock(m_Mutex);
            m_Stop = true;
        }
        m_CvStart.notify_all();

        for (std::thread& worker : m_Workers)
        {
            if (worker.joinable())
            {
                worker.join();
            }
        }
        m_Workers.clear();
    }

    void Solve(std::vector<FSphere>& spheres) override
    {
        const int count = static_cast<int>(spheres.size());
        m_Stats = {};
        if (count < 2) return;

        LARGE_INTEGER t0, t1, t2, t3;

        QueryPerformanceCounter(&t0);
        m_Stats.CandidatePairCount = static_cast<uint64_t>(count) * (count - 1) / 2;
        QueryPerformanceCounter(&t1);

        m_CurrentSpheres = &spheres;
        for (auto& vec : m_ThreadManifolds)
        {
            vec.clear();
        }

        if (m_Workers.empty())
        {
            DoNarrowPhaseChunk(0);
        }
        else
        {
            {
                std::unique_lock<std::mutex> lock(m_Mutex);
                m_CompletedCount = 0;
                m_Iteration++;
            }
            m_CvStart.notify_all();

            DoNarrowPhaseChunk(0);

            {
                std::unique_lock<std::mutex> lock(m_Mutex);
                m_CvDone.wait(lock, [&]() {
                    return m_CompletedCount >= static_cast<int>(m_Workers.size());
                });
            }
        }

        size_t totalManifolds = 0;
        for (int t = 0; t < m_ThreadCount; ++t)
        {
            totalManifolds += m_ThreadManifolds[t].size();
        }
        m_Manifolds.clear();
        m_Manifolds.reserve(totalManifolds);
        for (int t = 0; t < m_ThreadCount; ++t)
        {
            m_Manifolds.insert(m_Manifolds.end(), m_ThreadManifolds[t].begin(), m_ThreadManifolds[t].end());
        }
        m_Stats.ActualCollisionCount = static_cast<uint64_t>(m_Manifolds.size());
        QueryPerformanceCounter(&t2);

        ResolveCollisions(spheres, m_Manifolds);
        QueryPerformanceCounter(&t3);

        const double toMs = 1000.0 / static_cast<double>(m_TimerFreq.QuadPart);
        m_Stats.BroadPhaseTimeMs   = static_cast<double>(t1.QuadPart - t0.QuadPart) * toMs;
        m_Stats.NarrowPhaseTimeMs  = static_cast<double>(t2.QuadPart - t1.QuadPart) * toMs;
        m_Stats.ResolutionTimeMs   = static_cast<double>(t3.QuadPart - t2.QuadPart) * toMs;
        m_Stats.TotalSolveTimeMs   = static_cast<double>(t3.QuadPart - t0.QuadPart) * toMs;
    }

    const wchar_t* GetName()          const override { return L"NestedLoop (MT)"; }
    const wchar_t* GetAlgorithmName() const override { return L"Nested Loop (Naive)"; }
    const wchar_t* GetExecutionMode() const override { return L"Multi Thread"; }
    int            GetThreadCount()   const override { return m_ThreadCount; }

    const FCollisionStats& GetLastStats() const override { return m_Stats; }
    const std::vector<FCollisionManifold>& GetManifolds() const { return m_Manifolds; }

private:
    void WorkerLoop(int threadIdx)
    {
        int lastIteration = 0;
        while (true)
        {
            {
                std::unique_lock<std::mutex> lock(m_Mutex);
                m_CvStart.wait(lock, [&]() {
                    return m_Stop || (m_Iteration > lastIteration);
                });

                if (m_Stop) break;
                lastIteration = m_Iteration;
            }

            DoNarrowPhaseChunk(threadIdx);

            {
                std::unique_lock<std::mutex> lock(m_Mutex);
                m_CompletedCount++;
                if (m_CompletedCount == static_cast<int>(m_Workers.size()))
                {
                    m_CvDone.notify_one();
                }
            }
        }
    }

    static int GetSplitIndex(int count, int t, int T)
    {
        if (t <= 0) return 0;
        if (t >= T) return count;
        double fraction = static_cast<double>(t) / static_cast<double>(T);
        double root = std::sqrt(1.0 - fraction);
        int idx = static_cast<int>(std::round(count * (1.0 - root)));
        if (idx < 0) idx = 0;
        if (idx > count) idx = count;
        return idx;
    }

    void DoNarrowPhaseChunk(int threadIdx)
    {
        if (!m_CurrentSpheres) return;
        const std::vector<FSphere>& spheres = *m_CurrentSpheres;
        const int count = static_cast<int>(spheres.size());

        const int startI = GetSplitIndex(count, threadIdx, m_ThreadCount);
        const int endI   = GetSplitIndex(count, threadIdx + 1, m_ThreadCount);

        std::vector<FCollisionManifold>& localManifolds = m_ThreadManifolds[threadIdx];

        for (int i = startI; i < endI; ++i)
        {
            const FVector3 posA = spheres[i].Center;
            const float    radA = spheres[i].Radius;

            for (int j = i + 1; j < count; ++j)
            {
                const FVector3 diff   = posA - spheres[j].Center;
                const float    distSq = diff.LengthSq();
                const float    radSum = radA + spheres[j].Radius;

                if (distSq < radSum * radSum)
                {
                    const float dist = sqrtf(distSq);
                    const FVector3 normal = (dist > 1e-6f) ? diff * (1.0f / dist) : FVector3(1.0f, 0.0f, 0.0f);
                    localManifolds.push_back({ i, j, normal, radSum - dist });
                }
            }
        }
    }

private:
    LARGE_INTEGER                   m_TimerFreq      = {};
    int                             m_ThreadCount    = 4;
    FCollisionStats                 m_Stats          = {};
    std::vector<FCollisionManifold> m_Manifolds;

    std::vector<std::vector<FCollisionManifold>> m_ThreadManifolds;
    const std::vector<FSphere>*     m_CurrentSpheres = nullptr;

    std::vector<std::thread>        m_Workers;
    std::mutex                      m_Mutex;
    std::condition_variable         m_CvStart;
    std::condition_variable         m_CvDone;
    int                             m_Iteration      = 0;
    int                             m_CompletedCount = 0;
    bool                            m_Stop           = false;
};
