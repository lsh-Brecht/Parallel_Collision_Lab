#include "Network/NetworkManager.h"
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
#include <string>

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR lpCmdLine, int)
{
	srand(static_cast<unsigned int>(time(nullptr)));

	bool bServerArg = (strstr(lpCmdLine, "-server") != nullptr);
	bool bClientArg = (strstr(lpCmdLine, "-client") != nullptr);

	int posX = 10;
	int posY = 10;
	std::wstring windowTitle = Config::WINDOW_TITLE;

	if (bServerArg)
	{
		posX = Config::SERVER_WINDOW_POS_X;
		posY = Config::SERVER_WINDOW_POS_Y;
		windowTitle = L"[SERVER] Parallel Collision Lab";
	}
	else if (bClientArg)
	{
		posX = Config::CLIENT_WINDOW_POS_X;
		posY = Config::CLIENT_WINDOW_POS_Y;
		windowTitle = L"[CLIENT] Parallel Collision Lab";
	}

	FCPUInfo cpuInfo = QueryCPUInfo();

	FWindow window;
	if (!window.Init(hInstance, Config::WINDOW_WIDTH, Config::WINDOW_HEIGHT, windowTitle.c_str(), posX, posY))
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
	Network::FNetworkManager netManager;

	if (bServerArg)
	{
		netManager.StartServer(Config::DEFAULT_SERVER_PORT);
	}
	else if (bClientArg)
	{
		netManager.StartClient("127.0.0.1", Config::DEFAULT_SERVER_PORT);
	}

	uint32_t s_TickCounter = 0;
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

		if (input.StartServer)
		{
			netManager.StartServer(Config::DEFAULT_SERVER_PORT);
			window.SetTitle(L"[SERVER] Parallel Collision Lab");
		}
		else if (input.StartClient)
		{
			netManager.StartClient("127.0.0.1", Config::DEFAULT_SERVER_PORT);
			window.SetTitle(L"[CLIENT] Parallel Collision Lab");
		}

		controller.ProcessInput(input, world, window, renderer, textRenderer, cpuInfo);

		float dt = timer.Tick();
		s_TickCounter++;

		double updateMs = 0.0;
		if (netManager.GetRole() == Network::ENetworkRole::Client)
		{
			netManager.UpdateClient(world.GetSpheres(), Config::BOX_HALF_SIZE, dt);
		}
		else
		{
			updateMs = world.Update(dt, controller.IsPaused(), timer.GetFrequency());
			if (netManager.GetRole() == Network::ENetworkRole::Server)
			{
				netManager.UpdateServer(s_TickCounter, world.GetSpheres(), world.GetBoxHalfSize());
			}
		}

		wchar_t netStatusStr[128];
		if (netManager.GetRole() == Network::ENetworkRole::Server)
		{
			swprintf_s(netStatusStr, L"Server (Port: %u | Clients: %zu | Sent: %u)",
			           Config::DEFAULT_SERVER_PORT, netManager.GetClientCount(), netManager.GetPacketsSent());
		}
		else if (netManager.GetRole() == Network::ENetworkRole::Client)
		{
			swprintf_s(netStatusStr, L"Client (%s | Recv: %u | Tick: %u)",
			           netManager.IsConnected() ? L"Connected" : L"Searching...",
			           netManager.GetPacketsReceived(), netManager.GetLastSnapshotTick());
		}
		else
		{
			swprintf_s(netStatusStr, L"Standalone [F9: Server, F10: Client]");
		}

		hudTracker.Update(dt, updateMs, sceneRenderer.GetLastRenderTimeMs(),
		                  world.GetActiveSolver()->GetLastStats(),
		                  world.GetSphereCount(),
		                  world.GetActiveSolver(),
		                  world.GetThreadCount(),
		                  controller.IsGridVisEnabled(),
		                  world.IsMultiScaleSpheres(),
		                  netStatusStr);

		if (!window.IsMinimized())
		{
			FMatrix4x4 viewProj = controller.GetViewProj(window.GetAspectRatio());
			sceneRenderer.Render(renderer, world, viewProj, controller.IsGridVisEnabled(), timer.GetFrequency());

			if (controller.IsHUDEnabled())
			{
				int clientW = 0, clientH = 0;
				window.GetClientSize(clientW, clientH);
				hudTracker.Draw(textRenderer, clientW, clientH);
			}

			renderer.EndFrame();
		}
	}

	netManager.Shutdown();
	sceneRenderer.Shutdown(renderer);
	textRenderer.Shutdown();
	renderer.Shutdown();
	window.Shutdown();

	return 0;
}
