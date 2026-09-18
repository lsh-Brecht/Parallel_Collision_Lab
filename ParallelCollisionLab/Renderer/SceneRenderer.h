#pragma once

#include <vector>
#include <windows.h>
#include "Renderer.h"
#include "../Core/Common.h"
#include "../Core/AppConfig.h"
#include "../Core/Sphere.h"
#include "../Core/Timer.h"
#include "../Core/SimulationWorld.h"
#include "../Collision/UniformGrid/IUniformGridVisualizer.h"

//=============================================================================
// FSceneRenderer - Manages 3D geometry buffers and draws walls, spheres, and grid
//=============================================================================
class FSceneRenderer
{
public:
    void Init(URenderer& renderer, float boxHalfSize)
    {
        std::vector<FVertexSimple> unitSphereVerts = CreateUnitSphereVertices();
        m_SphereVB     = renderer.CreateVertexBuffer(unitSphereVerts);
        m_SphereVCount = static_cast<UINT>(unitSphereVerts.size());

        m_LeftWallVB   = renderer.CreateVertexBuffer(CreateWallVertices(0, boxHalfSize));
        m_RightWallVB  = renderer.CreateVertexBuffer(CreateWallVertices(1, boxHalfSize));
        m_OtherWallsVB = renderer.CreateVertexBuffer(CreateWallVertices(2, boxHalfSize));
    }

    void Shutdown(URenderer& renderer)
    {
        if (m_LeftWallVB)   { renderer.ReleaseVertexBuffer(m_LeftWallVB);   m_LeftWallVB   = nullptr; }
        if (m_RightWallVB)  { renderer.ReleaseVertexBuffer(m_RightWallVB);  m_RightWallVB  = nullptr; }
        if (m_OtherWallsVB) { renderer.ReleaseVertexBuffer(m_OtherWallsVB); m_OtherWallsVB = nullptr; }
        if (m_SphereVB)     { renderer.ReleaseVertexBuffer(m_SphereVB);     m_SphereVB     = nullptr; }
    }

    void Render(URenderer& renderer, const FSimulationWorld& world,
                const FMatrix4x4& viewProj, bool bShowGridVis, int64_t timerFreq)
    {
        LARGE_INTEGER r0, r1;
        QueryPerformanceCounter(&r0);

        renderer.BeginFrame(viewProj);

        // 1. Draw Cornell Box Walls
        renderer.RenderSphere(FMatrix4x4::Identity(), Config::WALL_LEFT_COLOR, m_LeftWallVB, 6);
        renderer.RenderSphere(FMatrix4x4::Identity(), Config::WALL_RIGHT_COLOR, m_RightWallVB, 6);
        renderer.RenderSphere(FMatrix4x4::Identity(), Config::WALL_OTHER_COLOR, m_OtherWallsVB, 24);

        // 2. Draw Spheres
        for (const FSphere& s : world.GetSpheres())
        {
            renderer.RenderSphere(s.GetModelMatrix(), s.Color, m_SphereVB, m_SphereVCount);
        }

        // 3. Draw 3D Uniform Grid Lines (if enabled)
        if (bShowGridVis)
        {
            RenderGridVisualization(renderer, world);
        }

        QueryPerformanceCounter(&r1);
        m_LastRenderTimeMs = FTimer::GetElapsedMs(r0, r1, timerFreq);
    }

    double GetLastRenderTimeMs() const { return m_LastRenderTimeMs; }

private:
    void RenderGridVisualization(URenderer& renderer, const FSimulationWorld& world)
    {
        IUniformGridVisualizer* gridVis = dynamic_cast<IUniformGridVisualizer*>(world.GetActiveSolver());
        if (!gridVis)
        {
            for (auto& s : world.GetSolvers())
            {
                gridVis = dynamic_cast<IUniformGridVisualizer*>(s.get());
                if (gridVis)
                {
                    gridVis->BuildGrid(world.GetSpheres());
                    break;
                }
            }
        }

        if (gridVis)
        {
            gridVis->GenerateFloorAndWallGridLines(m_WallGridLines, world.GetBoxHalfSize());
            renderer.RenderDynamicLines(m_WallGridLines, Config::GRID_WALL_COLOR);

            gridVis->GenerateActiveCellLines(m_ActiveCellLines);
            renderer.RenderDynamicLines(m_ActiveCellLines, Config::GRID_ACTIVE_COLOR);
        }
    }

private:
    ID3D11Buffer* m_LeftWallVB   = nullptr;
    ID3D11Buffer* m_RightWallVB  = nullptr;
    ID3D11Buffer* m_OtherWallsVB = nullptr;
    ID3D11Buffer* m_SphereVB     = nullptr;
    UINT          m_SphereVCount = 0;

    double        m_LastRenderTimeMs = 0.0;

    std::vector<FVertexSimple> m_WallGridLines;
    std::vector<FVertexSimple> m_ActiveCellLines;
};
