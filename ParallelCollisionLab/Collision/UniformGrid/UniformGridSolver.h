#pragma once

#include <vector>
#include <cmath>
#include <algorithm>
#include <windows.h>
#include "../ICollisionSolver.h"
#include "../CollisionTypes.h"
#include "../NarrowPhase.h"
#include "../Resolution.h"

//=============================================================================
// UniformGridSolver - Spatial partitioning grid collision solver (Single Thread)
//=============================================================================
class UniformGridSolver : public ICollisionSolver
{
public:
    UniformGridSolver()
    {
        QueryPerformanceFrequency(&m_TimerFreq);
        m_Manifolds.reserve(512);
        m_ActiveCells.reserve(1024);
    }

    void Solve(std::vector<FSphere>& spheres) override
    {
        const int count = static_cast<int>(spheres.size());
        m_Stats = {};
        if (count < 2) return;

        LARGE_INTEGER t0, t1, t2, t3;

        // 1. Broad Phase: Grid Setup & Sphere Hashing
        QueryPerformanceCounter(&t0);

        float maxRadius = 0.0f;
        for (int i = 0; i < count; ++i)
        {
            if (spheres[i].Radius > maxRadius)
            {
                maxRadius = spheres[i].Radius;
            }
        }

        // Domain bounds slightly enlarged beyond Cornell Box ([-2.0, 2.0])
        const float minX = -2.1f, minY = -2.1f, minZ = -2.1f;
        const float maxX =  2.1f, maxY =  2.1f, maxZ =  2.1f;
        const float boxW = maxX - minX;

        // Cell size >= 2 * maxRadius guarantees interacting spheres are in same or adjacent cells
        float cellSize = maxRadius * 2.0f;
        if (cellSize < 0.05f) cellSize = 0.05f;

        int dimX = static_cast<int>(ceilf(boxW / cellSize));
        int dimY = static_cast<int>(ceilf(boxW / cellSize));
        int dimZ = static_cast<int>(ceilf(boxW / cellSize));
        if (dimX < 1) dimX = 1; else if (dimX > 64) dimX = 64;
        if (dimY < 1) dimY = 1; else if (dimY > 64) dimY = 64;
        if (dimZ < 1) dimZ = 1; else if (dimZ > 64) dimZ = 64;

        int totalCells = dimX * dimY * dimZ;

        if (static_cast<int>(m_CellHead.size()) != totalCells)
        {
            m_CellHead.assign(totalCells, -1);
            m_ActiveCells.clear();
        }
        else
        {
            // O(active) fast clear of modified cells only
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

        const float invCell = 1.0f / cellSize;

        // Insert spheres into grid
        for (int i = 0; i < count; ++i)
        {
            int cx = static_cast<int>((spheres[i].Center.x - minX) * invCell);
            int cy = static_cast<int>((spheres[i].Center.y - minY) * invCell);
            int cz = static_cast<int>((spheres[i].Center.z - minZ) * invCell);

            if (cx < 0) cx = 0; else if (cx >= dimX) cx = dimX - 1;
            if (cy < 0) cy = 0; else if (cy >= dimY) cy = dimY - 1;
            if (cz < 0) cz = 0; else if (cz >= dimZ) cz = dimZ - 1;

            int cellID = cx + dimX * (cy + dimY * cz);
            if (m_CellHead[cellID] == -1)
            {
                m_ActiveCells.push_back(cellID);
            }
            m_SphereNext[i] = m_CellHead[cellID];
            m_CellHead[cellID] = i;
        }

        QueryPerformanceCounter(&t1);

        // 2. Narrow Phase: Same-cell & 13 Forward Neighbors collision test
        m_Manifolds.clear();
        uint64_t candidatePairs = 0;

        // 13 lexicographically positive neighbor offsets (dz >= 0)
        struct FOffset { int x, y, z; };
        static const FOffset FORWARD_NEIGHBORS[13] = {
            // dz = 0
            { +1,  0,  0 },
            { -1, +1,  0 }, {  0, +1,  0 }, { +1, +1,  0 },
            // dz = +1
            { -1, -1, +1 }, {  0, -1, +1 }, { +1, -1, +1 },
            { -1,  0, +1 }, {  0,  0, +1 }, { +1,  0, +1 },
            { -1, +1, +1 }, {  0, +1, +1 }, { +1, +1, +1 }
        };

        const int dimXY = dimX * dimY;

        for (int cellID : m_ActiveCells)
        {
            int cz  = cellID / dimXY;
            int rem = cellID % dimXY;
            int cy  = rem / dimX;
            int cx  = rem % dimX;

            // A. Pairs within the same cell (i < j)
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
                        m_Manifolds.push_back({ i, j, normal, radSum - dist });
                    }
                }
            }

            // B. Pairs with forward neighbor cells
            for (const auto& offset : FORWARD_NEIGHBORS)
            {
                int nx = cx + offset.x;
                int ny = cy + offset.y;
                int nz = cz + offset.z;

                if (nx < 0 || nx >= dimX || ny < 0 || ny >= dimY || nz < 0 || nz >= dimZ)
                {
                    continue;
                }

                int nCellID = nx + dimX * (ny + dimY * nz);
                if (m_CellHead[nCellID] == -1)
                {
                    continue; // Skip empty neighbor cells instantly
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
                            m_Manifolds.push_back({ i, j, normal, radSum - dist });
                        }
                    }
                }
            }
        }

        m_Stats.CandidatePairCount   = candidatePairs;
        m_Stats.ActualCollisionCount = static_cast<uint64_t>(m_Manifolds.size());
        QueryPerformanceCounter(&t2);

        // 3. Resolution Phase
        ResolveCollisions(spheres, m_Manifolds);
        QueryPerformanceCounter(&t3);

        const double toMs = 1000.0 / static_cast<double>(m_TimerFreq.QuadPart);
        m_Stats.BroadPhaseTimeMs   = static_cast<double>(t1.QuadPart - t0.QuadPart) * toMs;
        m_Stats.NarrowPhaseTimeMs  = static_cast<double>(t2.QuadPart - t1.QuadPart) * toMs;
        m_Stats.ResolutionTimeMs   = static_cast<double>(t3.QuadPart - t2.QuadPart) * toMs;
        m_Stats.TotalSolveTimeMs   = static_cast<double>(t3.QuadPart - t0.QuadPart) * toMs;
    }

    const wchar_t* GetName()          const override { return L"UniformGrid (ST)"; }
    const wchar_t* GetAlgorithmName() const override { return L"Uniform Grid"; }
    const wchar_t* GetExecutionMode() const override { return L"Single Thread"; }
    int            GetThreadCount()   const override { return 1; }

    const FCollisionStats& GetLastStats() const override { return m_Stats; }
    const std::vector<FCollisionManifold>& GetManifolds() const { return m_Manifolds; }

private:
    LARGE_INTEGER                   m_TimerFreq = {};
    FCollisionStats                 m_Stats     = {};
    std::vector<FCollisionManifold> m_Manifolds;

    // Head-Next linked list grid
    std::vector<int> m_CellHead;    // size: dimX * dimY * dimZ
    std::vector<int> m_SphereNext;  // size: count
    std::vector<int> m_ActiveCells; // non-empty cell indices
};
