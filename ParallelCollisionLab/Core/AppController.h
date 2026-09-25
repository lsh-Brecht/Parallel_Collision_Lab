#pragma once

#include "../Window/Window.h"
#include "../Window/BenchmarkWindow.h"
#include "../Renderer/Renderer.h"
#include "../Renderer/Trackball.h"
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
    void ProcessInput(const FInputState& Input, FSimulationWorld& World,
                      FWindow& Window, URenderer& Renderer,
                      const FCPUInfo& CPUInfo)
    {
        // View modes
        if (Input.Wireframe)
            Renderer.ToggleWireframe();

        if (Input.ToggleGridVis)
            bShowGridVis = !bShowGridVis;

        if (Input.ToggleHUD)
            bShowHUD = !bShowHUD;

        // Camera Trackball Interaction
        if (Input.bRButtonPressed || (Input.bLButtonPressed && Input.bShiftDown))
        {
            int w = 0, h = 0;
            Window.GetClientSize(w, h);
            Trackball.Begin(CameraEye, CameraUp, CursorToNDC(Input.MouseX, Input.MouseY, w, h), 2);
        }
        else if (Input.bLButtonPressed)
        {
            int w = 0, h = 0;
            Window.GetClientSize(w, h);
            Trackball.Begin(CameraEye, CameraUp, CursorToNDC(Input.MouseX, Input.MouseY, w, h), 1);
        }

        if (Input.bMouseMoving && Trackball.IsTracking())
        {
            int w = 0, h = 0;
            Window.GetClientSize(w, h);
            Trackball.Update(CursorToNDC(Input.MouseX, Input.MouseY, w, h), CameraAt, CameraEye, CameraUp);
        }

        if (Input.bLButtonReleased || Input.bRButtonReleased)
        {
            Trackball.End();
        }

        if (Input.ResetCamera)
        {
            CameraEye = Config::CAMERA_DEFAULT_EYE;
            CameraUp  = Config::CAMERA_DEFAULT_UP;
            Trackball.End();
        }

        // Simulation Sphere Count Controls
        if (Input.Reset)
        {
            World.ResetSpheres();
        }
        else if (Input.Toggle)
        {
            World.ToggleSphereCount();
        }
        else if (Input.ToggleSphereSize)
        {
            World.ToggleSphereSizeMode();
        }
        else if (Input.ToggleDamping)
        {
            World.ToggleDamping();
        }
        else if (Input.Add || Input.AddMany)
        {
            World.AddSpheres(Input.AddMany ? 16 : 1);
        }
        else if (Input.Sub || Input.SubMany)
        {
            World.SubSpheres(Input.SubMany ? 16 : 1);
        }

        // Thread Count Adjustment
        if (Input.DecThread || Input.IncThread)
        {
            int delta = Input.bShiftDown ? 4 : 1;
            World.AdjustThreadCount(Input.DecThread ? -delta : delta);
        }

        // Solver Selection
        if (Input.SelectSolver >= 0)
        {
            World.SelectSolver(static_cast<size_t>(Input.SelectSolver));
        }

        // BVH Visualizer Controls
        if (Input.IncBvhDepth || Input.DecBvhDepth || Input.CycleBvhMode)
        {
            IBVHVisualizer* bvhVis = dynamic_cast<IBVHVisualizer*>(World.GetActiveSolver());
            if (!bvhVis)
            {
                for (auto& s : World.GetSolvers())
                {
                    bvhVis = dynamic_cast<IBVHVisualizer*>(s.get());
                    if (bvhVis) break;
                }
            }

            if (bvhVis)
            {
                if (Input.IncBvhDepth)  bvhVis->IncrementVisualizerDepth();
                if (Input.DecBvhDepth)  bvhVis->DecrementVisualizerDepth();
                if (Input.CycleBvhMode) bvhVis->CycleVisualizerMode();
            }
        }

        // Pause
        if (Input.Pause)
        {
            bIsPaused = !bIsPaused;
        }

        // Benchmark Suite Execution
        if (Input.Benchmark)
        {
            FBenchmarkReport report = World.RunBenchmarkSuite();
            if (report.bValid)
            {
                std::wstring detailedReport = report.GenerateDetailedReport(CPUInfo);
                ShowBenchmarkWindow(Window.GetHWND(), detailedReport, World.GetSphereCount());
            }
        }
    }

    FMatrix4x4 GetViewProj(float Aspect) const
    {
        FMatrix4x4 view = FMatrix4x4::LookAtLH(CameraEye, CameraAt, CameraUp);
        FMatrix4x4 proj = FMatrix4x4::PerspectiveFovLH(
            Config::CAMERA_FOV_DEG * static_cast<float>(M_PI) / 180.0f,
            Aspect, Config::CAMERA_NEAR, Config::CAMERA_FAR);
        return proj * view;
    }

    bool IsPaused()         const { return bIsPaused; }
    bool IsGridVisEnabled() const { return bShowGridVis; }
    bool IsHUDEnabled()     const { return bShowHUD; }

private:
    FTrackball Trackball;
    FVector3   CameraEye = Config::CAMERA_DEFAULT_EYE;
    FVector3   CameraAt  = Config::CAMERA_DEFAULT_AT;
    FVector3   CameraUp  = Config::CAMERA_DEFAULT_UP;

    bool       bIsPaused      = false;
    bool       bShowGridVis   = false;
    bool       bShowHUD       = true;
};
