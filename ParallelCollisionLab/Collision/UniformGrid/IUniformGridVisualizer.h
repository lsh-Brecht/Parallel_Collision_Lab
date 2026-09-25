#pragma once

#include <vector>
#include <algorithm>
#include <cmath>
#include "../../Core/Common.h"
#include "../../Core/Sphere.h"

//=============================================================================
// IUniformGridVisualizer - Interface for 3D uniform grid lattice & cell visualization
//=============================================================================
class IUniformGridVisualizer
{
public:
    virtual ~IUniformGridVisualizer() = default;
    virtual void BuildGrid(const std::vector<FSphere>& Spheres) = 0;
    virtual void GenerateActiveCellLines(std::vector<FVertexSimple>& OutLines) const = 0;
    virtual void GenerateFloorAndWallGridLines(std::vector<FVertexSimple>& OutLines, float InBoxHalfSize = 2.0f) const = 0;
};

//=============================================================================
// FUniformGridBase - Common spatial hashing grid state, build, and line generator
//=============================================================================
class FUniformGridBase : public IUniformGridVisualizer
{
public:
    virtual ~FUniformGridBase() = default;

    void SetBoxHalfSize(float InBoxHalfSize) { BoxHalfSize = InBoxHalfSize; }

    void BuildGrid(const std::vector<FSphere>& Spheres) override
    {
        const int Count = static_cast<int>(Spheres.size());
        if (Count == 0) return;

        float MaxRadius = 0.0f;
        for (int i = 0; i < Count; ++i)
        {
            if (Spheres[i].Radius > MaxRadius)
            {
                MaxRadius = Spheres[i].Radius;
            }
        }

        const float BoxW = BoxHalfSize * 2.0f;

        float TargetCellSize = MaxRadius * 2.0f;
        if (TargetCellSize < 0.05f) TargetCellSize = 0.05f;

        int Dim = static_cast<int>(floorf(BoxW / TargetCellSize));
        if (Dim < 1)  Dim = 1;
        if (Dim > 64) Dim = 64;

        DimX = Dim;
        DimY = Dim;
        DimZ = Dim;

        CellSize = BoxW / static_cast<float>(Dim);

        GridMinX = -BoxHalfSize;
        GridMinY = -BoxHalfSize;
        GridMinZ = -BoxHalfSize;

        int TotalCells = DimX * DimY * DimZ;

        if (static_cast<int>(CellHead.size()) != TotalCells)
        {
            CellHead.assign(TotalCells, -1);
            ActiveCells.clear();
        }
        else
        {
            for (int c : ActiveCells)
            {
                CellHead[c] = -1;
            }
            ActiveCells.clear();
        }

        if (static_cast<int>(SphereNext.size()) < Count)
        {
            SphereNext.resize(Count);
        }

        const float InvCell = 1.0f / CellSize;

        for (int i = 0; i < Count; ++i)
        {
            int cx = static_cast<int>((Spheres[i].Center.x - GridMinX) * InvCell);
            int cy = static_cast<int>((Spheres[i].Center.y - GridMinY) * InvCell);
            int cz = static_cast<int>((Spheres[i].Center.z - GridMinZ) * InvCell);

            if (cx < 0) cx = 0; else if (cx >= DimX) cx = DimX - 1;
            if (cy < 0) cy = 0; else if (cy >= DimY) cy = DimY - 1;
            if (cz < 0) cz = 0; else if (cz >= DimZ) cz = DimZ - 1;

            int CellID = cx + DimX * (cy + DimY * cz);
            if (CellHead[CellID] == -1)
            {
                ActiveCells.push_back(CellID);
            }
            SphereNext[i] = CellHead[CellID];
            CellHead[CellID] = i;
        }
    }

