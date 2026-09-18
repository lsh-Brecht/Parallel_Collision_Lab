#include "Window/Window.h"
#include "Renderer/Renderer.h"
#include "Renderer/TextRenderer.h"
#include "Renderer/SceneRenderer.h"
#include "Renderer/HUDTracker.h"
#include "Core/AppConfig.h"
#include "Core/Timer.h"
#include "Core/CPUInfo.h"
#include "Core/SimulationWorld.h"
#include "Core/AppController.h"

#include <ctime>

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

	FSceneRenderer   sceneRenderer;
	sceneRenderer.Init(renderer, Config::BOX_HALF_SIZE);

	FSimulationWorld world;
	world.Init(MIN_SPHERES, Config::BOX_HALF_SIZE);

	FHUDTracker      hudTracker(cpuInfo);
	FTimer           timer;
	FAppController   controller;

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

		// 1. Process User Inputs
		controller.ProcessInput(input, world, window, renderer, textRenderer, cpuInfo);

		// 2. Physics & Collision Simulation
		float dt = timer.Tick();
		double updateMs = world.Update(dt, controller.IsPaused(), timer.GetFrequency());

		// 3. Performance Profiler & HUD Statistics
		hudTracker.Update(dt, updateMs, sceneRenderer.GetLastRenderTimeMs(),
		                  world.GetActiveSolver()->GetLastStats(),
		                  world.GetSphereCount(),
		                  world.GetActiveSolver(),
		                  world.GetThreadCount(),
		                  controller.IsGridVisEnabled());

		// 4. Render Scene
		if (!window.IsMinimized())
		{
			FMatrix4x4 viewProj = controller.GetViewProj(window.GetAspectRatio());
			sceneRenderer.Render(renderer, world, viewProj, controller.IsGridVisEnabled(), timer.GetFrequency());

			int clientW = 0, clientH = 0;
			window.GetClientSize(clientW, clientH);
			hudTracker.Draw(textRenderer, clientW, clientH);

			renderer.EndFrame();
		}
	}

	// Cleanup
	sceneRenderer.Shutdown(renderer);
	textRenderer.Shutdown();
	renderer.Shutdown();
	window.Shutdown();

	return 0;
}
