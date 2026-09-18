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
    UniformGridSolver(float boxHalfSize = 2.0f)
        : m_BoxHalfSize(boxHalfSize)
    {
        QueryPerformanceFrequency(&m_TimerFreq);
        m_Manifolds.reserve(512);
        m_ActiveCells.reserve(1024);
    }

    void SetBoxHalfSize(float boxHalfSize) { m_BoxHalfSize = boxHalfSize; }

    void BuildGrid(const std::vector<FSphere>& spheres)
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

        // Target cell size >= 2 * maxRadius
        float targetCellSize = maxRadius * 2.0f;
        if (targetCellSize < 0.05f) targetCellSize = 0.05f;

        // Fit integer number of cells into boxW
        // Since dim <= boxW / targetCellSize, cellSize = boxW / dim >= targetCellSize
        int dim = static_cast<int>(floorf(boxW / targetCellSize));
        if (dim < 1)  dim = 1;
        if (dim > 64) dim = 64;

        m_DimX = dim;
        m_DimY = dim;
        m_DimZ = dim;

        // Exact uniform cell size so dim * m_CellSize == boxW with zero remainder
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

        const float invCell = 1.0f / m_CellSize;

        // Insert spheres into grid
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

        // 1. Broad Phase: Grid Setup & Sphere Hashing
        QueryPerformanceCounter(&t0);
        BuildGrid(spheres);
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

        const int dimXY = m_DimX * m_DimY;

        for (int cellID : m_ActiveCells)
        {
            int cz  = cellID / dimXY;
            int rem = cellID % dimXY;
            int cy  = rem / m_DimX;
            int cx  = rem % m_DimX;

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

                if (nx < 0 || nx >= m_DimX || ny < 0 || ny >= m_DimY || nz < 0 || nz >= m_DimZ)
                {
                    continue;
                }

                int nCellID = nx + m_DimX * (ny + m_DimY * nz);
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

    void GenerateActiveCellLines(std::vector<FVertexSimple>& outLines) const
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

            float x0 = m_MinX + (float)cx * m_CellSize;
            float y0 = m_MinY + (float)cy * m_CellSize;
            float z0 = m_MinZ + (float)cz * m_CellSize;
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

            // Bottom 4 edges
            outLines.push_back(v0); outLines.push_back(v1);
            outLines.push_back(v1); outLines.push_back(v2);
            outLines.push_back(v2); outLines.push_back(v3);
            outLines.push_back(v3); outLines.push_back(v0);

            // Top 4 edges
            outLines.push_back(v4); outLines.push_back(v5);
            outLines.push_back(v5); outLines.push_back(v6);
            outLines.push_back(v6); outLines.push_back(v7);
            outLines.push_back(v7); outLines.push_back(v4);

            // Vertical 4 edges
            outLines.push_back(v0); outLines.push_back(v4);
            outLines.push_back(v1); outLines.push_back(v5);
            outLines.push_back(v2); outLines.push_back(v6);
            outLines.push_back(v3); outLines.push_back(v7);
        }
    }

    void GenerateFloorAndWallGridLines(std::vector<FVertexSimple>& outLines, float L = 2.0f) const
    {
        outLines.clear();
        if (m_CellSize <= 0.001f || m_DimX < 1) return;

        // Slight offset inward to completely eliminate Z-fighting against wall surfaces
        const float eps = 0.005f;
        const float floorY = -L + eps;
        const float backZ  =  L - eps;

        // Floor grid (y = -L)
        // Lines along X (from z = -L to +L)
        for (int i = 0; i <= m_DimX; ++i)
        {
            float x = -L + static_cast<float>(i) * m_CellSize;
            outLines.push_back({ x, floorY, -L });
            outLines.push_back({ x, floorY,  L });
        }
        // Lines along Z (from x = -L to +L)
        for (int k = 0; k <= m_DimZ; ++k)
        {
            float z = -L + static_cast<float>(k) * m_CellSize;
            outLines.push_back({ -L, floorY, z });
            outLines.push_back({  L, floorY, z });
        }

        // Back wall grid (z = L)
        // Vertical lines (from y = -L to +L)
        for (int i = 0; i <= m_DimX; ++i)
        {
            float x = -L + static_cast<float>(i) * m_CellSize;
            outLines.push_back({ x, -L, backZ });
            outLines.push_back({ x,  L, backZ });
        }
        // Horizontal lines (from x = -L to +L)
        for (int j = 0; j <= m_DimY; ++j)
        {
            float y = -L + static_cast<float>(j) * m_CellSize;
            outLines.push_back({ -L, y, backZ });
            outLines.push_back({  L, y, backZ });
        }
    }

    int   GetActiveCellCount() const { return static_cast<int>(m_ActiveCells.size()); }
    int   GetTotalCellCount()  const { return m_DimX * m_DimY * m_DimZ; }
    float GetCellSize()        const { return m_CellSize; }
    void  GetGridDims(int& dx, int& dy, int& dz) const { dx = m_DimX; dy = m_DimY; dz = m_DimZ; }

    const wchar_t* GetName()          const override { return L"UniformGrid (ST)"; }
    const wchar_t* GetAlgorithmName() const override { return L"Uniform Grid"; }
    const wchar_t* GetExecutionMode() const override { return L"Single Thread"; }
    int            GetThreadCount()   const override { return 1; }

    const FCollisionStats& GetLastStats() const override { return m_Stats; }
    const std::vector<FCollisionManifold>& GetManifolds() const { return m_Manifolds; }

private:
    LARGE_INTEGER                   m_TimerFreq   = {};
    FCollisionStats                 m_Stats       = {};
    std::vector<FCollisionManifold> m_Manifolds;

    // Grid domain & cell parameters
    float m_BoxHalfSize = 2.0f;
    float m_MinX        = -2.0f;
    float m_MinY        = -2.0f;
    float m_MinZ        = -2.0f;
    float m_CellSize    = 0.1f;
    int   m_DimX        = 1;
    int   m_DimY        = 1;
    int   m_DimZ        = 1;

    // Head-Next linked list grid
    std::vector<int> m_CellHead;    // size: dimX * dimY * dimZ
    std::vector<int> m_SphereNext;  // size: count
    std::vector<int> m_ActiveCells; // non-empty cell indices
};
