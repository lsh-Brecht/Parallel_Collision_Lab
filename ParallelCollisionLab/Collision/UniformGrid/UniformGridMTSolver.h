#pragma once

#include <vector>
#include <cmath>
#include <algorithm>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <windows.h>
#include "../ICollisionSolver.h"
#include "../CollisionTypes.h"
#include "../NarrowPhase.h"
#include "../Resolution.h"
#include "IUniformGridVisualizer.h"

//=============================================================================
// UniformGridMTSolver - Multi-threaded Spatial Partitioning Collision Solver
//=============================================================================
class UniformGridMTSolver : public ICollisionSolver, public IUniformGridVisualizer
{
public:
    UniformGridMTSolver(int threadCount = 0, float boxHalfSize = 2.0f)
        : m_BoxHalfSize(boxHalfSize)
    {
        QueryPerformanceFrequency(&m_TimerFreq);
        m_Manifolds.reserve(512);
        m_ActiveCells.reserve(1024);

        unsigned int hwThreads = std::thread::hardware_concurrency();
        int initialCount = (threadCount > 0) ? threadCount : (hwThreads > 0 ? static_cast<int>(hwThreads) : 4);
        InitThreadPool(initialCount);
    }

    ~UniformGridMTSolver() override
    {
        ShutdownThreadPool();
    }

