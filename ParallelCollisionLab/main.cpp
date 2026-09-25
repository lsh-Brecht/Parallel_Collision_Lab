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

	std::string targetServerIp = "127.0.0.1";
	uint16_t    targetPort     = Network::DEFAULT_SERVER_PORT;

	if (const char* ipArg = strstr(lpCmdLine, "-ip"))
	{
		ipArg += 3;
		while (*ipArg == ' ' || *ipArg == '=')
		{
			ipArg++;
		}
		std::string parsedIp;
		while (*ipArg && *ipArg != ' ' && *ipArg != '\t' && *ipArg != '\r' && *ipArg != '\n')
		{
			parsedIp += *ipArg++;
		}
		if (!parsedIp.empty())
		{
			targetServerIp = parsedIp;
		}
	}

	if (const char* portArg = strstr(lpCmdLine, "-port"))
	{
		portArg += 5;
		while (*portArg == ' ' || *portArg == '=')
		{
			portArg++;
		}
		int parsedPort = atoi(portArg);
		if (parsedPort > 0 && parsedPort <= 65535)
		{
			targetPort = static_cast<uint16_t>(parsedPort);
		}
	}

	int posX = 10;
	int posY = 10;
	std::wstring windowTitle = Config::WINDOW_TITLE;

	if (bServerArg)
	{
		posX = Config::SERVER_WINDOW_POS_X;
		posY = Config::SERVER_WINDOW_POS_Y;
		wchar_t titleBuf[128];
		swprintf_s(titleBuf, L"[SERVER :%u] Parallel Collision Lab", targetPort);
		windowTitle = titleBuf;
	}
	else if (bClientArg)
	{
		posX = Config::CLIENT_WINDOW_POS_X;
		posY = Config::CLIENT_WINDOW_POS_Y;
		std::wstring wIp(targetServerIp.begin(), targetServerIp.end());
		wchar_t titleBuf[128];
		swprintf_s(titleBuf, L"[CLIENT -> %s:%u] Parallel Collision Lab", wIp.c_str(), targetPort);
		windowTitle = titleBuf;
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
		netManager.StartServer(targetPort);
	}
	else if (bClientArg)
	{
		netManager.StartClient(targetServerIp.c_str(), targetPort);
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
			netManager.StartServer(targetPort);
			wchar_t titleBuf[128];
			swprintf_s(titleBuf, L"[SERVER :%u] Parallel Collision Lab", targetPort);
			window.SetTitle(titleBuf);
		}
		else if (input.StartClient)
		{
			netManager.StartClient(targetServerIp.c_str(), targetPort);
			std::wstring wIp(targetServerIp.begin(), targetServerIp.end());
			wchar_t titleBuf[128];
			swprintf_s(titleBuf, L"[CLIENT -> %s:%u] Parallel Collision Lab", wIp.c_str(), targetPort);
			window.SetTitle(titleBuf);
		}

		controller.ProcessInput(input, world, window, renderer, cpuInfo);

		float dt = timer.Tick();
		s_TickCounter++;

		FVector3 playerInput(0.0f, 0.0f, 0.0f);
		if (input.MoveLeft)     playerInput.x -= 1.0f;
		if (input.MoveRight)    playerInput.x += 1.0f;
		if (input.MoveUp)       playerInput.y += 1.0f;
		if (input.MoveDown)     playerInput.y -= 1.0f;
		if (input.MoveForward)  playerInput.z += 1.0f; // E: Forward (+Z)
		if (input.MoveBackward) playerInput.z -= 1.0f; // Q: Backward (-Z)

		double updateMs = 0.0;
		if (netManager.GetRole() == Network::ENetworkRole::Client)
		{
			netManager.SendClientInput(playerInput.x, playerInput.y, playerInput.z);
			netManager.UpdateClient(world.GetSpheres(), Config::BOX_HALF_SIZE, dt);
		}
		else if (netManager.GetRole() == Network::ENetworkRole::Server)
		{
			netManager.ProcessServerIncoming(world, dt);
			updateMs = world.Update(dt, controller.IsPaused(), timer.GetFrequency(), FVector3(0.0f, 0.0f, 0.0f));
			netManager.BroadcastServerSnapshot(s_TickCounter, world.GetSpheres(), world.GetBoxHalfSize());
		}
		else
		{
			updateMs = world.Update(dt, controller.IsPaused(), timer.GetFrequency(), playerInput);
		}

		static EPlanetType s_LastAssignedPlanet = EPlanetType::None;
		if (netManager.GetRole() == Network::ENetworkRole::Client)
		{
			EPlanetType currentPlanet = netManager.GetClientAssignedPlanet();
			if (currentPlanet != s_LastAssignedPlanet)
			{
				s_LastAssignedPlanet = currentPlanet;
				wchar_t titleBuf[128];
				if (currentPlanet == EPlanetType::Earth)
				{
					swprintf_s(titleBuf, L"[CLIENT 1 - EARTH (지구)] Parallel Collision Lab");
				}
				else if (currentPlanet == EPlanetType::Mars)
				{
					swprintf_s(titleBuf, L"[CLIENT 2 - MARS (화성)] Parallel Collision Lab");
				}
				else if (currentPlanet == EPlanetType::UVMap)
				{
					swprintf_s(titleBuf, L"[CLIENT 3 - UV MAP] Parallel Collision Lab");
				}
				else
				{
					std::wstring wIp(targetServerIp.begin(), targetServerIp.end());
					swprintf_s(titleBuf, L"[CLIENT - SPECTATOR -> %s:%u] Parallel Collision Lab", wIp.c_str(), targetPort);
				}
				window.SetTitle(titleBuf);
			}
		}

		wchar_t netStatusStr[256];
		const wchar_t* controlPromptStr = L"[Arrows/Q,E] Camera";

		if (netManager.GetRole() == Network::ENetworkRole::Server)
		{
			bool bEarth = netManager.IsServerPlanetActive(EPlanetType::Earth);
			bool bMars  = netManager.IsServerPlanetActive(EPlanetType::Mars);
			bool bUV    = netManager.IsServerPlanetActive(EPlanetType::UVMap);
			swprintf_s(netStatusStr, L"Server (Port: %u | Clients: %zu [Earth: %s | Mars: %s | UV: %s])",
			           targetPort, netManager.GetClientCount(),
			           bEarth ? L"ON" : L"OFF",
			           bMars  ? L"ON" : L"OFF",
			           bUV    ? L"ON" : L"OFF");
			controlPromptStr = L"Server Authority (Client Controls Only)";
		}
		else if (netManager.GetRole() == Network::ENetworkRole::Client)
		{
			std::wstring wIp(targetServerIp.begin(), targetServerIp.end());
			EPlanetType currentPlanet = netManager.GetClientAssignedPlanet();
			const wchar_t* planetName = L"Spectator";
			if (currentPlanet == EPlanetType::Earth)
			{
				planetName = L"Earth";
				controlPromptStr = L"[Arrows/Q,E] Earth";
			}
			else if (currentPlanet == EPlanetType::Mars)
			{
				planetName = L"Mars";
				controlPromptStr = L"[Arrows/Q,E] Mars";
			}
			else if (currentPlanet == EPlanetType::UVMap)
			{
				planetName = L"UVMap";
				controlPromptStr = L"[Arrows/Q,E] UV Sphere";
			}
			else
			{
				controlPromptStr = L"Spectator Mode";
			}

			swprintf_s(netStatusStr, L"Client [%s] -> %s:%u (%s | Recv: %u)",
			           planetName,
			           wIp.c_str(), targetPort,
			           netManager.IsConnected() ? L"Connected" : L"Searching...",
			           netManager.GetPacketsReceived());
		}
		else
		{
			swprintf_s(netStatusStr, L"Standalone [F9: Server, F10: Client]");
			controlPromptStr = L"Standalone (No Planets)";
		}

		hudTracker.Update(dt, updateMs, sceneRenderer.GetLastRenderTimeMs(),
		                  world.GetActiveSolver()->GetLastStats(),
		                  world.GetSphereCount(),
		                  world.GetActiveSolver(),
		                  world.GetThreadCount(),
		                  controller.IsGridVisEnabled(),
		                  world.IsMultiScaleSpheres(),
		                  netStatusStr,
		                  world.IsDampingEnabled(),
		                  world.GetSleepingSphereCount(),
		                  controlPromptStr);

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
