#include "Window/Window.h"
#include "Window/BenchmarkWindow.h"
#include "Renderer/Renderer.h"
#include "Renderer/TextRenderer.h"
#include "Renderer/Trackball.h"
#include "Renderer/HUDTracker.h"
#include "Core/AppConfig.h"
#include "Core/Timer.h"
#include "Core/Sphere.h"
#include "Core/CPUInfo.h"
#include "Collision/NestedLoop/NestedLoopSolver.h"
#include "Collision/NestedLoop/NestedLoopMTSolver.h"
#include "Collision/UniformGrid/UniformGridSolver.h"
#include "Collision/Benchmark.h"

#include <ctime>
#include <memory>
#include <vector>

//=============================================================================
// WinMain
//=============================================================================
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int)
{
	srand(static_cast<unsigned int>(time(nullptr)));

	FCPUInfo cpuInfo = QueryCPUInfo();

	FWindow window;
	if (!window.Init(hInstance, Config::WINDOW_WIDTH, Config::WINDOW_HEIGHT, Config::WINDOW_TITLE))
		return -1;

	URenderer renderer;
	renderer.Init(window.GetHWND());

	FTextRenderer textRenderer;
	textRenderer.Init(renderer.SwapChain);

	FHUDTracker hudTracker(cpuInfo);
	FTimer      timer;

	// Geometry Buffers
	std::vector<FVertexSimple> unitSphereVerts = CreateUnitSphereVertices();
	ID3D11Buffer* sphereVB     = renderer.CreateVertexBuffer(unitSphereVerts);
	const UINT    sphereVCount = static_cast<UINT>(unitSphereVerts.size());

	ID3D11Buffer* leftWallVB   = renderer.CreateVertexBuffer(CreateWallVertices(0, Config::BOX_HALF_SIZE));
	ID3D11Buffer* rightWallVB  = renderer.CreateVertexBuffer(CreateWallVertices(1, Config::BOX_HALF_SIZE));
	ID3D11Buffer* otherWallsVB = renderer.CreateVertexBuffer(CreateWallVertices(2, Config::BOX_HALF_SIZE));

	// Simulation State
	int numSpheres = MIN_SPHERES;
	std::vector<FSphere> spheres = CreateSpheres(numSpheres, Config::BOX_HALF_SIZE);

	int maxHardwareThreads = static_cast<int>(std::thread::hardware_concurrency());
	int configuredThreads  = (maxHardwareThreads > 0) ? maxHardwareThreads : 4;

	std::vector<std::unique_ptr<ICollisionSolver>> solvers;
	solvers.push_back(std::make_unique<NestedLoopSolver>());
	solvers.push_back(std::make_unique<NestedLoopMTSolver>(configuredThreads));
	solvers.push_back(std::make_unique<UniformGridSolver>(Config::BOX_HALF_SIZE));
	size_t currentSolverIdx = 0;
	FBenchmarkReport benchmarkReport;

	// Camera & Trackball
	FTrackball trackball;
	FVector3 eye = Config::CAMERA_DEFAULT_EYE;
	FVector3 at  = Config::CAMERA_DEFAULT_AT;
	FVector3 up  = Config::CAMERA_DEFAULT_UP;

	bool bPaused          = false;
	bool bShowGridVis     = false;
	double lastRenderTimeMs = 0.0;

	// Main Loop
	bool bRunning = true;
	while (bRunning)
	{
		FInputState input;
		if (!window.PumpMessages(input) || input.Quit)
			break;

		if (input.bResized && input.NewWidth > 0 && input.NewHeight > 0)
		{
			textRenderer.ReleaseRenderTarget();
			renderer.OnResize(input.NewWidth, input.NewHeight);
			textRenderer.CreateRenderTarget(renderer.SwapChain);
		}

		if (input.Wireframe)
			renderer.ToggleWireframe();

		if (input.ToggleGridVis)
			bShowGridVis = !bShowGridVis;

		// Mouse Trackball Interaction
		if (input.bRButtonPressed || (input.bLButtonPressed && input.bShiftDown))
		{
			int w, h;
			window.GetClientSize(w, h);
			trackball.Begin(eye, up, CursorToNDC(input.MouseX, input.MouseY, w, h), 2);
		}
		else if (input.bLButtonPressed)
		{
			int w, h;
			window.GetClientSize(w, h);
			trackball.Begin(eye, up, CursorToNDC(input.MouseX, input.MouseY, w, h), 1);
		}
		if (input.bMouseMoving && trackball.IsTracking())
		{
			int w, h;
			window.GetClientSize(w, h);
			trackball.Update(CursorToNDC(input.MouseX, input.MouseY, w, h), at, eye, up);
		}
		if (input.bLButtonReleased || input.bRButtonReleased)
		{
			trackball.End();
		}
		if (input.ResetCamera)
		{
			eye = Config::CAMERA_DEFAULT_EYE;
			up  = Config::CAMERA_DEFAULT_UP;
			trackball.End();
		}

		// Simulation Controls
		if (input.Reset)
		{
			spheres = CreateSpheres(numSpheres, Config::BOX_HALF_SIZE);
			benchmarkReport.bValid = false;
		}
		else if (input.Toggle)
		{
			numSpheres = (numSpheres == MAX_SPHERES) ? MIN_SPHERES : MAX_SPHERES;
			spheres    = CreateSpheres(numSpheres, Config::BOX_HALF_SIZE);
			benchmarkReport.bValid = false;
		}
		else if (input.Add || input.AddMany)
		{
			numSpheres = min(numSpheres + (input.AddMany ? 16 : 1), MAX_SPHERES);
			spheres    = CreateSpheres(numSpheres, Config::BOX_HALF_SIZE);
			benchmarkReport.bValid = false;
		}
		else if (input.Sub || input.SubMany)
		{
			numSpheres = max(numSpheres - (input.SubMany ? 16 : 1), MIN_SPHERES);
			spheres    = CreateSpheres(numSpheres, Config::BOX_HALF_SIZE);
			benchmarkReport.bValid = false;
		}

		// Thread Count Adjustment
		if (input.DecThread || input.IncThread)
		{
			int delta = input.bShiftDown ? 4 : 1;
			configuredThreads = input.DecThread ? max(1, configuredThreads - delta)
			                                    : min(64, configuredThreads + delta);
			for (auto& s : solvers)
			{
				s->SetThreadCount(configuredThreads);
			}
			benchmarkReport.bValid = false;
		}

		// Benchmark
		if (input.Benchmark)
		{
			benchmarkReport = RunBenchmark(solvers, spheres, Config::BOX_HALF_SIZE);
			if (benchmarkReport.bValid)
			{
				std::wstring detailedReport = benchmarkReport.GenerateDetailedReport(cpuInfo);
				ShowBenchmarkWindow(window.GetHWND(), detailedReport, numSpheres);
			}
		}

		// Solver Selection
		if (input.SelectSolver >= 0 && input.SelectSolver < static_cast<int>(solvers.size()))
		{
			currentSolverIdx = static_cast<size_t>(input.SelectSolver);
		}
		else if (input.CycleSolver)
		{
			currentSolverIdx = (currentSolverIdx + 1) % solvers.size();
		}

		if (input.Pause)
			bPaused = !bPaused;

		float dt = timer.Tick();

		// Physics Update
		LARGE_INTEGER updateStart, updateEnd;
		QueryPerformanceCounter(&updateStart);

		ICollisionSolver* activeSolver = solvers[currentSolverIdx].get();

		if (!bPaused && !window.IsMinimized())
		{
			for (FSphere& s : spheres)
			{
				s.Update(dt);
				s.BoxCollisionCheck(Config::BOX_HALF_SIZE);
			}
			activeSolver->Solve(spheres);
		}

		QueryPerformanceCounter(&updateEnd);
		double updateTimeMs = FTimer::GetElapsedMs(updateStart, updateEnd, timer.GetFrequency());

		// Performance Profiling & Statistics Update
		hudTracker.Update(dt, updateTimeMs, lastRenderTimeMs,
		                  activeSolver->GetLastStats(), spheres.size(),
		                  activeSolver, configuredThreads, bShowGridVis);

		// Render Pipeline
		LARGE_INTEGER renderStart, renderEnd;
		QueryPerformanceCounter(&renderStart);

		if (!window.IsMinimized())
		{
			float aspect = window.GetAspectRatio();
			FMatrix4x4 view = FMatrix4x4::LookAtLH(eye, at, up);
			FMatrix4x4 proj = FMatrix4x4::PerspectiveFovLH(Config::CAMERA_FOV_DEG * (float)M_PI / 180.0f, aspect, Config::CAMERA_NEAR, Config::CAMERA_FAR);
			FMatrix4x4 viewProj = proj * view;

			renderer.BeginFrame(viewProj);

			renderer.RenderSphere(FMatrix4x4::Identity(), Config::WALL_LEFT_COLOR, leftWallVB, 6);
			renderer.RenderSphere(FMatrix4x4::Identity(), Config::WALL_RIGHT_COLOR, rightWallVB, 6);
			renderer.RenderSphere(FMatrix4x4::Identity(), Config::WALL_OTHER_COLOR, otherWallsVB, 24);

			for (const FSphere& s : spheres)
			{
				renderer.RenderSphere(s.GetModelMatrix(), s.Color, sphereVB, sphereVCount);
			}

			// 3D Uniform Grid Visualization
			if (bShowGridVis)
			{
				UniformGridSolver* gridSolver = dynamic_cast<UniformGridSolver*>(activeSolver);
				if (!gridSolver)
				{
					for (auto& s : solvers)
					{
						gridSolver = dynamic_cast<UniformGridSolver*>(s.get());
						if (gridSolver)
						{
							gridSolver->BuildGrid(spheres);
							break;
						}
					}
				}

				if (gridSolver)
				{
					static std::vector<FVertexSimple> wallGridLines;
					static std::vector<FVertexSimple> activeCellLines;

					gridSolver->GenerateFloorAndWallGridLines(wallGridLines, Config::BOX_HALF_SIZE);
					renderer.RenderDynamicLines(wallGridLines, Config::GRID_WALL_COLOR);

					gridSolver->GenerateActiveCellLines(activeCellLines);
					renderer.RenderDynamicLines(activeCellLines, Config::GRID_ACTIVE_COLOR);
				}
			}

			int clientW = 0, clientH = 0;
			window.GetClientSize(clientW, clientH);
			hudTracker.Draw(textRenderer, clientW, clientH);

			renderer.EndFrame();
		}

		QueryPerformanceCounter(&renderEnd);
		lastRenderTimeMs = FTimer::GetElapsedMs(renderStart, renderEnd, timer.GetFrequency());
	}

	// Cleanup
	renderer.ReleaseVertexBuffer(leftWallVB);
	renderer.ReleaseVertexBuffer(rightWallVB);
	renderer.ReleaseVertexBuffer(otherWallsVB);
	renderer.ReleaseVertexBuffer(sphereVB);
	textRenderer.Shutdown();
	renderer.Shutdown();
	window.Shutdown();

	return 0;
}