    void GenerateActiveCellLines(std::vector<FVertexSimple>& OutLines) const override
    {
        OutLines.clear();
        OutLines.reserve(ActiveCells.size() * 24);

        const int DimXY = DimX * DimY;

        for (int CellID : ActiveCells)
        {
            int cz  = CellID / DimXY;
            int rem = CellID % DimXY;
            int cy  = rem / DimX;
            int cx  = rem % DimX;

            float x0 = GridMinX + static_cast<float>(cx) * CellSize;
            float y0 = GridMinY + static_cast<float>(cy) * CellSize;
            float z0 = GridMinZ + static_cast<float>(cz) * CellSize;
            float x1 = x0 + CellSize;
            float y1 = y0 + CellSize;
            float z1 = z0 + CellSize;

            FVertexSimple v0 = { x0, y0, z0 };
            FVertexSimple v1 = { x1, y0, z0 };
            FVertexSimple v2 = { x1, y1, z0 };
            FVertexSimple v3 = { x0, y1, z0 };
            FVertexSimple v4 = { x0, y0, z1 };
            FVertexSimple v5 = { x1, y0, z1 };
            FVertexSimple v6 = { x1, y1, z1 };
            FVertexSimple v7 = { x0, y1, z1 };

            OutLines.push_back(v0); OutLines.push_back(v1);
            OutLines.push_back(v1); OutLines.push_back(v2);
            OutLines.push_back(v2); OutLines.push_back(v3);
            OutLines.push_back(v3); OutLines.push_back(v0);

            OutLines.push_back(v4); OutLines.push_back(v5);
            OutLines.push_back(v5); OutLines.push_back(v6);
            OutLines.push_back(v6); OutLines.push_back(v7);
            OutLines.push_back(v7); OutLines.push_back(v4);

            OutLines.push_back(v0); OutLines.push_back(v4);
            OutLines.push_back(v1); OutLines.push_back(v5);
            OutLines.push_back(v2); OutLines.push_back(v6);
            OutLines.push_back(v3); OutLines.push_back(v7);
        }
    }

    void GenerateFloorAndWallGridLines(std::vector<FVertexSimple>& OutLines, float InBoxHalfSize = 2.0f) const override
    {
        OutLines.clear();
        if (CellSize <= 0.001f || DimX < 1) return;

        const float Eps = 0.002f;
        const float FloorY = -InBoxHalfSize + Eps;
        const float BackZ  =  InBoxHalfSize - Eps;

        for (int i = 0; i <= DimX; ++i)
        {
            float x = -InBoxHalfSize + static_cast<float>(i) * CellSize;
            OutLines.push_back({ x, FloorY, -InBoxHalfSize });
            OutLines.push_back({ x, FloorY,  InBoxHalfSize });
        }
        for (int k = 0; k <= DimZ; ++k)
        {
            float z = -InBoxHalfSize + static_cast<float>(k) * CellSize;
            OutLines.push_back({ -InBoxHalfSize, FloorY, z });
            OutLines.push_back({  InBoxHalfSize, FloorY, z });
        }

        for (int i = 0; i <= DimX; ++i)
        {
            float x = -InBoxHalfSize + static_cast<float>(i) * CellSize;
            OutLines.push_back({ x, -InBoxHalfSize, BackZ });
            OutLines.push_back({ x,  InBoxHalfSize, BackZ });
        }
        for (int j = 0; j <= DimY; ++j)
        {
            float y = -InBoxHalfSize + static_cast<float>(j) * CellSize;
            OutLines.push_back({ -InBoxHalfSize, y, BackZ });
            OutLines.push_back({  InBoxHalfSize, y, BackZ });
        }
    }

    int   GetActiveCellCount() const { return static_cast<int>(ActiveCells.size()); }
    int   GetTotalCellCount()  const { return DimX * DimY * DimZ; }
    float GetCellSize()        const { return CellSize; }
    void  GetGridDims(int& OutDimX, int& OutDimY, int& OutDimZ) const { OutDimX = DimX; OutDimY = DimY; OutDimZ = DimZ; }

protected:
    float BoxHalfSize = 2.0f;
    float GridMinX    = -2.0f;
    float GridMinY    = -2.0f;
    float GridMinZ    = -2.0f;
    float CellSize    = 0.1f;
    int   DimX        = 1;
    int   DimY        = 1;
    int   DimZ        = 1;

    std::vector<int> CellHead;
    std::vector<int> SphereNext;
    std::vector<int> ActiveCells;
};
