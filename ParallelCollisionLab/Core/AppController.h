#pragma once

#include "../Window/Window.h"
#include "../Window/BenchmarkWindow.h"
#include "../Renderer/Renderer.h"
#include "../Renderer/Trackball.h"
#include "../Renderer/TextRenderer.h"
#include "Common.h"
#include "SimulationWorld.h"
#include "CPUInfo.h"
#include "../Collision/BVH/IBVHVisualizer.h"

//=============================================================================
// FAppController - Translates user input into camera and simulation actions
//=============================================================================
class FAppController
{
public:
    void ProcessInput(const FInputState& input, FSimulationWorld& world,
                      FWindow& window, URenderer& renderer,
                      FTextRenderer& textRenderer, const FCPUInfo& cpuInfo)
    {
        // View modes
        if (input.Wireframe)
            renderer.ToggleWireframe();

        if (input.ToggleGridVis)
            m_bShowGridVis = !m_bShowGridVis;

        if (input.ToggleHUD)
            m_bShowHUD = !m_bShowHUD;

        // Camera Trackball Interaction
        if (input.bRButtonPressed || (input.bLButtonPressed && input.bShiftDown))
        {
            int w = 0, h = 0;
            window.GetClientSize(w, h);
            m_Trackball.Begin(m_Eye, m_Up, CursorToNDC(input.MouseX, input.MouseY, w, h), 2);
        }
        else if (input.bLButtonPressed)
        {
            int w = 0, h = 0;
            window.GetClientSize(w, h);
            m_Trackball.Begin(m_Eye, m_Up, CursorToNDC(input.MouseX, input.MouseY, w, h), 1);
        }

        if (input.bMouseMoving && m_Trackball.IsTracking())
        {
            int w = 0, h = 0;
            window.GetClientSize(w, h);
            m_Trackball.Update(CursorToNDC(input.MouseX, input.MouseY, w, h), m_At, m_Eye, m_Up);
        }

        if (input.bLButtonReleased || input.bRButtonReleased)
        {
            m_Trackball.End();
        }

        if (input.ResetCamera)
        {
            m_Eye = Config::CAMERA_DEFAULT_EYE;
            m_Up  = Config::CAMERA_DEFAULT_UP;
            m_Trackball.End();
        }

        // Simulation Sphere Count Controls
        if (input.Reset)
        {
            world.ResetSpheres();
        }
        else if (input.Toggle)
        {
            world.ToggleSphereCount();
        }
        else if (input.ToggleSphereSize)
        {
            world.ToggleSphereSizeMode();
        }
        else if (input.Add || input.AddMany)
        {
            world.AddSpheres(input.AddMany ? 16 : 1);
        }
        else if (input.Sub || input.SubMany)
        {
            world.SubSpheres(input.SubMany ? 16 : 1);
        }

        // Thread Count Adjustment
        if (input.DecThread || input.IncThread)
        {
            int delta = input.bShiftDown ? 4 : 1;
            world.AdjustThreadCount(input.DecThread ? -delta : delta);
        }

        // Solver Selection
        if (input.SelectSolver >= 0)
        {
            world.SelectSolver(static_cast<size_t>(input.SelectSolver));
        }
        else if (input.CycleSolver)
        {
            world.CycleSolver();
        }

        // BVH Visualizer Controls
        if (input.IncBvhDepth || input.DecBvhDepth || input.CycleBvhMode)
        {
            IBVHVisualizer* bvhVis = dynamic_cast<IBVHVisualizer*>(world.GetActiveSolver());
            if (!bvhVis)
            {
                for (auto& s : world.GetSolvers())
                {
                    bvhVis = dynamic_cast<IBVHVisualizer*>(s.get());
                    if (bvhVis) break;
                }
            }

            if (bvhVis)
            {
                if (input.IncBvhDepth)  bvhVis->IncrementVisualizerDepth();
                if (input.DecBvhDepth)  bvhVis->DecrementVisualizerDepth();
                if (input.CycleBvhMode) bvhVis->CycleVisualizerMode();
            }
        }

        // Pause
        if (input.Pause)
        {
            m_bPaused = !m_bPaused;
        }

        // Benchmark Suite Execution
        if (input.Benchmark)
        {
            FBenchmarkReport report = world.RunBenchmarkSuite();
            if (report.bValid)
            {
                std::wstring detailedReport = report.GenerateDetailedReport(cpuInfo);
                ShowBenchmarkWindow(window.GetHWND(), detailedReport, world.GetSphereCount());
            }
        }
    }

    FMatrix4x4 GetViewProj(float aspect) const
    {
        FMatrix4x4 view = FMatrix4x4::LookAtLH(m_Eye, m_At, m_Up);
        FMatrix4x4 proj = FMatrix4x4::PerspectiveFovLH(
            Config::CAMERA_FOV_DEG * static_cast<float>(M_PI) / 180.0f,
            aspect, Config::CAMERA_NEAR, Config::CAMERA_FAR);
        return proj * view;
    }

    bool IsPaused()         const { return m_bPaused; }
    bool IsGridVisEnabled() const { return m_bShowGridVis; }
    bool IsHUDEnabled()     const { return m_bShowHUD; }

private:
    FTrackball m_Trackball;
    FVector3   m_Eye = Config::CAMERA_DEFAULT_EYE;
    FVector3   m_At  = Config::CAMERA_DEFAULT_AT;
    FVector3   m_Up  = Config::CAMERA_DEFAULT_UP;

    bool       m_bPaused      = false;
    bool       m_bShowGridVis = false;
    bool       m_bShowHUD     = true;
};
