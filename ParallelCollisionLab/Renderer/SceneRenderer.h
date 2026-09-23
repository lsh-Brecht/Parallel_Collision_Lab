#pragma once

#include <vector>
#include <windows.h>
#include "Renderer.h"
#include "../Core/SimulationWorld.h"

//=============================================================================
// FSceneRenderer - Manages 3D geometry buffers and draws walls, spheres, and grid
//=============================================================================
class FSceneRenderer
{
public:
    void Init(URenderer& Renderer, float BoxHalfSize)
    {
        std::vector<FVertexSimple> unitSphereVerts = CreateUnitSphereVertices();
        SphereVB          = Renderer.CreateVertexBuffer(unitSphereVerts);
        SphereVertexCount = static_cast<UINT>(unitSphereVerts.size());

        LeftWallVB   = Renderer.CreateVertexBuffer(CreateWallVertices(0, BoxHalfSize));
        RightWallVB  = Renderer.CreateVertexBuffer(CreateWallVertices(1, BoxHalfSize));
        OtherWallsVB = Renderer.CreateVertexBuffer(CreateWallVertices(2, BoxHalfSize));
    }

    void Shutdown(URenderer& Renderer)
    {
        if (LeftWallVB)   { Renderer.ReleaseVertexBuffer(LeftWallVB);   LeftWallVB   = nullptr; }
        if (RightWallVB)  { Renderer.ReleaseVertexBuffer(RightWallVB);  RightWallVB  = nullptr; }
        if (OtherWallsVB) { Renderer.ReleaseVertexBuffer(OtherWallsVB); OtherWallsVB = nullptr; }
        if (SphereVB)     { Renderer.ReleaseVertexBuffer(SphereVB);     SphereVB     = nullptr; }
    }

    void Render(URenderer& Renderer, const FSimulationWorld& World,
                const FMatrix4x4& ViewProj, bool bShowGridVis, int64_t TimerFrequency)
    {
        LARGE_INTEGER r0, r1;
        QueryPerformanceCounter(&r0);

        Renderer.BeginFrame(ViewProj);

        Renderer.RenderSphere(FMatrix4x4::Identity(), Config::WALL_LEFT_COLOR, LeftWallVB, 6);
        Renderer.RenderSphere(FMatrix4x4::Identity(), Config::WALL_RIGHT_COLOR, RightWallVB, 6);
        Renderer.RenderSphere(FMatrix4x4::Identity(), Config::WALL_OTHER_COLOR, OtherWallsVB, 24);

        for (const FSphere& s : World.GetSpheres())
        {
            Renderer.RenderSphere(s.GetModelMatrix(), s.Color, SphereVB, SphereVertexCount);
        }

        if (bShowGridVis)
        {
            RenderGridVisualization(Renderer, World);
        }

        QueryPerformanceCounter(&r1);
        LastRenderTimeMs = FTimer::GetElapsedMs(r0, r1, TimerFrequency);
    }

    double GetLastRenderTimeMs() const { return LastRenderTimeMs; }

private:
    void RenderGridVisualization(URenderer& Renderer, const FSimulationWorld& World)
    {
        IBVHVisualizer* bvhVis = dynamic_cast<IBVHVisualizer*>(World.GetActiveSolver());
        if (bvhVis)
        {
            bvhVis->GenerateVisualizerLineGroups(BvhLineGroups);
            for (const auto& group : BvhLineGroups)
            {
                if (!group.Lines.empty())
                {
                    Renderer.RenderDynamicLines(group.Lines, group.Color);
                }
            }
            return;
        }

        IUniformGridVisualizer* gridVis = dynamic_cast<IUniformGridVisualizer*>(World.GetActiveSolver());
        if (!gridVis)
        {
            for (auto& s : World.GetSolvers())
            {
                gridVis = dynamic_cast<IUniformGridVisualizer*>(s.get());
                if (gridVis)
                {
                    gridVis->BuildGrid(World.GetSpheres());
                    break;
                }
            }
        }

        if (gridVis)
        {
            gridVis->GenerateFloorAndWallGridLines(WallGridLines, World.GetBoxHalfSize());
            Renderer.RenderDynamicLines(WallGridLines, Config::GRID_WALL_COLOR);

            gridVis->GenerateActiveCellLines(ActiveCellLines);
            Renderer.RenderDynamicLines(ActiveCellLines, Config::GRID_ACTIVE_COLOR);
        }
    }

private:
    ID3D11Buffer* LeftWallVB        = nullptr;
    ID3D11Buffer* RightWallVB       = nullptr;
    ID3D11Buffer* OtherWallsVB      = nullptr;
    ID3D11Buffer* SphereVB          = nullptr;
    UINT          SphereVertexCount = 0;

    double        LastRenderTimeMs  = 0.0;

    std::vector<FVertexSimple> WallGridLines;
    std::vector<FVertexSimple> ActiveCellLines;
    std::vector<FBVHLineGroup> BvhLineGroups;
};
