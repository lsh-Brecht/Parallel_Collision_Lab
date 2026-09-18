#include "Window/Window.h"
#include "Renderer/Renderer.h"
#include "Renderer/TextRenderer.h"
#include "Renderer/Trackball.h"
#include "Core/Sphere.h"
#include "Core/CPUInfo.h"
#include "Collision/NestedLoop/NestedLoopSolver.h"
#include "Collision/NestedLoop/NestedLoopMTSolver.h"
#include "Collision/UniformGrid/UniformGridSolver.h"
#include "Collision/Benchmark.h"
#include "Window/BenchmarkWindow.h"

#include <ctime>
#include <memory>
#include <string>

static std::wstring FormatCommas(uint64_t val)
{
	std::wstring s = std::to_wstring(val);
	int insertPos = static_cast<int>(s.length()) - 3;
	while (insertPos > 0)
	{
		s.insert(insertPos, L",");
		insertPos -= 3;
	}
	return s;
}

//=============================================================================
// Timer
//=============================================================================
static LARGE_INTEGER g_Frequency;
static LARGE_INTEGER g_LastTime;

static void InitTimer()
{
	QueryPerformanceFrequency(&g_Frequency);
	QueryPerformanceCounter(&g_LastTime);
}

static float GetDeltaTime()
{
	LARGE_INTEGER now;
	QueryPerformanceCounter(&now);
	float dt = (float)(now.QuadPart - g_LastTime.QuadPart) / (float)g_Frequency.QuadPart;
	g_LastTime = now;

	// Temporary solution: clamp dt to ~30 FPS (0.033s) to mitigate frame hitch
	// and avoid tunneling, rather than using sub-stepping. (Low FPS will cause slow-motion)
	if (dt > 0.033f)
	{
		dt = 0.033f;
	}

	return dt;
}

