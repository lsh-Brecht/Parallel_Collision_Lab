#include "Window.h"
#include "Renderer.h"
#include "TextRenderer.h"
#include "Sphere.h"
#include "Trackball.h"

#include <ctime>

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

	FWindow window;
	if (!window.Init(hInstance, 768, 768, L"Parallel Collision Lab"))
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

	InitTimer();

	bool bRunning = true;
	while (bRunning)
	{
		FInputState input;
		if (!window.PumpMessages(input))
			break;

		if (input.Quit)
			break;

		if (input.Wireframe)
			renderer.ToggleWireframe();

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
		if (input.WheelDelta != 0)
		{
			trackball.ApplyWheelZoom(input.WheelDelta, at, eye);
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
		}
		else if (input.Toggle)
		{
			numSpheres = (numSpheres == MAX_SPHERES) ? MIN_SPHERES : MAX_SPHERES;
			spheres    = CreateSpheres(numSpheres, boxHalfSize);
		}
		else if (input.Add || input.AddMany)
		{
			int delta  = input.AddMany ? 16 : 1;
			numSpheres = min(numSpheres + delta, MAX_SPHERES);
			spheres    = CreateSpheres(numSpheres, boxHalfSize);
		}
		else if (input.Sub || input.SubMany)
		{
			int delta  = input.SubMany ? 16 : 1;
			numSpheres = max(numSpheres - delta, MIN_SPHERES);
			spheres    = CreateSpheres(numSpheres, boxHalfSize);
		}

		if (input.Pause)
			bPaused = !bPaused;

		float dt = GetDeltaTime();

		LARGE_INTEGER updateStart, updateEnd;
		QueryPerformanceCounter(&updateStart);

		if (!bPaused && !window.IsMinimized())
		{
			for (FSphere& s : spheres)
			{
				s.Update(dt);
				s.BoxCollisionCheck(boxHalfSize);
			}

			for (size_t i = 0; i < spheres.size(); ++i)
			{
				for (size_t j = i + 1; j < spheres.size(); ++j)
				{
					if (spheres[i].CollisionCheck(spheres[j]) < 0.0f)
						spheres[i].HandleCollision(spheres[j]);
				}
			}
		}

		QueryPerformanceCounter(&updateEnd);
		float updateTimeMs = (float)(updateEnd.QuadPart - updateStart.QuadPart) * 1000.0f / (float)g_Frequency.QuadPart;

		static float timeAccum        = 0.0f;
		static int   frameAccum       = 0;
		static float updateAccumMs    = 0.0f;
		static float renderAccumMs    = 0.0f;
		static float lastRenderTimeMs = 0.0f;
		static wchar_t hudText[128]   = L"FPS: 60.0 (16.6 ms)\nUpdate: 0.00 ms | Render: 0.00 ms\nSpheres: 16";

		timeAccum     += dt;
		frameAccum    += 1;
		updateAccumMs += updateTimeMs;
		renderAccumMs += lastRenderTimeMs;

		if (timeAccum >= 0.25f)
		{
			float currentFPS  = (float)frameAccum / timeAccum;
			float frameTimeMs = (timeAccum / (float)frameAccum) * 1000.0f;
			float avgUpdateMs = updateAccumMs / (float)frameAccum;
			float avgRenderMs = renderAccumMs / (float)frameAccum;

			swprintf_s(hudText, L"FPS: %.1f (%.1f ms)\nUpdate: %.2f ms | Render: %.2f ms\nSpheres: %d",
			           currentFPS, frameTimeMs, avgUpdateMs, avgRenderMs, (int)spheres.size());

			timeAccum     = 0.0f;
			frameAccum    = 0;
			updateAccumMs = 0.0f;
			renderAccumMs = 0.0f;
		}

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

			textRenderer.DrawTextOverlay(hudText, 10.0f, 10.0f, 450.0f, 80.0f);
		}

		QueryPerformanceCounter(&renderEnd);
		lastRenderTimeMs = (float)(renderEnd.QuadPart - renderStart.QuadPart) * 1000.0f / (float)g_Frequency.QuadPart;

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