    void SetBoxHalfSize(float boxHalfSize) { m_BoxHalfSize = boxHalfSize; }

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
            vec.reserve(512);
        }

        m_ThreadCandidates.assign(m_ThreadCount, 0);

        m_Stop           = false;
        m_Iteration      = 0;
        m_CompletedCount = 0;
        m_Workers.clear();

        for (int t = 1; t < m_ThreadCount; ++t)
        {
            m_Workers.emplace_back(&UniformGridMTSolver::WorkerLoop, this, t);
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

    void BuildGrid(const std::vector<FSphere>& spheres) override
    {
        const int count = static_cast<int>(spheres.size());
        if (count == 0) return;

        float maxRadius = 0.0f;
        for (int i = 0; i < count; ++i)
        {
            if (spheres[i].Radius > maxRadius)
            {
                maxRadius = spheres[i].Radius;
            }
        }

        const float boxW = m_BoxHalfSize * 2.0f;

        float targetCellSize = maxRadius * 2.0f;
        if (targetCellSize < 0.05f) targetCellSize = 0.05f;

        int dim = static_cast<int>(floorf(boxW / targetCellSize));
        if (dim < 1)  dim = 1;
        if (dim > 64) dim = 64;

        m_DimX = dim;
        m_DimY = dim;
        m_DimZ = dim;

        m_CellSize = boxW / static_cast<float>(dim);

        m_MinX = -m_BoxHalfSize;
        m_MinY = -m_BoxHalfSize;
        m_MinZ = -m_BoxHalfSize;

        int totalCells = m_DimX * m_DimY * m_DimZ;

        if (static_cast<int>(m_CellHead.size()) != totalCells)
        {
            m_CellHead.assign(totalCells, -1);
            m_ActiveCells.clear();
        }
        else
        {
            for (int c : m_ActiveCells)
            {
                m_CellHead[c] = -1;
            }
            m_ActiveCells.clear();
        }

        if (static_cast<int>(m_SphereNext.size()) < count)
        {
            m_SphereNext.resize(count);
        }

        const float invCell = 1.0f / m_CellSize;

        for (int i = 0; i < count; ++i)
        {
            int cx = static_cast<int>((spheres[i].Center.x - m_MinX) * invCell);
            int cy = static_cast<int>((spheres[i].Center.y - m_MinY) * invCell);
            int cz = static_cast<int>((spheres[i].Center.z - m_MinZ) * invCell);

            if (cx < 0) cx = 0; else if (cx >= m_DimX) cx = m_DimX - 1;
            if (cy < 0) cy = 0; else if (cy >= m_DimY) cy = m_DimY - 1;
            if (cz < 0) cz = 0; else if (cz >= m_DimZ) cz = m_DimZ - 1;

            int cellID = cx + m_DimX * (cy + m_DimY * cz);
            if (m_CellHead[cellID] == -1)
            {
                m_ActiveCells.push_back(cellID);
            }
            m_SphereNext[i] = m_CellHead[cellID];
            m_CellHead[cellID] = i;
        }
    }

    void Solve(std::vector<FSphere>& spheres) override
    {
        const int count = static_cast<int>(spheres.size());
        m_Stats = {};
        if (count < 2) return;

        LARGE_INTEGER t0, t1, t2, t3;

        QueryPerformanceCounter(&t0);
        BuildGrid(spheres);
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
        uint64_t totalCandidates = 0;
        for (int t = 0; t < m_ThreadCount; ++t)
        {
            totalManifolds  += m_ThreadManifolds[t].size();
            totalCandidates += m_ThreadCandidates[t];
        }

        m_Manifolds.clear();
        m_Manifolds.reserve(totalManifolds);
        for (int t = 0; t < m_ThreadCount; ++t)
        {
            m_Manifolds.insert(m_Manifolds.end(), m_ThreadManifolds[t].begin(), m_ThreadManifolds[t].end());
        }

        m_Stats.CandidatePairCount   = totalCandidates;
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

    void GenerateActiveCellLines(std::vector<FVertexSimple>& outLines) const override
    {
        outLines.clear();
        outLines.reserve(m_ActiveCells.size() * 24);

        const int dimXY = m_DimX * m_DimY;

        for (int cellID : m_ActiveCells)
        {
            int cz  = cellID / dimXY;
            int rem = cellID % dimXY;
            int cy  = rem / m_DimX;
            int cx  = rem % m_DimX;

            float x0 = m_MinX + static_cast<float>(cx) * m_CellSize;
            float y0 = m_MinY + static_cast<float>(cy) * m_CellSize;
            float z0 = m_MinZ + static_cast<float>(cz) * m_CellSize;
            float x1 = x0 + m_CellSize;
            float y1 = y0 + m_CellSize;
            float z1 = z0 + m_CellSize;

            FVertexSimple v0 = { x0, y0, z0 };
            FVertexSimple v1 = { x1, y0, z0 };
            FVertexSimple v2 = { x1, y1, z0 };
            FVertexSimple v3 = { x0, y1, z0 };
            FVertexSimple v4 = { x0, y0, z1 };
            FVertexSimple v5 = { x1, y0, z1 };
            FVertexSimple v6 = { x1, y1, z1 };
            FVertexSimple v7 = { x0, y1, z1 };

            outLines.push_back(v0); outLines.push_back(v1);
            outLines.push_back(v1); outLines.push_back(v2);
            outLines.push_back(v2); outLines.push_back(v3);
            outLines.push_back(v3); outLines.push_back(v0);

            outLines.push_back(v4); outLines.push_back(v5);
            outLines.push_back(v5); outLines.push_back(v6);
            outLines.push_back(v6); outLines.push_back(v7);
            outLines.push_back(v7); outLines.push_back(v4);

            outLines.push_back(v0); outLines.push_back(v4);
            outLines.push_back(v1); outLines.push_back(v5);
            outLines.push_back(v2); outLines.push_back(v6);
            outLines.push_back(v3); outLines.push_back(v7);
        }
    }

    void GenerateFloorAndWallGridLines(std::vector<FVertexSimple>& outLines, float L = 2.0f) const override
    {
        outLines.clear();
        if (m_CellSize <= 0.001f || m_DimX < 1) return;

        const float eps = 0.002f;
        const float floorY = -L + eps;
        const float backZ  =  L - eps;

        for (int i = 0; i <= m_DimX; ++i)
        {
            float x = -L + static_cast<float>(i) * m_CellSize;
            outLines.push_back({ x, floorY, -L });
            outLines.push_back({ x, floorY,  L });
        }
        for (int k = 0; k <= m_DimZ; ++k)
        {
            float z = -L + static_cast<float>(k) * m_CellSize;
            outLines.push_back({ -L, floorY, z });
            outLines.push_back({  L, floorY, z });
        }

        // Back wall grid (z = L)
        for (int i = 0; i <= m_DimX; ++i)
        {
            float x = -L + static_cast<float>(i) * m_CellSize;
            outLines.push_back({ x, -L, backZ });
            outLines.push_back({ x,  L, backZ });
        }
        for (int j = 0; j <= m_DimY; ++j)
        {
            float y = -L + static_cast<float>(j) * m_CellSize;
            outLines.push_back({ -L, y, backZ });
            outLines.push_back({  L, y, backZ });
        }
    }

    const wchar_t* GetName()          const override { return L"UniformGrid (MT)"; }
    const wchar_t* GetAlgorithmName() const override { return L"Uniform Grid"; }
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

    void DoNarrowPhaseChunk(int threadIdx)
    {
        if (!m_CurrentSpheres) return;
        const std::vector<FSphere>& spheres = *m_CurrentSpheres;

        struct FOffset { int x, y, z; };
        static const FOffset FORWARD_NEIGHBORS[13] = {
            { +1,  0,  0 },
            { -1, +1,  0 }, {  0, +1,  0 }, { +1, +1,  0 },
            { -1, -1, +1 }, {  0, -1, +1 }, { +1, -1, +1 },
            { -1,  0, +1 }, {  0,  0, +1 }, { +1,  0, +1 },
            { -1, +1, +1 }, {  0, +1, +1 }, { +1, +1, +1 }
        };

        const int dimXY = m_DimX * m_DimY;
        const size_t numActive = m_ActiveCells.size();
        std::vector<FCollisionManifold>& localManifolds = m_ThreadManifolds[threadIdx];
        uint64_t candidatePairs = 0;

        for (size_t idx = static_cast<size_t>(threadIdx); idx < numActive; idx += static_cast<size_t>(m_ThreadCount))
        {
            int cellID = m_ActiveCells[idx];

            int cz  = cellID / dimXY;
            int rem = cellID % dimXY;
            int cy  = rem / m_DimX;
            int cx  = rem % m_DimX;

            for (int i = m_CellHead[cellID]; i != -1; i = m_SphereNext[i])
            {
                const FVector3 posA = spheres[i].Center;
                const float    radA = spheres[i].Radius;

                for (int j = m_SphereNext[i]; j != -1; j = m_SphereNext[j])
                {
                    candidatePairs++;
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

            for (const auto& offset : FORWARD_NEIGHBORS)
            {
                int nx = cx + offset.x;
                int ny = cy + offset.y;
                int nz = cz + offset.z;

                if (nx < 0 || nx >= m_DimX || ny < 0 || ny >= m_DimY || nz < 0 || nz >= m_DimZ)
                {
                    continue;
                }

                int nCellID = nx + m_DimX * (ny + m_DimY * nz);
                if (m_CellHead[nCellID] == -1)
                {
                    continue;
                }

                for (int i = m_CellHead[cellID]; i != -1; i = m_SphereNext[i])
                {
                    const FVector3 posA = spheres[i].Center;
                    const float    radA = spheres[i].Radius;

                    for (int j = m_CellHead[nCellID]; j != -1; j = m_SphereNext[j])
                    {
                        candidatePairs++;
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
        }

        m_ThreadCandidates[threadIdx] = candidatePairs;
    }

private:
    LARGE_INTEGER                   m_TimerFreq   = {};
    int                             m_ThreadCount = 4;
    FCollisionStats                 m_Stats       = {};
    std::vector<FCollisionManifold> m_Manifolds;

    float m_BoxHalfSize = 2.0f;
    float m_MinX        = -2.0f;
    float m_MinY        = -2.0f;
    float m_MinZ        = -2.0f;
    float m_CellSize    = 0.1f;
    int   m_DimX        = 1;
    int   m_DimY        = 1;
    int   m_DimZ        = 1;

    std::vector<int> m_CellHead;
    std::vector<int> m_SphereNext;
    std::vector<int> m_ActiveCells;

    std::vector<std::vector<FCollisionManifold>> m_ThreadManifolds;
    std::vector<uint64_t>                        m_ThreadCandidates;
    const std::vector<FSphere>*                  m_CurrentSpheres = nullptr;

    std::vector<std::thread> m_Workers;
    std::mutex               m_Mutex;
    std::condition_variable  m_CvStart;
    std::condition_variable  m_CvDone;
    int                      m_Iteration      = 0;
    int                      m_CompletedCount = 0;
    bool                     m_Stop           = false;
};