//=============================================================================
// WinMain
//=============================================================================
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int)
{
	srand((unsigned int)time(nullptr));

	FCPUInfo cpuInfo = QueryCPUInfo();

	FWindow window;
	if (!window.Init(hInstance, 1024, 1024, L"Parallel Collision Lab"))
		return -1;

	URenderer renderer;
	renderer.Init(window.GetHWND());

	FTextRenderer textRenderer;
	textRenderer.Init(renderer.SwapChain);

	const float boxHalfSize = 2.0f;

	std::vector<FVertexSimple> unitSphereVerts = CreateUnitSphereVertices();
	ID3D11Buffer* sphereVB     = renderer.CreateVertexBuffer(unitSphereVerts);
	const UINT    sphereVCount = (UINT)unitSphereVerts.size();

	ID3D11Buffer* leftWallVB   = renderer.CreateVertexBuffer(CreateWallVertices(0, boxHalfSize));
	ID3D11Buffer* rightWallVB  = renderer.CreateVertexBuffer(CreateWallVertices(1, boxHalfSize));
	ID3D11Buffer* otherWallsVB = renderer.CreateVertexBuffer(CreateWallVertices(2, boxHalfSize));

	const FVector4 leftWallColor(0.630f, 0.065f, 0.050f, 1.0f);
	const FVector4 rightWallColor(0.137f, 0.447f, 0.090f, 1.0f);
	const FVector4 otherWallsColor(0.725f, 0.710f, 0.680f, 1.0f);

	int numSpheres = MIN_SPHERES;
	std::vector<FSphere> spheres = CreateSpheres(numSpheres, boxHalfSize);

	FTrackball trackball;
	const FVector3 defaultEye(0.0f, 0.0f, -10.0f);
	const FVector3 defaultUp(0.0f, 1.0f, 0.0f);
	FVector3 eye = defaultEye;
	FVector3 at(0.0f, 0.0f, 0.0f);
	FVector3 up = defaultUp;

	bool bPaused = false;
	bool bShowGridVis = false;

	int maxHardwareThreads = static_cast<int>(std::thread::hardware_concurrency());
	if (maxHardwareThreads <= 0) maxHardwareThreads = 4;
	int configuredThreads = maxHardwareThreads;

	std::vector<std::unique_ptr<ICollisionSolver>> solvers;
	solvers.push_back(std::make_unique<NestedLoopSolver>());
	solvers.push_back(std::make_unique<NestedLoopMTSolver>(configuredThreads));
	solvers.push_back(std::make_unique<UniformGridSolver>(boxHalfSize));
	size_t currentSolverIdx = 0;
	FBenchmarkReport benchmarkReport;

	InitTimer();

	bool bRunning = true;
	while (bRunning)
	{
		FInputState input;
		if (!window.PumpMessages(input))
			break;

		if (input.Quit)
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

		if (input.bRButtonPressed || (input.bLButtonPressed && input.bShiftDown))
		{
			int w, h;
			window.GetClientSize(w, h);
			FVector2 npos = CursorToNDC(input.MouseX, input.MouseY, w, h);
			trackball.Begin(eye, up, npos, 2);
		}
		else if (input.bLButtonPressed)
		{
			int w, h;
			window.GetClientSize(w, h);
			FVector2 npos = CursorToNDC(input.MouseX, input.MouseY, w, h);
			trackball.Begin(eye, up, npos, 1);
		}
		if (input.bMouseMoving && trackball.IsTracking())
		{
			int w, h;
			window.GetClientSize(w, h);
			FVector2 npos = CursorToNDC(input.MouseX, input.MouseY, w, h);
			trackball.Update(npos, at, eye, up);
		}
		if (input.bLButtonReleased || input.bRButtonReleased)
		{
			trackball.End();
		}

		if (input.ResetCamera)
		{
			eye = defaultEye;
			up  = defaultUp;
			trackball.End();
		}

		if (input.Reset)
		{
			spheres = CreateSpheres(numSpheres, boxHalfSize);
			benchmarkReport.bValid = false;
		}
		else if (input.Toggle)
		{
			numSpheres = (numSpheres == MAX_SPHERES) ? MIN_SPHERES : MAX_SPHERES;
			spheres    = CreateSpheres(numSpheres, boxHalfSize);
			benchmarkReport.bValid = false;
		}
		else if (input.Add || input.AddMany)
		{
			int delta  = input.AddMany ? 16 : 1;
			numSpheres = min(numSpheres + delta, MAX_SPHERES);
			spheres    = CreateSpheres(numSpheres, boxHalfSize);
			benchmarkReport.bValid = false;
		}
		else if (input.Sub || input.SubMany)
		{
			int delta  = input.SubMany ? 16 : 1;
			numSpheres = max(numSpheres - delta, MIN_SPHERES);
			spheres    = CreateSpheres(numSpheres, boxHalfSize);
			benchmarkReport.bValid = false;
		}

		if (input.DecThread || input.IncThread)
		{
			int delta = input.bShiftDown ? 4 : 1;
			if (input.DecThread)
				configuredThreads = max(1, configuredThreads - delta);
			else
				configuredThreads = min(64, configuredThreads + delta);

			for (auto& s : solvers)
			{
				s->SetThreadCount(configuredThreads);
			}
			benchmarkReport.bValid = false;
		}

		if (input.Benchmark)
		{
			benchmarkReport = RunBenchmark(solvers, spheres, boxHalfSize);
			if (benchmarkReport.bValid)
			{
				std::wstring detailedReport = benchmarkReport.GenerateDetailedReport(cpuInfo);
				ShowBenchmarkWindow(window.GetHWND(), detailedReport, numSpheres);
			}
		}

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

		float dt = GetDeltaTime();

		LARGE_INTEGER updateStart, updateEnd;
		QueryPerformanceCounter(&updateStart);

		ICollisionSolver* activeSolver = solvers[currentSolverIdx].get();

		if (!bPaused && !window.IsMinimized())
		{
			for (FSphere& s : spheres)
			{
				s.Update(dt);
				s.BoxCollisionCheck(boxHalfSize);
			}

			activeSolver->Solve(spheres);
		}

		QueryPerformanceCounter(&updateEnd);
		double updateTimeMs = (double)(updateEnd.QuadPart - updateStart.QuadPart) * 1000.0 / (double)g_Frequency.QuadPart;

		static double   timeAccum        = 0.0;
		static int      frameAccum       = 0;
		static double   updateAccumMs    = 0.0;
		static double   renderAccumMs    = 0.0;
		static double   broadAccumMs     = 0.0;
		static double   narrowAccumMs    = 0.0;
		static double   resolveAccumMs   = 0.0;
		static double   lastRenderTimeMs = 0.0;
		static wchar_t      hudTopText[512]    = L"Initializing...";
		static wchar_t      hudBottomText[256] = L"";
		static std::wstring strCandidates      = L"0";
		static std::wstring strCollisions      = L"0";

		const FCollisionStats& stats = activeSolver->GetLastStats();

		timeAccum      += dt;
		frameAccum     += 1;
		updateAccumMs  += updateTimeMs;
		renderAccumMs  += lastRenderTimeMs;
		broadAccumMs   += stats.BroadPhaseTimeMs;
		narrowAccumMs  += stats.NarrowPhaseTimeMs;
		resolveAccumMs += stats.ResolutionTimeMs;

		if (timeAccum >= 0.25)
		{
			double currentFPS   = (double)frameAccum / timeAccum;
			double frameTimeMs  = (timeAccum / (double)frameAccum) * 1000.0;
			double avgUpdateMs  = updateAccumMs / (double)frameAccum;
			double avgRenderMs  = renderAccumMs / (double)frameAccum;
			double avgBroadMs   = broadAccumMs / (double)frameAccum;
			double avgNarrowMs  = narrowAccumMs / (double)frameAccum;
			double avgResolveMs = resolveAccumMs / (double)frameAccum;

			std::wstring strBalls = FormatCommas(spheres.size());
			strCandidates         = FormatCommas(stats.CandidatePairCount);
			strCollisions         = FormatCommas(stats.ActualCollisionCount);

			swprintf_s(hudTopText,
			           L"CPU         : %s\n"
			           L"Cache       : %s\n"
			           L"\n"
			           L"Balls       : %s | Threads: %d\n"
			           L"Algorithm   : %s [%s]\n"
			           L"\n"
			           L"Frame Time  : %.1f ms (%.1f FPS) | Render: %.2f ms\n"
			           L"Broad Phase : %.3f ms\n"
			           L"Narrow Phase: %.2f ms\n"
			           L"Resolution  : %.3f ms",
			           cpuInfo.GetSummaryString().c_str(),
			           cpuInfo.GetCacheString().c_str(),
			           strBalls.c_str(),
			           activeSolver->GetThreadCount(),
			           activeSolver->GetAlgorithmName(),
			           activeSolver->GetExecutionMode(),
			           frameTimeMs, currentFPS,
			           avgRenderMs,
			           avgBroadMs,
			           avgNarrowMs,
			           avgResolveMs);

			timeAccum      = 0.0;
			frameAccum     = 0;
			updateAccumMs  = 0.0;
			renderAccumMs  = 0.0;
			broadAccumMs   = 0.0;
			narrowAccumMs  = 0.0;
			resolveAccumMs = 0.0;
		}

		swprintf_s(hudBottomText,
		           L"Candidate Pairs : %s | Collisions: %s | Threads: %d (Hotkeys: [ / ] )\n"
		           L"[1] Naive ST  [2] Naive MT  [3] Grid ST  [B] Benchmark  [G] Grid: %s  (Tab: Cycle)",
		           strCandidates.c_str(),
		           strCollisions.c_str(),
		           configuredThreads,
		           bShowGridVis ? L"ON" : L"OFF");

		LARGE_INTEGER renderStart, renderEnd;
		QueryPerformanceCounter(&renderStart);

		if (!window.IsMinimized())
		{
			float aspect = window.GetAspectRatio();
			FMatrix4x4 view = FMatrix4x4::LookAtLH(eye, at, up);
			FMatrix4x4 proj = FMatrix4x4::PerspectiveFovLH(50.0f * (float)M_PI / 180.0f, aspect, 0.1f, 100.0f);
			FMatrix4x4 viewProj = proj * view;

			renderer.BeginFrame(viewProj);

			renderer.RenderSphere(FMatrix4x4::Identity(), leftWallColor, leftWallVB, 6);
			renderer.RenderSphere(FMatrix4x4::Identity(), rightWallColor, rightWallVB, 6);
			renderer.RenderSphere(FMatrix4x4::Identity(), otherWallsColor, otherWallsVB, 24);

			for (const FSphere& s : spheres)
			{
				renderer.RenderSphere(s.GetModelMatrix(), s.Color, sphereVB, sphereVCount);
			}

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

					// Blueprint grid on Cornell Box walls and floor (vibrant sky/royal blue)
					gridSolver->GenerateFloorAndWallGridLines(wallGridLines, boxHalfSize);
					renderer.RenderDynamicLines(wallGridLines, FVector4(0.0f, 0.0f, 0.0f, 0.0f));

					// Bright cyan wireframe around active occupied cells
					gridSolver->GenerateActiveCellLines(activeCellLines);
					renderer.RenderDynamicLines(activeCellLines, FVector4(0.0f, 0.95f, 1.0f, 1.0f));
				}
			}

			int clientW = 0, clientH = 0;
			window.GetClientSize(clientW, clientH);
			float bottomY = static_cast<float>(clientH) - 55.0f;

			textRenderer.DrawTextOverlay(hudTopText, 10.0f, 10.0f, 700.0f, 220.0f);
			textRenderer.DrawTextOverlay(hudBottomText, 10.0f, bottomY, 780.0f, 50.0f);
		}

		QueryPerformanceCounter(&renderEnd);
		lastRenderTimeMs = (double)(renderEnd.QuadPart - renderStart.QuadPart) * 1000.0 / (double)g_Frequency.QuadPart;

		if (!window.IsMinimized())
		{
			renderer.EndFrame();
		}
	}

	renderer.ReleaseVertexBuffer(leftWallVB);
	renderer.ReleaseVertexBuffer(rightWallVB);
	renderer.ReleaseVertexBuffer(otherWallsVB);
	renderer.ReleaseVertexBuffer(sphereVB);
	textRenderer.Shutdown();
	renderer.Shutdown();
	window.Shutdown();

	return 0;
}
